// [flush] The [flush] latch in ps2_netplay.cpp, and what the teardown leaves behind.
//
// The bug this covers: a session that ended without a teardown went on patching the game, because
// the pad override in game_overrides.cpp replaces player 2 with ps2NetGetInput() for as long as
// ps2NetActive() is true. Go back to the main menu after a match, start a 1P VS 2P, and the second
// player's pad is still the last match's remote input.
//
// What is asserted here, and what is not:
//
//   Asserted: the whole latch. When it arms, that it does NOT arm on the main menu (a naive
//   "state == 0x04" check refuses the connect it exists to serve), that the fight and post-fight
//   keep it armed (arming on the setup screens alone never fires for a match that ran to the end),
//   that a passing-through state neither flushes nor resets the debounce, the debounce itself, and
//   that the latch is forgotten on teardown so a second session cannot inherit it. Plus the residue
//   the teardown leaves, which is the "holds the patches" half of the report.
//
//   NOT asserted: that the pads come back. That is game_overrides.cpp, which needs the recompiled
//   guest; what the probe asserts is its predicate, ps2NetActive() == false, which is the whole of
//   what the override tests. The visible half needs two machines and is worth watching once by hand.
//
// The state is driven through g_bt3StateLive, which is the real atomic the real check reads and the
// run loop keeps fresh, so this exercises the shipped path rather than a copy of it.

#include "runtime/ps2_netplay.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// The one definition the real module leaves to its host. The run loop keeps this fresh every tick;
// the probe owns it, and that is the whole of the seam.
std::atomic<uint32_t> g_bt3StateLive{0xffffffffu};

namespace
{

int g_fail = 0;
int g_checks = 0;

void check(bool ok, const char *what)
{
    ++g_checks;
    if (!ok)
    {
        ++g_fail;
        std::printf("  FAIL  %s\n", what);
    }
}

// The debounce count is an argument rather than a constant in the message, so a run with
// PS2X_NET_FLUSHFRAMES set does not print a number that is not the one it tested.
void checkf(bool ok, const char *fmt, ...)
{
    ++g_checks;
    if (ok)
        return;
    ++g_fail;
    char what[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);
    std::printf("  FAIL  %s\n", what);
}

void setState(uint32_t st) { g_bt3StateLive.store(st, std::memory_order_relaxed); }

// Frames, at the shape the real caller uses: one per guest frame, numbered from a running counter.
uint32_t g_frame = 1000;
void frames(int n, uint32_t state)
{
    setState(state);
    for (int i = 0; i < n; ++i)
        ps2NetFrame(g_frame++);
}

// A port nothing else is likely to hold. netStart treats 0 as "not listening", so it cannot be
// asked for an ephemeral one, hence the sweep.
int g_port = 0;
bool host(int player = 1)
{
    for (int port = 47800; port < 47820; ++port)
        if (ps2NetHost(port, player)) { g_port = port; return true; }
    return false;
}

// The one packet a peer has to send for the module to consider itself connected, restated here
// byte for byte: magic 'BT3N', version 1, from player 2, carrying nothing. 28 bytes, which is the
// minimum pump() accepts (the full struct less the inputs array).
//
// This is a duplicate of the wire format and it is here on purpose: the residue assertions below are
// only worth anything if the containers are non-empty first, and every one of them needs a connected
// session, and a connected session needs a packet. If the format ever moves, this stops working and
// the probe FAILS -- the predictions never happen and test 6 reports them left behind. A drift that
// breaks the test is acceptable; one that passes it quietly is not.
constexpr uint32_t kMagic   = 0x4e335442u;   // 'BT3N'
constexpr uint16_t kVersion = 1u;
bool shake()
{
    unsigned char pkt[28] = {};
    std::memcpy(pkt + 0, &kMagic, 4);
    std::memcpy(pkt + 4, &kVersion, 2);
    pkt[6] = 2;              // player 2, so we are treated as player 1
    pkt[7] = 0;              // count: no inputs
    pkt[15] = 0;             // kind: 0 = ordinary input packet
    const int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(g_port));
    ::inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    const int n = ::sendto(s, reinterpret_cast<const char *>(pkt), sizeof pkt, 0,
                           reinterpret_cast<sockaddr *>(&to), sizeof to);
    ::close(s);
    return n == static_cast<int>(sizeof pkt);
}

// The states, named. 0x27 and 0x28 carry two different readings in this tree -- the state table calls
// them FIGHT and PREFIGHT_SETUP, the netjump walk calls them character select for Single and for
// Team/DP. It does not matter here, which is the point of listing both: every reading puts them
// inside the match flow, and that is all the latch looks at.
constexpr uint32_t kMenu       = 0x04u;   // main menu: where a session is STARTED
constexpr uint32_t kLoading    = 0x06u;   // passed through on the way anywhere
constexpr uint32_t kDuelMenu   = 0x26u;   // versus mode select
constexpr uint32_t kCharSingle = 0x27u;   // character select, Single
constexpr uint32_t kCharTeam   = 0x28u;   // character select, Team / DP
constexpr uint32_t kPreFight   = 0x29u;
constexpr uint32_t kInFight    = 0x2Du;   // the fight itself
constexpr uint32_t kPostFight  = 0x38u;

}  // namespace

int main()
{
    const int menuFrames = [](){ const char *v = std::getenv("PS2X_NET_FLUSHFRAMES");
                                 const int n = (v && v[0]) ? std::atoi(v) : 15; return n > 0 ? n : 15; }();
    std::printf("ps2x_netplay_probe -- [flush], debounce %d frames\n", menuFrames);

    // ---- 1. A session on the main menu is not a session to be flushed ----------------------------
    // The first thing the fix could have broken. 0x04 is where the Netplay popup lives, so this is
    // where every session is opened; a check that read 0x04 as "leaving" would refuse the connect.
    check(host(), "host() opens a session");
    check(ps2NetActive(), "the session is active right after host()");
    frames(120, kMenu);
    check(ps2NetActive(), "120 frames on the main menu do NOT flush: this is where you connect from");

    // ---- 2. The match flow arms it ---------------------------------------------------------------
    frames(1, kDuelMenu);
    check(ps2NetResidue().armed, "entering the duel menu arms the latch");
    check(ps2NetActive(), "being armed is not being disconnected");
    frames(1, kCharSingle);
    check(ps2NetResidue().armed, "character select keeps it armed");
    frames(1, kCharTeam);
    frames(1, kPreFight);
    check(ps2NetResidue().armed, "the rest of pre-fight setup keeps it armed");

    // ---- 3. The fight must not flush -------------------------------------------------------------
    // 0x2D and 0x38 are outside {0x26,0x27,0x28}. Taken literally that set tears the session down
    // on the frame the fight starts, and never arms at all for a match that runs to the end -- which
    // is the case that was reported.
    frames(600, kInFight);
    check(ps2NetActive(), "600 frames of fight do NOT flush");
    check(ps2NetResidue().armed, "the fight keeps the latch armed");
    frames(60, kPostFight);
    check(ps2NetActive(), "post-fight does NOT flush");

    // ---- 4. A state on the way past is neither a flush nor a debounce reset ------------------------
    frames(menuFrames + 10, kLoading);
    check(ps2NetActive(), "loading does NOT flush");
    check(ps2NetResidue().armed, "loading keeps the latch armed");
    check(ps2NetResidue().menuFrames == 0, "loading does not advance the debounce");

    // ---- 5. The debounce ------------------------------------------------------------------------
    frames(menuFrames - 1, kMenu);
    checkf(ps2NetActive(), "%d frames on the menu is one short of the flush", menuFrames - 1);
    check(ps2NetResidue().armed, "still armed one frame short");
    frames(1, kMenu);
    checkf(!ps2NetActive(), "the %dth frame on the main menu flushes", menuFrames);

    // ---- 6. The residue --------------------------------------------------------------------------
    // The other half of the report, and the half that needs the containers to be NON-EMPTY first:
    // asserting that a map is empty after teardown is worth nothing if it was empty before. So the
    // rollback path is driven for real -- a handshake, then a remote input nobody ever sent, which
    // is exactly the situation that fills g.predicted in a live session with a silent peer.
    //
    // Every one of these is keyed by RELATIVE frame while g.base is re-derived per session, so a
    // leftover is not stale, it is a live wrong answer for the new session's frame N.
    ps2NetDisconnect("probe: before the residue section");
    check(host(), "host() for the residue section");
    ps2NetSetRollback(4);                    // a rollback window is what makes the module guess
    check(shake(), "the handshake packet was sent");
    frames(4, kMenu);                        // pump() picks it up and marks the session connected
    check(ps2NetPeerConnected(), "the handshake took: a session with no peer answers nobody");
    {
        Ps2xNetInput out{};
        for (int i = 0; i < 6; ++i)          // frames past g.delay, so the predict branch is reached
            ps2NetGetInput(g_frame + 40u + static_cast<uint32_t>(i), 2, out);
        check(ps2NetResidue().predicted > 0,
              "the rollback path really filled g.predicted -- the next assertions are not vacuous");
    }
    // ...and THEN tear it down, which is the order that matters. Asserting the residue after the
    // flush in step 5 would have passed on empty maps and proved nothing.
    frames(1, kDuelMenu);
    frames(menuFrames, kMenu);
    check(!ps2NetActive(), "the session with live predictions is flushed by the same rule");
    {
        const Ps2xNetResidue r = ps2NetResidue();
        char what[160];
        std::snprintf(what, sizeof what, "teardown cleared predicted (%u left)", r.predicted);
        check(r.predicted == 0, what);
        std::snprintf(what, sizeof what, "teardown cleared the held-packet queue (%u left)", r.held);
        check(r.held == 0, what);
        std::snprintf(what, sizeof what, "teardown cleared local inputs (%u left)", r.local);
        check(r.local == 0, what);
        std::snprintf(what, sizeof what, "teardown cleared remote inputs (%u left)", r.remote);
        check(r.remote == 0, what);
        std::snprintf(what, sizeof what, "teardown cleared peer hashes (%u left)", r.peerHash);
        check(r.peerHash == 0, what);
        std::snprintf(what, sizeof what, "teardown cleared our hashes (%u left)", r.ourHash);
        check(r.ourHash == 0, what);
        std::snprintf(what, sizeof what, "teardown reset rollbackTo (0x%08x left)", r.rollbackTo);
        check(r.rollbackTo == 0xFFFFFFFFu, what);
        std::snprintf(what, sizeof what, "teardown reset lastSent (%u left)", r.lastSent);
        check(r.lastSent == 0, what);
        std::snprintf(what, sizeof what, "teardown reset desyncFrame (%u left)", r.desyncFrame);
        check(r.desyncFrame == 0, what);
        check(!r.autoRunning, "teardown stopped the auto-start cursor");
        check(!r.armed, "teardown forgot the latch, so the next session cannot inherit it");
        check(r.menuFrames == 0, "teardown cleared the debounce counter");
    }
    // g.held is asserted above but never populated: it is filled in pump() from a packet that the
    // fake-lag path holds back, which needs a peer that sends a real NetPkt -- the handshake above
    // sends the 28-byte minimum, which pump() accepts and applies rather than holds, because held is
    // only consulted when PS2X_NET_FAKELAG is set. So that one line of the teardown is covered by
    // inspection and by the fact that it sits in the same block as the ones that are covered. It is
    // called out here rather than left to look tested.

    // ---- 7. Frames on the menu do nothing once the session is gone ---------------------------------
    frames(300, kMenu);
    check(!ps2NetActive(), "still down: the flush is not a one-frame thing that re-arms");
    check(!ps2NetResidue().armed, "and it did not re-arm itself on the way");

    // ---- 8. A second session on the main menu is a fresh one ---------------------------------------
    check(host(), "a second host() opens a second session");
    frames(120, kMenu);
    check(ps2NetActive(), "the new session survives the main menu, un-armed");
    // And it arms on its own, so the fix did not turn into "never flush again".
    frames(1, kDuelMenu);
    check(ps2NetResidue().armed, "the new session arms on its own");
    frames(menuFrames, kMenu);
    checkf(!ps2NetActive(), "and the new session flushes on its own, at %d frames", menuFrames);

    // ---- 9. A state nobody has seen is not a reason to disconnect ---------------------------------
    // Anything outside the flow and outside the menu must be inert. 0x3E is Options, which the main
    // menu opens and closes, so a rule that flushed on "not in the flow" would drop a live session
    // every time the player looked at the settings.
    check(host(), "a third host() for the untouched states");
    frames(1, kDuelMenu);
    check(ps2NetResidue().armed, "armed before the sweep");
    for (const uint32_t st : {0x01u, 0x05u, 0x3Eu, 0x2Cu, 0x37u, 0x3Fu, 0xFFu, 0x0000u})
    {
        frames(120, st);
        char what[96];
        std::snprintf(what, sizeof what, "state 0x%02x neither flushes nor disarms", st);
        check(ps2NetActive() && ps2NetResidue().armed, what);
    }

    // ---- 10. Each match state arms the latch ON ITS OWN --------------------------------------------
    // The case that decides what belongs in the arm set, and the one an earlier version of this
    // probe missed. Leaving 0x2D does not flush by itself -- only 0x04 does -- so narrowing the set
    // to the setup screens {0x26,0x27,0x28} still passes every test above: the latch stays armed
    // from 0x26 and the flush on the way back to 0x04 works perfectly. What it breaks is any route
    // into a match that does not pass through the setup screens, where the latch is never armed and
    // the session survives the whole match.
    //
    // Reachable, not hypothetical: a state sync adopts the peer's state at a frame boundary, which
    // can be a fight frame; PS2X_MENU_JUMP lands on any state by number; a rollback re-simulates
    // through states the latch has not seen yet.
    for (const uint32_t st : {kDuelMenu, kCharSingle, kCharTeam, kPreFight, kInFight, kPostFight})
    {
        // Torn down first, so a failure in one iteration does not cascade into the next as a
        // "host() failed" -- which is what an un-torn-down session looks like, and it buries the
        // one line that says why.
        ps2NetDisconnect("probe: between sole-arming cases");
        check(host(), "host() for the sole-arming sweep");
        setState(kMenu);
        ps2NetFrame(g_frame++);
        check(!ps2NetResidue().armed, "a fresh session starts un-armed");
        frames(1, st);
        checkf(ps2NetResidue().armed, "state 0x%02x arms the latch on its own", st);
        frames(menuFrames, kMenu);
        checkf(!ps2NetActive(), "and 0x%02x alone is enough for the return to flush", st);
    }

    std::printf("%s (%d failures, %d checks)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_checks);
    return g_fail ? 1 : 0;
}
