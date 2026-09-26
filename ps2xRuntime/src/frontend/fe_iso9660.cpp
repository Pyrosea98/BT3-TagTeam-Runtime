#include "frontend/fe_iso9660.h"

#include "frontend/fe_fileio.h"

#include "frontend/fe_hash.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <optional>

namespace
{
    constexpr std::uint32_t kSectorSize = 2048;

    struct DirRecord
    {
        bool isDir = false;
        bool multiExtent = false;
        std::uint32_t lba = 0;
        std::uint64_t size = 0;
        std::string name; // without the ";N" version suffix; "." / ".." verbatim
    };

    std::uint32_t le32(const char *b, std::size_t off)
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(b[off])) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3])) << 24);
    }

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::optional<DirRecord> parseRecord(const std::string &dirData, std::size_t *cursor)
    {
        if (*cursor + 34 > dirData.size())
            return std::nullopt;
        const std::size_t len = static_cast<unsigned char>(dirData[*cursor]);
        if (len == 0)
            return std::nullopt; // zero-length padding terminates the listing
        if (*cursor + len > dirData.size())
            return std::nullopt;

        DirRecord r;
        r.lba = le32(dirData.data(), *cursor + 2);
        r.size = le32(dirData.data(), *cursor + 10);
        r.isDir = (static_cast<unsigned char>(dirData[*cursor + 25]) & 0x02) != 0;
        r.multiExtent = (static_cast<unsigned char>(dirData[*cursor + 25]) & 0x80) != 0;
        const std::size_t nameLen = static_cast<unsigned char>(dirData[*cursor + 32]);

        char name[256] = {};
        for (std::size_t i = 0; i < nameLen && i < 255; ++i)
            name[i] = static_cast<char>(dirData[*cursor + 33 + i]);

        // "." and ".." are stored as 0x00 / 0x01.
        if (nameLen == 1 && (name[0] == '\0' || name[0] == '\x01'))
            r.name = (name[0] == '\0') ? "." : "..";
        else
        {
            r.name.assign(name, nameLen);
            const std::size_t semi = r.name.find(';');
            if (semi != std::string::npos)
                r.name.erase(semi);
        }

        *cursor += len;
        return r;
    }
}

bool Iso9660::readBlocks(std::uint32_t startLba, std::uint64_t byteLen, const Sink &sink) const
{
    std::FILE *f = std::fopen(m_path.c_str(), "rb");
    if (!f)
        return false;

    const long long offset = (long long)startLba * m_blockSize;
    if (feio::seek(f, offset, SEEK_SET) != 0)
    {
        std::fclose(f);
        return false;
    }

    std::vector<char> buf(256 * 1024);
    std::uint64_t remaining = byteLen;
    while (remaining > 0)
    {
        const std::size_t want = (std::size_t)std::min<std::uint64_t>(buf.size(), remaining);
        const std::size_t got = std::fread(buf.data(), 1, want, f);
        if (got != want)
        {
            std::fclose(f);
            return false; // short read: truncated image
        }
        remaining -= got;
        sink(buf.data(), got);
    }
    std::fclose(f);
    return true;
}

bool Iso9660::open(const std::string &isoPath)
{
    m_opened = false;
    m_err.clear();
    m_files.clear();
    m_path = isoPath;

    std::FILE *f = std::fopen(isoPath.c_str(), "rb");
    if (!f)
    {
        m_err = "cannot open " + isoPath;
        return false;
    }

    // Volume descriptors start at sector 16; a descriptor must fit in one sector.
    std::string pvd;
    {
        pvd.resize(17 * kSectorSize);
        const std::size_t got = std::fread(&pvd[0], 1, pvd.size(), f);
        std::fclose(f);
        if (got != pvd.size())
        {
            m_err = "file too small to be an ISO image";
            return false;
        }
    }
    pvd.erase(0, 16 * kSectorSize);

    // [type(1)][standard-id "CD001"(5)][version(1)]
    if (pvd.size() < 6 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0)
    {
        m_err = "not an ISO9660 image (missing CD001)";
        return false;
    }
    if (static_cast<unsigned char>(pvd[0]) != 1)
    {
        m_err = "primary volume descriptor not found";
        return false;
    }

    const std::uint32_t blockSize =
        (std::uint32_t)(unsigned char)pvd[128] | ((std::uint32_t)(unsigned char)pvd[129] << 8);
    if (blockSize != kSectorSize)
    {
        m_err = "unsupported logical block size " + std::to_string(blockSize);
        return false;
    }

    std::size_t cur = 156; // root directory record inside the PVD (both-endian, 34 bytes)
    const auto root = parseRecord(pvd, &cur);
    if (!root || !root->isDir)
    {
        m_err = "root directory record missing";
        return false;
    }

    m_blockSize = blockSize;
    m_opened = true;

    if (!scanDirectory(root->lba, root->size, std::string()))
    {
        m_opened = false;
        m_err = "failed to read directory tree: " + m_err;
        return false;
    }
    return true;
}

bool Iso9660::scanDirectory(std::uint32_t lba, std::uint64_t byteLen, const std::string &prefix)
{
    std::string dirData;
    if (!readBlocks(lba, byteLen,
                    [&](const char *data, std::size_t size) { dirData.append(data, size); }))
    {
        m_err = "short read on directory";
        return false;
    }

    File *lastFile = nullptr;
    std::size_t cursor = 0;
    while (cursor < dirData.size())
    {
        const auto rec = parseRecord(dirData, &cursor);
        if (!rec)
            break; // padding terminates the directory listing

        const std::string path = prefix.empty() ? rec->name : prefix + "/" + rec->name;

        if (rec->isDir)
        {
            if (rec->name != "." && rec->name != "..")
            {
                File d;
                d.path = path;
                d.dir = true;
                m_files.push_back(d);
                if (!scanDirectory(rec->lba, rec->size, path))
                    return false;
            }
            lastFile = nullptr;
            continue;
        }

        if (rec->multiExtent && lastFile && lastFile->path == path && !lastFile->extents.empty())
        {
            lastFile->extents.push_back({rec->lba, rec->size});
            continue;
        }

        File entry;
        entry.path = path;
        entry.extents.push_back({rec->lba, rec->size});
        m_files.push_back(entry);
        lastFile = &m_files.back();
    }
    return true;
}

const Iso9660::File *Iso9660::find(const std::string &isoPath) const
{
    std::string want = isoPath;
    while (!want.empty() && want.front() == '/')
        want.erase(0, 1);
    while (!want.empty() && want.back() == '/')
        want.pop_back();
    const std::string lowerWant = toLower(want);
    for (const File &f : m_files)
    {
        if (!f.dir && toLower(f.path) == lowerWant)
            return &f;
    }
    return nullptr;
}

std::int64_t Iso9660::readFile(const File &f, const Sink &sink) const
{
    std::FILE *file = std::fopen(m_path.c_str(), "rb");
    if (!file)
        return -1;

    std::int64_t total = 0;
    std::vector<char> buf(256 * 1024);
    for (const Extent &e : f.extents)
    {
        const long long offset = (long long)e.start * m_blockSize;
        if (feio::seek(file, offset, SEEK_SET) != 0)
        {
            std::fclose(file);
            return -1;
        }
        std::uint64_t left = e.size;
        while (left > 0)
        {
            const std::size_t want = (std::size_t)std::min<std::uint64_t>(buf.size(), left);
            const std::size_t got = std::fread(buf.data(), 1, want, file);
            if (got != want)
            {
                std::fclose(file);
                return -1;
            }
            left -= got;
            total += (std::int64_t)got;
            sink(buf.data(), got);
        }
    }
    std::fclose(file);
    return total;
}

namespace DiscVerify
{
    bool verifySlusFromIso(const std::string &isoPath)
    {
        Iso9660 iso;
        if (!iso.open(isoPath))
            return false;
        const Iso9660::File *slus = iso.find("SLUS_216.78");
        if (!slus)
            return false;

        fe::Sha256 hash;
        if (!hash.valid())
            return false;
        const std::int64_t got =
            iso.readFile(*slus, [&](const char *data, std::size_t size) { hash.update(data, size); });
        if (got < 0)
            return false;

        return hash.finalHex() == kExpectedDiscElfSha256;
    }
}
