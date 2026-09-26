#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Minimal ISO9660 reader, no external dependencies. Enough for PS2 game discs: primary
// volume descriptor + directory records, both-endian fields, ";1" version suffix
// stripping and multi-extent files. The install wizard uses it to hash the boot ELF
// inside the user's own disc dump and to stream the game data tree out.
class Iso9660
{
public:
    struct Extent
    {
        std::uint32_t start = 0; // logical block
        std::uint64_t size = 0;  // bytes
    };

    struct File
    {
        std::string path; // iso-style forward slashes, no leading '/', no ";1"
        bool dir = false;
        std::vector<Extent> extents;

        std::uint64_t size() const
        {
            std::uint64_t total = 0;
            for (const Extent &e : extents)
                total += e.size;
            return total;
        }
    };

    bool open(const std::string &isoPath);
    bool isOpen() const { return m_opened; }
    const std::string &error() const { return m_err; }
    const std::vector<File> &files() const { return m_files; }

    // Case-insensitive lookup, e.g. "SLUS_216.78" or "BIN/DBZP.BIN".
    const File *find(const std::string &isoPath) const;

    using Sink = std::function<void(const char *data, std::size_t size)>;

    // Streams the whole file in 256 KiB chunks to sink(). Returns bytes read or -1.
    std::int64_t readFile(const File &f, const Sink &sink) const;

private:
    bool readBlocks(std::uint32_t startLba, std::uint64_t byteLen, const Sink &sink) const;
    bool scanDirectory(std::uint32_t lba, std::uint64_t byteLen, const std::string &prefix);

    std::string m_path;
    bool m_opened = false;
    std::uint32_t m_blockSize = 2048;
    std::string m_err;
    std::vector<File> m_files;
};

namespace DiscVerify
{
    // SLUS_216.78 (US release). MUST stay in sync with ELF_SHA256 in games/bt3/setup.py.
    constexpr const char *kExpectedDiscElfSha256 =
        "811188ba9b416500d921cd4d9514df0cbf42f3a41a99cf5aac5a3da37171bf99";

    // Hash of the boot ELF embedded in the disc image.
    bool verifySlusFromIso(const std::string &isoPath);
}
