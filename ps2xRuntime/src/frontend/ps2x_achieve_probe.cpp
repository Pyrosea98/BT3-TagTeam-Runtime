// [ach] A probe for the offline tracker, and the only thing in the tree that exercises it without
// the game. Two jobs, both about the parts that cannot be checked by reading the code:
//
//   1. The patch really is accepted. rc_client parses the MemAddr expressions at load time, and a
//      malformed one is a silent no-op rather than an error, so "the build compiles" says nothing
//      about whether the 4 conditions in the patch are well formed. This asserts the client reaches
//      RC_CLIENT_LOAD_GAME_STATE_DONE and that it reports 4 achievements.
//
//   2. The engine reads OUR addresses. A RAM buffer is filled with a recognisable pattern, one
//      known field is set to the value the "I. Am. Powerful." condition needs, do_frame() is
//      called, and the achievement is expected to fire. That closes the loop from the JSON on disk
//      through the generator's rewrite, through rcheevos' parser, through read_memory, to the
//      unlock event -- without a recomp, a disc, or a window.
//
// usage: ps2x_achieve_probe [path/to/bt3-achievements.patch.json]
//
// Run it twice against the same deploy: the first run earns one achievement and the second must
// report it as already unlocked before it forces anything. That pair is the persistence check.

#include "runtime/ps2x_achieve.h"
#include "runtime/ps2x_notify.h"   // [notify] the bus an unlock goes through
#include "runtime/ps2_memory.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// stateObj+0x2c in our address space, which is what the generator rewrote 0x006af1ac into.
constexpr uint32_t kModeField = 0x006B31ACu;   // == 1 for the one testable condition
constexpr uint32_t kStarField = 0x006B37CCu;   // the star level the same condition watches
constexpr uint32_t kTargetMode = 1u;
constexpr uint32_t kStarBefore = 5u;           // the value the d0x term asks to have seen
constexpr uint32_t kStarAfter = 6u;            // the value the 0x term asks to see now
constexpr uint32_t kStateField = 0x006B3198u;  // stateObj+0x18, the top-level screen id

int failures = 0;
int s_tonesPlayed = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok)
        ++failures;
}

}   // namespace

// Deliberately outside the anonymous namespace above: both of these have to be visible to the
// linker, and a definition inside one gets internal linkage and satisfies nothing.
//
// ps2xExeDirC() lives in main.cpp, which the runner links and this probe does not. The module only
// falls back to it when no root is passed, and main() below always passes one, so this stub is
// unreachable -- it exists to satisfy the link, not to be called.
extern "C" const char *ps2xExeDirC() { return ""; }

// The other end of the notification bus's seam. The real one is ps2x_notify_sound.cpp, which pulls
// in PS2AudioBackend and the host audio sink; a probe with no audio device has no business linking
// them. The bus itself IS linked and IS exercised -- an unlock really does go through
// ps2xNotifyPush() and lands in the queue -- so this replaces the part that would make noise and
// nothing else. Counting the calls is the point: it is how the probe asserts a sound was asked for
// without being able to hear one.
void ps2xNotifyPlayTone(NotifyTone tone)
{
    if (tone != NotifyTone::None)
        ++s_tonesPlayed;
}

int main(int argc, char **argv)
{
    // Where the module looks is <exeDir>/assets and ./assets; the probe takes the file directly so
    // it can be pointed at the source tree without copying anything into place.
    std::string patch;
    if (argc > 1)
    {
        patch = argv[1];
    }
    else
    {
        namespace fs = std::filesystem;
        const fs::path here = fs::current_path();
        for (const char *c : {"assets/bt3-achievements.patch.json",
                              "../ps2xRuntime/assets/bt3-achievements.patch.json"})
        {
            if (fs::exists(here / c))
            {
                patch = (here / c).string();
                break;
            }
        }
    }
    if (patch.empty())
    {
        std::fprintf(stderr, "no patch found; pass the path as argv[1]\n");
        return 2;
    }
    std::printf("[ach] patch: %s\n", patch.c_str());

    // The module resolves the patch from the deploy root, so point it at the directory holding it.
    const std::filesystem::path root = std::filesystem::path(patch).parent_path().parent_path();
    ps2AchInit(root.string().c_str());

    // The module is inert until it is switched on -- ps2AchFrame() returns immediately otherwise --
    // and a probe that asserted against a disabled tracker would pass a broken build for a working
    // one. This is the same call the overlay's switch makes.
    ps2xSetAchEnabled(true);

    // --- 1. the patch loads -----------------------------------------------------------------------------
    // The load is asynchronous inside the client but entirely local, so a bounded spin on the state
    // is enough; a real network round trip is what would need a timeout and a retry. Frames keep
    // being pumped past the load, because the achievement list is snapshotted on the first frame
    // after it and not before.
    std::vector<uint8_t> ram(PS2_RAM_SIZE, 0);
    bool done = false;
    for (int i = 0; i < 200; ++i)
    {
        ps2AchFrame(ram.data());
        if (ps2xAchLoaded() && ps2xAchTotal() > 0)
        {
            done = true;
            break;
        }
    }
    check(done, "the client reached the loaded state with a list");
    const int total = ps2xAchTotal();
    std::printf("  total achievements: %d\n", total);
    check(total > 0, "at least one achievement is in the list");

    // rcheevos skips any achievement whose trigger failed to parse (rc_trigger is NULL) and reports
    // that as the state, not as an error. Without this line a malformed condition is
    // indistinguishable from a condition that simply did not come true, which is the one thing the
    // probe exists to tell apart.
    for (int i = 0; i < total; ++i)
    {
        char title[128] = {};
        char desc[256] = {};
        int pts = 0;
        bool unlocked = false;
        ps2xAchListGet(i, title, sizeof title, desc, sizeof desc, &pts, &unlocked);
        std::printf("    [%d] state=%d bucket=%d cat=%u  %2d pts  %s\n", i, ps2xAchState(i),
                    ps2xAchBucket(i), ps2xAchCategory(i), pts, title);
    }

    // --- 2. what came back from disk -----------------------------------------------------------------------
    // Reported before anything is forced, because "did the last session's unlocks survive" is a
    // different question from "does a condition fire" and conflating them hides both. On a fresh
    // progress file this is 0; on the second run it must be the number the first run earned.
    const int restored = ps2xAchUnlocked();
    std::printf("  restored from progress: %d unlocked, %d points\n", restored,
                ps2xAchPointsEarned());

    // Which of the two achievements this probe drives were already earned BEFORE it touched
    // anything. The probe is a two-run test and the contract it is here to check has two halves:
    // the first run must announce, and every run after it must not. Asserting only the first half
    // would pass a tracker that re-toasts a card on every launch, which is the bug the ledger
    // exists to prevent, and asserting only the second would pass one that never announces.
    auto earnedAtStart = [&](const char *title) {
        char t[160] = {}, d[320] = {};
        int pts = 0;
        bool unlocked = false;
        const int i = ps2xAchFindByTitle(title);
        if (i < 0)
            return false;
        ps2xAchListGet(i, t, sizeof t, d, sizeof d, &pts, &unlocked);
        return unlocked;
    };
    const bool hadStar = earnedAtStart("I. Am. Powerful.");
    const bool hadVisit = earnedAtStart("Long Time no See!");
    std::printf("  already earned on entry: star=%d visit=%d (%s)\n", hadStar, hadVisit,
                (hadStar || hadVisit) ? "warm ledger" : "clean ledger");

    // --- 3. the engine reads our addresses ---------------------------------------------------------------
    std::fill(ram.begin(), ram.end(), 0xCD);
    // The condition is  0xH006b31ac=1  AND  d0xH006b37cc=5  AND  0xH006b37cc=6.
    //
    // "d0x" does NOT mean "changed by". rc_test_condition_compare_delta_to_const() in
    // rcheevos/src/rcheevos/condition.c compares the memref's `prior` field against the constant --
    // there is no subtraction anywhere on that path -- so the term asks for the value the address
    // held BEFORE its last change. Paired with the plain 0x term, the pair is an increment
    // detector: it was 5, and now it is 6. Which is exactly what "reach maximum star level" wants.
    //
    // So the value has to be held at 5 for a frame first (otherwise `prior` is whatever the fill
    // pattern left, and the term is false), and only then stepped to 6.
    ram[kModeField] = static_cast<uint8_t>(kTargetMode);
    ram[kStarField] = static_cast<uint8_t>(kStarBefore);
    ps2AchFrame(ram.data());                    // the address is now 5; prior is still the fill
    ps2AchFrame(ram.data());                    // unchanged, so prior becomes 5 as well
    ram[kStarField] = static_cast<uint8_t>(kStarAfter);
    ps2AchFrame(ram.data());                    // changed: prior 5, value 6 -- the pair now holds

    // A couple more frames with it held, the way the real game does, so an unlock cannot depend on
    // a single frame landing exactly right.
    for (int i = 0; i < 8; ++i)
        ps2AchFrame(ram.data());

    int unlocked = ps2xAchUnlocked();
    int points = ps2xAchPointsEarned();
    std::printf("  unlocked: %d, points: %d, tones: %d\n", unlocked, points, s_tonesPlayed);

    if (hadStar)
    {
        // Warm: rcheevos re-raises the event because its blob does not record unlocks, and the
        // ledger has to swallow it. Anything else here is a card the player already dismissed.
        check(s_tonesPlayed == 0, "a re-fired event is NOT announced again");
        NotifyEvent ev[4];
        check(ps2xNotifyDrain(ev, 4) == 0, "no card is queued for an already-earned achievement");
    }
    else
    {
        check(unlocked > 0, "a condition fired against the rewritten addresses");
        check(points > 0, "the earned points were counted");
        // The unlock has to reach the shared bus, not just our counters: that is the path the popup
        // and the sound are hung off, and a tracker that counted correctly but announced nothing
        // would look identical from the outside.
        check(s_tonesPlayed > 0, "the unlock went through the notification bus");
        NotifyEvent ev[4];
        const int got = ps2xNotifyDrain(ev, 4);
        check(got > 0, "a card is queued for the overlay to draw");
        for (int i = 0; i < got; ++i)
            std::printf("    card: kind=%s tone=%d \"%s\" / \"%s\"\n",
                        ev[i].kind == NotifyKind::Achievement ? "achievement" : "netplay",
                        static_cast<int>(ev[i].tone), ev[i].title, ev[i].body);
        // The kind is what keeps the two features apart on screen, so it is asserted rather than
        // assumed: an achievement announced as a netplay card would be a silent identity mix-up.
        check(got > 0 && ev[0].kind == NotifyKind::Achievement,
              "the card carries the achievement kind, not netplay's");
    }

    // --- 4. the custom achievement, and what makes it "first time" ------------------------------------------
    // "Long Time no See!" fires on stateObj+0x18 == 4, the main menu. The condition is not the
    // interesting part -- it is true every single launch, and rcheevos has no record of having
    // earned it because the ledger is ours. So what is being tested here is the pair: the card
    // comes exactly once, and the count survives a restart.
    //
    // Run this probe twice against the same deploy. On a clean savedata the first run announces it;
    // the second must report it as already earned and must NOT ask for a tone. The count is
    // asserted from the persisted ledger, not from anything rcheevos is doing.
    {
        std::fill(ram.begin(), ram.end(), 0);
        const uint32_t before = static_cast<uint32_t>(ps2xAchUnlocked());
        const int tonesBefore = s_tonesPlayed;

        ram[kStateField] = 0x04;   // the main menu
        for (int i = 0; i < 20; ++i)
            ps2AchFrame(ram.data());

        const uint32_t after = static_cast<uint32_t>(ps2xAchUnlocked());
        const int toneDelta = s_tonesPlayed - tonesBefore;
        std::printf("  main menu reached: %u -> %u unlocked, %d tone(s)\n", before, after,
                    toneDelta);

        const int found = ps2xAchFindByTitle("Long Time no See!");
        check(found >= 0, "the custom achievement is in the list");
        if (found >= 0)
        {
            char title[160] = {}, desc[320] = {};
            int pts = 0;
            bool unlocked = false;
            ps2xAchListGet(found, title, sizeof title, desc, sizeof desc, &pts, &unlocked);
            std::printf("    [%d] %d pts, %s -- \"%s\"\n", found, pts,
                        unlocked ? "earned" : "LOCKED", desc);
            check(pts > 0, "the custom achievement has points");
            check(desc[0] != '\0', "the custom achievement has a description");
            check(unlocked, "reaching the main menu leaves it earned");
        }

        if (hadVisit)
        {
            // This is the whole point of the achievement: the condition is true on every single
            // launch, and the card appears exactly once. A tracker that re-announces it is worse
            // than one that never announces it, because the player learns to ignore the popup.
            check(after == before, "the main menu does not re-earn it");
            check(toneDelta == 0, "and does not announce it a second time");
        }
        else
        {
            check(after == before + 1, "reaching the main menu earned the custom achievement");
            // Exactly one tone, not zero and not two: zero would mean the card is silent, and two
            // would mean the same unlock is being announced on every frame the menu is up.
            check(toneDelta == 1, "the unlock announced itself exactly once");
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    ps2AchShutdown();
    return failures ? 1 : 0;
}
