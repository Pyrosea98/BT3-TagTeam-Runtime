// [netplay] Deterministic lockstep netplay transport for BT3-Recomp.
//
// MODEL: the two machines never exchange game state during play -- only BUTTONS. Both run the
// full simulation (both fighters); each renders only its own player's viewport (PS2X_NETVIEW,
// see the online-viewport work). A PS2 pad sample is 6 bytes, so this is ~500 bytes/sec/player,
// four orders of magnitude below video streaming.
//
// TIMING: input-delay lockstep. Input sampled on frame F is APPLIED on frame F + delay, which
// gives the network delay*16.7 ms to deliver it. Rollback can replace this later without
// touching the transport; it needs save/load-state, which is a separate build.
//
// Each packet repeats the last kRedundancy frames of input, so a dropped datagram heals on the
// next arrival with no retransmit -- that is why this is UDP and not TCP: a late input is
// useless, so we would rather drop it than wait for it.
//
//   PS2X_NET=<host>:<port>   connect to a peer        PS2X_NET_LISTEN=<port>   wait for one
//   PS2X_NET_PLAYER=1|2      which player is local    PS2X_NET_DELAY=<frames>  default 4
//   PS2X_NET_TIMEOUT=<ms>    stall limit, default 2000
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unordered_map>
#include <deque>
#include <cstring>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define PS2X_CLOSESOCK closesocket
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int SOCKET;
#  define INVALID_SOCKET (-1)
#  define PS2X_CLOSESOCK ::close
#endif

#include "runtime/ps2_netplay.h"

namespace {

constexpr uint32_t kMagic       = 0x4e335442u;   // 'BT3N'
constexpr uint16_t kVersion     = 1u;
constexpr uint32_t kRedundancy  = 8u;            // inputs repeated per packet
constexpr uint32_t kMaxInputs   = 16u;

#pragma pack(push, 1)
struct NetPkt
{
    uint32_t     magic;
    uint16_t     version;
    uint8_t      player;        // sender's player index, 1 or 2
    uint8_t      count;         // inputs carried, oldest first
    uint32_t     baseFrame;     // frame of inputs[0]
    uint8_t      battleType;    // host's choice: 0 Single, 1 Team, 2 DP  (host is authoritative)
    uint8_t      timeLimit;     // host's choice: the Battle Settings time-limit index (3 = default)
    // DP Battle's point budget: 0 = 10 DP, 1 = 15 DP, 2 = 20 DP. Only meaningful when
    // battleType is 2. Carved out of pad0, so the wire format and version are unchanged and a
    // peer that predates this field sends 0 -- which is a valid budget, not garbage.
    uint8_t      dpLimit;
    // [statesync] 0 = inputs; 1 = SYNC OFFER (host -> joiner: baseFrame = the frame, checksum = the blob's
    // bytes, the inputs area carries the file path); 2 = SYNC DONE (joiner -> host: baseFrame = the frame).
    uint8_t      kind;
    uint32_t     checkFrame;    // frame the checksum belongs to (0 = none)
    uint64_t     checksum;      // gameplay-state hash, for desync detection
    Ps2xNetInput inputs[kMaxInputs];
};
#pragma pack(pop)

struct Net
{
    bool        active   = false;
    bool        listening = false;
    int         localPlayer = 1;      // 1 or 2
    // BT3 runs at 30 fps, so ONE frame of delay is 33 ms -- the old default of 4 was 133 ms,
    // chosen as if this were a 60 fps game. The delay only has to cover the network round trip:
    // 1 on loopback, 2 on a LAN, more only for a real internet link.
    uint32_t    delay    = 2;
    uint32_t    timeoutMs = 2000;
    SOCKET      sock     = INVALID_SOCKET;
    sockaddr_in peer{};
    bool        peerKnown = false;
    bool        connected = false;

    std::mutex  mtx;
    std::unordered_map<uint32_t, Ps2xNetInput> local;    // frame -> our input (already delayed)
    std::unordered_map<uint32_t, Ps2xNetInput> remote;   // frame -> peer input
    std::unordered_map<uint32_t, uint64_t>     peerHash; // frame -> peer's state hash
    std::unordered_map<uint32_t, uint64_t>     ourHash;
    // [relframe] Frame numbers are RELATIVE TO THE CONNECTION, not absolute. Connecting from the
    // overlay mid-session means the two machines are at completely different frame counts (2018 vs
    // 2333 when this was found), so keying inputs by absolute frame made every lookup miss and
    // both sides stalled the full timeout on every frame -- a hard freeze. Each side records its
    // own frame at connect and works in offsets from there; lockstep then keeps them in step.
    uint32_t    base = 0;
    bool        needBase = true;
    uint32_t    lastSent = 0;
    uint32_t    checkFrame = 0;
    uint64_t    checkValue = 0;
    std::atomic<uint64_t> stalls{0}, stallNs{0}, desyncs{0}, rx{0}, tx{0};
    std::chrono::steady_clock::time_point lastRx{};   // watchdog: peer silence
    uint32_t session = 0;
    // Both machines run their own netjump, so the match setup MUST be agreed or they build
    // different fights and desync at once. The HOST is authoritative: it stamps its choice into
    // every packet and the joiner adopts it.
    std::atomic<int> battleType{0};
    std::atomic<int> timeLimit{3};   // 3 is the game's default
    std::atomic<int> dpLimit{0};     // 0 = 10 DP                             // bumped per connect, so netjump can reset
    // [rollback]
    uint32_t    rbWindow = 0;                                    // PS2X_NET_ROLLBACK=<frames>, 0 = lockstep
    std::unordered_map<uint32_t, Ps2xNetInput> predicted;        // rel frame -> the remote input we GUESSED with
    uint32_t    rollbackTo = 0xFFFFFFFFu;                        // earliest rel frame whose real input differed from the guess
    uint32_t    fakeLagMs = 0;                                   // PS2X_NET_FAKELAG=<ms>: hold received packets (loopback testing)
    bool        testInput = false;                               // PS2X_NET_TESTINPUT=1: toggle R3 every 15 frames so the peer mispredicts
    std::deque<std::pair<std::chrono::steady_clock::time_point, NetPkt>> held;
    std::atomic<uint64_t> predictions{0}, rollbacks{0}, mispredicts{0};
    // [statesync]
    bool        syncOn = false;        // PS2X_NET_SYNC=1 / overlay
    bool        synced = true;         // false from connect until the state sync completes (when syncOn)
    bool        syncOffered = false, syncDone = false;
    uint32_t    syncFrame = 0; uint64_t syncBytes = 0; char syncPath[96] = {};
    int         port = 0;              // host: the UDP listen port; the state blob goes over TCP on the same number
    std::vector<uint8_t> syncBlob;     // host: the published state until the joiner acknowledged
    SOCKET      syncListen = INVALID_SOCKET;
    bool        envDefaults = false;   // PS2X_NET_ROLLBACK / PS2X_NET_SYNC read once; the overlay may override
    uint32_t    desyncFrame = 0;       // [desyncdump] first frame whose confirmed hash differed (0 = none yet)
    uint32_t    checkEvery = 60;       // PS2X_NET_CHECKEVERY: confirmed-state checksum interval in frames
};

Net g;
// [netjump] the HOST's choice, like the game mode: it rides in bit 7 of the timeLimit field
// (indices use 3 bits) so a joiner that left its own box unticked still jumps with the host.
std::atomic<bool> g_autoJump{false};
static uint8_t wireTimeLimit() { return static_cast<uint8_t>((g.timeLimit.load(std::memory_order_relaxed) & 0x7f) | (g.localPlayer == 1 && g_autoJump.load(std::memory_order_relaxed) ? 0x80 : 0)); }

const Ps2xNetInput kNeutral{0xFFFFu, 0x80u, 0x80u, 0x80u, 0x80u};

void setNonBlocking(SOCKET s)
{
#if defined(_WIN32)
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
}

bool parseHostPort(const char *v, sockaddr_in &out)
{
    std::string s(v);
    const size_t c = s.rfind(':');
    if (c == std::string::npos) return false;
    const std::string host = s.substr(0, c);
    const int port = std::atoi(s.c_str() + c + 1);
    if (port <= 0 || port > 65535) return false;
    std::memset(&out, 0, sizeof out);
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1) return true;
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.sin_addr = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

void sendOurs(uint32_t frame)
{
    if (!g.peerKnown) return;
    NetPkt p{};
    p.magic = kMagic; p.version = kVersion;
    p.player = static_cast<uint8_t>(g.localPlayer);
    p.battleType = static_cast<uint8_t>(g.battleType.load(std::memory_order_relaxed));
    p.timeLimit  = wireTimeLimit();
    p.dpLimit    = static_cast<uint8_t>(g.dpLimit.load(std::memory_order_relaxed));
    p.checkFrame = g.checkFrame; p.checksum = g.checkValue;
    const uint32_t base = frame > (kRedundancy - 1u) ? frame - (kRedundancy - 1u) : 0u;
    p.baseFrame = base;
    uint32_t n = 0;
    for (uint32_t f = base; f <= frame && n < kMaxInputs; ++f, ++n)
    {
        const auto it = g.local.find(f);
        p.inputs[n] = (it != g.local.end()) ? it->second : kNeutral;
    }
    p.count = static_cast<uint8_t>(n);
    const size_t bytes = sizeof(NetPkt) - sizeof(Ps2xNetInput) * (kMaxInputs - n);
    ::sendto(g.sock, reinterpret_cast<const char *>(&p), static_cast<int>(bytes), 0,
             reinterpret_cast<sockaddr *>(&g.peer), sizeof g.peer);
    g.tx.fetch_add(1, std::memory_order_relaxed);
}

static bool sameInput(const Ps2xNetInput &a, const Ps2xNetInput &b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// [rollback] A packet's inputs land here (directly, or after the fake lag). A confirmed input for a
// frame we already ran on a guess either matches (nothing to do) or names the earliest frame to
// roll back to.
static void applyInputs(const NetPkt &p)
{
    std::lock_guard<std::mutex> lk(g.mtx);
    for (uint32_t i = 0; i < p.count && i < kMaxInputs; ++i)
    {
        const uint32_t f = p.baseFrame + i;
        const Ps2xNetInput &in = p.inputs[i];
        if (g.rbWindow)
        {
            auto pit = g.predicted.find(f);
            if (pit != g.predicted.end())
            {
                if (!sameInput(pit->second, in)) { if (f < g.rollbackTo) g.rollbackTo = f; g.mispredicts.fetch_add(1, std::memory_order_relaxed); }
                g.predicted.erase(pit);
            }
        }
        g.remote[f] = in;
    }
    if (p.checkFrame)
    {
        g.peerHash[p.checkFrame] = p.checksum;
        const auto it = g.ourHash.find(p.checkFrame);   // ours already set: compare here too (the other order is in ps2NetSetChecksum)
        if (it != g.ourHash.end() && it->second != p.checksum)
        {
            if (g.desyncFrame == 0u) g.desyncFrame = p.checkFrame;
            if (g.desyncs.fetch_add(1, std::memory_order_relaxed) == 0)
                std::fprintf(stderr, "[netplay] *** DESYNC at frame %u: ours %016llx peer %016llx ***\n",
                             p.checkFrame, (unsigned long long)it->second, (unsigned long long)p.checksum);
        }
    }
}

// [statesync] control packets: no inputs, the header fields carry the payload
static void sendSyncCtl(uint8_t kind, uint32_t frame, uint64_t bytes, const char *path)
{
    if (!g.peerKnown || g.sock == INVALID_SOCKET) return;
    NetPkt p{}; p.magic = kMagic; p.version = kVersion; p.player = static_cast<uint8_t>(g.localPlayer);
    p.kind = kind; p.baseFrame = frame; p.checksum = bytes; p.count = 0u;
    p.battleType = static_cast<uint8_t>(g.battleType.load(std::memory_order_relaxed));
    p.timeLimit  = wireTimeLimit();
    p.dpLimit    = static_cast<uint8_t>(g.dpLimit.load(std::memory_order_relaxed));
    size_t bytesOut = sizeof(NetPkt) - sizeof(Ps2xNetInput) * kMaxInputs;
    if (path && path[0])
    {
        const size_t room = sizeof(NetPkt) - offsetof(NetPkt, inputs);
        std::strncpy(reinterpret_cast<char *>(&p.inputs[0]), path, room - 1u);
        bytesOut = sizeof(NetPkt);
    }
    ::sendto(g.sock, reinterpret_cast<const char *>(&p), static_cast<int>(bytesOut), 0,
             reinterpret_cast<sockaddr *>(&g.peer), sizeof g.peer);
    g.tx.fetch_add(1, std::memory_order_relaxed);
}
static void sendSyncDone(uint32_t frame) { for (int i = 0; i < 3; ++i) sendSyncCtl(2u, frame, 0u, nullptr); }
static void syncResetTables(uint32_t base)
{   // both sides restart input numbering at the adopted frame
    std::lock_guard<std::mutex> lk(g.mtx);
    g.local.clear(); g.remote.clear(); g.predicted.clear(); g.peerHash.clear(); g.ourHash.clear(); g.held.clear();
    g.base = base; g.needBase = false; g.rollbackTo = 0xFFFFFFFFu; g.checkFrame = 0; g.checkValue = 0; g.lastSent = 0;
    g.synced = true;
}

void pump()
{
    if (g.fakeLagMs)
    {   // release packets whose artificial delay has passed
        const auto now = std::chrono::steady_clock::now();
        while (!g.held.empty() && std::chrono::duration_cast<std::chrono::milliseconds>(now - g.held.front().first).count() >= (long)g.fakeLagMs)
        { applyInputs(g.held.front().second); g.held.pop_front(); }
    }
    for (;;)
    {
        NetPkt p{};
        sockaddr_in from{}; socklen_t fl = sizeof from;
        const int n = ::recvfrom(g.sock, reinterpret_cast<char *>(&p), sizeof p, 0,
                                 reinterpret_cast<sockaddr *>(&from), &fl);
        if (n <= 0) break;
        if (static_cast<size_t>(n) < sizeof(NetPkt) - sizeof(Ps2xNetInput) * kMaxInputs) continue;
        if (p.magic != kMagic || p.version != kVersion) continue;
        if (p.player == 0xFFu)
        {   // peer said goodbye
            g.peerKnown = false;
            std::fprintf(stderr, "[netplay] peer disconnected\n");
            ps2NetDisconnect("peer left");
            return;
        }
        g.lastRx = std::chrono::steady_clock::now();
        if (p.player == 1u && g.localPlayer != 1)      // the host decides the match setup
        {
            g.battleType.store(p.battleType, std::memory_order_relaxed);
            g.timeLimit.store(p.timeLimit & 0x7f, std::memory_order_relaxed);
            g_autoJump.store((p.timeLimit & 0x80) != 0, std::memory_order_relaxed);   // [netjump] host's choice
            g.dpLimit.store(p.dpLimit, std::memory_order_relaxed);
        }
        if (!g.peerKnown)
        {   // listener learns the peer address from the first valid packet
            g.peer = from; g.peerKnown = true;
            char ip[64] = {0}; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            std::fprintf(stderr, "[netplay] peer connected from %s:%u (they are player %u)\n",
                         ip, (unsigned)ntohs(from.sin_port), (unsigned)p.player);
        }
        if (!g.connected)
        {
            g.connected = true; ++g.session;
            if (g.syncOn) { g.synced = false; g.syncOffered = false; g.syncDone = false; }   // [statesync] inputs wait for the state
            // host drives; the joiner just follows the inputs it receives
            if (g.listening) { if (const char *a = std::getenv("PS2X_NET_AUTOSTART")) ps2NetBeginAutoStart(a); }
        }
        g.rx.fetch_add(1, std::memory_order_relaxed);
        if (p.kind == 1u)
        {   // [statesync] the host's offer (the joiner adopts it at a comparable boundary; an offer for a
            // frame we already adopted means our DONE was lost: answer again)
            if (!g.listening)
            {
                g.syncFrame = p.baseFrame; g.syncBytes = p.checksum;
                const size_t room = sizeof(NetPkt) - offsetof(NetPkt, inputs);
                std::memcpy(g.syncPath, &p.inputs[0], room < sizeof g.syncPath ? room : sizeof g.syncPath); g.syncPath[sizeof g.syncPath - 1] = 0;
                g.syncOffered = true;
                if (g.synced && g.base == p.baseFrame) sendSyncDone(p.baseFrame);
            }
            continue;
        }
        if (p.kind == 2u)
        {
            if (g.listening && p.baseFrame == g.syncFrame) g.syncDone = true;
            continue;
        }
        if (g.fakeLagMs) g.held.emplace_back(std::chrono::steady_clock::now(), p);
        else applyInputs(p);
    }
}

}  // namespace

bool ps2NetActive() { return g.active; }
int  ps2NetLocalPlayer() { return g.localPlayer; }
uint32_t ps2NetDelay() { return g.delay; }

// Shared by the env path and the overlay's Host/Join buttons. `listenPort != 0` hosts;
// otherwise `conn` is "host:port". Safe to call while the game is running: the socket is the
// only state, and the frame hook pumps it from the next frame on.
// [rollback] The environment sets the defaults once; the overlay's Netplay tab may change them before Host/Join.
static void netEnvDefaults()
{
    if (g.envDefaults) return;
    g.envDefaults = true;
    const char *w = std::getenv("PS2X_NET_ROLLBACK");
    g.rbWindow = (w && w[0]) ? (uint32_t)std::atoi(w) : 0u;
    if (g.rbWindow > 30u) g.rbWindow = 30u;
    const char *l = std::getenv("PS2X_NET_FAKELAG");
    g.fakeLagMs = (l && l[0]) ? (uint32_t)std::atoi(l) : 0u;
    const char *ti = std::getenv("PS2X_NET_TESTINPUT");
    g.testInput = ti && ti[0] && ti[0] != '0';
    const char *sy = std::getenv("PS2X_NET_SYNC");
    g.syncOn = !(sy && sy[0] == '0');   // [statesync] on unless PS2X_NET_SYNC=0 (only meaningful with a rollback window)
    if (const char *ce = std::getenv("PS2X_NET_CHECKEVERY")) { const int v = std::atoi(ce); if (v >= 1 && v <= 3600) g.checkEvery = (uint32_t)v; }
}
void ps2NetSetRollback(int frames) { netEnvDefaults(); if (frames >= 0 && frames <= 30) g.rbWindow = (uint32_t)frames; }
int  ps2NetRollbackSetting()        { netEnvDefaults(); return (int)g.rbWindow; }
void ps2NetSetSync(bool on)         { netEnvDefaults(); g.syncOn = on; }
bool ps2NetSyncSetting()            { netEnvDefaults(); return g.syncOn; }

static bool netStart(const char *conn, int listenPort, int player)
{
    netEnvDefaults();
    if (g.active) { std::fprintf(stderr, "[netplay] already connected\n"); return false; }
    const bool syncWanted = g.syncOn;
    g.syncOn = syncWanted && g.rbWindow != 0u;   // [statesync] needs the frame-boundary controller
    g.synced = !g.syncOn;
    if (g.rbWindow) std::fprintf(stderr, "[netplay] rollback window %u frames%s\n", g.rbWindow, g.fakeLagMs ? " (with fake lag)" : "");
    if (syncWanted && !g.rbWindow) std::fprintf(stderr, "[netplay] state sync needs a rollback window: lockstep without sync\n");
    if (g.syncOn) std::fprintf(stderr, "[netplay] state sync at connect (%s)\n", listenPort ? "host publishes" : "joiner adopts");
    g.port = listenPort;
#if defined(_WIN32)
    static bool s_wsa = [](){ WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); return true; }();
    (void)s_wsa;
#endif
    g.localPlayer = (player == 2) ? 2 : 1;
    if (const char *d = std::getenv("PS2X_NET_DELAY")) { const int v = std::atoi(d); if (v >= 0 && v <= 20) g.delay = (uint32_t)v; }
    if (const char *t = std::getenv("PS2X_NET_TIMEOUT")) { const int v = std::atoi(t); if (v > 0) g.timeoutMs = (uint32_t)v; }

    g.sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (g.sock == INVALID_SOCKET) { std::fprintf(stderr, "[netplay] socket() failed\n"); return false; }

    if (g.sock == INVALID_SOCKET) { std::fprintf(stderr, "[netplay] socket() failed\n"); return false; }
    if (listenPort)
    {
        int one = 1; setsockopt(g.sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);   // re-host right after a crash / a lingering socket
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY;
        a.sin_port = htons(static_cast<uint16_t>(listenPort));
        if (::bind(g.sock, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0)
        { std::fprintf(stderr, "[netplay] bind(%d) failed\n", listenPort); PS2X_CLOSESOCK(g.sock); g.sock = INVALID_SOCKET; return false; }
        g.listening = true;
        std::fprintf(stderr, "[netplay] listening on UDP %d as player %d (delay %u frames)\n", listenPort, g.localPlayer, g.delay);
    }
    else
    {
        if (!conn || !parseHostPort(conn, g.peer))
        { std::fprintf(stderr, "[netplay] '%s' is not host:port\n", conn ? conn : "(null)"); PS2X_CLOSESOCK(g.sock); g.sock = INVALID_SOCKET; return false; }
        g.peerKnown = true;
        char ip[64] = {0}; inet_ntop(AF_INET, &g.peer.sin_addr, ip, sizeof ip);
        std::fprintf(stderr, "[netplay] peer %s:%u, local player %d (delay %u frames)\n",
                     ip, (unsigned)ntohs(g.peer.sin_port), g.localPlayer, g.delay);
    }
    setNonBlocking(g.sock);
    g.active = true;
    return true;
}

bool ps2NetHost(int port, int player)              { return netStart(nullptr, port, player); }
bool ps2NetJoin(const char *hostPort, int player)  { return netStart(hostPort, 0, player); }
bool ps2NetPeerConnected()                         { return g.connected; }

// [netplay] The overlay switch. Seeded from the environment once, then owned by whoever last set it.
static std::atomic<int> g_netOverlay{0};
static bool ps2xNetOverlayInit()
{
    static const int seeded = [](){ const char *v = std::getenv("NET_OVERLAY");
                                     return (v && v[0] && v[0] != '0') ? 1 : 0; }();
    g_netOverlay.store(seeded, std::memory_order_relaxed);
    return seeded != 0;
}
bool ps2xNetOverlayEnabled()
{
    static const bool seeded = ps2xNetOverlayInit();
    (void)seeded;
    return g_netOverlay.load(std::memory_order_relaxed) != 0;
}
void ps2xSetNetOverlayEnabled(bool on)
{
    ps2xNetOverlayInit();   // so a later enable does not get clobbered by the seed
    g_netOverlay.store(on ? 1 : 0, std::memory_order_relaxed);
}

uint32_t ps2NetSession()                           { return g.session; }

// A "bye" is an ordinary packet with player = 0xFF. Sending one means the peer tears down at
// once instead of discovering us gone via the stall timeout, which would otherwise freeze their
// game for PS2X_NET_TIMEOUT on every frame.
static void sendBye()
{
    if (!g.peerKnown || g.sock == INVALID_SOCKET) return;
    NetPkt p{}; p.magic = kMagic; p.version = kVersion; p.player = 0xFFu; p.count = 0u;
    const size_t bytes = sizeof(NetPkt) - sizeof(Ps2xNetInput) * kMaxInputs;
    for (int i = 0; i < 3; ++i)   // UDP: send a few, they are tiny
        ::sendto(g.sock, reinterpret_cast<const char *>(&p), static_cast<int>(bytes), 0,
                 reinterpret_cast<sockaddr *>(&g.peer), sizeof g.peer);
}

void ps2NetDisconnect(const char *why)
{
    if (!g.active) return;
    sendBye();
    std::lock_guard<std::mutex> lk(g.mtx);
    if (g.sock != INVALID_SOCKET) { PS2X_CLOSESOCK(g.sock); g.sock = INVALID_SOCKET; }
    if (g.syncListen != INVALID_SOCKET) { PS2X_CLOSESOCK(g.syncListen); g.syncListen = INVALID_SOCKET; }
    g.syncBlob.clear(); g.syncBlob.shrink_to_fit();
    g.active = false; g.connected = false; g.peerKnown = false; g.listening = false;
    g.local.clear(); g.remote.clear(); g.peerHash.clear(); g.ourHash.clear();
    g.needBase = true; g.base = 0; g.checkFrame = 0; g.checkValue = 0;
    g.synced = true; g.syncOffered = false; g.syncDone = false;   // [statesync] (netStart re-derives syncOn from the setting)
    std::fprintf(stderr, "[netplay] disconnected (%s) -- local pads restored\n", why ? why : "requested");
}

// [netjump] Whether a successful connection should take both sides to character select.
// Owned by the overlay's Netplay tab (the env var stays as an override for headless runs), so
// the jump is a property of CONNECTING rather than of standing on the main menu.
void ps2NetSetAutoJump(bool on) { g_autoJump.store(on, std::memory_order_relaxed); }
void ps2NetSetDelay(int frames)  { if (frames >= 0 && frames <= 20) g.delay = (uint32_t)frames; }
void ps2NetSetBattleType(int t)  { if (t >= 0 && t <= 2) g.battleType.store(t, std::memory_order_relaxed); }
int  ps2NetBattleType()          { return g.battleType.load(std::memory_order_relaxed); }
void ps2NetSetDpLimit(int t)     { if (t >= 0 && t <= 2) g.dpLimit.store(t, std::memory_order_relaxed); }
int  ps2NetDpLimit()             { return g.dpLimit.load(std::memory_order_relaxed); }
// Battle Settings time limit: 0 = 60 s, 1 = 90 s, 2 = 180 s, 3 = 240 s (default), 4 = no limit
void ps2NetSetTimeLimit(int t)   { if (t >= 0 && t <= 4) g.timeLimit.store(t, std::memory_order_relaxed); }
int  ps2NetTimeLimit()           { return g.timeLimit.load(std::memory_order_relaxed); }
bool ps2NetAutoJump()           { return g_autoJump.load(std::memory_order_relaxed); }

// ---- auto-start -------------------------------------------------------------------------
// When the peer connects, the HOST replays a canned menu sequence as its own player-1 input.
// That crosses the wire through the ordinary path, so the peer follows automatically -- no new
// synchronisation is needed, because lockstep already guarantees both sides see the same inputs
// on the same frames. Record the sequence once with PS2X_INREC, walking title -> Duel ->
// 1P VS 2P -> character select, and point PS2X_NET_AUTOSTART at the file.
// OPEN-LOOP by nature: if one side hits an extra loading frame the presses land on the wrong
// screen, the same failure the rig's drive_duel.sh avoids by verifying each screen. Treat it as
// a convenience, not a guarantee -- and it only runs on the host, so the peer cannot fight it.
namespace {
#pragma pack(push, 1)
struct AutoSample { uint32_t frame; uint8_t player, pad0; uint16_t buttons; uint8_t rx, ry, lx, ly; };
#pragma pack(pop)
std::vector<Ps2xNetInput> g_auto;
size_t g_autoPos = 0;
bool   g_autoRunning = false;
}

void ps2NetBeginAutoStart(const char *path)
{
    if (!path || !path[0] || g_autoRunning) return;
    std::FILE *f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "[netplay] auto-start: cannot read %s\n", path); return; }
    AutoSample e{};
    g_auto.clear();
    while (std::fread(&e, sizeof e, 1, f) == 1)
        if (e.player == 0u) g_auto.push_back(Ps2xNetInput{e.buttons, e.rx, e.ry, e.lx, e.ly});
    std::fclose(f);
    if (g_auto.empty()) { std::fprintf(stderr, "[netplay] auto-start: %s has no player-1 samples\n", path); return; }
    g_autoPos = 0; g_autoRunning = true;
    std::fprintf(stderr, "[netplay] auto-start: replaying %zu menu inputs from %s\n", g_auto.size(), path);
}

bool ps2NetAutoInput(Ps2xNetInput &out)
{
    if (!g_autoRunning || !g.synced) return false;   // [statesync] the canned sequence starts once the state is shared
    if (g_autoPos >= g_auto.size())
    { g_autoRunning = false; std::fprintf(stderr, "[netplay] auto-start: sequence finished\n"); return false; }
    out = g_auto[g_autoPos++];
    return true;
}
bool ps2NetAutoStartActive() { return g_autoRunning; }

void ps2NetInit()
{
    static bool done = false;
    if (done) return;
    done = true;
    const char *conn = std::getenv("PS2X_NET");
    const char *lis  = std::getenv("PS2X_NET_LISTEN");
    if ((!conn || !conn[0]) && (!lis || !lis[0])) return;
    int player = 1;
    if (const char *p = std::getenv("PS2X_NET_PLAYER")) player = (std::atoi(p) == 2) ? 2 : 1;
    if (lis && lis[0]) ps2NetHost(std::atoi(lis), player);
    else               ps2NetJoin(conn, player);
}

static uint32_t relFrame(uint32_t frame)
{
    if (g.needBase) { g.base = frame; g.needBase = false;
                      std::fprintf(stderr, "[netplay] frame base = %u (inputs are relative from here)\n", frame); }
    return frame - g.base;
}

static void sendHello()
{   // count = 0: no inputs, just "I am here". The connector must send before anyone is connected
    // -- the listener learns the peer address from this. Gating sends on g.connected (which is
    // only set BY a receive) deadlocked both sides at tx 0, waiting for each other forever.
    if (!g.peerKnown) return;
    NetPkt p{};
    p.magic = kMagic; p.version = kVersion;
    p.player = static_cast<uint8_t>(g.localPlayer);
    p.battleType = static_cast<uint8_t>(g.battleType.load(std::memory_order_relaxed));
    p.timeLimit  = wireTimeLimit();
    p.dpLimit    = static_cast<uint8_t>(g.dpLimit.load(std::memory_order_relaxed));
    p.count = 0u; p.baseFrame = 0u;
    const size_t bytes = sizeof(NetPkt) - sizeof(Ps2xNetInput) * kMaxInputs;
    ::sendto(g.sock, reinterpret_cast<const char *>(&p), static_cast<int>(bytes), 0,
             reinterpret_cast<sockaddr *>(&g.peer), sizeof g.peer);
    g.tx.fetch_add(1, std::memory_order_relaxed);
}

uint32_t ps2NetRollbackWindow() { return g.rbWindow; }

uint32_t ps2NetRollbackPoll(uint32_t frameAbs, bool *mustStall)
{
    *mustStall = false;
    if (!g.active || !g.connected || !g.rbWindow || !g.synced) return 0u;   // [statesync] no frame numbering before the state is shared
    pump();
    const uint32_t cur = relFrame(frameAbs);
    uint32_t rb = 0u;
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        // The prediction depth is exhausted when a frame we RAN ON A GUESS is W or more frames behind
        // and its real input has still not arrived: stall until it does. (Frames nobody ever guessed
        // about -- before the delay, or gaps behind us -- do not count.)
        for (const auto &kv : g.predicted)
            if (kv.first + g.rbWindow <= cur) { *mustStall = true; break; }
        if (g.rollbackTo != 0xFFFFFFFFu)
        {
            if (g.rollbackTo + g.rbWindow < cur)
                std::fprintf(stderr, "[netplay] rollback to frame %u wanted at %u: beyond the %u-frame window (DESYNC likely)\n", g.rollbackTo, cur, g.rbWindow);
            else if (g.rollbackTo < cur) { rb = g.rollbackTo + g.base; g.rollbacks.fetch_add(1, std::memory_order_relaxed); }
            g.rollbackTo = 0xFFFFFFFFu;
        }
        // forget predictions and inputs far behind
        for (auto it = g.predicted.begin(); it != g.predicted.end();) it = (it->first + 600 < cur) ? g.predicted.erase(it) : ++it;
    }
    return rb;
}

void ps2NetSubmitLocal(uint32_t frameAbs, const Ps2xNetInput &in)
{
    if (!g.active) return;
    if (!g.connected || !g.synced) { sendHello(); pump(); return; }   // handshake / state sync only, no frame numbering yet
    const uint32_t frame = relFrame(frameAbs);
    Ps2xNetInput use = in;
    if (g.testInput && ((frame / 15u) & 1u)) use.buttons = static_cast<uint16_t>(use.buttons & ~0x0004u);   // R3 pressed (active low)
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        // First sample wins: a re-simulated frame calls this again with whatever the pad reads NOW, but the
        // value for frame + delay may already be on the wire -- rewriting it would desync the peer.
        g.local.emplace(frame + g.delay, use);   // sampled now, APPLIED delay frames later
    }
    sendOurs(frame + g.delay);
    pump();
}

bool ps2NetGetInput(uint32_t frameAbs, int player, Ps2xNetInput &out)
{
    if (!g.active || !g.connected || !g.synced) return false;   // pre-connection / pre-sync: leave the pads alone
    const uint32_t frame = relFrame(frameAbs);
    const bool wantLocal = (player == g.localPlayer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(g.timeoutMs);
    const auto t0 = std::chrono::steady_clock::now();
    bool stalled = false;
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            auto &m = wantLocal ? g.local : g.remote;
            const auto it = m.find(frame);
            if (it != m.end()) { out = it->second; break; }
            // the first `delay` frames have no sampled input yet: neutral, by definition
            if (frame < g.delay) { out = kNeutral; break; }
        }
        if (wantLocal) { out = kNeutral; break; }        // our own gap: never stall on ourselves
        if (g.rbWindow)
        {   // [rollback] predict: repeat the newest remote input we know; the boundary controller
            // stalls only when a missing input is older than the window, and rolls back when the
            // real input turns out different.
            std::lock_guard<std::mutex> lk(g.mtx);
            Ps2xNetInput guess = kNeutral;
            for (uint32_t back = 1; back <= 64u && back <= frame; ++back)
            {
                const auto it = g.remote.find(frame - back);
                if (it != g.remote.end()) { guess = it->second; break; }
            }
            g.predicted[frame] = guess;
            g.predictions.fetch_add(1, std::memory_order_relaxed);
            out = guess;
            break;
        }
        pump();
        if (std::chrono::steady_clock::now() > deadline)
        {
            std::fprintf(stderr, "[netplay] TIMEOUT waiting for peer input at frame %u -- using neutral (THIS WILL DESYNC)\n", frame);
            out = kNeutral;
            break;
        }
        stalled = true;
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    if (stalled)
    {
        g.stalls.fetch_add(1, std::memory_order_relaxed);
        g.stallNs.fetch_add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - t0).count(), std::memory_order_relaxed);
    }
    return true;
}

void ps2NetSetChecksum(uint32_t frame, uint64_t hash)
{
    if (!g.active) return;
    std::lock_guard<std::mutex> lk(g.mtx);
    g.checkFrame = frame; g.checkValue = hash; g.ourHash[frame] = hash;
    const auto it = g.peerHash.find(frame);
    if (it != g.peerHash.end() && it->second != hash)
    {
        if (g.desyncFrame == 0u) g.desyncFrame = frame;
        if (g.desyncs.fetch_add(1, std::memory_order_relaxed) == 0)
            std::fprintf(stderr, "[netplay] *** DESYNC at frame %u: ours %016llx peer %016llx ***\n",
                         frame, (unsigned long long)hash, (unsigned long long)it->second);
    }
    // keep the tables small
    if (g.ourHash.size() > 600)
    {
        for (auto i = g.ourHash.begin(); i != g.ourHash.end();) i = (i->first + 600 < frame) ? g.ourHash.erase(i) : ++i;
        for (auto i = g.peerHash.begin(); i != g.peerHash.end();) i = (i->first + 600 < frame) ? g.peerHash.erase(i) : ++i;
        std::lock_guard<std::mutex> lk2(g.mtx);
    }
}

void ps2NetFrame(uint32_t frame)
{
    if (!g.active) return;
    pump();
    if (g.connected)
    {   // Watchdog: a peer that vanishes (crash, closed window, cable out) would otherwise make
        // every frame burn the full stall timeout forever. Drop the session instead.
        static const int s_deadMs = [](){ const char *v = std::getenv("PS2X_NET_DEADMS");
                                          const int n = (v && v[0]) ? std::atoi(v) : 5000; return n > 0 ? n : 5000; }();
        if (g.lastRx.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g.lastRx).count() > s_deadMs)
        { ps2NetDisconnect("peer timed out"); return; }
    }
    static auto tStat = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - tStat).count() >= 5)
    {
        tStat = now;
        const uint64_t st = g.stalls.exchange(0), ns = g.stallNs.exchange(0);
        std::fprintf(stderr, "[netplay] frame %u | tx %llu rx %llu | stalls %llu (%.1f ms total) | desyncs %llu | peer %s | predictions %llu mispredicts %llu rollbacks %llu\n",
                     frame, (unsigned long long)g.tx.load(), (unsigned long long)g.rx.load(),
                     (unsigned long long)st, ns / 1e6, (unsigned long long)g.desyncs.load(),
                     g.connected ? "connected" : "WAITING",
                     (unsigned long long)g.predictions.load(), (unsigned long long)g.mispredicts.load(), (unsigned long long)g.rollbacks.load());
    }
}

// ---- [statesync] ------------------------------------------------------------------------
// The blob (40 MB) goes over a TCP connection on the host's port number (UDP carries the inputs, TCP
// the one-time state): the host listens while it waits for the acknowledgement, the joiner connects
// when it sees the OFFER, reads [u64 size][bytes], and adopts. Blocking sockets with timeouts; both
// sides are parked in the sync anyway.
static void sockTimeouts(SOCKET s, int ms)
{
#if defined(_WIN32)
    DWORD t = (DWORD)ms; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
#else
    timeval tv{}; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}
static bool sendAll(SOCKET s, const uint8_t *p, size_t n)
{
    while (n)
    {
        const int k = (int)::send(s, reinterpret_cast<const char *>(p), (int)(n > (1u << 20) ? (1u << 20) : n), 0);
        if (k <= 0) return false;
        p += k; n -= (size_t)k;
    }
    return true;
}
static bool recvAll(SOCKET s, uint8_t *p, size_t n)
{
    while (n)
    {
        const int k = (int)::recv(s, reinterpret_cast<char *>(p), (int)(n > (1u << 20) ? (1u << 20) : n), 0);
        if (k <= 0) return false;
        p += k; n -= (size_t)k;
    }
    return true;
}
bool ps2NetSyncPending() { return g.active && g.connected && g.syncOn && !g.synced; }
bool ps2NetSyncOn()      { return g.active && g.syncOn; }
bool ps2NetSyncIsHost()  { return g.listening; }
void ps2NetSyncOffer(uint32_t frameAbs, std::vector<uint8_t> &&blob)
{
    g.syncFrame = frameAbs; g.syncBytes = blob.size(); g.syncDone = false; g.syncPath[0] = 0;
    g.syncBlob = std::move(blob);
    if (g.syncListen == INVALID_SOCKET)
    {
        g.syncListen = ::socket(AF_INET, SOCK_STREAM, 0);
        if (g.syncListen != INVALID_SOCKET)
        {
            int one = 1; setsockopt(g.syncListen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);
            sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons(static_cast<uint16_t>(g.port));
            if (::bind(g.syncListen, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(g.syncListen, 1) != 0)
            { std::fprintf(stderr, "[statesync] host: TCP listen on %d failed\n", g.port); PS2X_CLOSESOCK(g.syncListen); g.syncListen = INVALID_SOCKET; }
            else setNonBlocking(g.syncListen);
        }
    }
    sendSyncCtl(1u, frameAbs, g.syncBytes, nullptr);
}
bool ps2NetSyncWaitDone(uint32_t timeoutMs)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto lastOffer = t0;
    for (;;)
    {
        pump();
        if (!g.active || !g.connected) return false;
        if (g.syncDone) break;
        if (g.syncListen != INVALID_SOCKET)
        {   // a joiner asking for the blob: serve it (blocking; the joiner is parked in the sync too)
            sockaddr_in from{}; socklen_t fl = sizeof from;
            const SOCKET c = ::accept(g.syncListen, reinterpret_cast<sockaddr *>(&from), &fl);
            if (c != INVALID_SOCKET)
            {
#if !defined(_WIN32)
                const int fl2 = fcntl(c, F_GETFL, 0); fcntl(c, F_SETFL, fl2 & ~O_NONBLOCK);
#endif
                sockTimeouts(c, 30000);
                const uint64_t sz = g.syncBlob.size();
                const auto ts = std::chrono::steady_clock::now();
                const bool ok = sendAll(c, reinterpret_cast<const uint8_t *>(&sz), sizeof sz) && sendAll(c, g.syncBlob.data(), g.syncBlob.size());
                PS2X_CLOSESOCK(c);
                std::fprintf(stderr, "[statesync] host: sent %llu bytes over TCP in %.2f s (%s)\n", (unsigned long long)sz,
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count(), ok ? "ok" : "FAILED");
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count() > (long)timeoutMs) return false;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastOffer).count() >= 200)   // UDP: repeat the offer
        { lastOffer = now; sendSyncCtl(1u, g.syncFrame, g.syncBytes, nullptr); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (g.syncListen != INVALID_SOCKET) { PS2X_CLOSESOCK(g.syncListen); g.syncListen = INVALID_SOCKET; }
    g.syncBlob.clear(); g.syncBlob.shrink_to_fit();
    syncResetTables(g.syncFrame);
    std::fprintf(stderr, "[netplay] frame base = %u (state sync)\n", g.syncFrame);
    return true;
}
bool ps2NetSyncOffered(uint32_t *frameAbs, uint64_t *bytes)
{
    pump();
    if (!g.syncOffered) return false;
    *frameAbs = g.syncFrame; *bytes = g.syncBytes;
    return true;
}
bool ps2NetSyncFetch(std::vector<uint8_t> &out)
{
    out.clear();
    const SOCKET c = ::socket(AF_INET, SOCK_STREAM, 0);
    if (c == INVALID_SOCKET) return false;
    sockTimeouts(c, 30000);
    sockaddr_in a = g.peer;   // the host's address; its TCP listener uses its UDP port number
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = ::connect(c, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0;
    uint64_t sz = 0;
    if (ok) ok = recvAll(c, reinterpret_cast<uint8_t *>(&sz), sizeof sz) && sz > 0 && sz <= (256ull << 20);
    if (ok) { out.resize((size_t)sz); ok = recvAll(c, out.data(), out.size()); }
    PS2X_CLOSESOCK(c);
    if (!ok) out.clear();
    std::fprintf(stderr, "[statesync] joiner: TCP fetch %s (%llu bytes, %.2f s)\n", ok ? "ok" : "FAILED", (unsigned long long)sz,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return ok;
}
void ps2NetSyncApplied(uint32_t frameAbs)
{
    syncResetTables(frameAbs);
    sendSyncDone(frameAbs);
    std::fprintf(stderr, "[netplay] frame base = %u (state sync)\n", frameAbs);
}
uint32_t ps2NetCheckEvery() { return g.checkEvery; }
uint32_t ps2NetDesyncFrame() { return g.desyncFrame; }
