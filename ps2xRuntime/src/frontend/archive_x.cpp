#include "archive_x.h"

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace
{
    std::string archiveError(struct archive *a)
    {
        const char *s = archive_error_string(a);
        return s ? std::string(s) : std::string("archive error");
    }

    std::filesystem::path pathFromUtf8(const std::string &s)
    {
        return std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
    }

    std::string entryNameUtf8(struct archive_entry *e)
    {
        const char *utf8 = archive_entry_pathname_utf8(e);
        if (utf8)
            return std::string(utf8);
        const char *fallback = archive_entry_pathname(e);
        return fallback ? std::string(fallback) : std::string();
    }

    bool openArchive(struct archive **out, const std::string &path, std::string *err)
    {
        struct archive *a = archive_read_new();
        if (!a)
        {
            if (err) *err = "out of memory";
            return false;
        }
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
#if defined(_WIN32)
        const std::filesystem::path wide = pathFromUtf8(path);
        const int opened = archive_read_open_filename_w(a, wide.c_str(), 1 << 16);
#else
        const int opened = archive_read_open_filename(a, path.c_str(), 1 << 16);
#endif
        if (opened != ARCHIVE_OK)
        {
            if (err) *err = archiveError(a);
            archive_read_free(a);
            return false;
        }
        *out = a;
        return true;
    }

    bool escapesRoot(const std::filesystem::path &rel)
    {
        if (rel.empty() || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory())
            return true;
        for (const auto &part : rel)
            if (part == "..")
                return true;
        return false;
    }
}

namespace archivex
{
    bool totalBytes(const std::string &archivePath, std::uint64_t *total, std::string *err)
    {
        struct archive *a = nullptr;
        if (!openArchive(&a, archivePath, err))
            return false;

        std::uint64_t sum = 0;
        struct archive_entry *e = nullptr;
        for (;;)
        {
            const int r = archive_read_next_header(a, &e);
            if (r == ARCHIVE_EOF)
                break;
            if (r == ARCHIVE_WARN)
                continue;
            if (r < ARCHIVE_WARN)
            {
                if (err) *err = archiveError(a);
                archive_read_free(a);
                return false;
            }
            if (archive_entry_filetype(e) == AE_IFREG && archive_entry_size(e) > 0)
                sum += static_cast<std::uint64_t>(archive_entry_size(e));
            archive_read_data_skip(a);
        }

        archive_read_free(a);
        if (total) *total = sum;
        return true;
    }

    bool extract(const std::string &archivePath, const std::string &destDir,
                 const ProgressFn &progress, std::string *err)
    {
        std::uint64_t total = 0;
        totalBytes(archivePath, &total, nullptr);

        struct archive *a = nullptr;
        if (!openArchive(&a, archivePath, err))
            return false;

        std::error_code ec;
        std::filesystem::path root =
            std::filesystem::absolute(std::filesystem::path(destDir), ec).lexically_normal();
        if (ec)
        {
            if (err) *err = "failed to resolve " + destDir;
            archive_read_free(a);
            return false;
        }
        std::filesystem::create_directories(root, ec);
        if (ec)
        {
            if (err) *err = "failed to create " + root.string();
            archive_read_free(a);
            return false;
        }

        std::uint64_t done = 0;
        auto tick = std::chrono::steady_clock::now();
        std::vector<char> chunk(1 << 20);

        struct archive_entry *e = nullptr;
        for (;;)
        {
            const int r = archive_read_next_header(a, &e);
            if (r == ARCHIVE_EOF)
                break;
            if (r == ARCHIVE_WARN)
                continue;
            if (r < ARCHIVE_WARN)
            {
                if (err) *err = archiveError(a);
                archive_read_free(a);
                return false;
            }

            std::string rel = entryNameUtf8(e);
            while (!rel.empty() && rel.front() == '/')
                rel.erase(0, 1);
            if (rel.empty())
            {
                archive_read_data_skip(a);
                continue;
            }

            const std::filesystem::path relPath = pathFromUtf8(rel).lexically_normal();
            if (escapesRoot(relPath))
            {
                if (err) *err = "unsafe path in archive: " + rel;
                archive_read_free(a);
                return false;
            }
            const std::filesystem::path outPath = (root / relPath).lexically_normal();

            const auto ft = archive_entry_filetype(e);
            if (ft == AE_IFDIR)
            {
                std::filesystem::create_directories(outPath, ec);
                archive_read_data_skip(a);
                continue;
            }
            if (ft != AE_IFREG)
            {
                archive_read_data_skip(a);
                continue;
            }

            std::filesystem::create_directories(outPath.parent_path(), ec);
            if (ec)
            {
                if (err) *err = "failed to create " + outPath.parent_path().string();
                archive_read_free(a);
                return false;
            }

            std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
            if (!f)
            {
                if (err) *err = "failed to write " + outPath.string();
                archive_read_free(a);
                return false;
            }

            for (;;)
            {
                const la_ssize_t n = archive_read_data(a, chunk.data(), chunk.size());
                if (n < 0)
                {
                    if (err) *err = archiveError(a);
                    f.close();
                    archive_read_free(a);
                    return false;
                }
                if (n == 0)
                    break;
                f.write(chunk.data(), static_cast<std::streamsize>(n));
                if (!f)
                {
                    if (err) *err = "failed to write " + outPath.string();
                    f.close();
                    archive_read_free(a);
                    return false;
                }
                done += static_cast<std::uint64_t>(n);
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - tick).count() >= 100)
                {
                    tick = now;
                    if (progress && !progress(done, total))
                    {
                        if (err) *err = "cancelled";
                        f.close();
                        archive_read_free(a);
                        return false;
                    }
                }
            }
            f.close();
        }

        archive_read_free(a);
        if (progress)
            progress(done, total);
        return true;
    }
}

ArchiveExtractJob::~ArchiveExtractJob()
{
    join();
}

void ArchiveExtractJob::start(const std::string &archivePath, const std::string &destDir)
{
    join();
    m_cancel.store(false, std::memory_order_relaxed);
    m_done.store(false, std::memory_order_relaxed);
    m_ok.store(false, std::memory_order_relaxed);
    m_bytesDone.store(0, std::memory_order_relaxed);
    m_bytesTotal.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_errMutex);
        m_err.clear();
    }

    m_thread = std::thread([this, archivePath, destDir]
    {
        std::string err;
        const bool ok = archivex::extract(
            archivePath, destDir,
            [this](std::uint64_t done, std::uint64_t total)
            {
                m_bytesDone.store(done, std::memory_order_relaxed);
                m_bytesTotal.store(total, std::memory_order_relaxed);
                return m_cancel.load(std::memory_order_relaxed) == 0;
            },
            &err);
        {
            std::lock_guard<std::mutex> lock(m_errMutex);
            m_err = err;
        }
        m_ok.store(ok, std::memory_order_relaxed);
        m_done.store(true, std::memory_order_release);
    });
}

std::string ArchiveExtractJob::error() const
{
    std::lock_guard<std::mutex> lock(m_errMutex);
    return m_err;
}

void ArchiveExtractJob::join()
{
    if (m_thread.joinable())
        m_thread.join();
}
