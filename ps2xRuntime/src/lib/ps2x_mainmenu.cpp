// [mainmenu] Read side of the main-menu lifecycle. See ps2x_mainmenu.h for the field map and for
// why each signal is trusted (or only observed).
//
// Every read is bounds-checked against the RAM mask and every pointer is masked to 0x1FFFFFFF,
// which is what the rest of the runtime does: a guest pointer is an EE address with the top bits
// carrying sign/garbage, and an unmasked one is an out-of-bounds host read.

#include "runtime/ps2x_mainmenu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

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

    // ---- the differential dumper ----------------------------------------------------------------

    bool Dumper::configure(uint64_t sampleEvery)
    {
        const char *p = std::getenv("PS2X_MENU_DUMP");
        if (!p || !p[0] || p[0] == '0')
            return false;
        m_prefix = p;
        m_every = sampleEvery ? sampleEvery : 1u;

        // PS2X_MENU_DUMP_FULL=1 switches to whole-RDRAM. The window holds structures, not pixels,
        // and the icons this is after are textures.
        const char *full = std::getenv("PS2X_MENU_DUMP_FULL");
        m_mode = (full && full[0] && full[0] != '0') ? Mode::FullRdram : Mode::Window;
        m_maxSamples = kMaxSamples;
        if (m_mode == Mode::FullRdram)
        {
            const char *n = std::getenv("PS2X_MENU_DUMP_SAMPLES");
            m_maxSamples = (n && n[0]) ? (int)std::atoi(n) : kMaxSamplesFull;
            if (m_maxSamples < 1)
                m_maxSamples = 1;
            // A full dump is 32 MB; sampling it every 30 frames for 8 samples is 256 MB of
            // scratch for no extra information. One sample is the A-vs-B pair; more only if asked.
            if (m_every < 30u)
                m_every = 30u;
        }
        return true;
    }

    void Dumper::tick(const Snapshot &s, const uint8_t *rdram, uint32_t ramMask)
    {
        if (!m_prefix || m_wrote || !rdram)
            return;
        const Phase ph = phase(s);

        // Samples only while the menu is actually up. Sampling during the build would capture a
        // half-populated structure and the diff would be against garbage.
        if (ph != Phase::Shown || !s.menuObj)
        {
            if (m_sampling)
                flush();   // the menu went away: write what we have
            return;
        }
        const uint32_t bytes = (m_mode == Mode::FullRdram) ? (ramMask + 1u) : kWindow;
        if (!m_buf)
        {
            m_buf = new (std::nothrow) uint8_t[bytes];
            if (!m_buf)
            {
                std::fprintf(stderr, "[menudump] out of memory (%u bytes), disabled\n", bytes);
                m_prefix = nullptr;
                return;
            }
        }
        if (!m_sampling)
        {
            m_sampling = true;
            m_count = 0;
            m_sinceLast = 0;
            m_firstFrame = s.frame;
        }
        if (m_sinceLast++ < m_every)
            return;

        // Window mode is anchored to menuObj so the two runs line up; full mode is the whole of
        // RDRAM, which is aligned with itself by definition.
        const uint8_t *src = rdram;
        if (m_mode == Mode::Window)
            src = rdram + ((s.menuObj - kPre) & ramMask);
        std::memcpy(m_buf, src, bytes);
        m_last = s;

        char name[512];
        std::snprintf(name, sizeof name, "%s.s%d.bin", m_prefix, m_count);
        if (FILE *f = std::fopen(name, "wb"))
        {
            std::fwrite(m_buf, 1, bytes, f);
            std::fclose(f);
        }
        else
        {
            std::fprintf(stderr, "[menudump] cannot write %s\n", name);
        }
        m_count++;

        if (m_count >= m_maxSamples)
            flush();
    }

    void Dumper::flush()
    {
        if (!m_sampling)
            return;
        m_sampling = false;
        m_wrote = true;

        // The manifest is what makes the two runs comparable: it records the window's absolute
        // address and the gate readings, so the diff does not have to assume menuObj landed in the
        // same place (it did in every run measured, but "did" is not "must").
        char name[512];
        std::snprintf(name, sizeof name, "%s.manifest.txt", m_prefix);
        if (FILE *f = std::fopen(name, "w"))
        {
            std::fprintf(f, "prefix        %s\n", m_prefix);
            std::fprintf(f, "mode          %s\n",
                         m_mode == Mode::FullRdram ? "full-rdram" : "menu-window");
            std::fprintf(f, "samples       %d (max %d)\n", m_count, m_maxSamples);
            std::fprintf(f, "sample_every  %llu frames\n", (unsigned long long)m_every);
            std::fprintf(f, "first_frame   %llu\n", (unsigned long long)m_firstFrame);
            std::fprintf(f, "menuObj       0x%x\n", m_last.menuObj);
            if (m_mode == Mode::Window)
                std::fprintf(f, "window        0x%08x..0x%08x  (%u bytes, menuObj-0x%x .. +0x%x)\n",
                             m_last.menuObj - kPre, m_last.menuObj - kPre + kWindow,
                             kWindow, kPre, kPost);
            else
                std::fprintf(f, "window        0x00000000..0x%08x  (all of RDRAM)\n", kWindow);
            std::fprintf(f, "plates        %u (complete at %u)\n", m_last.plates, kRowCount);
            std::fprintf(f, "row           %d\n", m_last.row);
            std::fprintf(f, "rowIndex      %d\n", m_last.rowIndex);
            std::fclose(f);
        }

        char line[384];
        format(m_last, line, sizeof line);
        std::fprintf(stderr, "[menudump] wrote %d %s sample(s) to %s.s*.bin  (%s)\n",
                     m_count, m_mode == Mode::FullRdram ? "full-rdram" : "window",
                     m_prefix, line);
        delete[] m_buf;
        m_buf = nullptr;
    }

}   // namespace ps2x::mainmenu
