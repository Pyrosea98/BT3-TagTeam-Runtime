// [seamprobe] see include/runtime/ps2_seamprobe.h and docs/NATIVE-RENDER-SEAM.md (Phase 0).
#include "runtime/ps2_seamprobe.h"
#include "runtime/ps2_memory.h"

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
