// [ach] RetroAchievements, evaluated locally. See src/lib/ps2x_achieve.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// The achievement tracker. rcheevos does the condition evaluation -- it is the reference
// implementation of the MemAddr expression language the 154 achievements are written in -- but
// nothing here talks to a server: the patch is a file in assets/, progress is a file in
// savedata/, and the only thing the guest has to provide is its RAM.
//
// Off unless ACHIEVEMENTS=1 or achievements.enabled is true in settings.toml. The address
// translation is not a runtime concern: scripts/gen_ach_patch.py rewrites the conditions into our
// address space and drops every achievement whose addresses it could not confirm, so the engine
// only ever sees conditions that mean what they say.

// Creates the client and loads the patch. `exeDir` is the deploy root; pass null to resolve it
// from ps2xExeDirC(). Safe to call more than once.
void     ps2AchInit(const char *exeDir = nullptr);

// Releases the client and writes progress one last time.
void     ps2AchShutdown();

// One evaluation per presented frame. `rdram` is the guest's 32 MB; it is not retained, only read
// during the call.
void     ps2AchFrame(uint8_t *rdram);

bool     ps2xAchEnabled();
void     ps2xSetAchEnabled(bool on);

// Offers the saved value at boot, without giving up the environment. ps2xSetAchEnabled is the
// player's action and always wins; this is what applySettings() calls, and it exists separately
// because sharing one setter would make the one-session environment override a no-op.
void     ps2xAchApplyDefault(bool on);

// Counts. Unlocked/total/points are 0 before the patch has loaded, which the UI renders as "..."
// rather than "0/0" so an unloaded tracker is not mistaken for an empty one.
int      ps2xAchTotal();
int      ps2xAchUnlocked();
int      ps2xAchPointsEarned();
int      ps2xAchPointsTotal();
bool     ps2xAchLoaded();

// The list, in patch order, for the overlay's Achievements tab. Returns false for an out-of-range
// index.
//
// The strings are COPIED into the caller's buffers rather than handed back as pointers into the
// module's storage. The unlock event is raised on the guest thread (rcheevos is built single-threaded
// here, so its callbacks run wherever do_frame is called) while the tab is drawn on the render
// thread, and the only field that changes after the snapshot is the earned flag -- but a pointer
// into a container another thread can still write to is a lifetime question nobody should have to
// re-derive later. Copying four achievements' worth of text costs nothing.
// Is this achievement's memory address one we have actually confirmed?
//
// Every achievement is in the patch, but 149 of them point at addresses that are still
// RetroAchievements' own and therefore read unrelated memory in this build. Those are extremely
// unlikely to unlock on their own -- the conditions are ANDs of three or more specific values -- but
// "unlikely" is not "impossible", and an unlock the player did not earn is exactly the thing they
// need to be able to tell apart from one they did. This flag is how the list says so.
bool     ps2xAchAddrVerified(int index);

int      ps2xAchListCount();
bool     ps2xAchListGet(int index, char *title, size_t titleCap, char *description,
                         size_t descriptionCap, int *points, bool *unlocked);

// Index of the achievement with this title, or -1. Titles are unique within a patch because rcheevos
// rejects duplicates, so this is a real lookup rather than a scan-and-hope -- but it scans, because
// the list is a handful of entries and this is a diagnostic, not a hot path.
int      ps2xAchFindByTitle(const char *title);

// The achievement id at a list index, or 0. The id is what a badge file is named after and what the
// ledger is keyed on, so it is exposed rather than left to the caller to guess.
uint32_t ps2xAchIdAt(int index);

// rcheevos' own view of one achievement, for diagnostics. The state is the useful one: an
// achievement whose condition failed to parse is reported as a state rather than as an error, and
// "it did not fire" then has two very different causes.
int      ps2xAchState(int index);
int      ps2xAchBucket(int index);
unsigned ps2xAchCategory(int index);

// Takes the oldest pending unlock toast, if any. The overlay calls this once per frame; each call
// consumes one entry, so a burst of unlocks plays out over a few frames instead of stacking.
//
// REMOVED. The unlock notification goes through the shared bus (ps2xNotifyPush) like every other
// feature's, so the card and its sound live in one place instead of one place per feature. The
// achievement module still owns the ledger, the counts and the list.
