// [seam] Native render seam, Phases 1-3 of docs/NATIVE-RENDER-SEAM.md.
//
//   seammesh   Phase 1: import the on-disc VIF list a batch CALLs into a host mesh, cached.
//   seamxform  Phase 2: host transform for a VU1 program, producing the GIF bytes VU1 would kick.
//   seam       Phase 3: PS2X_SEAMVERIFY=1 runs the host transform beside VU1 and compares the
//              kicked bytes; PS2X_SEAMSKIP=1 submits the host packet and does not run VU1 for
//              the batches it owns. Whole batches only, never part of one.
//
// Measured vocabulary of the lists (docs/SEAM-PHASE0-BATCHES.md): one DMA RET tag, unmasked
// UNPACK V4-32 only, MSCNT between chunks, NOP padding. Character lists: 5-qw header at TOP+0
// and up to 33 vertices of 3 qw at TOP+5. Stage/effect lists: a 1-qw setup unpack + MSCNT, then
// 3-qw header at TOP+0 and vertices at TOP+3.
#pragma once
#include <cstdint>
#include <vector>

namespace seammesh
{
    struct Chunk
    {
        uint32_t hdrQw = 0;          // 3 or 5
        uint8_t hdr[5 * 16] = {};
        uint32_t nvec = 0;           // raw quadwords of vertex data (3 or 4 per vertex, program-dependent)
        uint32_t count = 0;          // nvec / 3: the common stride, for the OBJ dump
        std::vector<uint8_t> verts;  // nvec * 16 bytes, raw VU quadwords
    };
    struct Mesh
    {
        uint32_t list = 0;           // guest address of the RET tag
        uint32_t bytes = 0;          // tag + qwc*16
        uint64_t hash = 0;           // of those bytes
        uint8_t head[64] = {}, tail[64] = {};   // for the cheap per-use check
        uint64_t checkedFrame = 0;   // last full-hash validation
        bool hasSetup = false;       // stage/effect lists start with a 1-qw unpack + MSCNT
        uint8_t setup[16] = {};
        std::vector<Chunk> chunks;
        uint32_t totalVerts = 0;
        bool ok = false;
    };
    // Cached import. Static lists (stage, characters) are re-validated cheaply per use and fully every
    // 64 frames; volatile lists (effects, rebuilt by the EE every frame at the same address) are
    // always re-hashed in full. nullptr if the list does not parse.
    const Mesh *get(uint8_t *rdram, uint32_t list, bool volatileList = false);
    void report();
}

namespace seam
{
    bool verifyOn();
    bool skipOn();
    bool on();

    // Kick thread. Called by the VIF1 interpreter around every MSCAL/MSCALF/MSCNT, after TOPS/DBF
    // have been advanced. beforeRun returns true when the host owns this run: the caller must
    // then NOT execute VU1. afterRun compares (verify) and clears.
    bool beforeRun(uint32_t startPc, bool mscnt, uint32_t top, uint8_t *vuData, uint32_t dataSize, void *memory);
    void afterRun();
    // From xgkickImpl: the bytes VU1 kicked.
    void onKick(const uint8_t *data, uint32_t bytes);
    // From the VIF1 DMA chain walk, at a CALL tag: true when the host owns the batch that CALLs
    // this list, in which case the walker must not follow the CALL (the list's VIF stream is
    // never unpacked; the host emits the whole batch at the MSCALF). Skip mode only.
    bool dmaCallSkip(uint8_t *rdram, uint32_t listAddr);
}
