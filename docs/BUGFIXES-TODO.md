# Bug fixes TODO

Source: user reports (Discord). Their wording is kept close to verbatim so nothing gets lost;
`->` lines are our notes / where to look.

Performance measurements (renderer x upscale, hardware) live in
[`docs/PERFORMANCE-METRICS.md`](PERFORMANCE-METRICS.md).

**Status:** 1, 2, 3, 5, 6 are not ours (other dev). 4, 7, 8, 9 are ours and are now closed —
see the "Closed" section at the bottom for what each one actually turned out to be.

---

## 1. Big audio delay (overlapping sounds)
There is a big audio delay, specially noticeable if you try to play one audio while another is
already playing. You can see so in the **character select**.
> -> audio scheduling/latency: the character-select screen fires many short SFX; check the SPU2
> mixing/queue depth and how a new voice is started while one is still playing.

## 2. Graphical corruption at the start and end of fights
Seen at the start and at the end of fights.
> -> already known/reported. Re-check with the OpenGL path; likely the transition frames
> (fade/wipe) around the fight intro/outro.

## 3. **Characters leak between fights** (the big one)
After playing one duel and then playing again with the **same characters**, some of the player's
characters were mixed with the enemy ones: got Android 17 and 18 doing Gohan attacks and having his
voicelines. Some attacks did connect (same type), others did not. For example
`L2 + Up + Triangle` is a projectile burst for both Gohan and Android 18, so that did hit. Sometimes
it was also impossible to charge Ki. The bug also results in **very wrong replay data**.
> -> character/side state is not reset (or is stale) between matches: the player slot ends up
> driving the enemy's move set / voice bank. Check per-character state and the player vs enemy slot
> assignment at match (re)start, plus anything cached per character id (game_overrides tables,
> voice/move lookups).

## 4. Aspect ratio differs between renderers
It used **4:3 by default on OpenGL**, but with Vulkan (which also switches to OpenGL) it uses
something close to **16:9 which doesn't cover the whole screen**.
> -> the reported Vulkan path falls back to OpenGL, so two different initial display setups end up
> with different aspect/scanout coverage. Check the default display setup and the letterbox/scanout
> region on both entry paths.

## 5. Enabling Glow (Kaioken aura) disables the music
For some reason enabling Glow seems to disable the music.
> -> looks like the glow effect's resource/pass setup is stepping on the audio (or on the frame the
> BGM stream is served on). Check the glow chain against the audio update path.

## 6. Micro freezes (frames pause and resume)
Windows builds (also reported on another build), and on Linux/OpenGL: "frames momentarily freeze
(one could say it pauses) and then it comes back".
> -> the known transient stalls: `[frameprof]`/`[ftspike]` with the diagnostics opt-in, plus the
> upload/readback paths (`[zpagewb]`, the upload budget) and the async-kick races.

## 7. Software renderer: weird purple screen before starting
The software renderer is very slow and shows a weird purple screen before it starts.
> -> looks like an uninitialised framebuffer / clear colour before the first frame.

---

## Closed

### 4. Aspect ratio differs between renderers — **fixed**
> It used **4:3 by default on OpenGL**, but with Vulkan (which also switches to OpenGL) it uses
> something close to **16:9 which doesn't cover the whole screen**.

### 6. Micro freezes (frames pause and resume) — **fixed**
> Windows builds (also reported on another build), and on Linux/OpenGL: "frames momentarily
> freeze (one could say it pauses) and then it comes back".

**The persistent texture cache.** Not the flush path — that was investigated and cleared.
The flush rewrote the whole file every 512 newly cached textures: 1.2 GB per flush, 5 flushes
and 5.8 GB over a 161 s session, and it never once stalled a frame. What it did cost was up
front: `[texcache] loaded 10752 entries (1621024768 bytes payload)` then
`init texcache 9094.9 ms`, so boot-to-first-frame was **9377.9 ms** on every launch.

The same A/B runs cleared the other two suspects, which is the useful part:

| run | result |
|---|---|
| default (cache on) | 0 real freezes (>100 ms), fmax max 52.1 ms |
| `PS2X_FRAMEGATE=0` | 4 freezes, fmax max 357.6 ms |
| `PS2X_GPUALIAS=0` | 0 freezes, max 46.9 ms — identical to baseline, mode 4 not the cause |

Default off, then removed outright (`4040850`, `523376f`). Boot to first frame: **275 ms**.

A measurement note worth keeping: the first pass counted anything over 33 ms as a freeze, and
reported ~0.3 hitches/s. Those were vsync jitter, not perceptible. Real freezes are >100 ms and
rare. The threshold was wrong before the conclusion was.

### 7. Software renderer purple screen — **fixed**

### 8. Characters/stages unlockable, red potaras in Custom 1 — **fixed**
> "all characters and stages unlockable, and red potaras in custom 1"

**Not a pnach.** The community `428113C2.pnach` (SLUS-216.78, our exact ISO) exists and covers
this, but its addresses are PCSX2 *memory* addresses, not guest RAM. The same function is
`0x201DC480` in PCSX2 and `0x000DD480` in the ELF. Writing them into `rdram` either lands on
valid-but-unrelated memory or corrupts something live.

What worked: a real save. `savedata\BASLUS-21678DBZT3\` held a 16 KB save that was 99.2% zeros —
the header and nothing else, because 40 of 43 `mc*` functions are stubs. Extracting
`BASLUS-21678DBZT3` from a `Mcd001.ps2` (a genuine 8.25 MB card) gave a save that is 22.2%
non-zero, and the game reads it: `[mclog] Read fd=4 dst=0x90f1f8 size=16384`.

The card parser follows `ps2mc.py` (Ross Ridge, public domain). The three things that cost time:
`page_size` is 512 not 528 (the rest is ECC), a dirent starts 64 bytes before its name, and
`read_fat(n)` maps to `fat_cluster = n // entries_per_cluster`.

**The freeze that came with it** was a separate bug, and the interesting one — see below.

### 9. Code-patched ISOs — **fixed** (`ec3c27d`)
Installer hint: `.bin`/`.elf` patch types do not work; the other patch types do.

---

## Overlay dispatch: NULL slots in the middle of a function body

Not a user report — found while chasing bug 8, and worth writing down because it will recur.

Loading a populated save froze the game with the main thread pegged at PC `0x334c40`, 100% of
samples with no second entry. That address is `f_334c00`, the overlay entry point
(`DBZP.BIN`'s `e_entry`).

`overlay_register.cpp` populates the overlay dispatch table from a static initializer, one entry
per *label* the generator detected:

```cpp
g_ps2OverlayFunctionTable[0]  = f_334c00_0x334c00;  // 0x334c00  entry
g_ps2OverlayFunctionTable[20] = f_334c00_0x334c00;  // 0x334c50  the one real label
```

Slots 1..19 are NULL. The host re-dispatches with `ctx->pc = 0x334c40` — mid-body, just before
the first `jal` — and the dispatcher resolves `(0x334c40 - 0x334c00) / 4 = 16`, finds NULL, and
falls into the gap handler, which re-dispatches the same PC. **Infinite host-side re-entry. The
guest never loops**: the function body `0x334c00-0x334c64` has no loop, it sets `$s0`, calls
`func_2AA120` and `func_10D8B0`, and returns. `[main] hot` pegged because of *that pc*, not
because of guest work.

**The fix** (`a6ddede`) fills slots 1..19 with a stub that sets `pc = 0x334c50` — the real label,
after the two `jal`s have been dispatched — so the pending call sequence completes.
`PS2X_OVMAIN=0` disables.

**This is a workaround, not the cause.** The generator is what leaves those slots NULL, and this
stub is armed at runtime, so a clean regeneration loses it. Any other overlay function with a
single label and NULL mid-body slots has the same latent freeze. `PS2X_OVLAZY` (see
`game_overrides.cpp`) reports NULL slots at boot so the next one shows up in the log instead of
as a freeze.
