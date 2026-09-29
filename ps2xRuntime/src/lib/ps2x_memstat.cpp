// [memstat] Process memory for the periodic log lines: a session that "lags more the longer it
// runs" needs the resident size and the GPU's free memory next to the frame rate, or the log
// cannot tell a leak from a slow machine. Player2's 2026-09-30 log ran the RTX 5080 out of
// its 16 GB after six minutes of story mode while Granite tracked 1.5 GB of it.
#include "runtime/ps2x_memstat.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

uint64_t ps2xRssMB()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return uint64_t(pmc.WorkingSetSize) >> 20;
    return 0u;
#elif defined(__APPLE__)
    mach_task_basic_info info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return uint64_t(info.resident_size) >> 20;
    return 0u;
#else
    // /proc/self/statm: size resident shared text lib data dt, in pages.
    if (FILE *f = std::fopen("/proc/self/statm", "r"))
    {
        unsigned long size = 0, resident = 0;
        const int n = std::fscanf(f, "%lu %lu", &size, &resident);
        std::fclose(f);
        if (n == 2)
            return (uint64_t(resident) * uint64_t(sysconf(_SC_PAGESIZE))) >> 20;
    }
    return 0u;
#endif
}
