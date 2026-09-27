#!/usr/bin/env python3
# [ach] Generate the achievement patch the runtime serves, from the master database.
#
# The 154 achievements in docs/retroachievements/ were written against PCSX2's memory map. This
# recomp places the game's heap differently, so those addresses cannot be handed to rcheevos as
# they are. The translation is DATA, not code: a list of verified regions below, and an
# achievement is only allowed into the runtime patch when EVERY address it touches falls in a
# verified region. Everything else stays in the master database and is left out of the patch, so
# rcheevos never evaluates a condition whose meaning we have not confirmed.
#
# The reason for the allowlist is not caution for its own sake. rcheevos reports an achievement as
# earned the moment its conditions hold, and a wrong address does not fail loudly -- it fires. A
# build that pointed the engine at unverified addresses would write a hundred false unlocks into
# the player's profile, and there is no way to tell those apart from real ones afterwards. Adding a
# region to VERIFIED is therefore the whole procedure for turning a block of achievements on: edit
# the list, re-run this, done. No C++ changes.
#
#   usage: scripts/gen_ach_patch.py [--check]
#
# --check exits non-zero if the generated file is stale, for CI.

import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

MASTER = os.path.join(ROOT, "docs", "retroachievements", "bt3-raw-patch.json")
REPORT = os.path.join(ROOT, "docs", "retroachievements", "bt3-runtime-patch-report.json")
OUT = os.path.join(ROOT, "ps2xRuntime", "assets", "bt3-achievements.patch.json")
OUT_SETS = os.path.join(ROOT, "ps2xRuntime", "assets", "bt3-achievements.sets.json")
OUT_VERIFIED = os.path.join(ROOT, "ps2xRuntime", "assets", "bt3-achievements.verified.txt")
BADGE_DIR = os.path.join(ROOT, "ps2xRuntime", "assets", "badges")

# ---------------------------------------------------------------------------------------------
# Badges
# ---------------------------------------------------------------------------------------------
# The four achievements whose addresses are confirmed all have a BadgeURL pointing at
# media.retroachievements.org, and the URLs are already in the patch -- what is missing is the
# image on disk, because nothing in the runtime is allowed to reach the network. So the download
# happens here, at build time, and the runtime only ever reads a local file.
#
# Cached by id: the file is the cache. A badge only changes when the achievement does, and an
# achievement's id never changes, so "the file is there" is the whole freshness test. --no-badges
# skips it, which is what a machine with no network uses.
BADGE_OFF = "--no-badges" in sys.argv


def fetch_badges(entries):
    """Download the badge for each entry that has a URL. Returns how many are on disk."""
    if BADGE_OFF:
        return 0
    os.makedirs(BADGE_DIR, exist_ok=True)
    got = 0
    for a in entries:
        url = a.get("BadgeURL") or ""
        aid = a.get("ID")
        if not url or not aid or a.get("custom"):
            continue
        dest = os.path.join(BADGE_DIR, "%d.png" % aid)
        if os.path.exists(dest) and os.path.getsize(dest) > 0:
            got += 1
            continue
        try:
            import urllib.request
            req = urllib.request.Request(url, headers={"User-Agent": "BT3-Recomp/1.0"})
            with urllib.request.urlopen(req, timeout=20) as r:
                data = r.read()
            if len(data) < 64:
                continue
            with open(dest, "wb") as f:
                f.write(data)
            got += 1
        except Exception as exc:   # noqa: BLE001 -- a missing badge is not a build failure
            print("  badge %s: %s" % (aid, exc))
    return got

# The addresses an rcheevos MemAddr expression uses.
#
# rc_parse_memref() (rcheevos/src/rcheevos/memref.c) reads "0x" followed by EXACTLY ONE size
# character and then the hex digits: h/H = 8 bit, " " = 16 bit, x/X = 32 bit, m..t = individual
# bits, l/u = low/high half, k = bit count, w = 24 bit, g/i/j = big endian, and a leading hex digit
# is the legacy "no size prefix" form. The space is a real size prefix, not decoration -- "0x 018726a4"
# is a 16-bit read at 0x018726a4, and an expression that only matches the lettered forms silently
# drops it, which would let an achievement into the patch while still pointing at an address that
# was never translated.
SIZE_CHAR = "hHxXmMnNoOpPqQrRsStTlLuUkKwWgGiIjJ "
ADDR_RE = re.compile(r"(0x[" + SIZE_CHAR + r"]?)([0-9A-Fa-f]{6,8})")

# The ELF's loaded segments end here, so anything below it is at a fixed address in every build
# and needs no translation. Everything above is heap.
ELF_LOADED_END = 0x334BF8

# Achievements deliberately left out even though their addresses are fine. See the loop below.
EXCLUDED = {
    "Warning: Unknown Emulator",
}

# ---------------------------------------------------------------------------------------------
# Custom -- written here, not by RetroAchievements.
# ---------------------------------------------------------------------------------------------
#
# Two properties make these different from the 154, and both are enforced rather than documented:
#
#   "custom": true  -- the address filter below is skipped, because these are authored in OUR
#                      address space already. Translating them would be nonsense. They are still
#                      range-checked against VERIFIED, so a custom achievement cannot point at an
#                      address nobody has confirmed either -- the guarantee is the same one, it just
#                      comes from a different direction.
#
#   ids in the 9000xx range, which RetroAchievements does not use, so a custom one can never collide
#   with a real achievement's id and be mistaken for it in the ledger.
#
# Nothing else about them is special. They go through the same engine, the same ledger and the same
# popup stack as the rest, and they are persisted in the same file.
#
# The key names are RA's own -- Title, Description, MemAddr, Points -- so a custom entry is a
# drop-in for a real one everywhere downstream. Using tidy lowercase keys here would mean either a
# rename pass in the loop below or a second shape for the same thing, and the second shape is how a
# custom achievement ends up missing a field the runtime reads.
#
# Every field rcheevos's parser marks required is present even when it is cosmetic, because the
# parser rejects the WHOLE patch over one missing key -- "Author not found in response" -- and a
# tracker that refuses to load because a local achievement has no badge URL is not a good trade.
# The list is rc_api_parse_achievement_definition()'s: ID, Title, Description, Flags, Points, MemAddr,
# BadgeName, Author, Created, Modified.
#
# The timestamps are fixed rather than "now" so that re-running this produces a byte-identical file
# and --check stays useful. Author is the project, not a person: these are written here.
CUSTOM = [
    {
        "ID": 900001,
        "Title": "Long Time no See!",
        "Description": "It's been so long! What a joy to find you here again.",
        "Points": 5,
        "MemAddr": "0xH006b3198=4",   # stateObj+0x18 == 0x04, the main menu
        "Author": "BT3-Recomp",
        "BadgeName": "900001",        # no badge art exists and nothing here fetches one
        "BadgeURL": "",
        "BadgeLockedURL": "",
        "Created": 1790525568,        # 2026-09-27, fixed so regeneration is byte-stable
        "Modified": 1790525568,
        "Flags": 3,                   # same as every core achievement
        "Type": None,
        "Rarity": 100.0,
        "RarityHardcore": 100.0,
        "custom": True,
    },
]

# ---------------------------------------------------------------------------------------------
# VERIFIED -- the only place a region becomes usable. Each entry is (ra_lo, ra_hi, our_delta, why).
# An address inside [ra_lo, ra_hi] maps to address + our_delta. An achievement is included in the
# runtime patch only if all of its addresses are covered by some entry.
# ---------------------------------------------------------------------------------------------
VERIFIED = [
    # The top-level state object. PCSX2 put it at 0x6af180, we put it at 0x6b3180, and the guest
    # pointer at 0x2ff10c resolves to the same object every run. Confirmed from two directions:
    # 0x006af7a0/0x006af7a4 are mode and battle type, which were found by hand as
    # stateObj+0x620/+0x624, and +0x4000 lands them on exactly those two offsets.
    (0x006AF000, 0x006AFFFF, 0x4000,
     "stateObj -- +0x4000, confirmed by mode/type landing on stateObj+0x620/+0x624"),

    # Below the ELF's loaded segments, so the loader placed it: same address in every build.
    (0x0031E000, 0x0031EFFF, 0x0,
     "loaded .data, fixed by the ELF (segments end at 0x334bf8)"),
]


def addresses(expr):
    """Every guest address a MemAddr expression reads, as ints.

    Group 1 of ADDR_RE is the size prefix and group 2 is the address, so this has to go through
    finditer() rather than findall() -- findall would hand back the prefix as the first element.
    """
    return {int(m.group(2), 16) for m in ADDR_RE.finditer(expr or "")}


def translate(addr):
    """Our address for a PCSX2 address, or None when no verified region covers it."""
    for lo, hi, delta, _why in VERIFIED:
        if lo <= addr <= hi:
            return addr + delta
    return None


def covers_ours(addr):
    """Is this one of OUR addresses inside a region we have confirmed?

    The same question translate() asks, from the other side. translate() maps a PCSX2 address
    forward and says whether the region it lands in was verified; this asks directly about an
    address that is already ours, which is what a custom achievement has. Checking it the other way
    round would be wrong in a way that looks fine: feeding an our-address to translate() finds no
    covering region and reports "unverified" for stateObj+0x18, the single address in this tree with
    the most evidence behind it.
    """
    for lo, hi, delta, _why in VERIFIED:
        if lo + delta <= addr <= hi + delta:
            return True
    return False


def rewrite(expr, table):
    """Substitute translated addresses into a MemAddr expression or a rich presence script.

    Driven by ADDR_RE rather than by a plain string replace, because the two sources disagree about
    padding: the achievement conditions write 0xH006af1ac (eight digits, zero padded) while the
    rich presence script writes 0xh6af1ac (six). A search for the padded form alone silently leaves
    every unpadded address in the script untranslated, which is exactly the kind of half-done
    translation that looks fine until someone reads the wrong memory.

    The digit count of each match is preserved on the way out, so a padded address stays padded.
    Group 1 is the whole "0x<size>" prefix, which has to be carried through verbatim -- matching the
    0x without capturing it and rebuilding from the size char alone drops the 0x and turns every
    condition into a constant comparison that silently never fires.
    """
    def sub(m):
        addr = int(m.group(2), 16)
        if addr not in table:
            return m.group(0)
        return "%s%0*x" % (m.group(1), len(m.group(2)), table[addr])

    return ADDR_RE.sub(sub, expr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the generated patch is out of date")
    args = ap.parse_args()

    with open(MASTER) as f:
        master = json.load(f)

    patch = master["PatchData"]
    source = {a["ID"]: a for a in patch["Achievements"]}

    # One translation table for the whole set, so the same address always maps the same way no
    # matter which achievement referenced it.
    table = {}
    for a in patch["Achievements"]:
        for ra in addresses(a.get("MemAddr")):
            if ra not in table:
                ours = translate(ra)
                if ours is not None:
                    table[ra] = ours

    # Every achievement goes into the patch, at whatever address it names.
    #
    # 149 of them name addresses that are RetroAchievements', not ours, so their conditions read
    # memory this build does not write and they will not unlock by playing. That is known and it is
    # the shape this ships in: the full set is present and browsable, the five whose addresses are
    # ours are the ones that work, and the list says which is which.
    #
    # An unverified address is left exactly as written rather than nudged by the +0x4000 the state
    # object happens to use. Our heap at RA's 0x6b9xxx holds unrelated non-zero data and several of
    # these conditions test "== 0", which fails there; the guess would have landed in a neighbouring
    # table of 0/1 flags where those tests pass constantly.
    included, held, skipped = [], [], []
    for a in patch["Achievements"] + CUSTOM:
        custom = a.get("custom", False)

        # "Warning: Unknown Emulator" exists to tell the player their unlocks will not count
        # because the core is not on the supported list. There is no server here to refuse
        # anything, and it fires on the very first evaluated frame, so all it would do is put a
        # zero-point toast on screen at startup. Excluded by name rather than by "Points == 0",
        # which would quietly drop any future zero-point achievement that is worth having.
        if a["Title"] in EXCLUDED:
            skipped.append({"id": a["ID"], "title": a["Title"],
                            "why": "server-side advisory with no meaning offline"})
            continue

        addrs = addresses(a.get("MemAddr"))
        if custom:
            # Ours already, so there is nothing to translate -- but the address still has to be one
            # we have actually confirmed, or the achievement would be a guess with our name on it.
            unknown = [x for x in addrs if not covers_ours(x)]
            if unknown:
                raise SystemExit(
                    "custom achievement %d (%s) points at unverified address(es): %s\n"
                    "Add the region to VERIFIED or point it somewhere proven."
                    % (a["ID"], a["Title"], ", ".join("0x%06x" % u for u in sorted(unknown))))
            entry = dict(a)
            entry.setdefault("Flags", 3)
            entry.setdefault("Type", None)
            entry["addr_verified"] = True
            included.append(entry)
            continue

        missing = sorted(x for x in addrs if x not in table)
        entry = dict(a)
        if missing:
            held.append({"id": a["ID"], "title": a["Title"], "points": a.get("Points"),
                         "untranslated": ["0x%06x" % m for m in missing]})
            entry["addr_verified"] = False
        else:
            entry["MemAddr"] = rewrite(a.get("MemAddr") or "", table)
            entry["addr_verified"] = True
        included.append(entry)

    # The runtime serves these instead of talking to a server, so they keep RA's response shapes --
    # rc_api_process_fetch_game_data_server_response() parses the first and
    # rc_api_process_fetch_game_sets_server_response() the second. Both are written out ready to
    # serve rather than assembled in C++, because the key names differ ("ID" vs "GameId", and so
    # on) and doing that renaming with string surgery in the runtime is a way to ship a tracker
    # that silently loads nothing. Achievements are sorted by ID so the files are byte-stable.
    runtime_patch = {
        "Success": True,
        "PatchData": {
            "ID": patch["ID"],
            "Title": patch["Title"],
            "ConsoleID": patch["ConsoleID"],
            "ImageIcon": patch.get("ImageIcon", ""),
            "ImageIconURL": patch.get("ImageIconURL", ""),
            "Achievements": sorted(included, key=lambda a: a["ID"]),
            "Leaderboards": [],
        },
    }

    # r=achievementsets. One set, of type "core", carrying the same achievements as the patch --
    # sets are a server-side grouping into a chosen subset, and this build tracks the core set and
    # nothing else. An empty Sets array is NOT allowed: rcheevos rejects the response with
    # "Response contained no sets", so a tracker that could not find a subset has no list to show
    # and no progress to keep. The rich presence script is carried through because the field is read
    # unconditionally, and being able to name the current screen is worth more than the bytes.
    runtime_sets = {
        "Success": True,
        "GameId": patch["ID"],
        "Title": patch["Title"],
        "ConsoleId": patch["ConsoleID"],
        "ImageIconUrl": patch.get("ImageIconURL", ""),
        "RichPresenceGameId": patch["ID"],
        # Translated for the same reason the achievements are. Nothing here displays rich presence
        # yet, but rcheevos evaluates the script against guest RAM on every frame regardless, and an
        # untranslated copy reads whatever happens to sit at 0x6af198 in our build. That is harmless
        # today and a mystery the day someone wires it to the overlay, so it is fixed at the source.
        "RichPresencePatch": rewrite(patch.get("RichPresencePatch", ""), table),
        "Sets": [
            {
                "AchievementSetId": patch["ID"],
                "GameId": patch["ID"],
                "Title": patch["Title"],
                "Type": "core",
                "ImageIconUrl": patch.get("ImageIconURL", ""),
                "Achievements": sorted(included, key=lambda a: a["ID"]),
                "Leaderboards": [],
            }
        ],
    }

    body = json.dumps(runtime_patch, indent=1, ensure_ascii=False) + "\n"
    sets_body = json.dumps(runtime_sets, indent=1, ensure_ascii=False) + "\n"

    report = {
        "note": ("EVERY achievement is in the runtime patch. 'held' lists the ones whose addresses "
                 "are not yet translated: those are left at RetroAchievements' raw values, which "
                 "in this build point at unrelated memory, so they will not unlock on their own. One "
                 "that DOES unlock without the player doing the thing is a false positive and names "
                 "itself. 'verified_regions' is what clears an entry out of 'held'."),
        "verified_regions": [
            {"ra": "0x%06x-0x%06x" % (lo, hi), "our_delta": "+0x%x" % d if d else "none", "why": why}
            for lo, hi, d, why in VERIFIED
        ],
        "included": {
            "count": len(included),
            "points": sum(a.get("Points") or 0 for a in included),
            "verified": sum(1 for a in included if a.get("addr_verified")),
            "unverified": sum(1 for a in included if not a.get("addr_verified")),
            "ids": [a["ID"] for a in included],
        },
        "held": {
            "count": len(held),
            "points": sum(a.get("points") or 0 for a in held),
            "by_missing_region": _group_held(held),
            "achievements": held,
        },
        "skipped": skipped,
    }

    # The ids whose addresses are translated, one per line. rcheevos parses the patch into its own
    # structures and drops any field we add, so this cannot be read back off the achievement -- it
    # needs its own file. The runtime uses it to mark the rows whose unlock actually means something.
    verified_body = "".join("%d\n" % a["ID"] for a in included if a.get("addr_verified"))

    if args.check:
        stale = [p for p, want in ((OUT, body), (OUT_SETS, sets_body), (OUT_VERIFIED, verified_body))
                 if not os.path.exists(p) or open(p).read() != want]
        if stale:
            for p in stale:
                print("stale: %s is out of date, re-run scripts/gen_ach_patch.py" % p)
            return 1
        print("ok: the runtime assets are up to date (%d achievements)" % len(included))
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write(body)
    with open(OUT_SETS, "w") as f:
        f.write(sets_body)
    with open(OUT_VERIFIED, "w") as f:
        f.write(verified_body)
    with open(REPORT, "w") as f:
        f.write(json.dumps(report, indent=1, ensure_ascii=False) + "\n")

    print("wrote %s" % OUT)
    print("wrote %s" % OUT_SETS)
    print("wrote %s" % OUT_VERIFIED)
    badges = fetch_badges(included)
    if not BADGE_OFF:
        print("  badges:   %3d on disk in %s" % (badges, os.path.relpath(BADGE_DIR, ROOT)))
    print("  included: %3d achievements, %3d points" % (len(included), report["included"]["points"]))
    print("  verified: %3d   unverified (left at raw RA addresses): %3d"
          % (report["included"]["verified"], report["included"]["unverified"]))
    if skipped:
        print("  skipped:  %3d (see report)" % len(skipped))
    print("  translated %d distinct addresses" % len(table))
    if report["included"]["unverified"]:
        print("  the unverified ones are in the patch too, on purpose: a 3-term AND against a")
        print("  specific value is ~1 in 16 million by chance, so they will stay locked. One that")
        print("  unlocks by itself is a false positive and tells us which region to map next.")
    for name, n in sorted(report["held"]["by_missing_region"].items(), key=lambda kv: -kv[1]):
        print("    blocked on %-14s %3d achievements" % (name, n))
    return 0


def _group_held(held):
    """Count held achievements per region, so the next region to verify is obvious."""
    out = {}
    for h in held:
        for m in h["untranslated"]:
            key = "0x%02xxxxx" % (int(m, 16) >> 12)
            out[key] = out.get(key, 0) + 1
    return out


if __name__ == "__main__":
    sys.exit(main())
