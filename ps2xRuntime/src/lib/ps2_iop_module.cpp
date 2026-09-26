#include "runtime/ps2_iop_module.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace ps2iop
{
namespace
{
    constexpr uint32_t PT_LOAD = 1;
    constexpr uint32_t PT_IOP = 0x70000080;

    uint16_t rdU16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    uint32_t rdU32(const uint8_t *p)
    { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

    bool readAll(const std::string &path, std::vector<uint8_t> &out)
    {
        std::FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0) { std::fclose(f); return false; }
        out.resize((size_t)sz);
        const bool ok = (sz == 0) || std::fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
        std::fclose(f);
        return ok;
    }
}

bool loadIrx(const std::string &path, Module &out)
{
    std::vector<uint8_t> d;
    if (!readAll(path, d)) return false;
    if (d.size() < 52 || std::memcmp(d.data(), "\x7f" "ELF", 4) != 0) return false;
    if (d[4] != 1 || d[5] != 1) return false;   // ELF32 little-endian

    out = Module{};
    out.name = std::filesystem::path(path).filename().string();
    out.entry = rdU32(d.data() + 24);
    const uint32_t phoff = rdU32(d.data() + 28);
    const uint16_t phentsize = rdU16(d.data() + 42);
    const uint16_t phnum = rdU16(d.data() + 44);

    uint32_t bestFilesz = 0;
    for (uint16_t i = 0; i < phnum; ++i)
    {
        const size_t off = phoff + (size_t)i * phentsize;
        if (off + 32 > d.size()) break;
        const uint32_t pType = rdU32(d.data() + off);
        const uint32_t pOffset = rdU32(d.data() + off + 4);
        const uint32_t pVaddr = rdU32(d.data() + off + 8);
        const uint32_t pFilesz = rdU32(d.data() + off + 16);
        const uint32_t pMemsz = rdU32(d.data() + off + 20);
        const uint32_t pFlags = rdU32(d.data() + off + 24);
        if (pType != PT_LOAD && pType != PT_IOP) continue;

        Segment s;
        s.vaddr = pVaddr; s.filesz = pFilesz; s.memsz = pMemsz; s.flags = pFlags;
        s.iopHeader = (pType == PT_IOP);
        if (pOffset + pFilesz <= d.size())
            s.data.assign(d.begin() + pOffset, d.begin() + pOffset + pFilesz);

        // IOP module header (p_type 0x70000080): u32 total, entry, gp, text, data, bss,
        // version(u16), name[8]. Read it straight from the file at p_offset: some modules
        // declare a smaller filesz than the 34 header bytes (e.g. MCMAN: 32), which would
        // otherwise leave gp/text unparsed.
        if (s.iopHeader && static_cast<uint64_t>(pOffset) + 34 <= d.size())
        {
            const uint8_t *h = d.data() + pOffset;
            out.entry = rdU32(h + 4);
            out.gp = rdU32(h + 8);
            out.hdrText = rdU32(h + 12);
            out.hdrData = rdU32(h + 16);
            out.hdrBss = rdU32(h + 20);
            char nm[9] = {0};
            std::memcpy(nm, h + 26, 8);
            out.moduleName.assign(nm);
        }

        // The main loadable segment (largest filesz) is the code+data image.
        if (pType == PT_LOAD && pFilesz > bestFilesz)
        {
            bestFilesz = pFilesz;
            out.textVaddr = pVaddr;
            out.textSize = pFilesz;
        }
        out.segments.push_back(std::move(s));
    }

    // Section headers: collect SHT_REL (.rel.text / .rel.data). Each entry is Elf32_Rel:
    // r_offset(u32) + r_info(u32), with r_sym = info >> 8 and r_type = info & 0xff.
    const uint32_t shoff = rdU32(d.data() + 32);
    const uint16_t shentsize = rdU16(d.data() + 46);
    const uint16_t shnum = rdU16(d.data() + 48);
    for (uint16_t i = 0; shoff && i < shnum; ++i)
    {
        const size_t off = shoff + (size_t)i * shentsize;
        if (off + 40 > d.size()) break;
        const uint32_t shType = rdU32(d.data() + off + 4);
        const uint32_t shOffset = rdU32(d.data() + off + 16);
        const uint32_t shSize = rdU32(d.data() + off + 20);
        const uint32_t shEntsize = rdU32(d.data() + off + 36);
        if (shType != 9) continue;   // SHT_REL
        const uint32_t es = shEntsize ? shEntsize : 8;
        for (uint32_t k = 0; k + 8 <= shSize; k += es)
        {
            const size_t e = (size_t)shOffset + k;
            if (e + 8 > d.size()) break;
            Reloc r;
            r.offset = rdU32(d.data() + e);
            const uint32_t info = rdU32(d.data() + e + 4);
            r.symbol = info >> 8;
            r.type = (uint8_t)(info & 0xff);
            out.relocs.push_back(r);
        }
    }

    // Import/export tables live inline in .text (ps2sdk irx.h): a table starts with a magic
    // word and is followed by stubs (import) or function pointers (export). Scan the loaded
    // segments for the magics; this also yields the exact export function roots.
    constexpr uint32_t kImportMagic = 0x41e00000;
    constexpr uint32_t kExportMagic = 0x41c00000;
    for (const auto &seg : out.segments)
    {
        if (seg.iopHeader || seg.data.size() < 8)
            continue;
        const uint8_t *p = seg.data.data();
        const size_t n = seg.data.size();
        for (size_t off = 0; off + 8 <= n;)
        {
            const uint32_t magic = rdU32(p + off);
            if (magic != kImportMagic && magic != kExportMagic)
            {
                off += 4;
                continue;
            }
            if (off + 20 > n)
                break;
            char nm[9] = {0};
            std::memcpy(nm, p + off + 12, 8);
            const uint16_t ver = rdU16(p + off + 8);
            const uint16_t mode = rdU16(p + off + 10);
            size_t q = off + 20;
            if (magic == kImportMagic)
            {
                // Stubs: {u32 jump=0x03e00008, u16 ordinal, u16 pad}, terminated by 8 zero bytes.
                while (q + 8 <= n && rdU32(p + q) != 0)
                {
                    Import im;
                    im.module = nm;
                    im.version = ver;
                    im.mode = mode;
                    im.stub = seg.vaddr + (uint32_t)q;
                    im.ordinal = rdU16(p + q + 4);
                    out.imports.push_back(std::move(im));
                    q += 8;
                }
                q += 8; // skip the terminator
            }
            else
            {
                // Exports: function pointers (vaddrs) from +20 on, one per ordinal -- the same
                // layout the importer reads (fptrAddr = tableVaddr + 20 + ordinal * 4). The table
                // may carry interior zero slots for unused ordinals, so the first zero is not
                // necessarily the end: cutting there hid every export past the gap and a
                // cross-module import then resolved to address 0. Accept a slot only when it
                // points into one of this module's own segments, and end the table on the first
                // run of two slots that do not -- that separates a single unused ordinal from
                // the code that follows the table.
                Export ex;
                ex.module = nm;
                ex.version = ver;
                ex.mode = mode;
                ex.tableVaddr = seg.vaddr + static_cast<uint32_t>(off);
                auto inModule = [&out](uint32_t v)
                {
                    for (const auto &s : out.segments)
                        if (s.memsz && v >= s.vaddr && v < s.vaddr + s.memsz) return true;
                    return false;
                };
                int bad = 0;
                size_t r = q;
                for (; r + 4 <= n; r += 4)
                {
                    const uint32_t abs = seg.vaddr + rdU32(p + r);
                    if (!inModule(abs))
                    {
                        if (++bad >= 2) break;
                        continue;
                    }
                    bad = 0;
                    ex.fptrs.push_back(abs);
                }
                out.exports.push_back(std::move(ex));
                q = r;   // resume the scan past the table, not inside it
            }
            off = q;
        }
    }

    return !out.segments.empty();
}
}
