// [netplay] see src/lib/ps2_netplay.cpp
#pragma once
#include <cstdint>
#include <vector>

#pragma pack(push, 1)
struct Ps2xNetInput { uint16_t buttons; uint8_t rx, ry, lx, ly; };   // 6 bytes on the wire
#pragma pack(pop)

void     ps2NetInit();

// The Netplay overlay, as one switch: the corner label, the panel behind it, AND the automatic
// character-select transition with its curtain. They are one feature, not three -- the panel without
// the transition is a settings page, and the transition without the curtain is the game being driven
// while the player watches nothing happen.
//
// NET_OVERLAY is the default, like every other PS2X_* here, and the checkbox in the launcher's Misc
// page and in the overlay's Netplay tab is the runtime override; both persist to settings.toml. A
// plain getenv read would not do, because the two UIs have to be able to flip it while the game is
// running and an env var is fixed at exec.
bool     ps2xNetOverlayEnabled();
void     ps2xSetNetOverlayEnabled(bool on);
bool     ps2NetHost(int port, int player);            // start hosting at runtime (overlay)
bool     ps2NetJoin(const char *hostPort, int player); // "1.2.3.4:7777"
bool     ps2NetPeerConnected();                        // a peer's packets have arrived
void     ps2NetSetAutoJump(bool on);                   // overlay: jump to char select on connect
bool     ps2NetAutoJump();
void     ps2NetSetDelay(int frames);                   // 1 loopback, 2 LAN; 1 frame = 33 ms
void     ps2NetDisconnect(const char *why);            // tear down, restore local pads
uint32_t ps2NetSession();                              // bumped per connect
void     ps2NetSetBattleType(int t);                   // 0 Single, 1 Team, 2 DP (host decides)
int      ps2NetBattleType();
void     ps2NetSetDpLimit(int t);                      // DP Battle budget: 0 = 10, 1 = 15, 2 = 20
int      ps2NetDpLimit();
void     ps2NetSetTimeLimit(int t);                    // Battle Settings index, 3 = default
int      ps2NetTimeLimit();
void     ps2NetBeginAutoStart(const char *path);       // host: replay a canned menu sequence
bool     ps2NetAutoInput(Ps2xNetInput &out);           // next canned input, if any
bool     ps2NetAutoStartActive();
bool     ps2NetActive();
int      ps2NetLocalPlayer();                 // 1 or 2
uint32_t ps2NetDelay();
void     ps2NetSubmitLocal(uint32_t frame, const Ps2xNetInput &in);
bool     ps2NetGetInput(uint32_t frame, int player, Ps2xNetInput &out);
void     ps2NetSetChecksum(uint32_t frame, uint64_t hash);
void     ps2NetFrame(uint32_t frame);
// [rollback] Rollback netplay (PS2X_NET_ROLLBACK=<window frames>; needs PS2X_FIBERS + frame stepping).
// A missing remote input is PREDICTED (last known repeated) instead of stalling; when the real one
// arrives and differs, the frame-boundary controller restores that frame's snapshot and re-simulates.
uint32_t ps2NetRollbackWindow();                       // 0 = lockstep as before
// Called by the controller at every frame boundary (frameAbs = the frame about to run). Pumps the
// network. Returns the absolute frame to roll back to (0 = none), and sets *mustStall when a remote
// input older than the window is still missing (the controller then waits as lockstep did).
uint32_t ps2NetRollbackPoll(uint32_t frameAbs, bool *mustStall);
// [statesync] State sync at connect (on by default with a rollback window; PS2X_NET_SYNC=0 disables). The
// host publishes its frame-boundary state over a TCP connection on its port number, the joiner adopts it
// at a structurally comparable boundary and acknowledges; inputs are exchanged from that frame on.
void     ps2NetSetRollback(int frames);                         // overlay: 0 = lockstep (env PS2X_NET_ROLLBACK is the default)
int      ps2NetRollbackSetting();
void     ps2NetSetSync(bool on);
bool     ps2NetSyncSetting();
bool     ps2NetSyncPending();                                   // connected, sync on, state not yet adopted
bool     ps2NetSyncOn();                                        // sync enabled for this session (netjump must stay out: it writes state on one side)
bool     ps2NetSyncIsHost();
void     ps2NetSyncOffer(uint32_t frameAbs, std::vector<uint8_t> &&blob);   // host: publish (TCP) + announce
bool     ps2NetSyncWaitDone(uint32_t timeoutMs);                 // host: serve the blob, pump until the joiner's DONE; sets the frame base
bool     ps2NetSyncOffered(uint32_t *frameAbs, uint64_t *bytes); // joiner: an offer arrived
bool     ps2NetSyncFetch(std::vector<uint8_t> &out);            // joiner: pull the blob over TCP (blocking, 30 s timeout)
void     ps2NetSyncApplied(uint32_t frameAbs);                   // joiner: state adopted; sets the base, sends DONE
uint32_t ps2NetCheckEvery();      // PS2X_NET_CHECKEVERY: confirmed-state checksum interval (default 60)
uint32_t ps2NetDesyncFrame();     // [desyncdump] the first frame whose confirmed hashes differed, 0 = none
