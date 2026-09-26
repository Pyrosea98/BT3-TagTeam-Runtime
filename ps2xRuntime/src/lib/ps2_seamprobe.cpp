// [seamprobe] see include/runtime/ps2_seamprobe.h and docs/NATIVE-RENDER-SEAM.md (Phase 0).
#include "runtime/ps2_seamprobe.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_seamvk.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

extern std::atomic<uint64_t> g_bt3FrameCount;   // game_overrides.cpp

namespace seamprobe
{
    namespace
    {
        struct DrawCtx { uint32_t actor = 0, entity = 0, mode = 0, texLo = 0, texHi = 0, ra = 0; bool live = false; };
        thread_local DrawCtx t_draw;

        struct Pending { uint32_t builder, pkt, arg0, ra; DrawCtx draw; uint64_t frame; };
        struct Batch   { uint32_t builder, pkt, arg0, ra, list, qw; DrawCtx draw; uint64_t frame; };

        struct Key { uint32_t builder, prog, pc; bool operator<(const Key &o) const { return std::tie(builder, prog, pc) < std::tie(o.builder, o.prog, o.pc); } };
        struct Tally { uint64_t n = 0; };

        struct State
        {
            std::mutex mtx;
            std::vector<Pending> pending;                   // built this list, block not yet hashed
            std::unordered_map<uint64_t, std::deque<Batch>> ready; // block hash -> batches in list order, waiting for their unpack
            std::deque<uint64_t> readyOrder;                // hashes in insertion order, for expiry
            std::deque<Batch> noBlock;                       // CALL+MSCALF headers with no constant block, in list order
            std::map<uint64_t, uint64_t> builderModes;       // builder<<8|mode<<1|(tex!=0) -> batches
            std::unordered_map<uint32_t, uint32_t> listBuilder; // CALL target -> builder, for the DMA-level skip
            std::map<Key, Tally> mscals;                    // (builder, program, pc) -> starts
            std::map<uint32_t, uint64_t> builderBatches;    // builder -> batches finalized
            std::map<uint32_t, uint64_t> unattrUnpackByQw;  // qw count of address-0 unpacks with no batch
            uint64_t unattrMscal = 0, attrMscal = 0, unpackHit = 0, unpackMiss = 0, blocksNoUnpackWord = 0;
            std::map<uint32_t, int> dumped;                 // builder -> blocks dumped so far
            std::map<uint32_t, int> listsDumped;            // builder -> lists printed in full
            long walkBudget = 20000;                        // lists to walk for the histogram, then stop
            std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        };
        State &st() { static State *s = new State; return *s; }

        // Current batch on the kick thread: set by the address-0 unpack, consumed by MSCAL/MSCNT.
        thread_local bool t_haveBatch = false;
        thread_local bool t_blockSinceMscal = false;   // a block batch was matched since the last pc-0 start
        thread_local Batch t_batch{};

        uint64_t fnv(const uint8_t *p, size_t n)
        {
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
            return h;
        }


        // ---- list walker: decode a CALL'd on-disc VIF list so the importer's vocabulary is measured ----
        struct ListStats
        {
            std::map<uint32_t, uint64_t> dmaIds;          // DMA tag id -> count
            std::map<uint32_t, uint64_t> vifCmds;         // VIF cmd byte -> count
            std::map<uint32_t, uint64_t> unpackKinds;     // (cmd<<16 | num<<8 | flag<<1 | usn) -> count ... simplified: cmd<<8|num
            std::map<uint32_t, uint64_t> unpackAddr;      // imm & 0x83FF -> count (flag bit kept)
            std::map<uint32_t, uint64_t> stcycl;          // imm -> count
            uint64_t lists = 0, truncated = 0, nested = 0;
        };
        ListStats g_ls;

        // Walk one list. `verbose` prints every code. Returns false if the walk ran off the end.
        bool walkList(uint8_t *rdram, uint32_t list, bool verbose, uint32_t builder)
        {
            ++g_ls.lists;
            uint32_t addr = list & 0x1FFFFFFFu;
            int depth = 0; uint32_t retStack[4] = {};
            int codes = 0;
            char line[400]; int ll = 0;
            auto flush = [&](){ if (verbose && ll) { std::fprintf(stderr, "[seamlist]   %s\n", line); ll = 0; line[0] = 0; } };
            auto emit = [&](const char *fmt, auto... a){ if (!verbose) return; ll += std::snprintf(line + ll, sizeof(line) - ll, fmt, a...); if (ll > 300) flush(); };
            if (verbose) std::fprintf(stderr, "[seamlist] builder=0x%06x list=0x%08x\n", builder, list);
            for (int tags = 0; tags < 4096; ++tags)
            {
                const uint8_t *tp = getMemPtr(rdram, addr);
                if (!tp) { ++g_ls.truncated; flush(); return false; }
                uint32_t w[4]; std::memcpy(w, tp, 16);
                const uint32_t qwc = w[0] & 0xFFFFu, id = (w[0] >> 28) & 7u, taddr = w[1] & 0x7FFFFFFFu;
                ++g_ls.dmaIds[id];
                emit("DMA id%u qwc%u", id, qwc);
                // TTE: words 2,3 of the tag go to VIF, then the qwc data quadwords.
                uint32_t dataStart = addr + 16u, dataEnd = dataStart + qwc * 16u;
                if (id == 3u || id == 4u || id == 0u) { dataStart = taddr & 0x1FFFFFFFu; dataEnd = dataStart + qwc * 16u; }
                // VIF stream = tag words 2..3 followed by the data block.
                uint32_t vbuf[2] = {w[2], w[3]};
                uint32_t pos = 0;             // byte position in the virtual stream (8 bytes of tag words, then data)
                const uint32_t total = 8u + qwc * 16u;
                auto rd = [&](uint32_t off, uint32_t &out) -> bool {
                    if (off + 4u > total) return false;
                    if (off < 8u) { out = vbuf[off / 4u]; return true; }
                    const uint8_t *q = getMemPtr(rdram, dataStart + (off - 8u));
                    if (!q) return false; std::memcpy(&out, q, 4); return true; };
                while (pos + 4u <= total)
                {
                    uint32_t c; if (!rd(pos, c)) break;
                    pos += 4u; ++codes;
                    const uint32_t cmd = (c >> 24) & 0x7Fu, num = (c >> 16) & 0xFFu, imm = c & 0xFFFFu;
                    const uint32_t nvec = num ? num : 256u;
                    ++g_ls.vifCmds[cmd];
                    if (cmd >= 0x60u)
                    {
                        const uint32_t vn = (cmd >> 2) & 3u, vl = cmd & 3u, msk = (cmd >> 4) & 1u;
                        uint32_t bpv = (vn + 1u) * (32u >> vl) / 8u;
                        if (vl == 3u && vn == 3u) bpv = 2u;
                        const uint32_t bytes = (nvec * bpv + 3u) & ~3u;   // cl >= wl assumed; STCYCL histogram says whether that holds
                        ++g_ls.unpackKinds[(cmd << 8) | num];
                        ++g_ls.unpackAddr[imm & 0x83FFu];
                        emit(" | UNPACK v%u l%u%s n%u @%s%u", vn + 1u, vl, msk ? "m" : "", nvec, (imm & 0x8000u) ? "TOP+" : "", imm & 0x3FFu);
                        pos += bytes;
                    }
                    else if (cmd == 0x01u) { ++g_ls.stcycl[imm]; emit(" | STCYCL cl%u wl%u", imm & 0xFFu, (imm >> 8) & 0xFFu); }
                    else if (cmd == 0x00u) { emit(" | NOP"); }
                    else if (cmd == 0x14u || cmd == 0x15u) { emit(" | MSCAL%s pc0x%x", cmd == 0x15u ? "F" : "", imm * 8u); }
                    else if (cmd == 0x17u) { emit(" | MSCNT"); }
                    else if (cmd == 0x10u || cmd == 0x11u || cmd == 0x13u) { emit(" | FLUSH%02x", cmd); }
                    else if (cmd == 0x20u) { emit(" | STMASK"); pos += 4u; }
                    else if (cmd == 0x30u || cmd == 0x31u) { emit(" | ST%s", cmd == 0x30u ? "ROW" : "COL"); pos += 16u; }
                    else if (cmd == 0x50u || cmd == 0x51u) { emit(" | DIRECT%s %uqw", cmd == 0x51u ? "HL" : "", imm ? imm : 65536u); pos += (imm ? imm : 65536u) * 16u; }
                    else if (cmd == 0x4Au) { emit(" | MPG"); pos += nvec * 8u; }
                    else { emit(" | cmd%02x", cmd); }
                }
                flush();
                // Next tag.
                if (id == 1u || id == 4u) addr = dataEnd;                       // CNT, REFS: after data
                else if (id == 3u) addr = addr + 16u;                            // REF: after the tag
                else if (id == 2u) addr = taddr & 0x1FFFFFFFu;                   // NEXT
                else if (id == 5u) { if (depth < 4) retStack[depth++] = dataEnd; ++g_ls.nested; addr = taddr & 0x1FFFFFFFu; }
                else if (id == 6u) { if (depth == 0) return true; addr = retStack[--depth]; }
                else return true;                                                // END, REFE
            }
            ++g_ls.truncated; return false;
        }

        void reportLists()
        {
            std::fprintf(stderr, "[seamlist] lists=%llu truncated=%llu nested=%llu\n", (unsigned long long)g_ls.lists, (unsigned long long)g_ls.truncated, (unsigned long long)g_ls.nested);
            for (auto &d : g_ls.dmaIds) std::fprintf(stderr, "[seamlist]   dma id%u: %llu\n", d.first, (unsigned long long)d.second);
            for (auto &v : g_ls.vifCmds) std::fprintf(stderr, "[seamlist]   vif cmd 0x%02x: %llu\n", v.first, (unsigned long long)v.second);
            for (auto &u : g_ls.unpackKinds) std::fprintf(stderr, "[seamlist]   unpack cmd 0x%02x num %u: %llu\n", u.first >> 8, u.first & 0xFFu, (unsigned long long)u.second);
            for (auto &u : g_ls.unpackAddr) std::fprintf(stderr, "[seamlist]   unpack addr %s%u: %llu\n", (u.first & 0x8000u) ? "TOP+" : "", u.first & 0x3FFu, (unsigned long long)u.second);
            for (auto &c : g_ls.stcycl) std::fprintf(stderr, "[seamlist]   stcycl cl%u wl%u: %llu\n", c.first & 0xFFu, (c.first >> 8) & 0xFFu, (unsigned long long)c.second);
        }

        void report(State &s)
        {
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.t0).count();
            std::fprintf(stderr, "[seamprobe] %.1fs  mscal attributed=%llu unattributed=%llu | addr0 unpack hit=%llu miss=%llu | blocks without unpack word=%llu\n",
                         dt, (unsigned long long)s.attrMscal, (unsigned long long)s.unattrMscal,
                         (unsigned long long)s.unpackHit, (unsigned long long)s.unpackMiss, (unsigned long long)s.blocksNoUnpackWord);
            for (auto &b : s.builderBatches)
                std::fprintf(stderr, "[seamprobe]   builder 0x%06x: %.1f batches/s\n", b.first, double(b.second) / dt);
            for (auto &b : s.builderModes)
                std::fprintf(stderr, "[seamprobe]   builder 0x%06x mode %llu tex%s: %.1f batches/s\n", (uint32_t)(b.first >> 8), (unsigned long long)((b.first >> 1) & 0x7f), (b.first & 1) ? "!=0" : "=0", double(b.second) / dt);
            for (auto &m : s.mscals)
                std::fprintf(stderr, "[seamprobe]   builder 0x%06x prog %08x pc 0x%03x: %.1f starts/s\n",
                             m.first.builder, m.first.prog, m.first.pc, double(m.second.n) / dt);
            for (auto &u : s.unattrUnpackByQw)
                std::fprintf(stderr, "[seamprobe]   unattributed addr0 unpack of %u qw: %.1f/s\n", u.first, double(u.second) / dt);
            reportLists();
            s.mscals.clear(); s.builderBatches.clear(); s.unattrUnpackByQw.clear(); s.builderModes.clear();
            s.attrMscal = s.unattrMscal = s.unpackHit = s.unpackMiss = s.blocksNoUnpackWord = 0;
            s.t0 = std::chrono::steady_clock::now();
        }
    }

    bool on()
    {
        static const bool s_on = [](){
            for (const char *k : {"PS2X_SEAMPROBE", "PS2X_SEAMVERIFY", "PS2X_SEAMSKIP"})
            { const char *v = std::getenv(k); if (v && v[0] && v[0] != '0') return true; }
            return false; }();
        return s_on;
    }

    void noteDrawEnter(uint32_t actor, uint32_t entity, uint32_t mode, uint32_t texLo, uint32_t texHi, uint32_t ra)
    {
        t_draw = DrawCtx{actor, entity, mode, texLo, texHi, ra, true};
    }
    void noteDrawExit() { t_draw.live = false; }

    void noteBuilder(uint32_t builderAddr, uint32_t pkt, uint32_t arg0, uint32_t ra)
    {
        State &s = st();
        std::lock_guard<std::mutex> lk(s.mtx);
        s.pending.push_back(Pending{builderAddr, pkt & 0x1FFFFFFFu, arg0, ra, t_draw, g_bt3FrameCount.load(std::memory_order_relaxed)});
        if (s.pending.size() > 4096) s.pending.erase(s.pending.begin(), s.pending.begin() + 2048);
    }

    void finalizeList(uint8_t *rdram)
    {
        State &s = st();
        std::lock_guard<std::mutex> lk(s.mtx);
        const uint64_t now = g_bt3FrameCount.load(std::memory_order_relaxed);
        for (const Pending &p : s.pending)
        {
            const uint8_t *hdr = getMemPtr(rdram, p.pkt);
            if (!hdr) continue;
            // Header words: DMA tag (2 words) then VIF codes. Find the UNPACK V4-32 to address 0
            // in the first 8 words; the block follows it. Builders without one (the 0x20-byte
            // CALL+MSCALF headers) are counted but cannot be attributed by content.
            uint32_t w[8]; std::memcpy(w, hdr, sizeof(w));
            uint32_t list = w[1] & 0x0FFFFFFFu;
            int upIdx = -1;
            for (int i = 2; i < 8; ++i)
                if ((w[i] & 0xFF00FFFFu) == 0x6C000000u) { upIdx = i; break; }
            ++s.builderBatches[p.builder];
            s.listBuilder[list] = p.builder;
            {
                int &lv = s.listsDumped[p.builder];
                const bool verbose = lv < 2;
                if (verbose) ++lv;
                if (s.walkBudget > 0) { --s.walkBudget; walkList(rdram, list, verbose, p.builder); }
            }
            ++s.builderModes[((uint64_t)p.builder << 8) | ((uint64_t)(p.draw.mode & 0x7f) << 1) | ((p.draw.texLo | p.draw.texHi) ? 1u : 0u)];
            if (upIdx < 0)
            {
                ++s.blocksNoUnpackWord;
                s.noBlock.push_back(Batch{p.builder, p.pkt, p.arg0, p.ra, list, 0u, p.draw, p.frame});
                if (s.noBlock.size() > 8192) s.noBlock.pop_front();
                int &d0 = s.dumped[p.builder];
                if (d0 < 2)
                {
                    ++d0;
                    std::fprintf(stderr, "[seamprobe] noblock builder=0x%06x pkt=0x%08x arg0=0x%08x ra=0x%06x list=0x%08x frame=%llu actor=0x%08x entity=0x%08x mode=%u tex=%08x%08x draw_ra=0x%06x\n",
                                 p.builder, p.pkt, p.arg0, p.ra, list, (unsigned long long)p.frame,
                                 p.draw.actor, p.draw.entity, p.draw.mode, p.draw.texHi, p.draw.texLo, p.draw.ra);
                    std::fprintf(stderr, "[seamprobe]   hdr:");
                    for (int i = 0; i < 8; ++i) std::fprintf(stderr, " %08x", w[i]);
                    std::fprintf(stderr, "\n");
                    if (const uint8_t *lp = getMemPtr(rdram, list))
                    {
                        uint32_t lw[16]; std::memcpy(lw, lp, sizeof(lw));
                        std::fprintf(stderr, "[seamprobe]   list head:");
                        for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %08x", lw[i]);
                        std::fprintf(stderr, "\n");
                    }
                }
                continue;
            }
            const uint32_t qw = (w[upIdx] >> 16) & 0xFFu;
            const uint32_t blockAddr = p.pkt + (uint32_t)(upIdx + 1) * 4u;
            const uint8_t *block = getMemPtr(rdram, blockAddr);
            if (!block || qw == 0u) { ++s.blocksNoUnpackWord; continue; }
            const uint64_t h = fnv(block, (size_t)qw * 16u);
            Batch b{p.builder, p.pkt, p.arg0, p.ra, list, qw, p.draw, p.frame};
            s.ready[h].push_back(b);
            s.readyOrder.push_back(h);
            // Dump the first few blocks per builder as float rows, plus the head of the CALL'd list,
            // so the constant-block layout can be reconciled against the kernel listings.
            int &d = s.dumped[(p.builder << 8) | ((p.draw.mode & 0x7f) << 1) | ((p.draw.texLo | p.draw.texHi) ? 1u : 0u)];
            if (d < 2)
            {
                ++d;
                std::fprintf(stderr, "[seamprobe] block  builder=0x%06x pkt=0x%08x arg0=0x%08x ra=0x%06x list=0x%08x qw=%u frame=%llu actor=0x%08x entity=0x%08x mode=%u tex=%08x%08x draw_ra=0x%06x\n",
                             p.builder, p.pkt, p.arg0, p.ra, list, qw, (unsigned long long)p.frame,
                             p.draw.actor, p.draw.entity, p.draw.mode, p.draw.texHi, p.draw.texLo, p.draw.ra);
                std::fprintf(stderr, "[seamprobe]   hdr:");
                for (int i = 0; i < 8; ++i) std::fprintf(stderr, " %08x", w[i]);
                std::fprintf(stderr, "\n");
                for (uint32_t q = 0; q < qw; ++q)
                {
                    float f[4]; uint32_t u[4];
                    std::memcpy(f, block + q * 16u, 16); std::memcpy(u, block + q * 16u, 16);
                    std::fprintf(stderr, "[seamprobe]   qw%02u: %12.5g %12.5g %12.5g %12.5g | %08x %08x %08x %08x\n",
                                 q, f[0], f[1], f[2], f[3], u[0], u[1], u[2], u[3]);
                }
                if (const uint8_t *lp = getMemPtr(rdram, list))
                {
                    uint32_t lw[16]; std::memcpy(lw, lp, sizeof(lw));
                    std::fprintf(stderr, "[seamprobe]   list head:");
                    for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %08x", lw[i]);
                    std::fprintf(stderr, "\n");
                }
            }
        }
        s.pending.clear();
        // Expire batches older than 4 frames: the unpack never came (list dropped or not kicked).
        while (!s.readyOrder.empty())
        {
            const uint64_t h = s.readyOrder.front();
            auto it = s.ready.find(h);
            if (it == s.ready.end() || it->second.empty()) { if (it != s.ready.end()) s.ready.erase(it); s.readyOrder.pop_front(); continue; }
            if (it->second.front().frame + 4 > now) break;
            it->second.pop_front();
            if (it->second.empty()) s.ready.erase(it);
            s.readyOrder.pop_front();
        }
    }

    void noteUnpack0(const uint8_t *payload, uint32_t qw)
    {
        State &s = st();
        const uint64_t h = fnv(payload, (size_t)qw * 16u);
        std::lock_guard<std::mutex> lk(s.mtx);
        auto it = s.ready.find(h);
        if (it != s.ready.end() && !it->second.empty())
        {   // identical blocks (meshes on one bone) are told apart by list order
            t_batch = it->second.front(); t_haveBatch = true; t_blockSinceMscal = true;
            it->second.pop_front();
            if (it->second.empty()) s.ready.erase(it);
            ++s.unpackHit;
        }
        else
        {
            t_haveBatch = false;
            ++s.unpackMiss;
            ++s.unattrUnpackByQw[qw];
        }
    }

    void beginMscal(uint32_t startPc, bool mscnt)
    {
        if (mscnt) return;
        State &s = st();
        std::lock_guard<std::mutex> lk(s.mtx);
        if (startPc == 0u && !t_blockSinceMscal)
        {   // no constant block preceded this start: it is a CALL+MSCALF header, next in list order
            if (!s.noBlock.empty()) { t_batch = s.noBlock.front(); s.noBlock.pop_front(); t_haveBatch = true; }
            else t_haveBatch = false;
        }
        t_blockSinceMscal = false;
    }

    uint32_t builderOfList(uint32_t list)
    {
        State &s = st();
        std::lock_guard<std::mutex> lk(s.mtx);
        auto it = s.listBuilder.find(list & 0x1FFFFFFFu);
        return it == s.listBuilder.end() ? 0u : it->second;
    }

    bool currentBatch(uint32_t &builder, uint32_t &list)
    {
        if (!t_haveBatch) return false;
        builder = t_batch.builder; list = t_batch.list;
        return true;
    }

    void noteMscal(uint32_t startPc, uint32_t progHashLo, bool mscnt)
    {
        State &s = st();
        std::lock_guard<std::mutex> lk(s.mtx);
        if (t_haveBatch) { ++s.attrMscal; ++s.mscals[Key{t_batch.builder, progHashLo, mscnt ? 0xFFFu : startPc}].n; }
        else             { ++s.unattrMscal; ++s.mscals[Key{0u, progHashLo, mscnt ? 0xFFFu : startPc}].n; }
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.t0).count();
        if (dt >= 5.0) report(s);
    }
}


// ---------------------------------------------------------------------------------------------
// [kickprobe]
// ---------------------------------------------------------------------------------------------
#include <map>
#include <set>
#include <atomic>
extern std::atomic<uint64_t> g_bt3FrameCount;
namespace seamprobe
{
    namespace
    {
        struct KickSite
        {
            uint64_t kicks = 0, tags = 0, directQw = 0, unpackQw = 0, mscal = 0, uploads = 0, uploadQw = 0;
            uint64_t prims[8] = {}, dateDraws = 0, alphaOnlyDraws = 0, rgbOnlyDraws = 0, ateDraws = 0, texDraws = 0;
            std::map<uint64_t, uint64_t> frames;   // (fbp | psm<<16 | fbmsk<<24 (class: 0 none, 1 rgb-only, 2 alpha-only, 3 other)) -> writes
            std::map<uint32_t, uint64_t> texPsm;   // texture psm -> TEX0 writes
            std::map<uint32_t, uint64_t> texBase;  // tbp -> TEX0 writes (top few)
        };
        struct KickState { uint32_t helper = 0, chain = 0, ra = 0; bool live = false; };
        thread_local KickState t_kick;
        std::map<uint32_t, KickSite> g_sites;   // by the allocating code's ra
        uint64_t g_lastPrint = 0;
        struct Alloc { uint32_t start, end, ra; };
        std::vector<Alloc> g_allocs;            // this frame's display-list allocations (game thread; published at the kick)
        std::mutex g_pubMtx;                    // guards everything below (game thread publishes, kick thread reads)
        std::deque<std::vector<Alloc>> g_pubAllocs;   // the last few kicks' allocations, sorted
        std::map<const void *, std::vector<std::array<uint32_t, 3>>> g_chainMaps;   // chain buffer -> (offset, guest, scratch)
        thread_local std::vector<std::array<uint32_t, 3>> t_chainMap;
        thread_local uint32_t t_lastOwner = 0;
        uint32_t ownerOfAddrLocked(uint32_t a)
        {
            for (auto it = g_pubAllocs.rbegin(); it != g_pubAllocs.rend(); ++it)
            {
                const std::vector<Alloc> &v = *it;
                size_t lo = 0, hi = v.size();
                while (lo < hi) { const size_t mid = (lo + hi) / 2; if (v[mid].start <= a) lo = mid + 1; else hi = mid; }
                if (lo == 0) continue;
                const Alloc &al = v[lo - 1];
                if (a >= al.start && a < al.end) return al.ra;
            }
            return 0u;
        }
        uint32_t ownerAt(uint32_t) { return t_lastOwner; }   // the DIRECT payload's owner, set by noteDirect

        struct GifState { uint32_t nloop = 0, nreg = 1, ri = 0, flg = 0; uint64_t regs = 0; bool lastOdd = false; uint32_t total = 0; };
        extern thread_local GifState t_gs;
        bool g_dumpArmed = false, g_dumping = false; uint64_t g_dumpFrame = 0; int g_dumpLines = 0;
        const char *regName(uint32_t a)
        {
            switch (a & 0xFF)
            {
            case 0x00: return "PRIM"; case 0x01: return "RGBAQ"; case 0x02: return "ST"; case 0x03: return "UV"; case 0x04: return "XYZF2"; case 0x05: return "XYZ2";
            case 0x06: return "TEX0_1"; case 0x07: return "TEX0_2"; case 0x08: return "CLAMP_1"; case 0x09: return "CLAMP_2"; case 0x0A: return "FOG"; case 0x0C: return "XYZF3"; case 0x0D: return "XYZ3";
            case 0x14: return "TEX1_1"; case 0x15: return "TEX1_2"; case 0x16: return "TEX2_1"; case 0x17: return "TEX2_2"; case 0x18: return "XYOFFSET_1"; case 0x19: return "XYOFFSET_2";
            case 0x1A: return "PRMODECONT"; case 0x1B: return "PRMODE"; case 0x1C: return "TEXCLUT"; case 0x22: return "SCANMSK"; case 0x3B: return "TEXA"; case 0x3D: return "FOGCOL"; case 0x3F: return "TEXFLUSH";
            case 0x40: return "SCISSOR_1"; case 0x41: return "SCISSOR_2"; case 0x42: return "ALPHA_1"; case 0x43: return "ALPHA_2"; case 0x44: return "DIMX"; case 0x45: return "DTHE"; case 0x46: return "COLCLAMP";
            case 0x47: return "TEST_1"; case 0x48: return "TEST_2"; case 0x49: return "PABE"; case 0x4A: return "FBA_1"; case 0x4B: return "FBA_2"; case 0x4C: return "FRAME_1"; case 0x4D: return "FRAME_2";
            case 0x4E: return "ZBUF_1"; case 0x4F: return "ZBUF_2"; case 0x50: return "BITBLTBUF"; case 0x51: return "TRXPOS"; case 0x52: return "TRXREG"; case 0x53: return "TRXDIR"; case 0x54: return "HWREG";
            case 0x60: return "SIGNAL"; case 0x61: return "FINISH"; case 0x62: return "LABEL"; default: return "?";
            }
        }
        struct GifScan
        {
            KickSite *k; uint32_t prim = 0; uint64_t frame[2] = {}, tex0[2] = {}, test[2] = {};
            bool ate = false, date = false;
            uint32_t off = 0;   // stream byte offset of the qword being processed
            uint32_t vr = 0, vg = 0, vb = 0, va = 0, vu = 0, vv = 0; float vq = 1.0f, vs = 0.0f, vt = 0.0f;   // latched vertex registers (transcript)
            void vert(uint32_t x, uint32_t y, uint32_t z, bool adc)
            {
                if (g_dumping && g_dumpLines < 20000)
                {
                    ++g_dumpLines;
                    std::fprintf(stderr, "[kickdump] %06x V%s xy %.1f %.1f z %u uv %.1f %.1f st %.4f %.4f q %.3f rgba %02x%02x%02x%02x\n", ownerAt(off), adc ? "(adc)" : "",
                                 float(x) / 16.0f, float(y) / 16.0f, z, float(vu) / 16.0f, float(vv) / 16.0f, vs, vt, vq, vr, vg, vb, va);
                }
                if (!adc) reg(0x05, 0);
            }
            void reg(uint32_t a, uint64_t v)
            {
                k = &g_sites[ownerAt(off)];
                if (g_dumping && g_dumpLines < 20000 && a != 0x05 && a != 0x04)
                {
                    ++g_dumpLines;
                    std::fprintf(stderr, "[kickdump] %06x %s %016llx\n", ownerAt(off), regName(a), (unsigned long long)v);
                    if ((a & 0xFF) == 0x06 || (a & 0xFF) == 0x07)
                    {
                        const uint32_t psm = (uint32_t)((v >> 20) & 0x3F);
                        if (psm == 19u || psm == 20u || psm == 27u || psm == 36u || psm == 44u)
                        {
                            uint32_t clut[256];
                            if (seamgs::peekClut((uint32_t)((v >> 37) & 0x3FFF), (uint32_t)((v >> 51) & 0xF), clut))
                            {
                                std::fprintf(stderr, "[kickdump] %06x CLUT cbp %x:", ownerAt(off), (unsigned)((v >> 37) & 0x3FFF));
                                for (int i = 0; i < (psm == 20u || psm == 36u || psm == 44u ? 16 : 256); ++i) std::fprintf(stderr, "%s%08x", (i % 16) ? " " : "\n[kickdump]   ", clut[i]);
                                std::fprintf(stderr, "\n");
                            }
                        }
                    }
                }
                switch (a & 0xFF)
                {
                case 0x00: prim = (uint32_t)(v & 0x7FF); break;
                case 0x04: case 0x05: case 0x0C: case 0x0D:
                {
                    if (a == 0x0C || a == 0x0D) break;
                    const uint32_t kind = prim & 7u, ctxt = (prim >> 9) & 1u;
                    k->prims[kind]++;
                    { static int s_dbg = 0; if (kind == 7u && s_dbg < 12) { ++s_dbg; std::fprintf(stderr, "[kickprobe] kind7: prim 0x%x reg 0x%x v %016llx owner 0x%x tag regs %016llx nreg %u ri %u flg %u nloop %u\n", prim, a, (unsigned long long)v, ownerAt(off), (unsigned long long)t_gs.regs, t_gs.nreg, t_gs.ri, t_gs.flg, t_gs.nloop); } }
                    const uint64_t fr = frame[ctxt], te = test[ctxt];
                    const uint32_t fbp = (uint32_t)(fr & 0x1FF) * 32u, psm = (uint32_t)((fr >> 24) & 0x3F), msk = (uint32_t)(fr >> 32);
                    const uint32_t cls = msk == 0u ? 0u : msk == 0xFF000000u ? 1u : msk == 0x00FFFFFFu ? 2u : 3u;
                    if ((te >> 14) & 1) { k->dateDraws++; date = true; }
                    if (te & 1) k->ateDraws++;
                    if (cls == 1u) k->rgbOnlyDraws++; else if (cls == 2u) k->alphaOnlyDraws++;
                    if ((prim >> 4) & 1) { k->texDraws++; const uint32_t tp = (uint32_t)((tex0[ctxt] >> 20) & 0x3F); k->texPsm[tp]++; }
                    k->frames[(uint64_t)fbp | ((uint64_t)psm << 16) | ((uint64_t)cls << 24)]++;
                    break;
                }
                case 0x06: tex0[0] = v; k->texBase[(uint32_t)(v & 0x3FFF)]++; break;
                case 0x07: tex0[1] = v; k->texBase[(uint32_t)(v & 0x3FFF)]++; break;
                case 0x47: test[0] = v; break;
                case 0x48: test[1] = v; break;
                case 0x4C: frame[0] = v; break;
                case 0x4D: frame[1] = v; break;
                case 0x53: if ((v & 3) == 0) k->uploads++; break;
                default: break;
                }
            }
        };
        // GIF packets inside DIRECT payloads: PACKED / REGLIST / IMAGE. The tag state persists across payloads
        // (an IMAGE upload or a PACKED loop continues in the next DIRECT), so the scanner is resumable.
        thread_local GifState t_gs;
        void scanGif(GifScan &g, const uint8_t *d, uint32_t n, uint32_t base)
        {
            GifState &st = t_gs;
            uint32_t off = 0;
            while (off < n)
            {
                g.off = base + off;
                if (st.nloop == 0)
                {
                    if (off + 16 > n) break;
                    uint64_t lo, hi; std::memcpy(&lo, d + off, 8); std::memcpy(&hi, d + off + 8, 8); off += 16;
                    st.nloop = (uint32_t)(lo & 0x7FFF); st.flg = (uint32_t)((lo >> 58) & 3); st.nreg = (uint32_t)((lo >> 60) & 0xF); if (!st.nreg) st.nreg = 16;
                    st.regs = hi; st.ri = 0; st.total = st.nloop * st.nreg;
                    if (st.flg == 3) st.flg = 2;
                    if (((lo >> 46) & 1) && st.nloop) g.prim = (uint32_t)((lo >> 47) & 0x7FF);
                    continue;
                }
                if (st.flg == 2)
                {
                    const uint32_t take = std::min(st.nloop * 16u, n - off);
                    g.k = &g_sites[ownerAt(g.off)]; g.k->uploadQw += take / 16u;
                    off += take; st.nloop -= take / 16u;
                    if (take % 16u) st.nloop = 0;
                    continue;
                }
                if (st.flg == 1)
                {
                    if (off + 8 > n) break;
                    uint64_t v; std::memcpy(&v, d + off, 8); off += 8;
                    const uint32_t desc = (uint32_t)((st.regs >> (4 * st.ri)) & 0xF);
                    if (desc == 0x1) { g.vr = (uint32_t)(v & 0xFF); g.vg = (uint32_t)((v >> 8) & 0xFF); g.vb = (uint32_t)((v >> 16) & 0xFF); g.va = (uint32_t)((v >> 24) & 0xFF); uint32_t q = (uint32_t)(v >> 32); std::memcpy(&g.vq, &q, 4); }
                    else if (desc == 0x2) { uint32_t s0 = (uint32_t)v, t0 = (uint32_t)(v >> 32); std::memcpy(&g.vs, &s0, 4); std::memcpy(&g.vt, &t0, 4); }
                    else if (desc == 0x3) { g.vu = (uint32_t)(v & 0x3FFF); g.vv = (uint32_t)((v >> 16) & 0x3FFF); }
                    else if (desc == 0x4 || desc == 0x5) g.vert((uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), desc == 0x5 ? (uint32_t)(v >> 32) : (uint32_t)((v >> 32) & 0xFFFFFF), false);
                    else if (desc == 0x0) { g.prim = (uint32_t)(v & 0x7FF); if (g_dumping && g_dumpLines < 20000) { ++g_dumpLines; std::fprintf(stderr, "[kickdump] %06x PRIM %03llx\n", ownerAt(g.off), (unsigned long long)(v & 0x7FF)); } }
                    else if (desc < 0xE) g.reg(desc, v);
                    if (++st.ri == st.nreg) { st.ri = 0; --st.nloop; }
                    if (st.nloop == 0 && (st.total & 1)) off += 8;
                    continue;
                }
                if (off + 16 > n) break;
                uint64_t qlo, qhi; std::memcpy(&qlo, d + off, 8); std::memcpy(&qhi, d + off + 8, 8); off += 16;
                const uint32_t desc = (uint32_t)((st.regs >> (4 * st.ri)) & 0xF);
                if (desc == 0xE)
                {
                    const uint32_t a = (uint32_t)(qhi & 0xFF);
                    if (a == 0x01) { g.vr = (uint32_t)(qlo & 0xFF); g.vg = (uint32_t)((qlo >> 8) & 0xFF); g.vb = (uint32_t)((qlo >> 16) & 0xFF); g.va = (uint32_t)((qlo >> 24) & 0xFF); uint32_t q = (uint32_t)(qlo >> 32); std::memcpy(&g.vq, &q, 4); }
                    else if (a == 0x02) { uint32_t s0 = (uint32_t)qlo, t0 = (uint32_t)(qlo >> 32); std::memcpy(&g.vs, &s0, 4); std::memcpy(&g.vt, &t0, 4); }
                    else if (a == 0x03) { g.vu = (uint32_t)(qlo & 0x3FFF); g.vv = (uint32_t)((qlo >> 16) & 0x3FFF); }
                    if (a == 0x05 || a == 0x04) g.vert((uint32_t)(qlo & 0xFFFF), (uint32_t)((qlo >> 16) & 0xFFFF), a == 0x05 ? (uint32_t)(qlo >> 32) : (uint32_t)((qlo >> 32) & 0xFFFFFF), false);
                    else g.reg(a, qlo);
                }
                else if (desc == 0x0) { g.prim = (uint32_t)(qlo & 0x7FF); if (g_dumping && g_dumpLines < 20000) { ++g_dumpLines; std::fprintf(stderr, "[kickdump] %06x PRIM %03llx\n", ownerAt(g.off), (unsigned long long)(qlo & 0x7FF)); } }
                else if (desc == 0x1) { g.vr = (uint32_t)(qlo & 0xFF); g.vg = (uint32_t)((qlo >> 32) & 0xFF); g.vb = (uint32_t)(qhi & 0xFF); g.va = (uint32_t)((qhi >> 32) & 0xFF); }
                else if (desc == 0x2) { uint32_t s0 = (uint32_t)qlo, t0 = (uint32_t)(qlo >> 32), q0 = (uint32_t)qhi; std::memcpy(&g.vs, &s0, 4); std::memcpy(&g.vt, &t0, 4); std::memcpy(&g.vq, &q0, 4); }
                else if (desc == 0x3) { g.vu = (uint32_t)(qlo & 0x3FFF); g.vv = (uint32_t)((qlo >> 32) & 0x3FFF); }
                else if (desc == 0x4 || desc == 0x5) g.vert((uint32_t)(qlo & 0xFFFF), (uint32_t)((qlo >> 32) & 0xFFFF), desc == 0x5 ? (uint32_t)qhi : (uint32_t)((qhi >> 4) & 0xFFFFFF), ((qhi >> 47) & 1) != 0);
                else if (desc == 0xA || desc == 0xC || desc == 0xD || desc == 0xF) {}
                else g.reg(desc, qlo);
                if (++st.ri == st.nreg) { st.ri = 0; --st.nloop; }
            }
        }
        // VIF codes of one DMA data block: DIRECT payloads to the GIF scan, UNPACK/MPG skipped by size.
        void scanVif(GifScan &g, const uint8_t *d, uint32_t words)
        {
            uint32_t i = 0;
            while (i < words)
            {
                g.off = i * 4; g.k = &g_sites[ownerAt(g.off)];
                uint32_t code; std::memcpy(&code, d + i * 4, 4); ++i;
                const uint32_t cmd = (code >> 24) & 0x7F, imm = code & 0xFFFF, num = (code >> 16) & 0xFF;
                if (cmd == 0x20) i += 1;
                else if (cmd == 0x30 || cmd == 0x31) i += 4;
                else if (cmd == 0x4A) i += (num ? num : 256) * 2;
                else if (cmd == 0x50 || cmd == 0x51) { const uint32_t qw = imm ? imm : 65536; const uint32_t w = std::min(qw * 4, words - i); g.k->directQw += qw; scanGif(g, d + i * 4, w * 4, i * 4); i += qw * 4; }
                else if (cmd >= 0x60)
                {
                    const uint32_t n = num ? num : 256, vn = ((cmd >> 2) & 3) + 1, vl = cmd & 3;
                    uint32_t bytes = vl == 3 ? n * 2 : n * vn * (vl == 0 ? 4 : vl == 1 ? 2 : 1);
                    const uint32_t w = (bytes + 3) / 4; g.k->unpackQw += (w + 3) / 4; i += w;
                }
                else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) g.k->mscal++;
            }
        }
        void printSites()
        {
            std::fprintf(stderr, "[kickprobe] frame %llu, per frame (%zu callers):\n", (unsigned long long)g_bt3FrameCount.load(), g_sites.size());
            if (g_sites.size() > 50u && g_dumpFrame == 0) g_dumpArmed = true;
            const double n = 300.0;
            const char *pn[8] = {"pt", "ln", "ls", "tri", "ts", "tf", "spr", "?"};
            for (auto &kv : g_sites)
            {
                KickSite &k = kv.second;
                uint64_t prims = 0; for (int i = 0; i < 8; ++i) prims += k.prims[i];
                if (k.kicks == 0 && k.directQw == 0 && k.unpackQw == 0 && k.mscal == 0 && prims == 0 && k.uploadQw == 0) continue;
                std::fprintf(stderr, "[kickprobe] ra 0x%06x: kicks %.1f tags %.0f direct %.0f qw unpack %.0f qw mscal %.0f | tex draws %.0f date %.0f alpha-only %.0f rgb-only %.0f ate %.0f uploads %.1f (%.0f qw) |",
                             kv.first, k.kicks / n, k.tags / n, k.directQw / n, k.unpackQw / n, k.mscal / n, k.texDraws / n, k.dateDraws / n, k.alphaOnlyDraws / n, k.rgbOnlyDraws / n, k.ateDraws / n, k.uploads / n, k.uploadQw / n);
                for (int i = 0; i < 8; ++i) if (k.prims[i]) std::fprintf(stderr, " %s %.0f", pn[i], k.prims[i] / n);
                std::fprintf(stderr, " | frames:");
                for (auto &f : k.frames) std::fprintf(stderr, " %llx/psm%llu%s(%.0f)", (unsigned long long)(f.first & 0xFFFF), (unsigned long long)((f.first >> 16) & 0xFF), ((f.first >> 24) & 3) == 1 ? "rgb" : ((f.first >> 24) & 3) == 2 ? "A" : ((f.first >> 24) & 3) == 3 ? "m" : "", f.second / n);
                std::fprintf(stderr, " | texpsm:");
                for (auto &t : k.texPsm) std::fprintf(stderr, " %u(%.0f)", t.first, t.second / n);
                std::fprintf(stderr, "\n");
                k = KickSite();
            }
        }
    }

    bool kickProbeOn()
    {
        static const bool s = [](){ const char *v = std::getenv("PS2X_KICKPROBE"); return v && v[0] && v[0] != '0'; }();
        return s;
    }
    void noteKick(uint32_t helper, uint32_t chainAddr, uint32_t ra)
    {
        t_kick.helper = helper; t_kick.chain = chainAddr; t_kick.ra = ra; t_kick.live = true;
    }
    namespace { thread_local uint32_t t_emitterRa[8]; thread_local int t_emitDepth = 0; }
    void pushEmitter(uint32_t, uint32_t callerRa) { if (t_emitDepth < 8) t_emitterRa[t_emitDepth] = callerRa; ++t_emitDepth; }
    void popEmitter() { if (t_emitDepth > 0) --t_emitDepth; }
    void noteAdvance(uint32_t before, uint32_t after, uint32_t ra)
    {
        if (!kickProbeOn()) return;
        if (t_emitDepth > 0) ra = t_emitterRa[std::min(t_emitDepth, 8) - 1];   // the effect that called the emitter
        static uint32_t s_lastEnd = 0;
        before &= 0x1FFFFFFu; after &= 0x1FFFFFFu;
        if (g_allocs.size() < 400000u)
        {
            if (s_lastEnd && before > s_lastEnd && before - s_lastEnd < 0x100000u) g_allocs.push_back(Alloc{ s_lastEnd, before, ra });   // inline writes since the last helper call
            if (after > before && after - before < 0x100000u) g_allocs.push_back(Alloc{ before, after, ra });
        }
        s_lastEnd = after > before ? after : before;
    }
    void classifyVif1Chain(const uint8_t *rdram, uint32_t tagAddr)
    {
        if (!kickProbeOn()) return;
        (void)rdram; (void)tagAddr;
        t_kick.live = false;
        std::sort(g_allocs.begin(), g_allocs.end(), [](const Alloc &a, const Alloc &b){ return a.start < b.start; });
        {
            std::lock_guard<std::mutex> lk(g_pubMtx);
            g_pubAllocs.push_back(std::move(g_allocs));
            while (g_pubAllocs.size() > 4u) g_pubAllocs.pop_front();
            g_sites[0xFFFFFFu].kicks++;
        }
        g_allocs.clear();
    }
    void publishChainMap(const void *chainData, const std::vector<std::array<uint32_t, 3>> &map)
    {
        std::lock_guard<std::mutex> lk(g_pubMtx);
        g_chainMaps[chainData] = map;
        while (g_chainMaps.size() > 8u) g_chainMaps.erase(g_chainMaps.begin());
    }
    void beginChain(const void *chainData)
    {
        std::lock_guard<std::mutex> lk(g_pubMtx);
        auto it = g_chainMaps.find(chainData);
        if (it != g_chainMaps.end()) { t_chainMap = it->second; g_chainMaps.erase(it); }
        else t_chainMap.clear();
    }
    void noteDirect(uint32_t pos, const uint8_t *gif, uint32_t bytes)
    {
        {   // PS2X_KICKPROBE_DUMP=1: transcript of one busy frame (armed by the first window with > 50 callers)
            static const bool s_dump = [](){ const char *v = std::getenv("PS2X_KICKPROBE_DUMP"); return v && v[0] && v[0] != '0'; }();
            const uint64_t fr = g_bt3FrameCount.load();
            if (s_dump && g_dumpArmed && !g_dumping && g_dumpFrame == 0) { g_dumpFrame = fr + 1; }
            if (s_dump && g_dumpFrame && fr == g_dumpFrame && !g_dumping) { g_dumping = true; std::fprintf(stderr, "[kickdump] BEGIN frame %llu\n", (unsigned long long)fr); }
            if (g_dumping && fr > g_dumpFrame) { g_dumping = false; g_dumpArmed = false; g_dumpFrame = ~0ull; std::fprintf(stderr, "[kickdump] END (%d lines)\n", g_dumpLines); }
        }
        uint32_t guest = 0;
        for (size_t mi = t_chainMap.size(); mi > 0; --mi)
            if (t_chainMap[mi - 1][0] <= pos) { guest = t_chainMap[mi - 1][1] + (pos - t_chainMap[mi - 1][0]); break; }
        std::lock_guard<std::mutex> lk(g_pubMtx);
        t_lastOwner = guest ? ownerOfAddrLocked(guest & 0x1FFFFFFu) : 0u;
        KickSite &k = g_sites[t_lastOwner];
        k.directQw += bytes / 16u;
        static thread_local GifScan g; g.k = &k;
        scanGif(g, gif, bytes, 0);
        const uint64_t fr = g_bt3FrameCount.load();
        if (fr - g_lastPrint >= 300u) { g_lastPrint = fr; printSites(); }
    }
}
