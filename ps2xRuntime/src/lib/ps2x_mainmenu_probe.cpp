// [mainmenu-probe] The main-menu lifecycle gate, checked against synthetic guest RAM.
//
// The question this exists to answer is "when does an in-menu popup draw, and when does it not",
// and that is a question about a state machine over guest RAM, not about the game running. So it
// is answered here: build a fake RDRAM, walk the lifecycle the way the game walks it, and assert
// what popupVisible() says on each step.
//
// It also pins the guards, because each one is a way the gate could report "draw" over a screen
// that is not there:
//   * a null RDRAM, and a state object that has not been allocated yet;
//   * state == 0x04 with menuObj == 0, the measured "started but never built" case;
//   * a menuState of DISPLAYED reached through a half-built mainStruct/subStruct chain, which must
//     NOT read as shown (9 is a value that would otherwise be read out of coincidence);
//   * menuObj+0x144 == 0, the divisor in the game's own row formula;
//   * a pointer with the top bits set, the way guest pointers actually arrive.
//
// No game, no window, no GL.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "runtime/ps2x_mainmenu.h"

namespace
{
using namespace ps2x::mainmenu;

int g_fail = 0;

void check(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FALLA", what);
    if (!ok)
        ++g_fail;
}

// PS2_RAM_MASK for a 32 MB RDRAM. NOT the same as the guest-pointer mask (0x1FFFFFFF, 512 MB):
// conflating the two is how a read ends up at a wrapped address instead of failing.
constexpr uint32_t kRamMask = 0x1FFFFFFu;

// A fake RDRAM with every pointer the reader follows pre-wired, so a test only has to poke the
// one field it cares about.
struct FakeRam
{
    std::vector<uint8_t> ram = std::vector<uint8_t>(32u << 20, 0u);

    static constexpr uint32_t kStateObj   = 0x00100000u;
    static constexpr uint32_t kMenuObj    = 0x00200000u;
    static constexpr uint32_t kMainStruct = 0x00300000u;
    static constexpr uint32_t kSubStruct  = 0x00400000u;
    static constexpr uint32_t kItemBase   = 0x00500000u;
    static constexpr uint32_t kIntroObj   = 0x00600000u;

    void poke(uint32_t addr, uint32_t v) { std::memcpy(&ram[addr & kRamMask], &v, sizeof v); }
    uint32_t peek(uint32_t addr) const { uint32_t v = 0; std::memcpy(&v, &ram[addr & kRamMask], sizeof v); return v; }

    FakeRam()
    {
        poke(kStatePtrSlot, kStateObj);
        poke(kMenuObjSlot, kMenuObj);
        poke(kMainSlot, kMainStruct);
        poke(kItemSlot, kItemBase);
        poke(kIntroSlot, kIntroObj);
        poke(kMainStruct + kOffSubStruct, kSubStruct);
        // A finished menu: the plate counter at its final value, cursor on row 3. With the
        // measured count of 9, base=0 and cursor=2 gives (0+2+1)%9 = 3, which is the one row the
        // runtime has verified (Duel -> state 0x26).
        poke(kMenuObj + kOffRowBase, 0u);
        poke(kMenuObj + kOffRowCount, kRowCount);
        poke(kMenuObj + kOffRowCursor, 2u);
        for (uint32_t i = 0; i < kRowCount; ++i)
            poke(kMenuObj + kOffRowTable + 4u * i, i);
        poke(kSubStruct + kOffMenuState, kMenuStateDisplayed);
    }

    void setState(uint32_t s)   { poke(kStateObj + kOffState, s); }
    void setMenuState(uint32_t v) { poke(kSubStruct + kOffMenuState, v); }
    void setMenuObj(uint32_t p) { poke(kMenuObjSlot, p); }
    void setSubStruct(uint32_t p) { poke(kMainStruct + kOffSubStruct, p); }
    void setMainStruct(uint32_t p) { poke(kMainSlot, p); }
    void setPlates(uint32_t n)  { poke(kMenuObj + kOffRowCount, n); }
    void setItemState(uint32_t v) { poke(kItemBase + kOffItemState, v); }

    const uint8_t *rd() const { return ram.data(); }
};

// A Snapshot is taken as the game would, and the popup gate is asked about it.
Phase phaseOf(FakeRam &f) { return phase(read(f.rd(), kRamMask)); }
bool shows(FakeRam &f)    { return popupVisible(read(f.rd(), kRamMask)); }
}   // namespace

int main()
{
    std::printf("[1] the lifecycle, in the order the game walks it\n");
    {
        FakeRam f;
        f.setState(0x01u);            // BOOT
        check(phaseOf(f) == Phase::Absent, "BOOT is ABSENT (no popup)");
        check(!shows(f), "nothing is drawn during BOOT");

        // 0x04 flips BEFORE the object is allocated. Measured: forcing 0x01 -> 0x04 leaves
        // menuObj at 0 and the menu never appears.
        f.setState(kStateMainMenu);
        f.setMenuObj(0u);
        check(phaseOf(f) == Phase::Started, "0x04 with menuObj == 0 is STARTED, not SHOWN");
        check(!shows(f), "nothing is drawn while STARTED (the screen is not there yet)");

        // The object exists, the build loop has not run: still STARTED, because no plate is up.
        f.setMenuObj(FakeRam::kMenuObj);
        f.setPlates(0u);
        check(phaseOf(f) == Phase::Started, "object up but no plate built yet is STARTED");
        check(!shows(f), "nothing is drawn before the first plate exists");

        // The build loop climbing 1..8: the menu is materialising.
        for (uint32_t n = 1; n < kRowCount; ++n)
        {
            f.setPlates(n);
            if (phaseOf(f) != Phase::Building)
            {
                check(false, "a partial plate count is BUILDING");
                break;
            }
        }
        check(phaseOf(f) == Phase::Building, "a partial plate count is BUILDING");
        check(!shows(f), "nothing is drawn while the plates are still going up");

        // Every plate built: this is "the lines are on screen". The measured value is 9, not 11
        // (see kRowCount), and 10 must also open it, because revealing the hidden entry adds one.
        f.setPlates(kRowCount);
        check(phaseOf(f) == Phase::Shown, "plates == 9 is SHOWN");
        check(shows(f), "THE POPUP DRAWS here");
        f.setPlates(kRowCount + 1u);
        check(shows(f), "plates == 10 (hidden entry revealed) is also SHOWN");

        // The cursor formula only becomes meaningful once the count is real, and row 3 is the
        // one entry verified at runtime (Duel, target state 0x26).
        f.setPlates(kRowCount);
        Snapshot s = read(f.rd(), kRamMask);
        check(s.plates == kRowCount, "the plate counter is read back");
        check(s.row == 3, "row == 3 for cursor offset 2 with 9 plates");
        check(s.rowIndex == 3, "rowIndex == 3 (the jump-table index for Duel)");
        check(kRowTargetState[s.row] == 0x26u, "row 3 targets state 0x26 (Duel, verified)");

        // Teardown: the counter falling again, then the object going away, then the state moving.
        f.setPlates(kRowCount - 1u);
        check(!shows(f), "the popup goes with the plates when they start being destroyed");
        f.setPlates(0u);
        check(phaseOf(f) == Phase::Started, "counter back to 0 is STARTED again");
        f.setMenuObj(0u);
        check(phaseOf(f) == Phase::Started, "and a freed menuObj is STARTED");
        f.setState(0x06u);            // LOADING: New Game / Continue took the screen
        check(phaseOf(f) == Phase::Absent, "leaving 0x04 is ABSENT");
        check(!shows(f), "nothing is drawn on the way out");
    }

    std::printf("[1b] the gate is the plate counter, not menuState\n");
    {
        // The real build: *(0x3B38D8) is 0 in this build, so the documented
        // subStruct+0x40 -> 9/10 chain cannot resolve and must not be able to open the gate.
        FakeRam f;
        f.setState(kStateMainMenu);
        f.setPlates(kRowCount);
        f.setMainStruct(0u);          // what the log actually shows
        Snapshot dead = read(f.rd(), kRamMask);
        check(dead.menuState == 0xFFFFFFFFu, "the documented menuState chain stays unresolved");
        check(phase(dead) == Phase::Shown, "and the menu is still SHOWN without it");
        check(popupVisible(dead), "the gate does not depend on the dead chain");

        // And with the plates half up it must stay shut even if that chain were somehow readable.
        f.setPlates(5u);
        check(!popupVisible(read(f.rd(), kRamMask)), "half-built plates never open the gate");
    }

    std::printf("[2] the guards: ways the gate could say 'draw' over nothing\n");
    {
        FakeRam f;
        f.setState(kStateMainMenu);
        check(phase(read(nullptr, kRamMask)) == Phase::Absent, "a null RDRAM is ABSENT");
        check(!popupVisible(read(nullptr, kRamMask)), "a null RDRAM draws nothing");

        FakeRam g;
        g.poke(kStatePtrSlot, 0u);     // the state object itself is not allocated yet
        Snapshot s = read(g.rd(), kRamMask);
        check(!s.valid, "no state object means valid == false");
        check(phase(s) == Phase::Absent, "no state object is ABSENT");
        check(!popupVisible(s), "no state object draws nothing");

        // A half-built chain must not be able to fake DISPLAYED: subStruct is null, so menuState
        // must stay unresolved instead of reading whatever sits at address 0x40. Now that the
        // gate is the plate counter, the broken chain is inert: it is telemetry, and the phase is
        // decided by the plates alone. The two must not be allowed to disagree.
        FakeRam h;
        h.setState(kStateMainMenu);
        h.setSubStruct(0u);
        Snapshot hs = read(h.rd(), kRamMask);
        check(hs.menuState == 0xFFFFFFFFu, "a broken subStruct leaves menuState unresolved");
        check(phase(hs) == Phase::Shown, "a broken chain does not change the phase");
        h.setPlates(3u);
        Snapshot hs2 = read(h.rd(), kRamMask);
        check(phase(hs2) == Phase::Building, "and it cannot hold the gate open either");
        check(!popupVisible(hs2), "a broken chain never opens the gate on its own");
    }

    std::printf("[3] the row formula, against the verified row\n");
    {
        FakeRam f;
        f.setState(kStateMainMenu);
        Snapshot s = read(f.rd(), kRamMask);
        // (base=0 + cursor=2 + 1) % 11 == 3, and index 3 is the Duel entry, target state 0x26.
        check(s.row == 3, "row == 3 for cursor offset 2 with 9 rows");
        check(s.rowIndex == 3, "rowIndex == 3 (the jump-table index for Duel)");
        check(kRowTargetState[s.row] == 0x26u, "row 3 targets state 0x26 (Duel, verified)");
        check(std::strcmp(rowName(3), kRowNames[3]) == 0, "rowName(3) is the Duel entry");

        // The divisor lives in guest RAM and can be zero mid-build; the formula must skip, not
        // divide by zero.
        FakeRam z;
        z.setState(kStateMainMenu);
        z.poke(FakeRam::kMenuObj + kOffRowCount, 0u);
        Snapshot zs = read(z.rd(), kRamMask);
        check(zs.row == -1, "a zero row count leaves row == -1 instead of dividing by zero");
    }

    std::printf("[4] telemetry is read but not trusted for the gate\n");
    {
        FakeRam f;
        f.setState(kStateMainMenu);
        f.setItemState(7u);           // VISUAL_RENDER
        Snapshot s = read(f.rd(), kRamMask);
        check(s.itemState == 7u, "itemState is reported");
        check(std::strcmp(itemStateName(s.itemState), "VISUAL_RENDER") == 0,
              "itemState 7 decodes as VISUAL_RENDER");
        // itemState is not in the gate, so it cannot change the answer.
        f.setItemState(0u);
        check(phaseOf(f) == phase(read(f.rd(), kRamMask)), "itemState does not move the phase");
        check(shows(f), "the gate is unchanged by itemState");
    }

    std::printf("[5] guest pointers: garbage top bits, and pointers past the end of RAM\n");
    {
        // Bits 29-31 are the ones a guest pointer mask (0x1FFFFFFF) clears, so those are the ones
        // that actually arrive dirty. Bit 28 survives the mask and is a different case, below.
        FakeRam f;
        f.setState(kStateMainMenu);
        f.poke(kMenuObjSlot, FakeRam::kMenuObj | 0xE0000000u);
        Snapshot s = read(f.rd(), kRamMask);
        check(s.menuObj == FakeRam::kMenuObj, "a pointer with the top bits set is masked back");
        check(phase(s) == Phase::Shown, "and the gate still reads it as SHOWN");

        // Bit 28 survives 0x1FFFFFFF, so 0xF0200000 masks to 0x10000000: inside the pointer range,
        // past the end of a 32 MB RDRAM. Masking the read address would wrap it to 0x00000000 and
        // read a plausible-looking word; the reader has to call it absent instead.
        FakeRam g;
        g.setState(kStateMainMenu);
        g.poke(kMenuObjSlot, 0xF0200000u);
        Snapshot gs = read(g.rd(), kRamMask);
        check(gs.menuObj == 0u, "a pointer past the end of RAM is reported as absent");
        check(phase(gs) == Phase::Started, "and the gate refuses to claim the screen is up");
        check(!popupVisible(gs), "a bogus pointer never draws");

        // A zero RAM mask would resolve every address to offset 0 and return one word forever.
        FakeRam z;
        z.setState(kStateMainMenu);
        check(!read(z.rd(), 0u).valid, "a zero RAM mask is refused rather than read through");
    }

    std::printf("%s (%d fallos)\n", g_fail ? "PROBE FAILED" : "PROBE OK", g_fail);
    return g_fail ? 1 : 0;
}
