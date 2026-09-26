// [perf] The accumulator behind ps2x::GetPerfStatus(). See runtime/ps2x_perf_status.h for why the
// GPU figure carries a source and a coverage.
//
// Threading: PerfTick() runs on the runtime's main loop once per present. PerfAddGpuBusyNs() runs on
// whichever thread the renderer's query harvest is on (the GL thread / the kick worker), and
// PerfPublishCpu() runs on the main loop's once-a-second stats tick. The ring is therefore
// mutex-guarded, but only the per-frame store and the 1 Hz recompute take it, and neither is long:
// the recompute sorts at most kFrameSamples doubles, once a second.

#include "runtime/ps2x_perf_status.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace
{
// ~4 s of presents at 60 fps, and the percentile window is the whole ring: a longer history smooths
// more but stops reacting to a stutter that just started, which is the case we care about.
constexpr int kFrameSamples = 240;
constexpr double kWindowSeconds = 1.0;

struct PerfState
{
    std::mutex mtx;

    // Present-to-present deltas in ms, in a ring.
    double frameMs[kFrameSamples] = {};
    int frameCount = 0;          // presents recorded in the ring
    int frameNext = 0;           // write cursor

    // Current window.
    double windowPresents = 0.0;
    double windowSeconds = 0.0;

    // CPU side. The counters live in other translation units' namespaces, so the runtime's [fps]
    // block publishes absolute totals once a second via PerfPublishCpu(), which differences them
    // against the previous publish and accumulates the delta into the current window.
    unsigned long long guestBusyNs = 0;
    unsigned long long wallNs = 0;
    unsigned long long submitNs = 0;
    unsigned long long cpuGuestWindowNs = 0;
    unsigned long long cpuWallWindowNs = 0;
    unsigned long long cpuSubmitWindowNs = 0;
    bool cpuSeen = false;
    bool cpuResetSeen = false;   // a counter went backwards this window: the CPU figures are unknown

    // GPU side, accumulated over the current window.
    unsigned long long gpuBusyNs = 0;
    int gpuSamplesLanded = 0;
    int gpuDropped = 0;

    ps2x::GpuSource gpuSource = ps2x::GpuSource::None;
    ps2x::GpuQuality gpuQuality = ps2x::GpuQuality::Unknown;
    int refreshHz = 0;
    bool overlayEnabled = false;

    // The last computed window, handed out by GetPerfStatus().
    ps2x::PerfStatus out{};
};

PerfState g_perf;

// Nearest-rank percentile of a SORTED sample, so p95 is a frame that actually happened rather than an
// interpolation between two that did not -- for stutter hunting you want the real one.
double percentileOfSorted(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
        return 0.0;
    const double rank = p * (static_cast<double>(sorted.size()) - 1.0);
    size_t i = static_cast<size_t>(std::lround(rank));
    if (i >= sorted.size())
        i = sorted.size() - 1;
    return sorted[i];
}

// Coverage is a property of the source, not of the run. A backend that only ever times its draw list
// covers an unknown share of the frame and the UI shows it as a lower bound; the software backend is
// not measuring a GPU at all.
double coverageOf(ps2x::GpuQuality q)
{
    return q == ps2x::GpuQuality::FullFrame ? 1.0 : 0.0;
}

}   // namespace

namespace ps2x
{

void SetPerfOverlayEnabled(bool on)
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    g_perf.overlayEnabled = on;
}

bool PerfOverlayEnabled()
{
    return g_perf.overlayEnabled;
}

void PerfSetGpuSource(GpuSource source, GpuQuality quality)
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    g_perf.gpuSource = source;
    g_perf.gpuQuality = quality;
}

void PerfSetRefreshHz(int hz)
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    g_perf.refreshHz = hz > 0 ? hz : 0;
    g_perf.out.displayRefreshHz = g_perf.refreshHz;
}

void PerfPublishCpu(unsigned long long guestBusyNs, unsigned long long wallNs, unsigned long long submitNs)
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    // These arrive as CUMULATIVE totals, not per-window values, and the difference matters: dividing
    // an absolute nanosecond count by a one-second window produced guest_pct values in the hundreds
    // and thousands, which is not a percentage of anything. Take the delta here, where the previous
    // sample lives, and accumulate it into the window -- a single publish per second is then one
    // window's worth of work.
    //
    // A counter that moves BACKWARDS is a reset, not a negative delta: g_bt3FrameCount is store()d
    // (not just incremented) on a savestate load, and the timing accumulators reset with the runtime.
    // Treat it as zero for that window rather than wrapping into an absurd figure.
    if (g_perf.cpuSeen)
    {
        const auto delta = [](unsigned long long now, unsigned long long prev, bool *reset) -> unsigned long long
        {
            if (now < prev)
            {
                *reset = true;   // savestate load or runtime restart, not a negative delta
                return 0ull;
            }
            return now - prev;
        };
        bool reset = false;
        g_perf.cpuGuestWindowNs += delta(guestBusyNs, g_perf.guestBusyNs, &reset);
        g_perf.cpuWallWindowNs += delta(wallNs, g_perf.wallNs, &reset);
        g_perf.cpuSubmitWindowNs += delta(submitNs, g_perf.submitNs, &reset);
        g_perf.cpuResetSeen = g_perf.cpuResetSeen || reset;
    }
    g_perf.guestBusyNs = guestBusyNs;
    g_perf.wallNs = wallNs;
    g_perf.submitNs = submitNs;
    g_perf.cpuSeen = true;
}

void PerfAddGpuBusyNs(unsigned long long ns, int samplesLanded, int samplesDropped)
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    g_perf.gpuBusyNs += ns;
    g_perf.gpuSamplesLanded += samplesLanded < 0 ? 0 : samplesLanded;
    g_perf.gpuDropped += samplesDropped < 0 ? 0 : samplesDropped;
}

void PerfTick(double frameSeconds)
{
    if (!(frameSeconds > 0.0))
        return;

    std::lock_guard<std::mutex> lk(g_perf.mtx);

    g_perf.frameMs[g_perf.frameNext] = frameSeconds * 1000.0;
    g_perf.frameNext = (g_perf.frameNext + 1) % kFrameSamples;
    if (g_perf.frameCount < kFrameSamples)
        ++g_perf.frameCount;

    g_perf.windowPresents += 1.0;
    g_perf.windowSeconds += frameSeconds;
    g_perf.out.valid = true;

    if (g_perf.windowSeconds < kWindowSeconds)
        return;

    PerfStatus &out = g_perf.out;
    out.valid = true;
    out.displayRefreshHz = g_perf.refreshHz;
    out.windowSeconds = g_perf.windowSeconds;
    // The present rate, counted. Not a scanout measurement: with a blocking vsync present this is
    // what the panel shows, and without one it is the submit rate -- which is why the UI prints the
    // monitor's nominal rate next to it.
    out.displayFps = g_perf.windowPresents / g_perf.windowSeconds;

    std::vector<double> sorted;
    sorted.reserve(static_cast<size_t>(g_perf.frameCount));
    for (int i = 0; i < g_perf.frameCount; ++i)
        sorted.push_back(g_perf.frameMs[i]);
    std::sort(sorted.begin(), sorted.end());
    out.frameSamples = g_perf.frameCount;
    out.frameMsP50 = static_cast<float>(percentileOfSorted(sorted, 0.50));
    out.frameMsP95 = static_cast<float>(percentileOfSorted(sorted, 0.95));
    out.frameMsMax = static_cast<float>(sorted.empty() ? 0.0 : sorted.back());

    // CPU percentages over the same wall window, from the accumulated deltas. A window in which a
    // counter moved backwards reports 0 rather than carrying the previous window's figure forward:
    // same rule as the GPU side -- a number we did not measure must not be shown as a stale one.
    if (g_perf.cpuResetSeen)
    {
        out.guestPct = 0.0;
        out.submitPct = 0.0;
    }
    else if (g_perf.cpuSeen && g_perf.cpuWallWindowNs > 0)
    {
        out.guestPct = 100.0 * static_cast<double>(g_perf.cpuGuestWindowNs)
                              / static_cast<double>(g_perf.cpuWallWindowNs);
        out.submitPct = 100.0 * static_cast<double>(g_perf.cpuSubmitWindowNs)
                               / static_cast<double>(g_perf.cpuWallWindowNs);
    }

    // gpuBusyPct has the same shape as a vendor 3D-engine counter, so that is the one to compare with
    // Task Manager. gpuMsPerFrame answers a different question: whether this machine is GPU-bound
    // against the frame budget. With no samples this window both are reported as 0 AND flagged by
    // gpuSamples == 0, because a registered backend that got no measurement is not an idle GPU.
    const double windowNsGpu = g_perf.windowSeconds * 1.0e9;
    out.gpuSamples = g_perf.gpuSamplesLanded;
    out.gpuBusyPct = windowNsGpu > 0.0 ? 100.0 * static_cast<double>(g_perf.gpuBusyNs) / windowNsGpu : 0.0;
    out.gpuMsPerFrame = g_perf.windowPresents > 0.0
                            ? static_cast<double>(g_perf.gpuBusyNs) / 1.0e6 / g_perf.windowPresents
                            : 0.0;
    out.gpuSource = g_perf.gpuSource;
    out.gpuQuality = g_perf.gpuQuality;
    out.gpuCoverage = coverageOf(g_perf.gpuQuality);
    out.gpuSamplesDropped = g_perf.gpuDropped;

    g_perf.windowPresents = 0.0;
    g_perf.windowSeconds = 0.0;
    g_perf.cpuGuestWindowNs = 0;
    g_perf.cpuWallWindowNs = 0;
    g_perf.cpuSubmitWindowNs = 0;
    g_perf.cpuResetSeen = false;
    g_perf.gpuBusyNs = 0;
    g_perf.gpuSamplesLanded = 0;
    g_perf.gpuDropped = 0;
}

PerfStatus GetPerfStatus()
{
    std::lock_guard<std::mutex> lk(g_perf.mtx);
    return g_perf.out;
}

}   // namespace ps2x
