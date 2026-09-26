#include "frontend/fe_afs.h"

#include "frontend/fe_fileio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{

namespace fs = std::filesystem;

constexpr char kAfsMagic[4] = {'A', 'F', 'S', 0};
constexpr char kIdxMagic[8] = {'D', 'B', 'Z', 'A', 'F', 'S', '0', '1'};
constexpr uint32_t kIdxVersion = 3u;
constexpr size_t kSlotNameLen = 48u;

enum Type : uint32_t
{
    T_OTHER = 0,
    T_MODEL = 1,
    T_ANIM = 2,
    T_EFF = 3,
    T_PAK1 = 4,
    T_PAK3 = 5,
    T_Q1 = 6,
    T_S12C = 7,
    T_R19 = 8,
    T_GS = 9,
    T_PAK2 = 10,
};

uint32_t classify(const uint8_t *head, uint32_t avail)
{
    if (avail < 8)
        return T_OTHER;
    uint32_t w0, w1;
    std::memcpy(&w0, head, 4);
    std::memcpy(&w1, head + 4, 4);
    if (w0 == 0xFC && w1 == 0x400) return T_MODEL;
    if (w0 == 0x1B8 && w1 == 0x700) return T_ANIM;
    if (w0 == 0x6 && w1 == 0x40) return T_EFF;
    if (w0 == 0x1 && w1 == 0x40) return T_PAK1;
    if (w0 == 0x3 && w1 == 0x40) return T_PAK3;
    if (w0 == 0x1 && w1 == 0x8) return T_Q1;
    if (w0 == 0x12C && w1 == 0x4C0) return T_S12C;
    if (w0 == 0x19 && w1 == 0x80) return T_R19;
    if (w0 == 0x435347) return T_GS;
    if (w0 == 0x2 && w1 == 0x40) return T_PAK2;
    return T_OTHER;
}

struct Entry
{
    uint32_t off = 0;
    uint32_t size = 0;
    uint32_t type = 0;
};

struct Slot
{
    Entry en;
    uint32_t index = 0;   // position in the AFS entry table (== slot id, offsets ascending)
};

bool readFile(const fs::path &p, std::vector<uint8_t> &out)
{
    std::FILE *f = std::fopen(p.string().c_str(), "rb");
    if (!f)
        return false;
    // 64-bit throughout: PZS3US1.AFS is 1.46 GB and a bigger container would report a negative
    // size through a 32-bit long, then allocate nothing and read nothing.
    long long sz = 0;
    if (!feio::size(f, &sz) || sz < 0)
    {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(sz));
    if (sz > 0 && std::fread(out.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz))
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

bool parseAfs(const fs::path &p, std::vector<Entry> &entries)
{
    std::vector<uint8_t> data;
    if (!readFile(p, data))
        return false;
    if (data.size() < 8)
        return false;
    uint32_t magic = 0;
    std::memcpy(&magic, data.data(), 4);
    if (std::memcmp(&magic, kAfsMagic, 4) != 0)
        return false;
    uint32_t count = 0;
    std::memcpy(&count, data.data() + 4, 4);
    if (data.size() < 8ull + count * 8ull)
        return false;

    entries.clear();
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t off, sz;
        std::memcpy(&off, data.data() + 8 + i * 8, 4);
        std::memcpy(&sz, data.data() + 8 + i * 8 + 4, 4);
        uint32_t type = T_OTHER;
        if (static_cast<uint64_t>(off) + 8 <= data.size())
            type = classify(data.data() + off, std::min<uint32_t>(sz, 8));
        entries.push_back({off, sz, type});
    }
    return true;
}

std::vector<Slot> computeSlots(const std::vector<Entry> &rows, uint32_t afsBytes,
                               uint32_t &outDataStart)
{
    std::vector<Slot> slots;
    outDataStart = afsBytes;
    for (const auto &en : rows)
        if (en.size > 0 && en.off < outDataStart)
            outDataStart = en.off;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const Entry &en = rows[i];
        if (en.size == 0) // skip sentinel / empty rows
            continue;
        if (en.off > afsBytes)
            continue;
        slots.push_back({en, static_cast<uint32_t>(i)});
    }
    std::sort(slots.begin(), slots.end(),
              [](const Slot &a, const Slot &b) { return a.en.off < b.en.off; });
    return slots;
}

// On-disk names must stay inside the filesystem namespace. The game's AFS names
// are tame (e.g. "Battle_00.adx", "VIC-JP-B-"), sanitize as a safety net.
std::string sanitizeName(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F)
            continue;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            out.push_back('_');
        else
            out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (out.empty() || out == "." || out == "..")
        return {};
    return out;
}

std::string fmtIndex(uint32_t index)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04u", index);   // min 4 digits, like the reference tool
    return buf;
}

// Reads the file names embedded in the AFS itself. DBZ BT3 keeps a metadata
// table at the end of the container, addressed by the extra (addr,size) pair at
// 8 + 8*count. Each record is 48 bytes: name[32] (NUL-padded), then the date as
// 6x u16 LE (year..seconds), then a u32. Names are raw bytes (SJIS preserved).
bool parseEmbeddedNames(const std::vector<uint8_t> &data, const std::vector<Entry> &entries,
                        std::vector<std::string> &namesOut)
{
    namesOut.clear();
    if (entries.empty())
        return false;
    const uint64_t count = entries.size();
    const size_t pairAt = 8ull + count * 8ull;
    if (pairAt + 8 > data.size())
        return false;

    uint32_t metaAddr = 0, metaSize = 0;
    std::memcpy(&metaAddr, data.data() + pairAt, 4);
    std::memcpy(&metaSize, data.data() + pairAt + 4, 4);
    if (metaSize < count * 48u)
        return false;
    if (static_cast<uint64_t>(metaAddr) + count * 48u > data.size())
        return false;

    namesOut.resize(static_cast<size_t>(count));
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *rec = data.data() + metaAddr + i * 48u;
        size_t len = 0;
        while (len < 32u && rec[len] != 0)
            ++len;
        if (len == 0)   // tool synthesizes "blank_<i+1>" for nameless records
        {
            namesOut[i] = "blank_" + std::to_string(i + 1);
            continue;
        }
        namesOut[i].assign(reinterpret_cast<const char *>(rec), len);
    }
    return true;
}

// Reads the community name list sidecars. Two formats are accepted:
//   * binary AFL ("AFL\0" magic, then reserved u32s and count@12, then count
//     NUL-padded 32-byte names@16) -- what the fresh NTSC-U file lists ship as;
//   * legacy text ("index\tname" lines) -- the old obfuscated sidecars.
// Looked up next to the source first, then in the bundled assets dir passed by
// the caller. The caller expects an entry per slot, so the matching index must
// line up with the AFS entry table; binary lists are validated against it.
bool loadNameTable(const fs::path &afsPath, const std::string &nameTableDir,
                   const std::string &stem, size_t expectedCount,
                   std::vector<std::string> &namesOut)
{
    fs::path chosen;
    for (const fs::path &candidate : {fs::path(afsPath.string() + ".afl"),
                                      fs::path(nameTableDir) / (stem + ".afl")})
    {
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.is_open())
        {
            chosen = candidate;
            break;
        }
    }
    if (chosen.empty())
        return false;

    std::ifstream in(chosen, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (raw.size() >= 4 && std::memcmp(raw.data(), "AFL\0", 4) == 0)
    {
        if (raw.size() < 16)
            return false;
        uint32_t count = 0;
        std::memcpy(&count, raw.data() + 12, 4);
        if (count != expectedCount || raw.size() < 16ull + count * 32ull)
            return false;
        namesOut.resize(expectedCount);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t *p = raw.data() + 16 + i * 32;
            size_t len = 0;
            while (len < 32u && p[len] != 0)
                ++len;
            if (len > 0)
                namesOut[i].assign(reinterpret_cast<const char *>(p), len);
        }
        return true;
    }

    namesOut.clear();
    std::string line;
    size_t pos = 0;
    while (pos < raw.size())
    {
        const auto nlIt = std::find(raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.end(), '\n');
        const size_t nl = nlIt == raw.end() ? std::string::npos
                                            : static_cast<size_t>(nlIt - raw.begin());
        const size_t end = nl == std::string::npos ? raw.size() : nl;
        if (end > pos)
        {
            line.assign(reinterpret_cast<const char *>(raw.data() + pos), end - pos);
            if (line.back() == '\r')
                line.pop_back();
            const size_t tab = line.find('\t');
            if (tab != std::string::npos)
            {
                char *endp = nullptr;
                const long idx = std::strtol(line.c_str(), &endp, 10);
                if (endp != line.c_str() && endp == line.c_str() + tab)
                {
                    const std::string name = line.substr(tab + 1);
                    if (!name.empty())
                    {
                        if (static_cast<size_t>(idx) + 1 > namesOut.size())
                            namesOut.resize(static_cast<size_t>(idx) + 1);
                        namesOut[static_cast<size_t>(idx)] = name;
                    }
                }
            }
        }
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    return !namesOut.empty();
}

// Per-slot on-disk filenames mirroring the reference AFS tool:
//   unique name -> plain name, duplicated name -> <index>_<name> (index >= 4 digits).
// Unnamed slots keep the plain %06u form the runtime used since v1.
// The used-set guard only forces an index prefix on the (practically impossible)
// collision between a unique plain name and a duplicated prefixed name.
std::vector<std::string> computeFileNames(const std::vector<Slot> &slots,
                                          const std::vector<std::string> &rawNames)
{
    std::unordered_map<std::string, size_t> dup;
    for (const Slot &s : slots)
    {
        const std::string n =
            s.index < rawNames.size() ? sanitizeName(rawNames[s.index]) : std::string();
        if (!n.empty())
            ++dup[n];
    }

    std::vector<std::string> names(slots.size());
    std::unordered_set<std::string> used;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const Slot &s = slots[i];
        const std::string n =
            s.index < rawNames.size() ? sanitizeName(rawNames[s.index]) : std::string();
        std::string fn;
        if (!n.empty())
        {
            if (dup[n] > 1)
                fn = fmtIndex(s.index) + "_" + n;
            else
                fn = n;
            if (used.count(fn))   // collision guard (see above)
                fn = fmtIndex(s.index) + "_" + n;
            if (used.count(fn))
            {
                char b[32];
                std::snprintf(b, sizeof(b), "%06zu", i);
                fn = b;
            }
        }
        else
        {
            char b[32];
            std::snprintf(b, sizeof(b), "%06zu", i);
            fn = b;
        }
        used.insert(fn);
        names[i] = fn;
    }
    return names;
}

void writeName(std::vector<uint8_t> &out, const std::string &name)
{
    char buf[kSlotNameLen] = {};
    std::snprintf(buf, sizeof(buf), "%s", name.c_str());
    out.insert(out.end(), buf, buf + kSlotNameLen);
}

void writeU32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

} // namespace

AfsConvertResult convertAfsToFolder(const std::string &afsPath, const std::string &outDir,
                                    const std::function<void(const std::string &)> &onStatus,
                                    const std::function<void(uint64_t, uint64_t)> &onProgress,
                                    const std::string &nameTableDir)
{
    using namespace std::literals;

    auto fail = [](std::string msg) { return AfsConvertResult{false, std::move(msg)}; };

    const fs::path src(afsPath);
    const fs::path base = src.filename().stem();

    std::vector<Entry> entries;
    if (!parseAfs(src, entries))
        return fail("cannot read " + src.string() + " as an AFS container");

    std::error_code ec;
    fs::path dir = fs::path(outDir) / base.string();
    fs::create_directories(dir, ec);
    if (ec)
        return fail("cannot create " + dir.string());
    if (onStatus)
        onStatus("reading " + base.string() + " index");

    std::vector<uint8_t> afs;
    if (!readFile(src, afs))
        return fail("cannot read " + src.string());
    if (afs.size() > UINT32_MAX)
        return fail(src.string() + " too large for the index");
    const uint32_t afsBytes = static_cast<uint32_t>(afs.size());

    uint32_t dataStart = 0;
    std::vector<Slot> slots = computeSlots(entries, afsBytes, dataStart);
    std::vector<std::string> rawNames;
    if (!loadNameTable(src, nameTableDir, base.string(), entries.size(), rawNames) &&
        !parseEmbeddedNames(afs, entries, rawNames))
        rawNames.clear();
    const std::vector<std::string> slotNames = computeFileNames(slots, rawNames);
    const uint64_t total = 2ull * afsBytes;
    uint64_t done = 0;

    // Slot files: exact per-entry slices [off, off+size) -- the inter-slot
    // padding the disc packs between entries is dropped, matching what the
    // reference AFS tools dump and what the runtime's idx contract (slotBytes >=
    // size, sector-aligned, contiguous) expects. Files carry the game's real
    // names (assets/<stem>.afl) when a table is available.
    if (onStatus)
        onStatus("extracting " + base.string() + " entries (" +
                 std::to_string(slots.size()) + " slots)");
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const Slot &s = slots[i];
        if (static_cast<uint64_t>(s.en.off) + s.en.size > afs.size())
            return fail("entry out of range in " + src.string());
        std::FILE *f = std::fopen((dir / slotNames[i]).string().c_str(), "wb");
        if (!f)
            return fail("cannot write " + (dir / slotNames[i]).string());
        const size_t written = std::fwrite(afs.data() + s.en.off, 1, s.en.size, f);
        std::fclose(f);
        if (written != s.en.size)
            return fail("short write " + (dir / slotNames[i]).string());
        done += s.en.size;
        if (onProgress)
            onProgress(done, total);
    }

    // Index blob + slot table (idx v3; v2 had no name column), byte-exact
    // reassembly contract.
    std::vector<uint8_t> idx;
    idx.insert(idx.end(), kIdxMagic, kIdxMagic + 8);
    writeU32(idx, kIdxVersion);
    writeU32(idx, static_cast<uint32_t>(slots.size()));
    writeU32(idx, afsBytes);
    writeU32(idx, dataStart);
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const Slot &s = slots[i];
        writeU32(idx, s.en.off);
        writeU32(idx, s.en.size);
        writeU32(idx, s.en.size);
        writeU32(idx, s.en.type);
        writeName(idx, slotNames[i]);
    }
    idx.insert(idx.end(), afs.begin(), afs.begin() + dataStart);

    const fs::path idxPath = fs::path(outDir) / (base.string() + ".idx");
    {
        std::FILE *f = std::fopen(idxPath.string().c_str(), "wb");
        if (!f)
            return fail("cannot write " + idxPath.string());
        const bool ok = std::fwrite(idx.data(), 1, idx.size(), f) == idx.size();
        std::fclose(f);
        if (!ok)
            return fail("short write " + idxPath.string());
    }

    // Byte-for-byte verification before the caller drops the source .AFS.
    if (onStatus)
        onStatus("verifying " + base.string());
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const Slot &s = slots[i];
        std::vector<uint8_t> got;
        if (!readFile(dir / slotNames[i], got) || got.size() != s.en.size ||
            std::memcmp(got.data(), afs.data() + s.en.off, s.en.size) != 0)
            return fail("verification mismatch at slot " + std::to_string(i) + " of " +
                        src.string());
        done += s.en.size;
        if (onProgress)
            onProgress(done, total);
    }

    return AfsConvertResult{true, std::string()};
}