// [injected] See include/runtime/ps2x_injected.h.

#include "runtime/ps2x_injected.h"

#include "ps2_runtime.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace
{
    std::unordered_map<uint32_t, ps2x_injected::Fn> &table()
    {
        static std::unordered_map<uint32_t, ps2x_injected::Fn> t;
        return t;
    }
    std::mutex &mtx()
    {
        static std::mutex m;
        return m;
    }
}

namespace ps2x_injected
{
    void add(uint32_t addr, Fn fn)
    {
        if (!fn) return;
        addr &= 0x1FFFFFFFu;
        std::lock_guard<std::mutex> lk(mtx());
        table()[addr] = fn;
        std::fprintf(stderr, "[injected] registered 0x%08x\n", addr);
    }    Fn find(uint32_t addr)
    {
        if (table().empty()) return nullptr;   // hot path: nothing injected
        addr &= 0x1FFFFFFFu;
        // [DIAG] temporary: show every lookup aimed at our code range
        if (addr >= 0x00D00000u && addr < 0x00E00000u)
        {
            static std::atomic<int> s_n{0};
            if (s_n.fetch_add(1) < 20) std::fprintf(stderr, "[injected] lookup 0x%08x\n", addr);
        }
        std::lock_guard<std::mutex> lk(mtx());
        const auto it = table().find(addr);
        return (it == table().end()) ? nullptr : it->second;
    }

    uint32_t count()
    {
        std::lock_guard<std::mutex> lk(mtx());
        return static_cast<uint32_t>(table().size());
    }
}
