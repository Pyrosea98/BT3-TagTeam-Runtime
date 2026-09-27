# Achievements (offline)

RetroAchievements' 154 achievements for Dragon Ball Z: Budokai Tenkaichi 3, evaluated locally
against this recomp's guest RAM. No account, no network, nothing sent anywhere.

The definitions are public RetroAchievements data. The *memory addresses* in them are not ours:
they were written against PCSX2, and this build places the game's heap differently. That
difference is the whole subject of this document.

## What runs

| | |
|---|---|
| `ps2xRuntime/third_party/rcheevos` | vendored, MIT. The reference implementation of the `MemAddr` expression language the 154 conditions are written in. It does the parsing and the evaluation; it never talks to a server. |
| `ps2xRuntime/src/lib/ps2x_achieve.cpp` | the module. Three callbacks: `read_memory` off guest RAM, `server_call` answering from local files, and the event handler. |
| `ps2xRuntime/assets/bt3-achievements.patch.json` | the `r=patch` response, generated. |
| `ps2xRuntime/assets/bt3-achievements.sets.json` | the `r=achievementsets` response, generated. |
| `savedata/achievements.progress` | rcheevos' partial progress (measured values, hit counts). |
| `savedata/achievements.unlocked` | **our** ledger of earned achievements. See below for why rcheevos cannot hold this. |
| `scripts/gen_ach_patch.py` | the generator, and the only place a translation is decided. |
| `docs/retroachievements/` | the master database: all 154, the raw patch, and a per-run report. |

One evaluation per presented frame, from `ps2AchFrame()` in the guest thread's tick
(`game_overrides.cpp`, next to the netplay tick). It is a no-op unless the tracker is on.

## Turning it on

`ACHIEVEMENTS=1` in the environment, or `enabled = true` under `[achievements]` in
`settings.toml`, or the switch in the launcher's Misc page / the overlay's **Achievements** tab.
All three write the same key and the environment is the default, as everywhere else in this tree.

Off by default. It costs a condition evaluation per frame, and the set it can evaluate is currently
small — see below.

## The address translation

The achievements reference PCSX2's memory map. This build is a recompilation with its own
allocator, so the same objects land elsewhere.

**The one shift we have is `+0x4000`, and it is confirmed from two directions.** RetroAchievements'
rich-presence patch names two of our screen states from its own address, `0x006af198 == 1` for the
title screen and `== 4` for the main menu; our state object is at `0x6b3180`, so `+0x4000` puts the
field at `0x6b3198 = stateObj+0x18`, which is exactly the field we read for the screen id. Two
further addresses confirm it independently: `0x006af7a0` and `0x006af7a4` are mode and battle type,
found by hand long before this as `stateObj+0x620` and `stateObj+0x624`, and `+0x4000` lands on
those two offsets precisely.

The other 392 addresses are in regions nobody has mapped yet. They cluster into seven:

| RA region | addresses | what it looks like | achievements gated |
|---|---|---|---|
| `0x6AFxxx` | 9 | `stateObj`, `+0x4000` | done |
| `0x31Exxx` | 2 | below the ELF's loaded segments (`0x334bf8`), so fixed | done |
| `0x6B9xxx` | 215 | a `16B x 8` per-character table and a `12B x ~50` table whose first five records are Missions 1-5 | 94 |
| `0x6BAxxx` | 35 | a byte array | 33 |
| `0x6BCxxx` | 131 | one dense `0x97`-byte blob | 3 |
| `0x1871xxx` / `0x1872xxx` | 10 | two tables of `0xA4`-byte records — the story chapters | 57 |
| `0x8C0xxx` | 1 | a single global | 7 |

**Currently 4 of the 154 are evaluated; 149 are held back.** That is deliberate, and the reason is
not caution. rcheevos reports an achievement as earned the moment its conditions hold. A wrong
address does not fail — it fires. Loading all 149 would write roughly 140 false unlocks, and once
they are in a profile there is no way to tell them apart from real ones afterwards.

So `scripts/gen_ach_patch.py` emits an achievement only when *every* address it touches falls
inside a verified region, and rewrites those addresses on the way out. The rest stay in the master
database and are reported in `docs/retroachievements/bt3-runtime-patch-report.json`, grouped by the
region that is blocking them, so the next region to verify is obvious.

**Adding a region is a one-line change** to that script's `VERIFIED` list, then re-run it. No C++
changes: the engine has no idea an address could be wrong.

## Custom achievements

The `CUSTOM` list in the same script holds achievements written here rather than by
RetroAchievements. They are ordinary entries from there on: same engine, same ledger, same popup
stack, same file.

```python
CUSTOM = [
    {
        "ID": 900001,
        "Title": "Long Time no See!",
        "Description": "Coming back to the main menu after all this time. It has been a while.",
        "Points": 5,
        "MemAddr": "0xH006b3198=4",   # stateObj+0x18 == 0x04, the main menu
        "BadgeName": "900001",
        ...
    },
]
```

Three rules, and all three are enforced by the script rather than written down:

- **`ID` in the 9000xx range.** RetroAchievements does not use it, so a custom one can never
  collide with a real one in the ledger.
- **Every field rcheevos's parser requires must be present**, even the cosmetic ones. It rejects
  the *whole patch* over one missing key — "Author not found in response" — so an entry that omits
  `BadgeURL` takes all 154 down with it. The list is
  `rc_api_parse_achievement_definition()`'s: ID, Title, Description, Flags, Points, MemAddr,
  BadgeName, Author, Created, Modified.
- **The address must be inside a `VERIFIED` region**, checked on the *our* side of the translation.
  Custom entries are already authored in our address space, so feeding one to the RA→our mapping
  would be nonsense — and the failure looks like a false alarm, because the one address in this tree
  with the most evidence behind it, `stateObj+0x18`, would be reported unverified.

`Long Time no See!` is the worked example of a **first-time** achievement: its condition is true on
every launch at the main menu, and the card appears exactly once. That half is the ledger's job, and
`ps2x_achieve_probe` asserts both directions of it — a clean ledger must announce, a warm one must
not.

## Notifying the player

An unlock does not draw anything itself. It goes through the shared bus
(`runtime/ps2x_notify.h`), and the overlay draws the result — so netplay and the achievement tracker
share a presentation without sharing a feature. See [NOTIFY.md](NOTIFY.md) for the stack and the
sounds.

## The unlock ledger is ours, not rcheevos'

`rc_runtime_progress_write_achievements()` skips any trigger that is no longer active — the comment
in the source is "don't store state for inactive or triggered achievements". In a client that talks
to a server that is correct: the server holds the unlock list, and the local blob only carries
partial progress for the achievements still in flight.

There is no server here, so nothing else would remember an unlock. The symptom is not obvious:
`achievements.progress` reloads cleanly, reports no error, and every achievement comes back locked,
so the counts reset each launch.

Hence `savedata/achievements.unlocked` — one line per unlock, `<id> <unix seconds>`, append-only.
Plain text on purpose. It is also what suppresses a repeat toast: rcheevos re-raises the unlock
event for anything it has no record of, and the ledger is what tells a fresh unlock from a
condition that simply holds again.

## Two things that are not what they look like

**`d0x` does not mean "changed by".** In `rc_test_condition_compare_delta_to_const()`,
rcheevos compares the memref's `prior` field against the constant; there is no subtraction on that
path. So `d0xX=5` asks for *the value X held before its last change*. Paired with a plain `0xX=6`
the two terms are an increment detector: it was 5, and now it is 6. `ps2x_achieve_probe` exercises
exactly that, and getting it backwards is a silent never-fires.

**A 16-bit read can be written `0x 018726a4`.** `rc_parse_memref()` reads `0x` followed by exactly
one size character, and a space is a valid one. Anything that scans a `MemAddr` for
`0x` + a letter misses every one of those, which is how achievements whose only untranslated
address is written that way end up looking translatable.

## Checking it

```sh
python3 scripts/gen_ach_patch.py            # regenerate the assets
python3 scripts/gen_ach_patch.py --check    # fail if they are stale (CI)
./build/ps2xRuntime/ps2x_achieve_probe ps2xRuntime/assets/bt3-achievements.patch.json
```

The probe needs no game, no disc and no window. It asserts the generated patch is accepted by
rcheevos, that the achievement list is populated, and that a condition fires through the rewritten
addresses — which is the only check in the tree that can catch a malformed `MemAddr` or a bad
rewrite. Run it twice against the same deploy: the second run must report the achievement as
already earned before it forces anything. That pair is the persistence check.

`PS2X_ACH_TRACE=1` logs every address the tracker reads. rcheevos says only "earned" or "not", so
when a condition that should hold does not, the way to tell a wrong address from a wrong comparison
is to see what it actually read. `PS2X_DUMPKEY=<prefix>` + F9 dumps the whole 32 MB of guest RAM for
the same job at a larger scale; the two together are how the remaining regions get mapped.
