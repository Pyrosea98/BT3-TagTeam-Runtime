// [fe-probe] The perf accumulator's arithmetic, with no GL, no window and no game.
//
// What is worth testing here is not "does the ring work" but the two claims the readout makes to the
// user, because those are the things that were wrong before:
//
//   * p95 is a frame that ACTUALLY HAPPENED, not an interpolation between two that did not. A mean
//     or a linear-interpolated percentile of a 16 ms/80 ms mix reads like "48 ms", which is a frame
//     that never occurred -- and the whole point of the number is to name the hitch.
//   * A backend that never measured anything reports "not measured", not 0. The old code printed 0%
//     on two of three backends and it read as an idle GPU.
//
// The ring is 240 presents, so the percentile window is fed past capacity on purpose.
#include <cstdio>
#include <cmath>

#include "runtime/ps2x_perf_status.h"

static int g_fail = 0;

static void check(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FALLA", what);
    if (!ok)
        ++g_fail;
}

static bool near(double a, double b, double eps = 0.05)
{
    return std::fabs(a - b) <= eps;
}

// The readout recomputes at 1 Hz, so "what does GetPerfStatus() say" is only meaningful once a window
// boundary has actually been crossed. Feed enough uniform frames to close one, so a test never reads
// a window that is still filling.
static void closeWindow()
{
    for (int i = 0; i < 61; ++i)
        ps2x::PerfTick(1.0 / 60.0);
}

int main()
{
    std::printf("[1] no presents, the state is invalid (not 0 fps presented as real)\n");
    {
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(!p.valid, "valid == false before the first frame");
        check(near(p.displayFps, 0.0), "displayFps starts at 0");
    }

    std::printf("[2] 60 frames of 16.667 ms -> 60 fps, p50 = p95 = 16.7\n");
    {
        for (int i = 0; i < 60; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(p.valid, "valid after the first window");
        check(near(p.displayFps, 60.0, 0.5), "displayFps ~ 60");
        check(near(p.frameMsP50, 16.667, 0.05), "p50 ~ 16.7 ms");
        check(near(p.frameMsP95, 16.667, 0.05), "p95 ~ 16.7 ms");
        check(near(p.frameMsMax, 16.667, 0.05), "max ~ 16.7 ms");
        check(p.frameSamples == 60, "60 samples in the ring");
    }

    std::printf("[3] 10%% of frames slow: p50 does not move, p95 and max do\n");
    {
        // This is the shape of bugs 4 and 5: mostly-16 ms with a visible share of slow frames. A mean
        // here reads ~20 ms and looks like mild jitter; p95 names the slow frames. The rate is 10% and
        // not 5% on purpose: at exactly 5% the 95th percentile lands on the last FAST frame, which is
        // the correct answer for the definition and a useless one for the reader.
        for (int i = 0; i < 120; ++i)
            ps2x::PerfTick((i % 10 == 0) ? 0.050 : (1.0 / 60.0));
        closeWindow();
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(p.frameMsP50 < 17.0, "p50 stays at 16.7 (the slow frames do not move it)");
        check(near(p.frameMsP95, 50.0, 0.01), "p95 == 50.0: the slow frames show up here");
        check(near(p.frameMsMax, 50.0, 0.01), "max == 50.0, the real frame");
        double sum = 0.0;
        for (int i = 0; i < p.frameSamples; ++i)
            sum += 16.667;
        std::printf("       the average would have read ~%.1f ms and said nothing\n",
                    (sum + 50.0 * (p.frameSamples / 10)) / static_cast<double>(p.frameSamples));
    }

    std::printf("[4] a rare hitch is seen by max, NOT p95 -- which is why we have all three\n");
    {
        // One 80 ms frame in ~180 is 0.5%. p95 covers the worst 5%, so it correctly does not see it;
        // max does. Asserting that here on purpose: it is the honest limit of a percentile, and the
        // reason the readout prints max next to it instead of p95 alone.
        for (int i = 0; i < 180; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        ps2x::PerfTick(0.080);
        closeWindow();
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(near(p.frameMsP95, 16.667, 0.05), "p95 does NOT see a 0.5%% hitch (the percentile's real limit)");
        check(near(p.frameMsMax, 80.0, 0.01), "max does see it: that is why it is here");
    }

    std::printf("[5] the p95 is a frame that happened, not a point between two\n");
    {
        // Half at 10 ms, half at 30 ms. Nearest-rank p95 must land ON 30; a linear interpolation
        // would report 29, a frame that never happened.
        for (int i = 0; i < 120; ++i)
            ps2x::PerfTick(i < 60 ? 0.010 : 0.030);
        closeWindow();
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(near(p.frameMsP95, 30.0, 0.01), "p95 lands on a real sample (30.0, not 29)");
        check(p.frameMsMax >= 30.0, "max >= 30.0");
    }

    std::printf("[6] with no source registered, the GPU says 'not measured' (coverage 0), not 0%%\n");
    {
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(p.gpuSource == ps2x::GpuSource::None, "the source starts as None");
        check(near(p.gpuCoverage, 0.0), "coverage 0 with an unknown source");
        check(near(p.gpuBusyPct, 0.0), "and the percentage at 0, which the UI must label as n/a");
    }

    std::printf("[7] coverage by source\n");
    {
        // 100 ms of GPU work inside a 1 s window is 10% busy, and spread over the window's 60 frames
        // that is 1.67 ms per frame. Both units have to be right: the percentage is the one to hold
        // against Task Manager, the per-frame one is the one to hold against the frame budget.
        ps2x::PerfSetGpuSource(ps2x::GpuSource::OpenGL, ps2x::GpuQuality::DrawListOnly);
        ps2x::PerfAddGpuBusyNs(100ull * 1000ull * 1000ull, 1, 0);
        for (int i = 0; i < 60; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(near(p.gpuCoverage, 0.0), "OpenGL draw-list: coverage 0 (shown as a minimum)");
        check(near(p.gpuBusyPct, 10.0, 0.5), "100 ms of GPU in 1 s -> ~10 %%");
        check(near(p.gpuMsPerFrame, 1.667, 0.05), "-> ~1.67 ms per frame");
        check(p.gpuSamples == 1 && p.gpuMeasured(), "1 sample, and gpuMeasured() returns true");

        ps2x::PerfSetGpuSource(ps2x::GpuSource::VulkanTimestamps, ps2x::GpuQuality::FullFrame);
        ps2x::PerfAddGpuBusyNs(100ull * 1000ull * 1000ull, 1, 0);
        for (int i = 0; i < 60; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        p = ps2x::GetPerfStatus();
        check(near(p.gpuCoverage, 1.0), "Vulkan: coverage 1.0, comparable with Task Manager");
        check(near(p.gpuBusyPct, 10.0, 0.5), "the same work reports the same");

        // A window with no GPU work at all must read 0, not "unknown" -- and a source we never set
        // must read unknown, not 0. That distinction is the whole point of the field.
        ps2x::PerfSetGpuSource(ps2x::GpuSource::SoftwareCpu, ps2x::GpuQuality::CpuOnly);
        closeWindow();
        p = ps2x::GetPerfStatus();
        check(p.gpuSource == ps2x::GpuSource::SoftwareCpu, "software: the source says so");
        check(near(p.gpuCoverage, 0.0), "software: coverage 0, not reported as an idle GPU");
        check(near(p.gpuBusyPct, 0.0), "and a real 0%, not a 0 that hides a missing measurement");
    }

    std::printf("[8] a window with no samples is NOT an idle GPU\n");
    {
        // This is the case the whole gpuSource/gpuQuality pair existed for, and it was found by
        // measuring: three windows in a row read 61% -> 0.65% -> 0.61% because the guest stopped
        // issuing draw lists, and 0.65% is indistinguishable from "the GPU is doing nothing".
        ps2x::PerfSetGpuSource(ps2x::GpuSource::OpenGL, ps2x::GpuQuality::DrawListOnly);
        closeWindow();                       // a full window with nothing fed
        ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(p.gpuSource == ps2x::GpuSource::OpenGL, "the source stays registered");
        check(p.gpuSamples == 0, "gpuSamples == 0");
        check(!p.gpuMeasured(), "gpuMeasured() returns false: the window was not measured");
        check(near(p.gpuBusyPct, 0.0), "gpuBusyPct == 0, which only reads correctly with gpuSamples == 0 alongside");

        // And a real idle GPU is a different thing: samples landed, each one measuring nothing.
        ps2x::PerfAddGpuBusyNs(0ull, 4, 0);
        closeWindow();
        p = ps2x::GetPerfStatus();
        check(p.gpuSamples == 4 && p.gpuMeasured(), "4 samples measuring 0: that IS an idle GPU");
        check(near(p.gpuBusyPct, 0.0), "and also gives 0% -- hence the need for the counter");
    }

    std::printf("[9] dropped samples are counted and reported\n");
    {
        ps2x::PerfSetGpuSource(ps2x::GpuSource::OpenGL, ps2x::GpuQuality::DrawListOnly);
        ps2x::PerfAddGpuBusyNs(0, 0, 3);
        for (int i = 0; i < 60; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        const ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(p.gpuSamplesDropped == 3, "gpuSamplesDropped == 3");
        check(p.gpuSamples == 0, "and do not count as samples: a dropped sample is not a measurement");
        closeWindow();
        check(ps2x::GetPerfStatus().gpuSamplesDropped == 0, "the counter resets per window");
    }

    std::printf("[10] the CPU counters arrive cumulative and must be differenced\n");
    {
        // Caught by running it: guest_pct came out at 413% and once at 1572%, because an absolute
        // nanosecond count was being divided by a one-second window. PerfPublishCpu takes CUMULATIVE
        // totals, so the delta has to happen where the previous sample lives.
        const unsigned long long ms = 1000ull * 1000ull * 1000ull;   // one ms in ns

        // The FIRST publish only establishes the baseline: there is no previous sample to difference
        // against, so that window has no CPU data. It reports 0 rather than a guess.
        ps2x::PerfPublishCpu(300ull * ms, 1000ull * ms, 20ull * ms);
        closeWindow();
        check(near(ps2x::GetPerfStatus().guestPct, 0.0, 0.01), "the first publish is the baseline: 0%%");

        // Second window: the guest gained 300 ms of busy time over 1 s of wall -> 30%.
        ps2x::PerfPublishCpu(600ull * ms, 2000ull * ms, 40ull * ms);
        closeWindow();
        ps2x::PerfStatus p = ps2x::GetPerfStatus();
        check(near(p.guestPct, 30.0, 0.5), "guest_pct ~ 30%% of 300 ms in 1 s");
        check(near(p.submitPct, 2.0, 0.5), "submit_pct ~ 2%%");

        // The next window keeps growing from there: 900 ms cumulative busy over 3 s of wall is still
        // 30%, and only differencing gets that. A non-differencing version would report 900%.
        ps2x::PerfPublishCpu(900ull * ms, 3000ull * ms, 60ull * ms);
        closeWindow();
        p = ps2x::GetPerfStatus();
        check(near(p.guestPct, 30.0, 0.5), "stays at 30%%: 900 ms accumulated over 3 s, not 900%%");
        check(p.guestPct < 100.0, "and can never exceed 100%%");

        // A counter that jumps BACKWARDS is a savestate load or a runtime reset, not a negative
        // delta. That window is unmeasured, so it reports 0 instead of wrapping or going stale.
        ps2x::PerfPublishCpu(10ull * ms, 1000ull * ms, 5ull * ms);
        closeWindow();
        p = ps2x::GetPerfStatus();
        check(near(p.guestPct, 0.0, 0.01), "a counter reset gives 0%%, not a giant unwrap");
        check(p.guestPct >= 0.0 && p.guestPct <= 100.0, "the range is always 0..100");
    }

    std::printf("[11] nominal refresh and overlay enable\n");    {
        ps2x::PerfSetRefreshHz(144);
        for (int i = 0; i < 60; ++i)
            ps2x::PerfTick(1.0 / 60.0);
        check(ps2x::GetPerfStatus().displayRefreshHz == 144, "refresh nominal 144 Hz");
        ps2x::SetPerfOverlayEnabled(true);
        check(ps2x::PerfOverlayEnabled(), "the overlay can be assembled");
        ps2x::SetPerfOverlayEnabled(false);
        check(!ps2x::PerfOverlayEnabled(), "and disassemble");
    }

    std::printf("%s (%d fallos)\n", g_fail ? "PROBE FAILED" : "PROBE OK", g_fail);
    return g_fail ? 1 : 0;
}
