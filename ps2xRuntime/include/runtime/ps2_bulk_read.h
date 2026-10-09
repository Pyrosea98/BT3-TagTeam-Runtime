#pragma once
#include <cstdint>

// Private bridge opcode16: one bounded contiguous read per packet, no writes.
// The standard PINE protocol is retained for every other operation.
constexpr uint32_t Ps2BulkReplyLimit = 450000u - 5u;
inline bool ps2BulkReadValid(uint32_t address, uint32_t bytes, uint32_t ramBytes) {
    return bytes && bytes <= Ps2BulkReplyLimit &&
        uint64_t(address) + bytes <= ramBytes;
}
