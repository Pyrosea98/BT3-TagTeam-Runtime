// [mainmenu] Read side of the main-menu lifecycle. See ps2x_mainmenu.h for the field map and for
// why each signal is trusted (or only observed).
//
// Every read is bounds-checked against the RAM mask and every pointer is masked to 0x1FFFFFFF,
// which is what the rest of the runtime does: a guest pointer is an EE address with the top bits
// carrying sign/garbage, and an unmasked one is an out-of-bounds host read.

#include "runtime/ps2x_mainmenu.h"

#include <cstdio>
#include <cstring>

namespace ps2x::mainmenu
{
    namespace
    {
        // A guest pointer is masked with 0x1FFFFFFF (512 MB) everywhere in the runtime, but RDRAM
        // is only 32 MB, so PS2_RAM_MASK is 0x1FFFFFF. The two differ, and that gap matters:
        // a pointer whose masked value is above the RAM size is not out of bounds once the read
        // address is masked again -- it silently reads a DIFFERENT, valid address. On a
        // half-built object that is exactly the case, so the pointer is range-checked here and
        // anything past the end of RAM is reported as absent rather than dereferenced.
        constexpr uint32_t kPtrMask = 0x1FFFFFFFu;

        inline uint32_t rd32(const uint8_t *rd, uint32_t mask, uint32_t addr)
        {
            uint32_t v = 0u;
            std::memcpy(&v, rd + (addr & mask), sizeof v);
            return v;
        }

        // Returns the guest pointer in `slot`, or 0 when it is null or points past the end of RDRAM.
        inline uint32_t follow(const uint8_t *rd, uint32_t mask, uint32_t slot)
        {
            const uint32_t p = rd32(rd, mask, slot) & kPtrMask;
            return (p <= mask) ? p : 0u;
        }
    }   // namespace

    Snapshot read(const uint8_t *rdram, uint32_t ramMask, uint64_t frame)
    {
        Snapshot s;
        s.frame = frame;
        if (!rdram)
            return s;

        // A zero mask would make every address resolve to offset 0 and quietly return the same
        // word forever, which reads as a plausible snapshot. Refuse it instead.
        if (!ramMask)
            return s;

        s.stateObj = follow(rdram, ramMask, kStatePtrSlot);
        if (!s.stateObj)
            return s;   // no state object yet: not even "started", so nothing is claimable
        s.valid = true;

        s.state      = rd32(rdram, ramMask, s.stateObj + kOffState);
        s.menuObj    = follow(rdram, ramMask, kMenuObjSlot);
        s.mainStruct = follow(rdram, ramMask, kMainSlot);

        if (s.mainStruct)
        {
            s.globalFlags = rd32(rdram, ramMask, s.mainStruct + kOffGlobalFlags);
            s.subStruct  = follow(rdram, ramMask, s.mainStruct + kOffSubStruct);
            // menuState is the gate, so it is only trusted with the whole chain resolved. A
            // half-built chain reads whatever happens to sit at that address, and 9 is a value
            // that would then read as "DISPLAYED" out of pure coincidence.
            if (s.subStruct)
                s.menuState = rd32(rdram, ramMask, s.subStruct + kOffMenuState);
        }

        s.itemBase = follow(rdram, ramMask, kItemSlot);
        if (s.itemBase)
        {
            s.itemState  = rd32(rdram, ramMask, s.itemBase + kOffItemState);
            s.itemCursor = rd32(rdram, ramMask, s.itemBase + kOffItemCursor);
            s.itemSel    = rd32(rdram, ramMask, s.itemBase + kOffItemSel);
        }

        // The intro timer is a boot marker, only meaningful on the way IN to the menu, so a
        // missing intro object is normal and not a reason to distrust the rest of the snapshot.
        if (const uint32_t introObj = follow(rdram, ramMask, kIntroSlot))
            s.introT = rd32(rdram, ramMask, introObj + kOffIntroT);

        // The row the cursor is on, with the game's own formula (0x33643C..0x33648C), the same
        // one [bt3cursor] reports. count is the divisor, so a zero there is skipped rather than
        // turned into a division by zero.
        if (s.menuObj)
        {
            const uint32_t base  = rd32(rdram, ramMask, s.menuObj + kOffRowBase);
            const uint32_t cur   = rd32(rdram, ramMask, s.menuObj + kOffRowCursor);
            // The same word the build loop increments, and the thing the gate is made of.
            s.plates = rd32(rdram, ramMask, s.menuObj + kOffRowCount);
            if (s.plates)
            {
                s.row = (int32_t)((base + cur + 1u) % s.plates);
                s.rowIndex = (int32_t)rd32(rdram, ramMask, s.menuObj + kOffRowTable + 4u * (uint32_t)s.row);
            }
        }
        return s;
    }

    Phase phase(const Snapshot &s)
    {
        if (!s.valid || s.state != kStateMainMenu)
            return Phase::Absent;
        // 0x04 with no object is the state having been flipped without the menu being built.
        // Measured, not hypothetical: forcing 0x01 -> 0x04 leaves menuObj at 0 and the menu never
        // appears, so gating on the state alone would draw over a screen that is not there.
        if (!s.menuObj)
            return Phase::Started;

        // The gate is the plate counter, not a state field. menuObj+0x144 is incremented once per
        // entry by the game's own build loop (0x3355b8) and stops at 11, so:
        //   0            the object exists but the loop has not run yet
        //   1..10        plates going up, the menu is materialising
        //   11           every plate built: the menu is on screen  <-- draw here
        //   <11 again    the object is being torn down
        // The teardown case is why the counter beats a state variable: leaving the menu frees
        // menuObj and the state moves to the next screen, and both of those fall out of the test
        // above without a separate "am I leaving" flag to keep in sync.
        if (s.plates >= kRowCount)
            return Phase::Shown;
        if (s.plates == 0u)
            return Phase::Started;
        return Phase::Building;
    }

    bool popupVisible(const Snapshot &s)
    {
        return phase(s) == Phase::Shown;
    }

    const char *phaseName(Phase p)
    {
        switch (p)
        {
        case Phase::Absent:     return "ABSENT";
        case Phase::Started:    return "STARTED";
        case Phase::Building:   return "BUILDING";
        case Phase::Shown:      return "SHOWN";
        case Phase::Leaving:    return "LEAVING";
        }
        return "?";
    }

    const char *itemStateName(uint32_t v)
    {
        // docs/MAIN-MENU.md section 7. Reported, not gated on: the per-frame meaning of this
        // ladder is not established yet, so it is telemetry until a capture says otherwise.
        static const char *const kNames[9] = {
            "PLATE_LOAD", "SECOND_PASS", "REFERENCE_COUNTER", "ANIMATION", "CONFIRM_ACCEPT",
            "NAVIGATION", "CHARACTER_SELECT", "VISUAL_RENDER", "FINAL_CONFIRM",
        };
        return v < 9u ? kNames[v] : "?";
    }

    const char *stateName(uint32_t v)
    {
        switch (v)
        {
        case 0x01u: return "BOOT";
        case 0x04u: return "MAIN_MENU";
        case 0x06u: return "LOADING";
        case 0x0Du: return "ULTIMATE_BATTLE";
        case 0x21u: return "DRAGON_WORLD_TOUR";
        case 0x26u: return "DUEL_MENU";
        case 0x27u: return "CHARACTER_SELECT";
        case 0x28u: return "PREFIGHT_SETUP(0x28)";
        case 0x29u: return "PREFIGHT_SETUP(0x29)";
        case 0x2Cu: return "ULTIMATE_TRAINING";
        case 0x2Du: return "IN_FIGHT";
        case 0x30u: return "EVOLUTION_Z";
        case 0x35u: return "DATA_CENTER";
        case 0x38u: return "POST_FIGHT";
        case 0x3Cu: return "CHARACTER_REFERENCE";
        case 0x3Eu: return "OPTIONS";
        case 0x46u: return "EXTRA(0x46)?";
        default:    return nullptr;
        }
    }

    const char *rowName(int32_t row)
    {
        if (row < 0 || row >= 11)
            return nullptr;
        return kRowNames[row];
    }

    void format(const Snapshot &s, char *buf, std::size_t n)
    {
        if (!buf || n == 0u)
            return;
        const char *nm = stateName(s.state);
        // The cursor is a row index, not a state, so it is printed as a number. Decoding it with
        // itemStateName() reads as "?" for every real cursor value, which looks like a missing
        // field when it is actually a perfectly good one.
        std::snprintf(buf, n,
                      "phase=%s state=0x%02x%s menuObj=0x%x plates=%u/%u row=%d(%s) "
                      "item=%s cursor=%u sel=%u menuState=%s",
                      phaseName(phase(s)), s.state, nm ? nm : "?", s.menuObj,
                      s.plates, kRowCount, s.row, rowName(s.row) ? rowName(s.row) : "?",
                      itemStateName(s.itemState), s.itemCursor, s.itemSel,
                      s.menuState == kMenuStateDisplayed     ? "DISPLAYED"
                      : s.menuState == kMenuStateTransitioning ? "TRANSITIONING"
                      : s.menuState == 0xFFFFFFFFu            ? "(no chain)"
                                                                : "?");
    }

}   // namespace ps2x::mainmenu
