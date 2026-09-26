#pragma once

#include <cstdint>
#include <string>

// Best-effort cross-platform hardware probe for the install-wizard summary. Every field is
// left empty/0 when it cannot be determined, so the summary degrades instead of lying.
namespace hw
{
    struct Info
    {
        std::string cpuName;
        int logicalCores = 0;
        int physicalCores = 0;
        std::uint64_t ramMB = 0;
        std::string gpuName;
        std::uint64_t vramMB = 0;
    };

    Info detect();

    // "AMD Ryzen 5 5500 (6C/12T)  ·  16 GB RAM  ·  Radeon RX 580 4 GB"
    std::string summary(const Info &info);

    // Single-thread micro-benchmark (integer dependency chain, not vectorizable, ~40-90 ms).
    // Returns R relative to a Zen 3 (Ryzen 5 5500 = 1.0). 0.0 on failure.
    double benchSingleThreadR();

    struct Recommendation
    {
        std::string tierName;
        int renderScale = 1;
        bool widescreen = true;
        bool texPackFull = false;
        bool fps60 = false;
        int windowMode = 1; // 0 windowed, 1 borderless, 2 fullscreen
    };

    // Map detected hardware + the CPU R to the settings ceiling.
    Recommendation recommend(const Info &info, double cpuR);
}
