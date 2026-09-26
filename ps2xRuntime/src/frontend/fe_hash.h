#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace fe
{
    // Incremental SHA-256, used both for files on disk and for streams read out of a disc
    // image. FFmpeg's avutil hash is what backs it: the runtime already links FFmpeg for the
    // FMV decode, so this adds no crypto dependency of its own.
    class Sha256
    {
    public:
        Sha256();
        ~Sha256();

        Sha256(const Sha256 &) = delete;
        Sha256 &operator=(const Sha256 &) = delete;

        bool valid() const { return m_ok; }
        void update(const char *data, std::size_t size);
        std::string finalHex();

    private:
        void *m_ctx = nullptr;
        bool m_ok = false;
    };

    // SHA-256 of a file, lowercase hex. ok=false when the file is missing or unreadable.
    std::string sha256Hex(const std::filesystem::path &file, bool &ok);
}
