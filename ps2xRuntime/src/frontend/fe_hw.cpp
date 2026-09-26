#include "frontend/fe_hw.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#pragma comment(lib, "dxgi.lib")
#elif defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace
{
    std::string trimmed(const char *begin, std::size_t n)
    {
        // Registry and /proc buffers are NUL-terminated inside the reported length, so the
        // terminator has to end the string first: the CPU name comes back space-padded and the
        // padding would otherwise survive the trim.
        std::size_t len = 0;
        while (len < n && begin[len] != '\0')
            ++len;
        std::string s(begin, len);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
            s.pop_back();
        std::size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
            ++start;
        return s.substr(start);
    }
}

namespace hw
{
    Info detect()
    {
        Info i;
        i.logicalCores = (int)std::thread::hardware_concurrency();

#if defined(_WIN32)
        {
            HKEY k = nullptr;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                              "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                              0, KEY_READ, &k) == ERROR_SUCCESS)
            {
                char buf[256] = "";
                DWORD len = sizeof buf;
                if (RegQueryValueExA(k, "ProcessorNameString", nullptr, nullptr,
                                     (LPBYTE)buf, &len) == ERROR_SUCCESS)
                    i.cpuName = trimmed(buf, len < sizeof buf ? len : sizeof buf - 1);
                RegCloseKey(k);
            }
        }
        {
            DWORD len = 0;
            GetLogicalProcessorInformation(nullptr, &len);
            if (len)
            {
                std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(
                    len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
                if (GetLogicalProcessorInformation(buf.data(), &len))
                    for (const auto &e : buf)
                        if (e.Relationship == RelationProcessorCore)
                            ++i.physicalCores;
            }
        }
        {
            MEMORYSTATUSEX ms{};
            ms.dwLength = sizeof ms;
            if (GlobalMemoryStatusEx(&ms))
                i.ramMB = ms.ullTotalPhys / (1024ull * 1024ull);
        }
        {
            IDXGIFactory1 *fac = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac)) && fac)
            {
                IDXGIAdapter1 *ad = nullptr;
                for (UINT a = 0; fac->EnumAdapters1(a, &ad) == S_OK; ++a)
                {
                    DXGI_ADAPTER_DESC1 d{};
                    if (SUCCEEDED(ad->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                    {
                        const int need = WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, nullptr, 0,
                                                            nullptr, nullptr);
                        if (need > 0)
                        {
                            std::vector<char> utf8((std::size_t)need);
                            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, utf8.data(), need,
                                                nullptr, nullptr);
                            i.gpuName = trimmed(utf8.data(), (std::size_t)need);
                        }
                        i.vramMB = d.DedicatedVideoMemory / (1024ull * 1024ull);
                        ad->Release();
                        break;
                    }
                    ad->Release();
                }
                fac->Release();
            }
        }
#elif defined(__linux__)
        {
            if (FILE *f = std::fopen("/proc/cpuinfo", "r"))
            {
                char line[512];
                while (std::fgets(line, sizeof line, f))
                    if (std::strncmp(line, "model name", 10) == 0)
                    {
                        if (char *c = std::strchr(line, ':'))
                            i.cpuName = trimmed(c + 1, std::strlen(c + 1));
                        break;
                    }
                std::fclose(f);
            }
            struct sysinfo si{};
            if (sysinfo(&si) == 0)
                i.ramMB = (std::uint64_t)si.totalram * si.mem_unit / (1024ull * 1024ull);
        }
#elif defined(__APPLE__)
        {
            char buf[256] = "";
            size_t len = sizeof buf;
            if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0)
                i.cpuName = trimmed(buf, std::strlen(buf));
            std::uint64_t mem = 0;
            len = sizeof mem;
            if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
                i.ramMB = mem / (1024ull * 1024ull);
        }
#endif
        return i;
    }

    std::string summary(const Info &i)
    {
        std::vector<std::string> parts;
        if (!i.cpuName.empty())
        {
            std::string c = i.cpuName;
            if (i.logicalCores > 0)
            {
                if (i.physicalCores > 0)
                    c += " (" + std::to_string(i.physicalCores) + "C/" +
                         std::to_string(i.logicalCores) + "T)";
                else
                    c += " (" + std::to_string(i.logicalCores) + "T)";
            }
            parts.push_back(c);
        }
        else if (i.logicalCores > 0)
        {
            parts.push_back(std::to_string(i.logicalCores) + " threads");
        }

        if (i.ramMB)
            parts.push_back(std::to_string((i.ramMB + 512) / 1024) + " GB RAM");

        if (!i.gpuName.empty())
        {
            std::string g = i.gpuName;
            if (i.vramMB)
                g += " " + std::to_string((i.vramMB + 512) / 1024) + " GB";
            parts.push_back(g);
        }

        std::string out;
        for (std::size_t k = 0; k < parts.size(); ++k)
        {
            if (k)
                out += "   ·   ";
            out += parts[k];
        }
        return out;
    }

    double benchSingleThreadR()
    {
        // The volatile sink is what stops the loop from being optimized away; a plain
        // uint64 would let the compiler hoist the whole multiply chain out of the
        // timed loop, and -Wvolatile rejects the compound assignment (deprecated in
        // C++20, so -Wextra warns on gcc/clang).
        volatile std::uint64_t sink = 0;
        std::uint64_t x = 0x9E3779B97F4A7C15ull;
        constexpr int N = 30000000;
        for (int i = 0; i < N / 10; ++i)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            x ^= x >> 33;
            sink = sink + x;
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
        {
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            x ^= x >> 33;
            sink = sink + x;
        }
        const auto t1 = std::chrono::steady_clock::now();
        (void)sink;
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        static const double kZen3RefNs = 43000000.0;
        if (ns <= 0.0)
            return 0.0;
        return kZen3RefNs / ns;
    }

    Recommendation recommend(const Info &info, double cpuR)
    {
        Recommendation r;
        if (cpuR <= 0.0)
            cpuR = 1.0; // benchmark failed: assume the reference

        if (cpuR < 0.55)
        {
            r.tierName = "Minimum";
            r.renderScale = 1;
            r.fps60 = false;
        }
        else if (cpuR < 0.80)
        {
            r.tierName = "Entry";
            r.renderScale = 2;
            r.fps60 = false;
        }
        else if (cpuR < 1.10)
        {
            r.tierName = "Balanced";
            r.renderScale = 3;
            r.fps60 = true;
        }
        else
        {
            r.tierName = "Alta";
            r.renderScale = 4;
            r.fps60 = true;
        }

        if (info.logicalCores > 0 && info.logicalCores < 4 && r.renderScale > 1)
            --r.renderScale;

        r.widescreen = true;
        r.texPackFull = info.ramMB >= 12000 && (info.vramMB == 0 || info.vramMB >= 2500);
        return r;
    }
}
