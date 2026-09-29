#include "frontend/fe_gpu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <dxgi1_2.h>
#  include <SDL_video.h>
#endif

namespace gpu
{
    namespace
    {
        std::string lower(std::string s)
        {
            for (char &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // "NVIDIA GeForce RTX 3060" vs "GeForce RTX 3060": the GL string often carries vendor
        // noise the DXGI name lacks, so a substring match either way is the useful test.
        bool sameAdapter(const std::string &a, const std::string &b)
        {
            if (a.empty() || b.empty())
                return false;
            const std::string la = lower(a), lb = lower(b);
            if (la == lb)
                return true;
            return la.find(lb) != std::string::npos || lb.find(la) != std::string::npos;
        }

#if defined(_WIN32)
        // The key Windows uses for "which GPU should this exe use". Written per executable path
        // under HKCU, which needs no admin rights.
        const char *const kGpuPrefKey = "Software\\Microsoft\\DirectX\\UserGpuPreferences";

        std::string wideToUtf8(const wchar_t *w)
        {
            const int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (need <= 1)
                return std::string();
            std::vector<char> utf8(static_cast<std::size_t>(need));
            WideCharToMultiByte(CP_UTF8, 0, w, -1, utf8.data(), need, nullptr, nullptr);
            return std::string(utf8.data());
        }

        std::string ownExePath()
        {
            char buf[MAX_PATH] = "";
            const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
            return n > 0 && n < MAX_PATH ? std::string(buf, n) : std::string();
        }
#endif
    }

    bool supported()
    {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }

    std::string currentRenderer()
    {
#if defined(_WIN32)
        // Resolved through SDL so this TU does not have to pull in a GL header, which is kept
        // away from ImGui's GL backend on purpose.
        typedef const unsigned char *(*PFN_glGetString)(unsigned int);
        PFN_glGetString fn = (PFN_glGetString)SDL_GL_GetProcAddress("glGetString");
        if (!fn)
            return std::string();
        const unsigned char *s = fn(0x1F01 /*GL_RENDERER*/);
        return s ? std::string(reinterpret_cast<const char *>(s)) : std::string();
#else
        return std::string();
#endif
    }

    std::vector<Adapter> adapters(const std::string &activeName)
    {
        std::vector<Adapter> out;
#if defined(_WIN32)
        IDXGIFactory1 *fac = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&fac))) ||
            !fac)
            return out;
        for (UINT i = 0;; ++i)
        {
            IDXGIAdapter1 *ad = nullptr;
            if (fac->EnumAdapters1(i, &ad) != S_OK || !ad)
                break;
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(ad->GetDesc1(&d)))
            {
                Adapter a;
                a.name = wideToUtf8(d.Description);
                a.vramMB = d.DedicatedVideoMemory / (1024ull * 1024ull);
                a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                a.active = sameAdapter(a.name, activeName);
                out.push_back(a);
            }
            ad->Release();
        }
        fac->Release();
        // Most VRAM first: that is the "performance" adapter Windows would pick, and the page
        // lists them in that order so the default entry is the fast one.
        std::stable_sort(out.begin(), out.end(), [](const Adapter &a, const Adapter &b) {
            if (a.software != b.software)
                return !a.software;
            return a.vramMB > b.vramMB;
        });
#else
        (void)activeName;
#endif
        return out;
    }

    bool applyPreference(const std::string &adapterName, std::string *error)
    {
#if defined(_WIN32)
        const std::string exe = ownExePath();
        if (exe.empty())
        {
            if (error)
                *error = "could not get the executable path";
            return false;
        }

        // 2 = high performance, 1 = power saving. The adapter with the most VRAM gets the
        // performance flag, which is the same choice Windows' own Settings page offers.
        int preference = -1;
        if (!adapterName.empty())
        {
            const std::vector<Adapter> list = adapters(std::string());
            if (list.empty())
            {
                if (error)
                    *error = "could not enumerate the GPUs";
                return false;
            }
            const Adapter *chosen = nullptr;
            const Adapter *biggest = nullptr;
            for (const Adapter &a : list)
            {
                if (a.software)
                    continue;
                if (!biggest || a.vramMB > biggest->vramMB)
                    biggest = &a;
                if (sameAdapter(a.name, adapterName))
                    chosen = &a;
            }
            if (!chosen)
            {
                if (error)
                    *error = "the selected GPU is no longer connected";
                return false;
            }
            preference = (biggest && chosen != biggest) ? 1 : 2;
        }

        HKEY key = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, kGpuPrefKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                           &key, nullptr) != ERROR_SUCCESS)
        {
            if (error)
                *error = "could not open the Windows preferences key";
            return false;
        }
        char data[64] = "";
        if (preference > 0)
            std::snprintf(data, sizeof data, "GpuPreference=%d;", preference);
        const DWORD len = static_cast<DWORD>(std::strlen(data));
        const LONG rc = RegSetValueExA(key, exe.c_str(), 0, REG_SZ,
                                       reinterpret_cast<const BYTE *>(data), len);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS)
        {
            if (error)
                *error = "Windows did not accept the GPU preference";
            return false;
        }
        if (error)
            *error = preference > 0 ? "applies when the game restarts" : "preference cleared";
        return true;
#else
        (void)adapterName;
        if (error)
            *error = "Windows only";
        return false;
#endif
    }
}
