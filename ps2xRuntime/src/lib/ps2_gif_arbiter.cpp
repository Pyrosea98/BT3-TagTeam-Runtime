#include "runtime/ps2_gif_arbiter.h"
#include "runtime/ps2_seamprobe.h"   // [pktoracle]
#include "runtime/ps2_seamvk.h"   // [seamvk]
#include "runtime/ps2_gs_pgs.h"   // [pgs]
#include <cstdlib>
extern "C" void ps2xGsRecordPacket(uint8_t path, const uint8_t *data, uint32_t sizeBytes);   // [recpgs] ps2_gs_gpu.cpp
#include <algorithm>
#include <map>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
// [giflock] lock the queue, counting only the acquisitions that actually had to wait, so the cost of the
// lock is measured instead of guessed ([giflock] line every 5 s; PS2X_GIFLOCKSTAT=0 silences it).
struct CountedLock
{
    std::mutex &m;
    CountedLock(std::mutex &mm, std::atomic<uint64_t> &waits, std::atomic<uint64_t> &ns) : m(mm)
    {
        if (m.try_lock()) return;
        const auto t0 = std::chrono::steady_clock::now();
        m.lock();
        ns.fetch_add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count(), std::memory_order_relaxed);
        waits.fetch_add(1, std::memory_order_relaxed);
    }
    ~CountedLock() { m.unlock(); }
};
}

// Diagnostic: pathId of the packet currently being dispatched to the GS (1=XGKICK, 2=DIRECT,
// 3=path3 DMA, 0=idle). Consumed by the runtime's process callback to tag GS::m_curSrcPath.
uint8_t g_gifArbCurPath = 0;

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    return flg == 2u;
}

// [gifcensus] PS2X_GIFCENSUS=1: what the GIF stream is made of, per class, every 5 s -- the datum for deciding which
// draws a native (non-GIF) path could take. Class = path (1 XGKICK / 2 VIF DIRECT / 3 DMA) and, for PATH1, the VU1
// program that emitted it. Per class: packets, KB, GIF tags, vertex kicks (XYZ2/XYZF2 with ADC=0, packed + A+D +
// REGLIST forms) split by the current PRIM type, IMAGE bytes (uploads), and the A+D writes that mark uploads/texture
// binds (BITBLTBUF, TRXDIR, TEX0). A walk over bytes the arena already copies; nothing when off.
extern thread_local uint32_t g_vu1CensusProg;   // ps2_vu1.cpp
namespace
{
struct CensusClass
{
    uint64_t pkts = 0, bytes = 0, tags = 0, imageBytes = 0, adWrites = 0, bitblt = 0, trxdir = 0, tex0 = 0;
    uint64_t verts[8] = {};   // by PRIM type 0..6 (7 = unknown)
};
struct Census
{
    std::mutex mtx;
    std::map<uint64_t, CensusClass> cls;   // key = path << 32 | prog
    uint32_t prim = 7;                     // last PRIM type seen (GS state is global across paths; approximate)
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
};
Census &census() { static Census *c = new Census; return *c; }
bool censusOn() { static const bool s = [](){ const char *v = std::getenv("PS2X_GIFCENSUS"); return v && v[0] && v[0] != '0'; }(); return s; }
void censusWalk(GifPathId pathId, const uint8_t *data, uint32_t size)
{
    Census &c = census();
    std::lock_guard<std::mutex> lk(c.mtx);
    const uint64_t key = ((uint64_t)pathId << 32) | (pathId == GifPathId::Path1 ? g_vu1CensusProg : 0u);
    CensusClass &k = c.cls[key];
    k.pkts++; k.bytes += size;
    size_t off = 0;
    while (off + 16 <= size)
    {
        uint64_t lo, hi; std::memcpy(&lo, data + off, 8); std::memcpy(&hi, data + off + 8, 8);
        off += 16; k.tags++;
        const uint32_t nloop = (uint32_t)(lo & 0x7FFFu), flg = (uint32_t)((lo >> 58) & 3u);
        uint32_t nreg = (uint32_t)((lo >> 60) & 0xFu); if (nreg == 0) nreg = 16;
        if (lo & (1ull << 46)) c.prim = (uint32_t)((lo >> 47) & 7u);   // PRE: the tag's PRIM
        if (flg == 2u || flg == 3u) { const size_t n = (size_t)nloop * 16u; k.imageBytes += std::min(n, size - off); off += n; continue; }
        if (flg == 1u)
        {   // REGLIST: two 64-bit registers per qword
            const size_t nRegs = (size_t)nloop * nreg; size_t idx = 0;
            for (; idx < nRegs && off + 8 <= size; ++idx)
            {
                const uint32_t d = (uint32_t)((hi >> (4u * (idx % nreg))) & 0xFu);
                if (d == 0x4u || d == 0x5u) k.verts[c.prim < 7 ? c.prim : 7]++;
                else if (d == 0x0u) { uint64_t v; std::memcpy(&v, data + off, 8); c.prim = (uint32_t)(v & 7u); }
                off += 8;
            }
            if (nRegs & 1u) off += 8;   // pad to a qword
            continue;
        }
        // PACKED
        for (uint32_t l = 0; l < nloop && off + 16 <= size; ++l)
            for (uint32_t r = 0; r < nreg && off + 16 <= size; ++r, off += 16)
            {
                const uint32_t d = (uint32_t)((hi >> (4u * r)) & 0xFu);
                uint64_t qlo, qhi; std::memcpy(&qlo, data + off, 8); std::memcpy(&qhi, data + off + 8, 8);
                if (d == 0x4u || d == 0x5u) { if (!(qhi & (1ull << 47))) k.verts[c.prim < 7 ? c.prim : 7]++; }   // ADC (bit 111) = no kick
                else if (d == 0x0u) c.prim = (uint32_t)(qlo & 7u);
                else if (d == 0xEu)
                {
                    const uint32_t a = (uint32_t)(qhi & 0xFFu); k.adWrites++;
                    if (a == 0x00u) c.prim = (uint32_t)(qlo & 7u);
                    else if (a == 0x04u || a == 0x05u) k.verts[c.prim < 7 ? c.prim : 7]++;
                    else if (a == 0x50u) k.bitblt++;
                    else if (a == 0x53u) k.trxdir++;
                    else if (a == 0x06u || a == 0x07u) k.tex0++;
                }
            }
    }
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - c.t0).count();
    if (dt >= 5.0)
    {
        static const char *const primName[8] = {"pt", "ln", "lstrip", "tri", "tstrip", "tfan", "sprite", "?"};
        std::fprintf(stderr, "[gifcensus] %.1fs, per second:\n", dt);
        for (auto &kv : c.cls)
        {
            const CensusClass &x = kv.second; const uint32_t path = (uint32_t)(kv.first >> 32), prog = (uint32_t)kv.first;
            uint64_t tv = 0; for (auto v : x.verts) tv += v;
            std::fprintf(stderr, "   path%u%s%08x: %7.0f pk %8.0f KB %8.0f tags %9.0f verts [", path, path == 1u ? " prog " : " ", prog, x.pkts / dt, x.bytes / 1024.0 / dt, x.tags / dt, tv / dt);
            for (int i = 0; i < 8; ++i) if (x.verts[i]) std::fprintf(stderr, " %s %.0f%%", primName[i], 100.0 * x.verts[i] / (double)(tv ? tv : 1));
            std::fprintf(stderr, " ] image %6.0f KB, A+D %6.0f (bitblt %.0f trxdir %.0f tex0 %.0f)\n", x.imageBytes / 1024.0 / dt, x.adWrites / dt, x.bitblt / dt, x.trxdir / dt, x.tex0 / dt);
        }
        c.cls.clear(); c.t0 = now;
    }
}
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!data || sizeBytes < 16 || !m_processFn)
        return;
    if (censusOn() && pathId != GifPathId::HostDraw) censusWalk(pathId, data, sizeBytes);   // [gifcensus]

    GifArbiterPacket pkt;
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.size = sizeBytes;
    if (pathId == GifPathId::Path2 && seamprobe::kickProbeOn()) pkt.owner = seamprobe::lastDirectOwner();   // [pktoracle]
    Lane &ln = lane();
    CountedLock lk(m_qMtx, m_lockWaits, m_lockWaitNs);
    pkt.offset = static_cast<uint32_t>(ln.arena.size());   // [gifarena] append, no per-packet block
    ln.arena.insert(ln.arena.end(), data, data + sizeBytes);
    ln.queue.push_back(pkt);
    ln.pending.store(static_cast<uint32_t>(ln.queue.size()), std::memory_order_relaxed);
}

static thread_local bool t_gifWorkerLane = false;   // [giflane]
void GifArbiter::markWorkerThread() { t_gifWorkerLane = true; }
GifArbiter::Lane &GifArbiter::lane() { return m_lanes[t_gifWorkerLane ? 1 : 0]; }
uint32_t GifArbiter::pending() const { return m_lanes[t_gifWorkerLane ? 1 : 0].pending.load(std::memory_order_relaxed); }

void GifArbiter::takeQueue(GifArbiterBatch &out)
{   // [vu1pipe] the ordering drain() applies, without processing
    static const bool s_sort = [](){ const char *v = std::getenv("PS2X_GIF_SORT"); return v && v[0] && v[0] != '0'; }();
    static const bool s_stat = [](){ const char *v = std::getenv("PS2X_GIFLOCKSTAT"); return !(v && v[0] == '0'); }();
    Lane &ln = lane();
    std::vector<GifArbiterPacket> &q = ln.queue;
    CountedLock lk(m_qMtx, m_lockWaits, m_lockWaitNs);
    if (s_sort)
        std::stable_sort(q.begin(), q.end(),
                         [](const GifArbiterPacket &a, const GifArbiterPacket &b)
                         {
                             if (a.path2DirectHl != b.path2DirectHl || a.path3Image != b.path3Image)
                             {
                                 if (a.path3Image && b.path2DirectHl)
                                     return true;
                                 if (a.path2DirectHl && b.path3Image)
                                     return false;
                             }
                             return pathPriority(a.pathId) < pathPriority(b.pathId);
                         });
    out.pkts.swap(q);
    q.clear();
    if (!out.pkts.empty())
    {   // [gifarena] the arena is final now: resolve the views, then hand the buffer over by move
        std::vector<uint8_t> arena;
        arena.swap(ln.arena);
        for (GifArbiterPacket &p : out.pkts) p.data = arena.data() + p.offset;
        out.arenas.push_back(std::move(arena));
    }
    ln.pending.store(0u, std::memory_order_relaxed);
    if (s_stat)
    {
        static auto s_t0 = std::chrono::steady_clock::now(); static uint64_t s_w0 = 0, s_n0 = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - s_t0 >= std::chrono::seconds(5))
        {
            const uint64_t w = m_lockWaits.load(std::memory_order_relaxed), n = m_lockWaitNs.load(std::memory_order_relaxed);
            const double secs = std::chrono::duration<double>(now - s_t0).count();
            std::fprintf(stderr, "[giflock] contended %.0f/s, waiting %.3f ms/s\n", (double)(w - s_w0) / secs, (double)(n - s_n0) / 1e6 / secs);
            s_t0 = now; s_w0 = w; s_n0 = n;
        }
    }
}
// [pktoracle] PS2X_PKTORACLE=<lo>-<hi>[:<nth>][,<lo>-<hi>[:<nth>]...] (hex guest addresses, default the 200th entry): the run
// of consecutive packets whose owner (the code that built them, PS2X_KICKPROBE=1) lies in [lo, hi) is bracketed with VRAM
// dumps of the backend in stream order: oracle_<lo>_before.bin/.txt before its first packet, oracle_<lo>_after.bin/.txt
// after its last, into PS2X_STEPORACLE_DIR (default /tmp). The pass's exact semantics can then be read off the two dumps
// offline (tools/gsvram.py, tools/oracle_diff.py). The packets themselves are processed unchanged.
namespace
{
    struct PktOracle { uint32_t lo = 0, hi = 0, nth = 200, entries = 0; bool inRun = false, done = false; };
    std::vector<PktOracle> g_pktOracles = [](){ std::vector<PktOracle> out; const char *v = std::getenv("PS2X_PKTORACLE"); if (!v || !v[0]) return out;
        for (const char *p = v; p && *p; )
        {
            PktOracle o; o.lo = (uint32_t)std::strtoul(p, nullptr, 16);
            if (const char *d = std::strchr(p, '-')) o.hi = (uint32_t)std::strtoul(d + 1, nullptr, 16);
            const char *comma = std::strchr(p, ',');
            if (const char *c = std::strchr(p, ':'); c && (!comma || c < comma)) o.nth = (uint32_t)std::atoi(c + 1);
            if (o.hi > o.lo) out.push_back(o);
            p = comma ? comma + 1 : nullptr;
        }
        return out; }();
    void pktOracleDump(uint32_t lo, const char *what)
    {
        static const char *s_dir = [](){ const char *v = std::getenv("PS2X_STEPORACLE_DIR"); return v && v[0] ? v : "/tmp"; }();
        char b[512], t[512]; std::snprintf(b, sizeof(b), "%s/oracle_%x_%s.bin", s_dir, lo, what); std::snprintf(t, sizeof(t), "%s/oracle_%x_%s.txt", s_dir, lo, what);
#ifdef PS2X_HAVE_PGS
        const bool ok = ps2x_pgs::dumpVramRaw(b, t);
#else
        const bool ok = false;
#endif
        std::fprintf(stderr, "[pktoracle] %x %s dump %s (%s)\n", lo, what, ok ? "ok" : "FAILED", b);
    }
}
// [postnative] PS2X_POSTNATIVE=<mask> (bit i = step i of ps2x_pgs::nativePostStep; needs PS2X_KICKPROBE=1 for the owners):
// the packets those steps built are dropped and the host pass runs at the position of the first one.
namespace
{
    struct NativeStep { uint32_t lo, hi; int id; bool inRun; };
    NativeStep g_nativeSteps[] = { {0x109848u, 0x109938u, 0, false}, {0x106ba8u, 0x106c5cu, 1, false}, {0x24b118u, 0x24b1dcu, 2, false}, {0x245a50u, 0x245de4u, 3, false}, {0x103070u, 0x103254u, 4, false} };
    const uint32_t g_nativeMask = [](){ const char *v = std::getenv("PS2X_POSTNATIVE"); return v && v[0] ? (uint32_t)std::strtoul(v, nullptr, 0) : 0u; }();
    // The step's packets are not dropped: their register writes (FRAME/ZBUF/SCISSOR/TEST... which the game's later draws
    // inherit) still reach the backend; only their DRAW kicks are neutralised, XYZ2/XYZF2 -> XYZ3/XYZF3 (no kick), by
    // rewriting the GIF tags' register descriptors and A+D addresses in place. Tag state persists across PATH2 payloads.
    struct P2Scan { uint32_t nloop = 0, nreg = 0, ri = 0; uint8_t flg = 0; uint64_t regs = 0; } g_p2scan;
    void neutralizeKicks(uint8_t *d, uint32_t n)
    {
        P2Scan &p = g_p2scan; uint32_t off = 0;
        while (off < n)
        {
            if (p.nloop == 0u)
            {
                if (off + 16u > n) break;
                uint64_t lo, hi; std::memcpy(&lo, d + off, 8); std::memcpy(&hi, d + off + 8, 8);
                p.nloop = (uint32_t)(lo & 0x7FFFu); p.flg = (uint8_t)((lo >> 58) & 3u); p.nreg = (uint32_t)((lo >> 60) & 0xFu); if (!p.nreg) p.nreg = 16u; p.ri = 0;
                if (p.flg == 3u) p.flg = 2u;
                if (p.flg != 2u)
                {   // XYZ2 (5) -> XYZ3 (d), XYZF2 (4) -> XYZF3 (c) in the descriptors
                    uint64_t r2 = 0;
                    for (uint32_t i = 0; i < 16; ++i) { uint64_t nib = (hi >> (4 * i)) & 0xFu; if (nib == 5u) nib = 0xDu; else if (nib == 4u) nib = 0xCu; r2 |= nib << (4 * i); }
                    hi = r2; std::memcpy(d + off + 8, &hi, 8);
                }
                p.regs = hi; off += 16;
                continue;
            }
            if (p.flg == 2u) { const uint32_t take = std::min(p.nloop * 16u, n - off); off += take; p.nloop -= take / 16u; if (take % 16u) p.nloop = 0; continue; }
            if (p.flg == 1u)
            {   // REGLIST: 8 bytes per register, descriptors already rewritten in the tag
                if (off + 8u > n) break;
                off += 8; if (++p.ri == p.nreg) { p.ri = 0; --p.nloop; if (p.nloop == 0u && ((p.nreg & 1u) != 0u)) off += 8; }
                continue;
            }
            if (off + 16u > n) break;
            const uint32_t desc = (uint32_t)((p.regs >> (4u * p.ri)) & 0xFu);
            if (desc == 0xEu) { uint8_t &addr = d[off + 8]; if (addr == 0x05u) addr = 0x0Du; else if (addr == 0x04u) addr = 0x0Cu; }
            off += 16; if (++p.ri == p.nreg) { p.ri = 0; --p.nloop; }
        }
    }
    bool nativeIntercept(const GifArbiterPacket &pkt)
    {
        for (NativeStep &st : g_nativeSteps)
        {
            if (!((g_nativeMask >> st.id) & 1u)) continue;
            const bool in = pkt.owner >= st.lo && pkt.owner < st.hi;
            if (in && !st.inRun)
            {
                st.inRun = true;
#ifdef PS2X_HAVE_PGS
                static uint32_t s_fail[8] = {};
                if (!ps2x_pgs::nativePostStep(st.id) && s_fail[st.id]++ < 3u) std::fprintf(stderr, "[postnative] step %d: pass FAILED (frame not 512 wide?)\n", st.id);
#endif
            }
            else if (!in && st.inRun) st.inRun = false;
            if (in) { neutralizeKicks(const_cast<uint8_t *>(pkt.data), pkt.size); return true; }
        }
        return false;
    }
}
void GifArbiter::process(const GifArbiterPacket &pkt)
{
    if (!m_processFn || !pkt.data || pkt.size == 0u) return;
    if (!g_pktOracles.empty() && pkt.pathId == GifPathId::Path2)
        for (PktOracle &o : g_pktOracles)
        {   // (before the native intercept: a natively replaced step is bracketed the same way, so its output is verified)
            if (o.done) continue;
            const bool in = pkt.owner >= o.lo && pkt.owner < o.hi;
            if (in && !o.inRun) { o.inRun = true; if (++o.entries == o.nth) pktOracleDump(o.lo, "before"); }
            else if (!in && o.inRun) { o.inRun = false; if (o.entries == o.nth) { pktOracleDump(o.lo, "after"); o.done = true; } }
        }
    if (g_nativeMask && pkt.pathId == GifPathId::Path2) nativeIntercept(pkt);   // [postnative] the host pass ran; the packet goes on with its kicks neutralised
    uint8_t pathId = static_cast<uint8_t>(pkt.pathId);
    const uint8_t *data = pkt.data; uint32_t size = pkt.size;
    if (pkt.pathId == GifPathId::HostDraw)
    {   // [seamvk] the seam's packets, consumed here in stream order
#ifdef PS2X_HAVE_PGS
        uint32_t magic = 0; if (size >= 4u) std::memcpy(&magic, data, 4);
        if (magic == seamvk::kHostGifMagic && size >= sizeof(seamvk::HostGifHeader))
        {   // 'SVKG': the host-transformed GIF packet -> the native front-end (registers only) and the GS backend as PATH1
            data += sizeof(seamvk::HostGifHeader); size -= sizeof(seamvk::HostGifHeader); pathId = 1u;
            seamvk::onGifPacket(pathId, data, size, true);
        }
        else { seamvk::onHostDraw(data, size); return; }
#else
        return;
#endif
    }
#ifdef PS2X_HAVE_PGS
    else if (seamvk::on()) seamvk::onGifPacket(pathId, data, size, false);
#endif
    if (ps2x_pgs::enabled() && ps2x_pgs::exclusive())
    {   // [recpgs] PS2X_GS_RECORD: our parser will not see this packet, so record it here
        static const bool s_rec = [](){ const char *v = std::getenv("PS2X_GS_RECORD"); return v && v[0]; }();
        if (s_rec) { ps2xGsRecordPacket(pathId, data, size); }
    }
    if (ps2x_pgs::enabled())
    {   // [pgs] the paraLLEl-GS backend consumes the same packet, on its own path index. Pack mode: OUR parse first, so the
        // VRAM and palettes its replacement hook hashes already include this packet's uploads.
        if (ps2x_pgs::packMode())
        {
            g_gifArbCurPath = pathId;
            m_processFn(data, size);
            g_gifArbCurPath = 0;
            ps2x_pgs::gifTransfer(pathId, data, size);
            return;
        }
        const bool consumed = ps2x_pgs::gifTransfer(pathId, data, size);
        if (consumed && ps2x_pgs::exclusive()) return;   // not consumed (backend unavailable): our parse takes it
    }
    g_gifArbCurPath = pathId;
    m_processFn(data, size);
    g_gifArbCurPath = 0;
}
void GifArbiter::drain()
{
    if (!m_processFn)
        return;
    GifArbiterBatch b;
    takeQueue(b);
    for (const auto &pkt : b.pkts) process(pkt);
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
