#pragma once

// [perf] Live performance facts for the in-game overlay's Video tab. Same shape and intent as
// ps2_video_status.h: a plain struct in its own light header plus a free function the UI polls once
// per frame, so the overlay does not have to reach into the runtime.
//
// The one rule this file exists to enforce: a number is meaningless without saying what it covers.
// The old [fps] line printed `gpu_ms` from three backends that meant three different things, and on
// two of them (software mode, and PS2X_GPUSPLIT) it silently read 0 -- which looks like "the GPU is
// idle" rather than "we never measured". So every figure here travels with its source and, for GPU
// work, its coverage.
//
// On "display fps": this is the rate at which the host PRESENTED frames, which equals what the panel
// scans out whenever the present blocks on vsync, and equals the submit rate when it does not. It is
// not a scanout measurement, and callers should show it next to the monitor's nominal refresh so a
// vsync-locked 60 reads as 60/60 rather than as a suspicious round number.

namespace ps2x
{
// Where a GPU-busy figure came from. SoftwareCpu is not a GPU measurement at all: the software
// backend rasterises on the CPU, so its "GPU" row is labelled as CPU rather than shown as 0%.
enum class GpuSource : int
{
    None = 0,
    OpenGL,             // GL_TIME_ELAPSED spanning the frame
    VulkanTimestamps,   // paraLLEl-GS, real GPU timestamps, brackets scanout + readback
    SoftwareCpu,        // no GPU: the number is CPU rasterisation time
};

// How much of the frame the measurement spans. A busy% with FullFrame coverage is comparable with a
// vendor 3D-engine counter; with DrawListOnly it is a lower bound and must be shown as one.
enum class GpuQuality : int
{
    Unknown = 0,
    FullFrame,     // the whole presented frame
    DrawListOnly,  // the GS draw list only: no composite, no present
    CpuOnly,       // not a GPU measurement
};

struct PerfStatus
{
    // --- present rate and frame pacing ---
    bool   valid = false;          // false until the first present lands
    double displayFps = 0.0;       // presents per second, smoothed
    int    displayRefreshHz = 0;   // the panel's nominal rate, from the monitor; 0 if unknown
    // Frame time distribution in ms. p95 is the one that exposes stutter: a mean of 16.7 can hide
    // 50 ms hitch frames, which is exactly what bugs 4 (micro-freezes) and 5 (60fps patch) are.
    float  frameMsP50 = 0.0f;
    float  frameMsP95 = 0.0f;
    float  frameMsMax = 0.0f;
    int    frameSamples = 0;       // how many presents the percentiles are computed over

    // --- CPU side, both as a percentage of wall time over the same window ---
    double guestPct = 0.0;         // guest/emulated threads, CPU busy time
    double submitPct = 0.0;        // driver flush/submit time, GL only; 0 elsewhere

    // --- GPU side, with its unit and its honesty about coverage ---
    double   gpuBusyPct = 0.0;     // comparable with Task Manager's 3D engine column
    double   gpuMsPerFrame = 0.0;  // against the frame budget, for "am I GPU bound?"
    GpuSource gpuSource = GpuSource::None;
    GpuQuality gpuQuality = GpuQuality::Unknown;
    double   gpuCoverage = 0.0;    // 0..1, what fraction of the frame the measurement spans
    int      gpuSamples = 0;       // queries that landed in this window
    int      gpuSamplesDropped = 0;// queries whose result had not landed and were discarded

    // A percentage is only meaningful with samples behind it, and "the backend registered but the
    // guest issued no draw list this window" is a real state that a bare 0% cannot express -- it looks
    // exactly like an idle GPU. gpuSamples == 0 with a real gpuSource is that state. Measured live:
    // three consecutive windows went 61% -> 0.65% -> 0.61% purely because the guest stopped drawing.
    bool gpuMeasured() const { return gpuSamples > 0; }

    // Wall seconds covered by the most recent sample. A caller updating its own average needs this
    // rather than assuming a fixed interval.
    double windowSeconds = 0.0;
};

// Filled by the runtime once per present; safe to poll every frame.
PerfStatus GetPerfStatus();

// True while the perf overlay is enabled, so the runtime can skip the work when nobody is watching.
// GPU timing queries are not free and this keeps the default path paying nothing.
void SetPerfOverlayEnabled(bool on);
bool PerfOverlayEnabled();

// Tell the perf module which backend is running and how good its GPU measurement is. The OpenGL and
// Vulkan backends call this once at init; the software path never does, and its absence is what marks
// the figures as CPU-only.
void PerfSetGpuSource(GpuSource source, GpuQuality quality);

// Add GPU busy time for the current window. samplesLanded is how many measurements backed `ns` and
// samplesDropped how many were discarded because their result had not arrived. Keeping the landed
// count is what lets a caller tell a real 0% from "nothing was measured this window".
void PerfAddGpuBusyNs(unsigned long long ns, int samplesLanded, int samplesDropped);

// The CPU counters live in other translation units' namespaces, so the runtime's once-a-second stats
// tick publishes absolute totals and this module differences them. Passing 0 until the first publish
// is fine: the CPU percentages simply stay 0 rather than reading as a divide by nothing.
void PerfPublishCpu(unsigned long long guestBusyNs, unsigned long long wallNs, unsigned long long submitNs);

// The panel's nominal refresh rate, so a present rate of "60" can be shown as 60/60 rather than as a
// suspicious round number. Set at init and whenever the monitor changes.
void PerfSetRefreshHz(int hz);

// Called by the runtime's main loop once per present. The fps/percentile window only recomputes at
// 1 Hz, so the per-frame cost is a timestamp store.
void PerfTick(double frameSeconds);

}   // namespace ps2x
