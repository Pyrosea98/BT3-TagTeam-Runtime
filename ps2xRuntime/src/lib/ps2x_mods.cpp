// ps2x_mods.cpp -- loadable game mods: the folder scan, the API table handed to each mod, the registration slots.
#include "ps2x_mods.h"
#include "ps2_runtime.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

extern std::atomic<uint64_t> g_bt3FrameCount;   // game_overrides.cpp: the frame hook's counter
namespace ps2_stubs { uint16_t ps2xLivePadButtons(int player, uint8_t &lx, uint8_t &ly, uint8_t &rx, uint8_t &ry); }   // pad_config.cpp

std::atomic<int> g_ps2xModCurtainWant{0};

namespace
{
    Ps2xGuestFn g_frameHook = nullptr;
    int (*g_hudProvider)(uint32_t *, int) = nullptr;
    int (*g_feedProvider)(uint32_t *, int) = nullptr;
    const char *(*g_nameProvider)(uint32_t) = nullptr;
    const std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();
    std::string g_exeDir;
    std::vector<void *> g_handles;

    const char *exeDirC()
    {
        if (!g_exeDir.empty()) return g_exeDir.c_str();
        if (const char *e = std::getenv("PS2X_EXEDIR"); e && e[0]) { g_exeDir = e; return g_exeDir.c_str(); }
        char buf[4096] = {};
#if defined(_WIN32)
        const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf) - 1);
        if (n > 0) { g_exeDir = std::filesystem::path(std::string(buf, n)).parent_path().string(); }
#else
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = 0; g_exeDir = std::filesystem::path(buf).parent_path().string(); }
#endif
        if (g_exeDir.empty()) g_exeDir = ".";
        return g_exeDir.c_str();
    }
    int apiReplace(PS2Runtime *rt, uint32_t address, Ps2xGuestFn fn) { return rt && rt->replaceFunction(address, fn) ? 1 : 0; }
    Ps2xGuestFn apiLookup(PS2Runtime *rt, uint32_t address) { return rt ? rt->lookupFunction(address) : nullptr; }
    int apiHas(PS2Runtime *rt, uint32_t address) { return rt && rt->hasFunction(address) ? 1 : 0; }
    int apiStop(PS2Runtime *rt) { return rt && rt->isStopRequested() ? 1 : 0; }
    uint64_t apiFrame() { return g_bt3FrameCount.load(std::memory_order_relaxed); }
    double apiSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count(); }
    uint32_t apiPad(int player, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry)
    {
        uint8_t a = 0x80, b = 0x80, c = 0x80, d = 0x80;
        const uint16_t w = ps2_stubs::ps2xLivePadButtons(player, a, b, c, d);
        if (lx) *lx = a; if (ly) *ly = b; if (rx) *rx = c; if (ry) *ry = d;
        return w;
    }
    void apiLog(const char *line) { if (line) std::fprintf(stderr, "%s\n", line); }
    void apiSetFrame(Ps2xGuestFn fn) { g_frameHook = fn; }
    void apiSetCurtain(int want) { g_ps2xModCurtainWant.store(want ? 1 : 0, std::memory_order_relaxed); }
    void apiSetHud(int (*fn)(uint32_t *, int)) { g_hudProvider = fn; }
    void apiSetFeed(int (*fn)(uint32_t *, int), const char *(*name)(uint32_t)) { g_feedProvider = fn; g_nameProvider = name; }

    Ps2xModApi g_api = {};

    bool disabled(const std::string &name)
    {
        if (const char *all = std::getenv("PS2X_MODS"); all && all[0] == '0') return true;
        const char *csv = std::getenv("PS2X_MODS_DISABLE"); if (!csv || !csv[0]) return false;
        std::string list = csv; size_t p = 0;
        while (p <= list.size())
        {
            size_t q = list.find(',', p); if (q == std::string::npos) q = list.size();
            if (list.compare(p, q - p, name) == 0) return true;
            p = q + 1;
        }
        return false;
    }
}

void ps2xModsInstall(PS2Runtime &runtime)
{
    static bool s_done = false; if (s_done) return; s_done = true;
    g_api.size = sizeof(Ps2xModApi); g_api.version = PS2X_MOD_API_VERSION; g_api.runtime = &runtime; g_api.ramSize = PS2_RAM_SIZE;
    g_api.replaceFunction = &apiReplace; g_api.lookupFunction = &apiLookup; g_api.hasFunction = &apiHas; g_api.stopRequested = &apiStop;
    g_api.frameCount = &apiFrame; g_api.secondsSinceBoot = &apiSeconds; g_api.exeDir = &exeDirC; g_api.livePadButtons = &apiPad; g_api.log = &apiLog;
    g_api.setFrameHook = &apiSetFrame; g_api.setOverlayCurtain = &apiSetCurtain; g_api.setHudProvider = &apiSetHud; g_api.setFeedProvider = &apiSetFeed;
    std::filesystem::path dir = std::filesystem::path(exeDirC()) / "mods";
    if (const char *d = std::getenv("PS2X_MODS_DIR"); d && d[0]) dir = d;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::filesystem::path> files;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec))
    {
        const std::string ext = e.path().extension().string();
#if defined(_WIN32)
        if (ext == ".dll") files.push_back(e.path());
#elif defined(__APPLE__)
        if (ext == ".dylib" || ext == ".so") files.push_back(e.path());
#else
        if (ext == ".so") files.push_back(e.path());
#endif
    }
    std::sort(files.begin(), files.end());
    for (const auto &f : files)
    {
        const std::string name = f.stem().string();
        if (disabled(name)) { std::fprintf(stderr, "[mods] %s: disabled\n", name.c_str()); continue; }
#if defined(_WIN32)
        HMODULE h = LoadLibraryA(f.string().c_str());
        if (!h) { std::fprintf(stderr, "[mods] %s: cannot load (error %lu)\n", name.c_str(), (unsigned long)GetLastError()); continue; }
        Ps2xModInitFn init = (Ps2xModInitFn)GetProcAddress(h, "ps2xModInit");
#else
        void *h = dlopen(f.string().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) { std::fprintf(stderr, "[mods] %s: cannot load: %s\n", name.c_str(), dlerror()); continue; }
        Ps2xModInitFn init = (Ps2xModInitFn)dlsym(h, "ps2xModInit");
#endif
        if (!init) { std::fprintf(stderr, "[mods] %s: no ps2xModInit, ignored\n", name.c_str()); continue; }
        const int rc = init(&g_api);
        std::fprintf(stderr, "[mods] %s: %s\n", name.c_str(), rc == 0 ? "loaded" : (rc == 1 ? "declined" : "failed"));
        g_handles.push_back((void *)h);
    }
}

void ps2xModsFrame(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { if (g_frameHook) g_frameHook(rdram, ctx, runtime); }
int ps2xModsHud(uint32_t *out, int maxEntries) { return g_hudProvider ? g_hudProvider(out, maxEntries) : 0; }
int ps2xModsFeed(uint32_t *out, int maxEntries) { return g_feedProvider ? g_feedProvider(out, maxEntries) : 0; }
const char *ps2xModsCharName(uint32_t id) { const char *s = g_nameProvider ? g_nameProvider(id) : nullptr; return s ? s : ""; }
