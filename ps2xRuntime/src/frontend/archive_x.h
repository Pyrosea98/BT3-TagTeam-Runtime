#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace archivex
{
    bool totalBytes(const std::string &archivePath, std::uint64_t *total, std::string *err);

    using ProgressFn = std::function<bool(std::uint64_t done, std::uint64_t total)>;

    bool extract(const std::string &archivePath, const std::string &destDir,
                 const ProgressFn &progress, std::string *err);
}

class ArchiveExtractJob
{
public:
    ArchiveExtractJob() = default;
    ~ArchiveExtractJob();

    ArchiveExtractJob(const ArchiveExtractJob &) = delete;
    ArchiveExtractJob &operator=(const ArchiveExtractJob &) = delete;

    void start(const std::string &archivePath, const std::string &destDir);
    void requestCancel() { m_cancel.store(true, std::memory_order_relaxed); }

    bool finished() const { return m_done.load(std::memory_order_acquire); }
    bool ok() const { return m_ok.load(std::memory_order_acquire); }
    std::uint64_t bytesDone() const { return m_bytesDone.load(std::memory_order_relaxed); }
    std::uint64_t bytesTotal() const { return m_bytesTotal.load(std::memory_order_relaxed); }
    std::string error() const;
    void join();

private:
    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_ok{false};
    std::atomic<std::uint64_t> m_bytesDone{0};
    std::atomic<std::uint64_t> m_bytesTotal{0};
    mutable std::mutex m_errMutex;
    std::string m_err;
};
