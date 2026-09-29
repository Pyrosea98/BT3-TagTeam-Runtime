// [dueldump] Total capture of the Duel menu flow. See include/runtime/ps2x_dueldump.h.
//
// Everything is written from tick() (frame hook, guest thread); the raster workers only push
// POD records into a locked queue. Disabled = one pointer compare per frame.

#include "runtime/ps2x_dueldump.h"

#include "gfx/bt3gl_api.h"          // raylib Image + ExportImage (PNG, no GL needed)
#include "runtime/ps2_texreplace.h" // ps2tex::identify (PCSX2-compatible texture identity)
#include "ps2_runtime.h"            // PS2Runtime::memory(), PS2_RAM_SIZE/PS2_GS_VRAM_SIZE

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

extern std::atomic<uint64_t> g_bt3FrameCount;   // game_overrides.cpp (global scope)

namespace
{
    constexpr uint32_t kMainPtrAddr   = 0x2FF10Cu;   // mainPtr; +0x18 = screen id
    constexpr uint32_t kMenuObjAddr   = 0x3B0E80u;
    constexpr uint32_t kDuelObjAddr   = 0x3B38E8u;
    constexpr uint32_t kMainStructAddr= 0x3B38D8u;
    constexpr uint32_t kJumpTableAddr = 0x3B1100u;
    constexpr uint32_t kHandlersAddr  = 0x3B4290u;
    constexpr uint32_t kDuelRow       = 3u;          // main-menu row -> Duel
    constexpr uint32_t kDuelState     = 0x26u;
    constexpr uint32_t kMainMenuState = 0x04u;

    constexpr uint64_t kMaxFrames  = 60000ull;       // ~16 min cap
    constexpr size_t   kMaxTexRows = 2000000ull;

    inline uint32_t rd32(const uint8_t *rd, uint32_t addr)
    { uint32_t v = 0; std::memcpy(&v, rd + (addr & 0x1FFFFFFFu), 4); return v; }
    inline uint8_t rd8(const uint8_t *rd, uint32_t addr)
    { return rd[(addr & 0x1FFFFFFFu)]; }
    inline uint16_t rd16(const uint8_t *rd, uint32_t addr)
    { uint16_t v = 0; std::memcpy(&v, rd + (addr & 0x1FFFFFFFu), 2); return v; }

    // The main-menu row states, indexed by the row's entry in the 11-row jump table at
    // kJumpTableAddr. 0xFFFFFFFF = the row has no target (the hidden Network row).
    const uint32_t kRowState[11] = { 0x06u, 0x0Du, 0x21u, 0x26u, 0xFFFFFFFFu,
                                     0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                     0xFFFFFFFFu, 0xFFFFFFFFu };

    // ---- state ---------------------------------------------------------------------
    bool s_armed = false, s_sawDuel = false, s_done = false;
    uint64_t s_armFrame = 0;
    std::string s_dir;
    std::FILE *s_fEvents = nullptr, *s_fTex = nullptr;
    uint32_t s_lastState = 0xFFFFFFFFu;              // transitions we have captured (hex/vram/delta)
    uint32_t s_evState = 0xFFFFFFFFu;                // last state written to events.csv
    uint32_t s_lastRow = 0xFFFFFFFFu, s_lastEntry = 0xFFFFFFFFu;
    uint32_t s_lastDuel[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    uint32_t s_lastCopy[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::vector<uint8_t> s_base;                     // delta base (last RAM dump)
    bool s_haveBase = false;
    size_t s_texRows = 0;

    // texture records pushed by the raster workers
    std::mutex s_texMx;
    std::vector<ps2x_dueldump::TexSample> s_texQ;
    std::vector<uint64_t> s_pngHashes;

    uint64_t nowFrame() { return g_bt3FrameCount.load(std::memory_order_relaxed); }

    uint32_t stateOf(const uint8_t *rd)
    {
        const uint32_t p = rd32(rd, kMainPtrAddr) & 0x1FFFFFFFu;
        return p ? rd32(rd, p + 0x18u) : 0xFFFFFFFFu;
    }

    std::string stamp()
    {
        char buf[32] = {0};
        const std::time_t t = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", &tmv);
        return buf;
    }

    bool ensureDir(const std::string &d)
    {
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return !ec;
    }

    void writeMeta(const char *what)
    {
        if (s_dir.empty()) return;
        std::FILE *f = std::fopen((s_dir + "/meta.txt").c_str(), "a");
        if (!f) return;
        std::fprintf(f, "%s frame=%llu state=0x%x\n", what, (unsigned long long)nowFrame(), s_lastState);
        std::fclose(f);
    }

    // ---- hex ------------------------------------------------------------------------
    void hexRegion(std::FILE *f, const uint8_t *ram, uint32_t addr, uint32_t bytes, const char *label)
    {
        std::fprintf(f, "\n===== %s @ 0x%08x (%u bytes) =====\n", label, addr, bytes);
        for (uint32_t o = 0; o < bytes; o += 16u)
        {
            std::fprintf(f, "0x%08x |", addr + o);
            const uint32_t n = (bytes - o < 16u) ? (bytes - o) : 16u;
            for (uint32_t i = 0; i < 16u; ++i)
                std::fprintf(f, " %02x", i < n ? rd8(ram, addr + o + i) : 0);
            std::fprintf(f, "  |");
            for (uint32_t i = 0; i < n; ++i)
            {
                const uint8_t c = rd8(ram, addr + o + i);
                std::fprintf(f, "%c", (c >= 0x20 && c < 0x7F) ? (char)c : '.');
            }
            std::fprintf(f, "|\n");
        }
    }

    // ---- RAM + deltas ---------------------------------------------------------------
    void writeFullRam(const std::string &path, const uint8_t *ram)
    {
        if (std::FILE *f = std::fopen(path.c_str(), "wb"))
        {
            std::fwrite(ram, 1, PS2_RAM_SIZE, f);
            std::fclose(f);
            std::fprintf(stderr, "[dueldump] full RAM -> %s\n", path.c_str());
        }
    }

    void writeDelta(const std::string &baseName, const uint8_t *ram)
    {
        if (!s_haveBase) return;
        std::FILE *fb = std::fopen((baseName + ".bin").c_str(), "wb");
        std::FILE *fi = std::fopen((baseName + ".idx").c_str(), "w");
        if (!fb || !fi) { if (fb) std::fclose(fb); if (fi) std::fclose(fi); return; }
        std::fprintf(fi, "# offset length  (apply over the previous RAM dump)\n");
        size_t total = 0, ranges = 0;
        uint32_t i = 0;
        while (i < PS2_RAM_SIZE)
        {
            if (ram[i] == s_base[i]) { ++i; continue; }
            uint32_t j = i;
            while (j < PS2_RAM_SIZE && ram[j] != s_base[j]) ++j;   // coarse; splitting is fine
            const uint32_t len = j - i;
            std::fwrite(ram + i, 1, len, fb);
            std::fprintf(fi, "0x%08x %u\n", i, len);
            total += len; ++ranges; i = j;
        }
        std::fclose(fb); std::fclose(fi);
        std::fprintf(stderr, "[dueldump] delta -> %s.bin (%zu ranges, %zu bytes)\n",
                     baseName.c_str(), ranges, total);
    }

    void snapshotRam(const uint8_t *ram, const char *tag, uint32_t state, uint64_t frame)
    {
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/ram/%s_%u_%llu", s_dir.c_str(), tag, state,
                      (unsigned long long)frame);
        if (std::strcmp(tag, "arm") == 0) writeFullRam(std::string(nm) + ".bin", ram);
        else                              writeDelta(nm, ram);
        s_base.assign(ram, ram + PS2_RAM_SIZE);
        s_haveBase = true;
    }
}

// ---- capture lifecycle ----------------------------------------------------------
namespace
{    void dumpHexAndVram(const uint8_t *rdram, PS2Runtime *runtime, uint32_t state, uint64_t frame);

    void arm(const uint8_t *rdram, PS2Runtime *runtime)
    {
        const std::string base = [](){
            const char *v = std::getenv("PS2X_DUELDUMP_DIR");
            return (v && v[0]) ? std::string(v) : ("dumps/duel_" + stamp());
        }();
        s_dir = base;
        ensureDir(s_dir);
        ensureDir(s_dir + "/ram"); ensureDir(s_dir + "/hex");
        ensureDir(s_dir + "/vram"); ensureDir(s_dir + "/png");
        s_fEvents = std::fopen((s_dir + "/events.csv").c_str(), "w");
        if (s_fEvents)
            std::fprintf(s_fEvents,
                "frame,state,cursor_row,entry,handler,target,duel_vs,duel_type,duel_dp,duel_time,"
                "st620,st624,st630\n");
        s_fTex = std::fopen((s_dir + "/textures.csv").c_str(), "w");
        if (s_fTex)
            std::fprintf(s_fTex,
                "frame,hash,tbp0,tbw,psm,tcc,cbp,cpsm,w,h,swizzle,texKey,clutKey,fbp,zbp,tfx,srcAddr,packHit,cacheHit,how\n");
        {
            std::FILE *f = std::fopen((s_dir + "/meta.txt").c_str(), "w");
            if (f)
            {
                std::fprintf(f, "build=%s %s\n", __DATE__, __TIME__);
                std::fprintf(f, "arm_frame=%llu\n", (unsigned long long)nowFrame());
                const char *e = std::getenv("PS2X_DUELDUMP_DIR");
                std::fprintf(f, "dir_env=%s\n", e ? e : "(default)");
                std::fclose(f);
            }
        }
        s_armed = true;
        s_armFrame = nowFrame();
        s_lastState = kMainMenuState;   // so the first 0x04 -> 0x26 transition is captured
        s_evState = 0xFFFFFFFFu;
        snapshotRam(rdram, "arm", kMainMenuState, s_armFrame);
        dumpHexAndVram(rdram, runtime, kMainMenuState, s_armFrame);   // the "before" structures
        writeMeta("arm");
        std::fprintf(stderr, "[dueldump] ARM -> %s (frame %llu)\n", s_dir.c_str(),
                     (unsigned long long)s_armFrame);
    }

    void disarm()
    {
        s_armed = false;
        s_done = true;
        if (s_fEvents) { std::fclose(s_fEvents); s_fEvents = nullptr; }
        if (s_fTex)    { std::fclose(s_fTex);    s_fTex = nullptr; }
        writeMeta("disarm");
        std::fprintf(stderr, "[dueldump] DISARM (frame %llu) -> %s\n",
                     (unsigned long long)nowFrame(), s_dir.c_str());
    }

    void dumpHexAndVram(const uint8_t *rdram, PS2Runtime *runtime, uint32_t state, uint64_t frame)
    {
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/hex/structs_%u_%llu.txt", s_dir.c_str(), state,
                      (unsigned long long)frame);
        if (std::FILE *f = std::fopen(nm, "w"))
        {
            const uint32_t mo = rd32(rdram, kMenuObjAddr) & 0x1FFFFFFFu;
            const uint32_t dod = rd32(rdram, kDuelObjAddr) & 0x1FFFFFFFu;
            const uint32_t stp = rd32(rdram, kMainPtrAddr) & 0x1FFFFFFFu;
            if (mo)
            {
                hexRegion(f, rdram, mo, 0x200u, "menuObj");
                const uint32_t count = rd32(rdram, mo + 0x144u);
                if (count && count < 0x100u) hexRegion(f, rdram, mo + 0x118u, 4u * count, "menuObj->rows(+0x118)");
            }
            if (dod) hexRegion(f, rdram, dod, 0x400u, "duelObj");
            if (stp) hexRegion(f, rdram, stp, 0x100u, "stateObj");
            hexRegion(f, rdram, kMainStructAddr, 0x100u, "mainStruct");
            hexRegion(f, rdram, kDuelObjAddr, 0x40u, "itemBase/jumpTablePtr");
            hexRegion(f, rdram, kJumpTableAddr, 0x2Cu, "menu jump table (11 rows)");
            hexRegion(f, rdram, kHandlersAddr, 0x40u, "entry handlers");
            std::fclose(f);
        }
        if (runtime)
        {
            const uint8_t *vram = runtime->memory().getGSVRAM();
            if (vram)
            {
                std::snprintf(nm, sizeof nm, "%s/vram/vram_%u_%llu.bin", s_dir.c_str(), state,
                              (unsigned long long)frame);
                if (std::FILE *f = std::fopen(nm, "wb"))
                {
                    std::fwrite(vram, 1, PS2_GS_VRAM_SIZE, f);
                    std::fclose(f);
                }
            }
        }
    }

    void writeEvent(const uint8_t *rdram, uint32_t state, const ps2x_dueldump::CursorInfo &ci)
    {
        if (!s_fEvents) return;
        const uint32_t dod = rd32(rdram, kDuelObjAddr) & 0x1FFFFFFFu;
        uint32_t dv = 0xFFFFFFFFu, dt = 0xFFFFFFFFu, dp = 0xFFFFFFFFu, dl = 0xFFFFFFFFu;
        if (dod)
        {
            dv = rd32(rdram, dod + 0x110u); dt = rd32(rdram, dod + 0x114u);
            dp = rd32(rdram, dod + 0x118u); dl = rd32(rdram, dod + 0x13Cu);
        }
        const uint32_t stp = rd32(rdram, kMainPtrAddr) & 0x1FFFFFFFu;
        uint32_t c620 = 0xFFFFFFFFu, c624 = 0xFFFFFFFFu, c630 = 0xFFFFFFFFu;
        if (stp)
        {
            c620 = rd32(rdram, stp + 0x620u); c624 = rd32(rdram, stp + 0x624u);
            c630 = rd32(rdram, stp + 0x630u);
        }
        const uint32_t duel[4] = {dv, dt, dp, dl};
        const uint32_t copy[3] = {c620, c624, c630};
        bool changed = (state != s_evState) || (ci.row != s_lastRow) || (ci.entry != s_lastEntry);
        for (int i = 0; i < 4 && !changed; ++i) changed = (duel[i] != s_lastDuel[i]);
        for (int i = 0; i < 3 && !changed; ++i) changed = (copy[i] != s_lastCopy[i]);
        if (!changed) return;
        s_evState = state; s_lastRow = ci.row; s_lastEntry = ci.entry;
        for (int i = 0; i < 4; ++i) s_lastDuel[i] = duel[i];
        for (int i = 0; i < 3; ++i) s_lastCopy[i] = copy[i];
        std::fprintf(s_fEvents, "%llu,0x%x,%u,0x%x,0x%x,0x%x,%u,%u,%u,%u,%u,%u,%u\n",
                     (unsigned long long)nowFrame(), state, ci.row, ci.entry, ci.handler, ci.target,
                     dv, dt, dp, dl, c620, c624, c630);
        std::fflush(s_fEvents);
    }

    void drainTextures()    {
        if (!s_fTex) return;
        std::vector<ps2x_dueldump::TexSample> q;
        { std::lock_guard<std::mutex> lk(s_texMx); q.swap(s_texQ); }
        for (const auto &s : q)
        {
            if (s_texRows >= kMaxTexRows) break;
            std::fprintf(s_fTex,
                "%llu,%016llx,%u,%u,%u,%u,%u,%u,%u,%u,%u,%016llx,%016llx,%d,%d,%d,0x%x,%u,%u,%s\n",
                (unsigned long long)s.frame, (unsigned long long)s.hash,
                s.tbp0, s.tbw, s.psm, s.tcc, s.cbp, s.cpsm, s.w, s.h, s.swizzle,
                (unsigned long long)s.texKey, (unsigned long long)s.clutKey,
                s.fbp, s.zbp, s.tfx, s.srcAddr, (unsigned)s.packHit, (unsigned)s.cacheHit, s.how);
            ++s_texRows;
        }
        if (!q.empty()) std::fflush(s_fTex);
    }

    // ---- [dueltime] -----------------------------------------------------------------
    std::string s_tmDir;
    std::FILE *s_tmCsv = nullptr;
    uint32_t s_tmCur = 0xFFFFFFFFu, s_tmState = 0xFFFFFFFFu;

    void timeDump(const uint8_t *rdram, uint32_t d, uint32_t s)
    {
        if (s_tmDir.empty())
        {
            s_tmDir = "dumps/dueltime_" + stamp();
            ensureDir(s_tmDir);
            s_tmCsv = std::fopen((s_tmDir + "/time.csv").c_str(), "w");
            if (s_tmCsv)
                std::fprintf(s_tmCsv, "frame,state,cur,t0,t1,t2,t3,t4,c620,c624,c628,c630,c634,c638,c63c,c640\n");
            std::fprintf(stderr, "[dueltime] writing to %s\n", s_tmDir.c_str());
        }
        const uint32_t frame = (uint32_t)nowFrame();
        const uint32_t state = stateOf(rdram);
        const uint32_t cur = rd32(rdram, d + 0x13Cu);
        // The value table the main executable holds (SLUS file 0x1C4482 -> RAM 0x2C3480).
        uint16_t t[5];
        for (int i = 0; i < 5; ++i) t[i] = rd16(rdram, 0x2C3480u + 2u * (uint32_t)i);
        uint32_t c[9];
        for (int i = 0; i < 9; ++i) c[i] = rd32(rdram, s + 0x620u + 4u * (uint32_t)i);
        if (s_tmCsv)
        {
            std::fprintf(s_tmCsv, "%u,0x%x,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                         frame, state, cur, t[0], t[1], t[2], t[3], t[4],
                         c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
            std::fflush(s_tmCsv);
        }
        std::fprintf(stderr, "[dueltime] frame=%u state=0x%x cur=%u table=[%u,%u,%u,%u,%u] "
                             "st620..63c=[%u,%u,%u,%u,%u,%u,%u,%u]\n",
                     frame, state, cur, t[0], t[1], t[2], t[3], t[4],
                     c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
    }

    // ---- [duelsettings] -------------------------------------------------------------
    // The Battle Settings cursor (duelObj+0x13C) is what the arrows used to move. Snapshotting the
    // duel object and the committed copies on every change, and diffing consecutive snapshots,
    // isolates the field each row actually writes its VALUE into.
    std::string s_setDir;
    std::FILE *s_setCsv = nullptr;
    uint32_t s_setCur = 0xFFFFFFFFu, s_setN = 0;
    std::vector<uint8_t> s_setDuel, s_setState;

    void settingsDump(const uint8_t *rdram, uint32_t d, uint32_t s, uint32_t cur)
    {
        if (s_setDir.empty())
        {
            s_setDir = "dumps/duelsettings_" + stamp();
            ensureDir(s_setDir);
            s_setCsv = std::fopen((s_setDir + "/settings.csv").c_str(), "w");
            if (s_setCsv) std::fprintf(s_setCsv, "n,frame,state,cur,vs,type,dp,changed_offsets\n");
            std::fprintf(stderr, "[duelsettings] writing to %s\n", s_setDir.c_str());
        }
        const uint32_t frame = (uint32_t)nowFrame();
        const uint32_t state = stateOf(rdram);
        const uint32_t vs   = rd32(rdram, d + 0x110u);
        const uint32_t type = rd32(rdram, d + 0x114u);
        const uint32_t dp   = rd32(rdram, d + 0x118u);

        std::vector<uint8_t> duel(0x400u), stb(0x60u);
        for (uint32_t i = 0; i < duel.size(); ++i) duel[i] = rd8(rdram, d + i);
        for (uint32_t i = 0; i < stb.size();  ++i) stb[i]  = rd8(rdram, s + 0x600u + i);

        std::string diff;
        if (!s_setDuel.empty())
        {
            char b[48];
            for (uint32_t i = 0; i < duel.size(); ++i)
                if (duel[i] != s_setDuel[i])
                { std::snprintf(b, sizeof b, "duel+0x%X:%02X>%02X ", i, s_setDuel[i], duel[i]); diff += b; }
            for (uint32_t i = 0; i < stb.size(); ++i)
                if (stb[i] != s_setState[i])
                { std::snprintf(b, sizeof b, "st+0x%X:%02X>%02X ", 0x600u + i, s_setState[i], stb[i]); diff += b; }
        }

        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/set_%u_%u_cur%u.txt", s_setDir.c_str(), s_setN, frame, cur);
        if (std::FILE *f = std::fopen(nm, "w"))
        {
            std::fprintf(f, "frame=%u state=0x%x cur=%u vs=%u type=%u dp=%u duelObj=0x%x stateObj=0x%x\n",
                         frame, state, cur, vs, type, dp, d, s);
            hexRegion(f, rdram, d, 0x400u, "duelObj");
            hexRegion(f, rdram, s + 0x600u, 0x60u, "stateObj+0x600");
            std::fclose(f);
        }
        if (s_setCsv)
        {
            std::fprintf(s_setCsv, "%u,%u,0x%x,%u,%u,%u,%u,%s\n",
                         s_setN, frame, state, cur, vs, type, dp, diff.c_str());
            std::fflush(s_setCsv);
        }
        std::fprintf(stderr, "[duelsettings] #%u frame=%u state=0x%x cur=%u changed: %s\n",
                     s_setN, frame, state, cur, diff.empty() ? "(first)" : diff.c_str());
        s_setDuel.swap(duel);
        s_setState.swap(stb);
        ++s_setN;
    }
}

namespace ps2x_dueldump
{
    // Where the main menu's cursor is pointing. This used to live in ps2x_net_menu.cpp for the
    // Dragon Net entry; it moved here when that entry was retired, because this dump was the only
    // thing still reading it. The formula is the game's own (0x33643C..0x33648C).
    bool queryCursor(const uint8_t *rdram, CursorInfo &out)
    {
        out = CursorInfo{};
        if (!rdram) return false;
        const uint32_t mo = rd32(rdram, kMenuObjAddr) & 0x1FFFFFFFu;
        if (!mo) return false;
        //   row = (menuObj+0x10C + menuObj+0x148 + 1) % menuObj+0x144
        const uint32_t base  = rd32(rdram, mo + 0x10Cu);
        const uint32_t curs  = rd32(rdram, mo + 0x148u);
        const uint32_t count = rd32(rdram, mo + 0x144u);
        if (!count) return false;
        const uint32_t row = (base + curs + 1u) % count;
        const uint32_t idx = rd32(rdram, mo + 0x118u + 4u * row);
        if (idx >= 11u) return false;
        out.row     = row;
        out.idx     = idx;
        out.entry   = kJumpTableAddr + 4u * idx;
        out.handler = rd32(rdram, out.entry);
        out.target  = kRowState[idx];
        out.valid   = true;
        return true;
    }

    bool enabled()
    {
        static const bool s = [](){
            const char *v = std::getenv("PS2X_DUELDUMP");
            return v && v[0] && v[0] != '0';
        }();
        return s;
    }

    void tick(uint8_t *rdram, PS2Runtime *runtime)
    {
        if (!enabled() || !rdram) return;
        const uint32_t state = stateOf(rdram);
        const uint64_t frame = nowFrame();

        if (s_armed)
        {
            if (frame - s_armFrame > kMaxFrames) { disarm(); return; }

            CursorInfo ci;
            (void)queryCursor(rdram, ci);

            // Capture the transition FIRST (hex/vram/delta at the moment we enter the screen),
            // then the event row. They use separate "previous" trackers on purpose: writeEvent
            // must not clear the transition state (it silently killed the deltas before).
            if (state != s_lastState)
            {
                if (state == kDuelState) s_sawDuel = true;
                snapshotRam(rdram, "delta", state, frame);
                dumpHexAndVram(rdram, runtime, state, frame);
                std::fprintf(stderr, "[dueldump] transition -> state 0x%x (frame %llu)\n",
                             state, (unsigned long long)frame);
                s_lastState = state;
            }
            writeEvent(rdram, state, ci);
            drainTextures();

            if (s_sawDuel && state == kMainMenuState) disarm();
            return;
        }

        if (s_done) return;
        // Arm: the main-menu cursor sits on the Duel entry (row 3), or we are already entering it.
        if (state == kMainMenuState)
        {
            CursorInfo ci;
            if (queryCursor(rdram, ci) && ci.row == kDuelRow) arm(rdram, runtime);
        }
        else if (state == kDuelState)
        {
            arm(rdram, runtime);
        }
    }

    void pushTexSample(const TexSample &s)
    {
        if (!s_armed) return;
        std::lock_guard<std::mutex> lk(s_texMx);
        if (s_texQ.size() < 65536u) s_texQ.push_back(s);
    }

    void offerTextureSample(const uint8_t *vram, uint32_t tbp0, uint32_t tbw, uint32_t psm,
                            uint32_t tw, uint32_t th, const uint32_t *clut,
                            uint32_t ta0, uint32_t aem, uint32_t ta1,
                            uint32_t cbp, uint32_t csa, uint32_t csm, uint32_t cpsm,
                            uint64_t texKey, int w, int h, const uint8_t *rgba, const char *how)
    {
        if (!s_armed) return;
        ps2tex::TexIdent id;
        const bool pal = (psm == 19u || psm == 20u);
        if (!ps2tex::identify(vram, tbp0, tbw, (uint8_t)psm, (uint8_t)tw, (uint8_t)th,
                              pal ? clut : nullptr, ta0, aem, ta1, id, cbp, csa, csm, cpsm))
            return;
        TexSample s;
        s.frame = nowFrame();
        s.hash = id.tex0Hash;
        s.clutKey = id.clutHash;
        s.texKey = texKey;
        s.tbp0 = tbp0; s.tbw = tbw; s.psm = psm; s.tcc = id.bits & 1u;
        s.cbp = cbp; s.cpsm = cpsm;
        s.w = (uint32_t)(w > 0 ? w : 0);
        s.h = (uint32_t)(h > 0 ? h : 0);
        // All PS2 texture formats are swizzled except the CT24/CT16S variants (psm 1 and 10).
        s.swizzle = (uint8_t)((psm == 1u || psm == 10u) ? 0u : 1u);
        s.srcAddr = tbp0 * 64u;   // VRAM byte address of the source block
        std::snprintf(s.how, sizeof s.how, "%s", how ? how : "?");
        pushTexSample(s);
        if (rgba && w > 0 && h > 0) offerDecodedTexture(id.tex0Hash, rgba, (uint32_t)w, (uint32_t)h);

        // [netmenu] Swizzle probe: PS2X_NETMENU_TEXPROBE=<tbp0> dumps everything needed to derive the
        // exact texel map for one texture -- the params, the CLUT (256 u32), the whole VRAM and the
        // runtime's decoded RGBA -- so the permutation can be solved offline instead of guessed.
        static const long s_probeTbp = []() {
            const char *v = std::getenv("PS2X_NETMENU_TEXPROBE");
            return v ? std::strtol(v, nullptr, 0) : -1L;
        }();
        if (s_probeTbp >= 0 && (long)tbp0 == s_probeTbp && rgba && w > 0 && h > 0)
        {
            char nm[512];
            std::snprintf(nm, sizeof nm, "%s/probe_%u_%dx%d.bin", s_dir.c_str(), tbp0, w, h);
            FILE *pf = std::fopen(nm, "wb");
            if (pf)
            {
                const uint32_t hdr[12] = { tbp0, tbw, psm, tw, th, cbp, csa, csm, cpsm,
                                           (uint32_t)w, (uint32_t)h, 0u };
                std::fwrite(hdr, 4, 12, pf);
                if (clut) std::fwrite(clut, 4, 256, pf);
                if (vram) std::fwrite(vram, 1, 4u * 1024u * 1024u, pf);
                std::fwrite(rgba, 1, (size_t)w * (size_t)h * 4u, pf);
                std::fclose(pf);
                std::fprintf(stderr, "[texprobe] dumped %s (tbp=%u %dx%d psm=%u cbp=%u)\n",
                             nm, tbp0, w, h, psm, cbp);
            }
        }
    }

    void tickTime(uint8_t *rdram)
    {
        if (!rdram) return;
        static const bool on = [](){
            const char *v = std::getenv("PS2X_DUELTIME");
            return v && v[0] && v[0] != '0';
        }();
        if (!on) return;
        const uint32_t d = rd32(rdram, kDuelObjAddr) & 0x1FFFFFFFu;
        const uint32_t s = rd32(rdram, kMainPtrAddr) & 0x1FFFFFFFu;
        if (!d || !s) return;
        const uint32_t state = stateOf(rdram);
        const uint32_t cur = rd32(rdram, d + 0x13Cu);
        if (state == s_tmState && cur == s_tmCur && s_tmCsv) return;   // log on change
        s_tmState = state;
        s_tmCur = cur;
        timeDump(rdram, d, s);
    }

    void tickSettings(uint8_t *rdram)
    {
        if (!rdram) return;
        static const bool on = [](){
            const char *v = std::getenv("PS2X_DUELSETTINGS");
            return v && v[0] && v[0] != '0';
        }();
        if (!on) return;
        const uint32_t d = rd32(rdram, kDuelObjAddr) & 0x1FFFFFFFu;
        const uint32_t s = rd32(rdram, kMainPtrAddr) & 0x1FFFFFFFu;
        if (!d || !s) return;
        const uint32_t cur = rd32(rdram, d + 0x13Cu);
        if (cur == s_setCur && s_setN != 0u) return;   // nothing changed
        s_setCur = cur;
        settingsDump(rdram, d, s, cur);
    }

    void offerDecodedTexture(uint64_t hash, const uint8_t *rgba, uint32_t w, uint32_t h)
    {
        if (!s_armed || !rgba || !w || !h) return;
        for (uint64_t seen : s_pngHashes) if (seen == hash) return;
        if (s_pngHashes.size() > 4096u) return;
        s_pngHashes.push_back(hash);
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/png/%016llx.png", s_dir.c_str(), (unsigned long long)hash);
        Image img{};
        img.data = const_cast<uint8_t *>(rgba);
        img.width = (int)w; img.height = (int)h; img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        if (ExportImage(img, nm)) std::fprintf(stderr, "[dueldump] png %ux%u -> %s\n", w, h, nm);
    }
}
