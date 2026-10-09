#pragma once
#include <atomic>
#include <cstdint>

namespace ps2x {
// One producer (socket worker); read cumulatively by the FPS reporter. Socket
// receive idle time is intentionally excluded from packet service time.
struct BridgePerf {
    std::atomic<uint64_t> packets{0}, operations{0}, rejected{0};
    std::atomic<uint64_t> serviceNs{0}, sendNs{0}, receivedBytes{0}, sentBytes{0};
};
inline BridgePerf& bridgePerf() { static BridgePerf counters; return counters; }
}
