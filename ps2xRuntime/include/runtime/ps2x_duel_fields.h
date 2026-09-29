#pragma once

// [duelfields] The complete Duel-setup field map + the write methods, as a single source of truth.
//
// Everything here was reverse-engineered and verified against a full capture (see
// dumps/duel_* and docs/MAIN-MENU.md):
//   * duelObj  = *(0x3B38E8)     the Duel module object, live only while 0x26 is up
//       +0x110 vs mode      0=1P vs CPU, 1=1P vs 2P, 2=CPU vs CPU, 3=Battle Settings
//       +0x114 battle type  0=Single, 1=Team, 2=DP
//       +0x118 DP variant   0=10, 1=15, 2=20
//       +0x13C time limit   (also acts as the Battle Settings row cursor, 0..6)
//   * stateObj = *(0x2FF10C)     the committed copies the later screens read
//       +0x620 vs mode copy  (written by the commit gate 0x356234 -> 0x356238)
//       +0x624 battle type   (read by character select at 0x352DA8)
//       +0x628 extra field   (the netjump tracks it too)
//       +0x630 DP copy       (written by the commit gate at 0x35625C)
//
// The menu tracks its cursor live and overwrites duelObj every frame, so a setup that must
// survive the confirm has to be written right up to the commit, and the committed copies
// re-asserted while character select / pre-fight are up. Both halves are provided here so the
// netplay backend wire can drive a match with one call.

#include <cstdint>

struct R5900Context;
class PS2Runtime;

namespace ps2x_duelfields
{
    // ---- addresses (guest RAM) ------------------------------------------------------
    constexpr uint32_t kDuelObjSlot = 0x3B38E8u;   // holds duelObj while 0x26 is up
    constexpr uint32_t kStatePtrSlot = 0x2FF10Cu;  // holds stateObj always
    constexpr uint32_t kMenuObjSlot = 0x3B0E80u;   // holds menuObj while 0x04 is up (row list)

    constexpr uint32_t kOffVs       = 0x110u;
    constexpr uint32_t kOffType     = 0x114u;
    constexpr uint32_t kOffDp       = 0x118u;
    constexpr uint32_t kOffTime     = 0x13Cu;
    constexpr uint32_t kOffCommitVs = 0x620u;
    constexpr uint32_t kOffCommitType = 0x624u;
    constexpr uint32_t kOffCommitX  = 0x628u;
    constexpr uint32_t kOffCommitDp = 0x630u;

    // ---- the value space ------------------------------------------------------------
    enum class VsMode : uint32_t
    {
        P1VsCpu = 0u, P1VsP2 = 1u, CpuVsCpu = 2u, BattleSettings = 3u,
    };
    enum class BattleType : uint32_t { Single = 0u, Team = 1u, DP = 2u };
    enum class DpVariant : uint32_t { DP10 = 0u, DP15 = 1u, DP20 = 2u };

    constexpr const char *kVsNames[4]     = { "1P_VS_CPU", "1P_VS_2P", "CPU_VS_CPU", "BATTLE_SETTINGS" };
    constexpr const char *kTypeNames[3]   = { "SINGLE", "TEAM", "DP" };
    constexpr const char *kDpNames[3]     = { "DP10", "DP15", "DP20" };

    // The five combat subtypes the Duel menu offers (mode-independent); dp is only meaningful
    // when type == DP, so Single/Team carry DP10 as a placeholder.
    struct Subtype { BattleType type; DpVariant dp; const char *name; };
    constexpr Subtype kSubtypes[5] = {
        { BattleType::Single, DpVariant::DP10, "SINGLE" },
        { BattleType::Team,   DpVariant::DP10, "TEAM"   },
        { BattleType::DP,     DpVariant::DP10, "DP10"   },
        { BattleType::DP,     DpVariant::DP15, "DP15"   },
        { BattleType::DP,     DpVariant::DP20, "DP20"   },
    };

    // Battle Settings rows (duelObj+0x13C is the row cursor while in Battle Settings). The value
    // field of each row is NOT mapped yet -- see the [duelsettings] plan in docs/MAIN-MENU.md.
    constexpr const char *kSettingRows[6] = { "DUEL_TIME", "COM_LEVEL", "REFEREE",
                                              "TRANSFORMATIONS", "MAP_SETTING", "DEFAULT" };
    constexpr uint32_t kDuelTimeOptions = 6u;   // 60 / 90 / 120 / 180 / 240 / Infinite

    // ---- the fields ----------------------------------------------------------------
    struct Fields
    {
        uint32_t vs        = (uint32_t)VsMode::P1VsP2;
        uint32_t battleType = (uint32_t)BattleType::Single;
        uint32_t dpVariant = (uint32_t)DpVariant::DP10;
        uint32_t timeLimit = 0xFFFFFFFFu;   // 0xFFFFFFFF = leave whatever the game set
    };

    // ---- read / write ---------------------------------------------------------------
    // Read the live values (duel object when it is up, plus the committed copies). Missing
    // objects are reported as 0xFFFFFFFF.
    Fields read(const uint8_t *rdram);

    // Write the duel object fields (the live the menu overwrites; write every frame until the
    // confirm). Returns false when duelObj is not up yet.
    bool writeDuelObject(uint8_t *rdram, const Fields &f);

    // Write the committed copies the later screens read (re-assert while 0x27/0x28 are up).
    bool writeCommitted(uint8_t *rdram, const Fields &f);

    // The complete method: both halves in one call, for the backend wire.
    bool apply(uint8_t *rdram, const Fields &f);

    // Convenience: one of the five subtypes on a chosen opponent mode (time kept as-is).
    Fields subtype(VsMode vs, BattleType type, DpVariant dp = DpVariant::DP10);

    // A readable one-line description, e.g. "1P_VS_2P DP15 dp=1 time=keep".
    void format(const Fields &f, char *out, unsigned n);
}
