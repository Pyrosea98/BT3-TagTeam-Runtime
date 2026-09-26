#pragma once

// [mainmenu] The main menu's lifecycle: when it STARTS, when it is actually SHOWN, and when it is
// being torn down. This is the gate an in-menu popup sits on.
//
// "The state is 0x04" is NOT the same thing as "the menu is on screen", and the difference is the
// whole point:
//
//   * 0x04 flips BEFORE the menu object exists. Measured (game_overrides.cpp, bt3MenuJumpDiag):
//     forcing 0x01 -> 0x04 flips the state and menuObj stays 0, and nothing is ever built. So
//     state == 0x04 alone is "started", not "shown".
//   * menuObj = *(0x3B0E80) exists ONLY while 0x04 is up; the game frees it on exit.
//   * menuObj+0x144 is the game's OWN plate-build counter, and it is what the gate is made of. It
//     climbs 0 -> 9 as the entries are registered and drops back when the menu is torn down, so
//     the popup's lifetime is the plates' lifetime by construction. Nothing has to be kept in
//     step with it, because the game increments it itself.
//
// NOT the gate, and this is worth stating because docs/MAIN-MENU.md section 7 says it is:
//
//   menuState = *(*(*(0x3B38D8)+0x9A4)+0x40), documented as "9 = DISPLAYED, 10 = TRANSITIONING".
//   It does not resolve in this build. *(0x3B38D8) reads 0, and a live run says so out loud:
//
//     [bt3menu] fr=1983 phase=SHOWN state=0x04 menuObj=0xa3db40 plates=9/9 menuState=(no chain)
//     [hstate]  phase=MENU sub=mainStruct=0 (menu not initialised yet) raw=0x4
//
//   A gate built on it never leaves BUILDING, so the popup never draws. It is read and reported as
//   telemetry only. Same for the row count at menuObj+0x144 as "11 rows": the table holds 9, see
//   kRowCount.
//
// Everything here is a READ. Nothing in this module writes guest RAM, so it cannot perturb what it
// measures, and it is safe to call from the GS submit path.
//
// itemBase = *(0x3B38E8) is telemetry only. The same slot is the duel object in 0x26 and the
// menu's item base in 0x04, and its +0x13C itemState is documented as a 0..8 ladder
// (PLATE_LOAD .. FINAL_CONFIRM) whose per-frame meaning is not established. Gating on an unverified
// field is how a popup ends up drawing during the fade-out.

#include <cstddef>
#include <cstdint>

namespace ps2x::mainmenu
{
    // ---- addresses (guest RAM) ------------------------------------------------------
    // The three slots below are the same values ps2x_duel_fields.h declares; the menu is what
    // puts something in them first, so they are asserted equal there rather than duplicated.
    constexpr uint32_t kStatePtrSlot = 0x2FF10Cu;  // -> stateObj, always live
    constexpr uint32_t kMenuObjSlot  = 0x3B0E80u;  // -> menuObj, live only while 0x04 is up
    constexpr uint32_t kMainSlot     = 0x3B38D8u;  // -> mainStruct
    constexpr uint32_t kItemSlot     = 0x3B38E8u;  // -> itemBase in 0x04 (duelObj in 0x26)
    constexpr uint32_t kIntroSlot    = 0x3B0E88u;  // sibling of menuObj; read defensively

    constexpr uint32_t kOffState       = 0x018u;  // stateObj+0x18  : the committed screen state
    constexpr uint32_t kOffGlobalFlags = 0x3BCu;  // mainStruct+0x3BC: bit 3 = a submenu is up
    constexpr uint32_t kOffSubStruct   = 0x9A4u;  // mainStruct+0x9A4: -> subStruct
    constexpr uint32_t kOffMenuState   = 0x040u;  // subStruct+0x40 : 9 DISPLAYED, 10 TRANSITIONING
    constexpr uint32_t kOffItemState   = 0x13Cu;  // itemBase+0x13C : 0..8 ladder (telemetry)
    constexpr uint32_t kOffItemCursor  = 0x12Cu;  // itemBase+0x12C : cursor
    constexpr uint32_t kOffItemSel     = 0x138u;  // itemBase+0x138 : selection
    constexpr uint32_t kOffRowBase     = 0x10Cu;  // menuObj+0x10C : first row index
    constexpr uint32_t kOffRowCount    = 0x144u;  // menuObj+0x144 : row count (divisor)
    constexpr uint32_t kOffRowCursor   = 0x148u;  // menuObj+0x148 : cursor offset
    constexpr uint32_t kOffRowTable    = 0x118u;  // menuObj+0x118 : row -> jump-table index
    constexpr uint32_t kOffIntroT      = 0x0C4u;  // *(introPtr)+0xC4 : boot intro timer

    constexpr uint32_t kStateMainMenu = 0x04u;
    constexpr uint32_t kMenuStateDisplayed     = 9u;
    constexpr uint32_t kMenuStateTransitioning = 10u;

    // How many rows the menu's table ends up holding, which is the value menuObj+0x144 reaches
    // once the build loop is done. NOT 11, and the difference is the point.
    //
    // The loop runs $s1 from 0 to 10, but the word is only incremented on the path through
    // 0x3355A0, and two values never reach it:
    //
    //   0x335578  beql $s1, $t0, 0x3355C0   $t0 = 4: the hidden "Network Battle" entry is
    //                                          skipped, so the counter does not move for it
    //   0x335580  bnel $s1, $a3, 0x3355A0   $a3 = 10: index 10 falls through to a different
    //                                          path and does not increment either
    //
    // So $s1 in {0,1,2,3,5,6,7,8,9} increments it: nine rows, measured live (a real run goes
    // 0 -> 9 and stops). With PS2X_REVEAL_HIDDEN_MENU_ENTRY=1, $t0 becomes 0xFF and index 4
    // reaches the increment, giving ten -- which is why the gate tests ">= 9" and not "== 9":
    // 9 is the floor for a complete menu in either configuration.
    constexpr uint32_t kRowCount = 9u;

    // The main menu row order (retail), indexed by the game's own row formula. '?' marks the
    // names docs/MAIN-MENU.md still lists as hypotheses; row 3 (Duel) is the verified one.
    constexpr const char *kRowNames[11] = {
        "Dragon History?", "Ultimate Battle?", "Dragon World Tour?", "Duel (verified)",
        "Network Battle (hidden)", "Evolution Z?", "Ultimate Training?",
        "Data Center?", "Character Reference?", "Options?", "Extra 0x46?",
    };
    constexpr uint32_t kRowTargetState[11] = {
        0x06u, 0x0Du, 0x21u, 0x26u, 0xFFFFFFFFu, 0x30u, 0x2Cu, 0x35u, 0x3Cu, 0x3Eu, 0x46u,
    };

    // The lifecycle, anchored to the plates rather than to a state variable.
    enum class Phase : uint8_t
    {
        Absent = 0,   // the state is not 0x04: no menu, nothing to draw over
        Started,      // state == 0x04 but menuObj == 0: the screen was requested, not built yet
        Building,     // menuObj is alive and the plate counter is climbing (0 < plates < 11)
        Shown,        // every plate is built (plates == 11)             <-- draw here
        Leaving,      // the object is being torn down (plates dropping below 11 again)
    };

    // One coherent read of every signal, so a caller cannot mix values from two different frames.
    struct Snapshot
    {
        bool     valid      = false;   // rdram was readable and stateObj resolved
        uint32_t stateObj   = 0u;
        uint32_t state      = 0xFFFFFFFFu;   // stateObj+0x18
        uint32_t menuObj    = 0u;            // 0 until the menu object is built
        uint32_t mainStruct = 0u;
        uint32_t globalFlags = 0u;           // bit 3 = a submenu is up
        uint32_t subStruct  = 0u;
        uint32_t menuState  = 0xFFFFFFFFu;   // subStruct+0x40
        uint32_t itemBase   = 0u;
        uint32_t itemState  = 0xFFFFFFFFu;   // telemetry
        uint32_t itemCursor = 0xFFFFFFFFu;   // telemetry
        uint32_t itemSel    = 0xFFFFFFFFu;   // telemetry
        // menuObj+0x144: how many plates have been built so far, 0..11. THIS is the gate. It is
        // not a state variable that somebody has to keep in sync, it is the build loop's own
        // counter, so the popup's lifetime is the plates' lifetime by construction: no plates, no
        // popup, and nothing to get out of step with the game's own idea of "the menu is up".
        uint32_t plates = 0u;
        uint32_t introT     = 0u;            // boot intro timer, 0..0x708
        int32_t  row        = -1;            // the game's own formula, -1 when not resolvable
        int32_t  rowIndex   = -1;            // menuObj+0x118 -> jump-table index
        uint64_t frame      = 0u;            // caller-supplied, for transition logs
    };

    // Reads every field above from guest RAM. `rdram` is the 32 MB RDRAM view; `frame` is only
    // carried through for logging.
    //
    // Total by contract: it never writes, never throws and never reads out of bounds. The
    // bounds part is not theoretical. Masking a guest pointer with 0x1FFFFFFF clears bits 29-31,
    // but bit 28 SURVIVES, so a value like 0xF0200000 masks to 0x10000000 -- 256 MB, far past the
    // end of a 32 MB RDRAM. A partially built object is exactly when that happens, and this runs
    // per frame, so a masked pointer that lands outside RAM is treated as absent instead of being
    // dereferenced.
    Snapshot read(const uint8_t *rdram, uint32_t ramMask, uint64_t frame = 0u);

    Phase phase(const Snapshot &s);

    // The gate an in-menu popup uses: true only while the menu is on screen and has not been
    // confirmed. False during Absent, during Started/Building, and on the confirm frame.
    bool popupVisible(const Snapshot &s);

    const char *phaseName(Phase p);
    const char *itemStateName(uint32_t v);
    const char *stateName(uint32_t v);
    const char *rowName(int32_t row);

    // One line with every signal, for the transition log and for eyeballing what a frame did.
    // `buf` must hold at least 320 bytes.
    void format(const Snapshot &s, char *buf, std::size_t n);

}   // namespace ps2x::mainmenu
