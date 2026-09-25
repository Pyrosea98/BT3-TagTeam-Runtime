#ifndef PS2_GIF_ARBITER_H
#define PS2_GIF_ARBITER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

enum class GifPathId : uint8_t
{
    Path1 = 1,
    Path2 = 2,
    Path3 = 3,
    HostDraw = 4,   // [seamvk] a native draw record travelling the stream in order; never reaches the GS
};

// [gifarena] A packet is a VIEW into an arena the batch owns, not its own heap block. submit() used to
// allocate a std::vector per packet and takeQueue() handed each one over as its own item: at ~300k kicks/s
// that was a malloc + free + lock + queue push + wake per packet on the two busiest threads. Now submit()
// appends into one growing arena and takeQueue() hands the whole run over as one batch; the packet's
// `data` pointer is resolved when the arena is final and stays valid because the arena moves by buffer.
struct GifArbiterPacket
{
    GifPathId pathId;
    bool path2DirectHl = false;
    bool path3Image = false;
    uint32_t offset = 0;              // into the arena it was submitted to
    uint32_t size = 0;
    const uint8_t *data = nullptr;    // resolved by takeQueue()
};

struct GifArbiterBatch
{
    std::vector<GifArbiterPacket> pkts;
    std::vector<std::vector<uint8_t>> arenas;   // the packets point into these; merging batches appends arenas
    bool empty() const { return pkts.empty(); }
};

class GifArbiter
{
public:
    using ProcessPacketFn = std::function<void(const uint8_t *, uint32_t)>;

    GifArbiter() = default;
    explicit GifArbiter(ProcessPacketFn processFn);

    void setProcessPacketFn(ProcessPacketFn fn) { m_processFn = std::move(fn); }

    void submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl = false);

    void drain();
    void takeQueue(GifArbiterBatch &out);     // [vu1pipe] hand the queued packets (ordered as drain would) to another thread
    void process(const GifArbiterPacket &pkt);   // [vu1pipe] run the process function on one packet (the thread that owns the GS)
    uint32_t pending() const;                    // packets this thread's lane holds since its last takeQueue/drain

    // [giflane] The kick worker and the guest thread are two independent producers whose packets were
    // never meant to interleave: with [vu1pipe] the worker's go to stage 2 in order and the guest's are
    // processed in place at once (the known guest-ahead-of-queue hazard, sceGsSyncPath drains first).
    // One shared FIFO let a guest-thread drain STEAL the worker's still-queued packets and run them in
    // place, out of order with the ones already on stage 2 -- a microsecond window while the worker
    // flushed per packet, a real one now that it flushes in runs. Each producer therefore owns a lane;
    // the worker registers itself once and every submit/take/drain acts on the caller's lane only.
    static void markWorkerThread();

private:
    struct Lane
    {
        std::vector<GifArbiterPacket> queue;
        std::vector<uint8_t> arena;
        std::atomic<uint32_t> pending{0};
    };
    ProcessPacketFn m_processFn;
    // [giflock] The lanes are written by the EE guest thread (GS/font stubs -> processPendingTransfers ->
    // submitGifPacket) AND by the kick worker (VIF1/path3). Unsynchronised access once corrupted the host
    // heap: two crashes on 2026-09-10 aborted in malloc/free (cores 781549, 787693). Since [giflane] each
    // thread only touches its own lane, but the lock stays (uncontended it is a few ns and it keeps the
    // sync-mode/single-thread configurations trivially correct); packet processing stays outside it.
    std::mutex m_qMtx;
    std::atomic<uint64_t> m_lockWaits{0};    // times a thread had to wait
    std::atomic<uint64_t> m_lockWaitNs{0};   // total wait, nanoseconds
    Lane m_lanes[2];                         // 0 = guest / any thread, 1 = the kick worker
    Lane &lane();

    static bool isImagePacket(const uint8_t *data, uint32_t sizeBytes);
    static uint8_t pathPriority(GifPathId id);
};

#endif
