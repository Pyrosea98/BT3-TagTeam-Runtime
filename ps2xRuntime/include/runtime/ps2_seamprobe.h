// [seamprobe] PS2X_SEAMPROBE=1: attribute every VU1 microprogram start in a frame to the EE
// "begin batch" builder that produced its constant block, and print the table every 5 s.
// Phase 0 of docs/NATIVE-RENDER-SEAM.md. Observation only: nothing here changes guest behaviour.
//
// How the attribution works. The builders (sub_00123278 and its siblings) allocate a header
// packet from the display-list bump allocator and write a VIF UNPACK V4-32 to VU1 address 0;
// the caller then fills the block (bone matrices, camera, TEX0). The block is complete by the
// time the list is closed (sub_00100798). At that point the probe hashes each pending block in
// guest memory. When VIF1 later unpacks a block to address 0 the payload is hashed the same way
// and looked up, so the match needs no chain source map and survives the async kick pipeline.
// The MSCAL/MSCNT that follow are charged to that batch until the next address-0 unpack.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace seamprobe
{
    bool on();

    // Game thread, from the game_overrides hooks.
    void noteDrawEnter(uint32_t actor, uint32_t entity, uint32_t mode, uint32_t texLo, uint32_t texHi, uint32_t ra);
    void noteDrawExit();
    void noteBuilder(uint32_t builderAddr, uint32_t pkt, uint32_t arg0, uint32_t ra);
    void finalizeList(uint8_t *rdram);   // sub_00100798 entry: the list is complete

    // Kick thread, from the VIF1 interpreter.
    void noteUnpack0(const uint8_t *payload, uint32_t qw);
    void beginMscal(uint32_t startPc, bool mscnt);              // before VU1 runs: attribute the run to a batch
    void noteMscal(uint32_t startPc, uint32_t progHashLo, bool mscnt);   // after: tally
    bool currentBatch(uint32_t &builder, uint32_t &list);        // the batch the current run belongs to
    uint32_t builderOfList(uint32_t list);                       // last builder seen CALLing this list, 0 if none

    // [kickprobe] PS2X_KICKPROBE=1: attribute every VIF1 DMA chain to the EE code that sent it. The three DMA-send
    // helpers (0x100cc0, 0x100b98, 0x100d88) are hooked: noteKick records the caller; classifyVif1Chain, called
    // from the DMAC when the channel starts, walks the chain in guest memory and tallies what its DIRECT packets
    // do (frame targets, texture formats, uploads, primitive kinds, DATE / alpha-only draws) per caller.
    bool kickProbeOn();
    void noteKick(uint32_t helper, uint32_t chainAddr, uint32_t ra);
    void noteAdvance(uint32_t before, uint32_t after, uint32_t ra);   // the display-list pointer moved before..after in a helper called from ra
    void classifyVif1Chain(const uint8_t *rdram, uint32_t tagAddr);
    // Game thread: the chain buffer's offset -> guest address map, keyed by the buffer pointer (it moves with the vector).
    void publishChainMap(const void *chainData, const std::vector<std::array<uint32_t, 3>> &map);
    // Kick thread: bind the map for the chain being interpreted, then tally each DIRECT payload by its guest owner.
    void beginChain(const void *chainData);
    void noteDirect(uint32_t pos, const uint8_t *gif, uint32_t bytes);
}
