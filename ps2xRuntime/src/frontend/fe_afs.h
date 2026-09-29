#pragma once

#include <cstdint>
#include <functional>
#include <string>

struct AfsConvertResult
{
    bool ok = false;
    std::string error;
};

// Extracts one Dragon Ball: BT3 AFS container (PZS3US*.AFS) into the layout that
// the runtime serves from folders:
//   <outDir>/<stem>/<name>, ...      per-entry files (exact [off, off+size),
//                                    inter-slot padding dropped). Names mirror
//                                    the reference AFS tools: unique name ->
//                                    plain name, duplicated name ->
//                                    <index>_<name> (index >= 4 digits). The
//                                    names come first from the bundled
//                                    assets/<stem>.afl name lists (community
//                                    "new AFLs" carrying the real file names and
//                                    formats; legacy "index\tname" text is also
//                                    understood), then from the AFS's own
//                                    embedded metadata table (48-byte records at
//                                    the end of the container; empty records are
//                                    named "blank_<i+1>" like the reference
//                                    tool). Unnamed entries fall back to %06u.
//   <outDir>/<stem>.idx              v3 index (blob + slot table + per-slot name)
// After dumping it re-reads every slot file and byte-compares it against the
// source so the caller can safely drop the .AFS afterwards.
//
// onStatus receives a short human-readable stage ("reading index...",
// "extracting entries...", "verifying..."); onProgress receives accumulated
// done/total bytes where total == 2 * fileSize (one pass to write the slots,
// one pass to verify them), or no progress row written when serializing.
//
// nameTableDir: bundled assets dir the converter searches for <stem>.afl (the
//     sidecar <afsPath>.afl is tried first). May be empty when no names exist.
//     A binary community AFL ("AFL\0" magic, count@12, names@16 as 32-byte
//     NUL-padded entries) must hold exactly one name per AFS entry, in order.
AfsConvertResult convertAfsToFolder(
    const std::string &afsPath, const std::string &outDir,
    const std::function<void(const std::string &)> &onStatus = {},
    const std::function<void(uint64_t done, uint64_t total)> &onProgress = {},
    const std::string &nameTableDir = {});