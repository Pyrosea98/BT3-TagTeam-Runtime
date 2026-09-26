#include "archive_x.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
    bool runSync(const std::string &archive, const std::string &dest)
    {
        auto tick = std::chrono::steady_clock::now();
        std::string err;
        const bool ok = archivex::extract(
            archive, dest,
            [&tick](std::uint64_t done, std::uint64_t totalBytesOut)
            {
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - tick).count() >= 1000)
                {
                    tick = now;
                    const double pct = totalBytesOut ? done * 100.0 / totalBytesOut : 100.0;
                    std::printf("  %.1f / %.1f MB (%.1f%%)\n",
                                done / 1048576.0, totalBytesOut / 1048576.0, pct);
                    std::fflush(stdout);
                }
                return true;
            },
            &err);
        if (!ok)
            std::printf("extract failed: %s\n", err.c_str());
        return ok;
    }

    int runJob(const std::string &archive, const std::string &dest, bool cancel)
    {
        ArchiveExtractJob job;
        job.start(archive, dest);
        if (cancel)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            job.requestCancel();
        }
        while (!job.finished())
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        job.join();
        const bool ok = job.ok();
        std::printf("job: ok=%d done=%llu total=%llu err=%s\n",
                    ok ? 1 : 0,
                    (unsigned long long)job.bytesDone(),
                    (unsigned long long)job.bytesTotal(),
                    job.error().c_str());
        if (cancel)
            return (ok || job.error() == "cancelled") ? 0 : 1;
        return ok ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: ps2x_archive_probe <archive> <destDir> [sync|job|cancel]\n");
        return 2;
    }

    const std::string archive = argv[1];
    const std::string dest = argv[2];
    const std::string mode = argc > 3 ? argv[3] : "sync";

    if (mode == "job" || mode == "cancel")
        return runJob(archive, dest, mode == "cancel");

    std::uint64_t total = 0;
    std::string err;
    if (!archivex::totalBytes(archive, &total, &err))
    {
        std::printf("totalBytes failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("total uncompressed: %llu bytes (%.2f MB)\n",
                (unsigned long long)total, total / 1048576.0);

    if (!runSync(archive, dest))
        return 1;
    std::printf("extract ok\n");
    return 0;
}
