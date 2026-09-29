# NetMenu — the cut Dragon Net and its reconstruction

> This document covers the **ghost** (what row 4 did in retail) and the **state-hosting
> infrastructure**. For the final implementation of the new menu (entry, pad gate, AFS serve, audio,
> GS layer, 2D menu, icons) see **`docs/NEW-NETMENU.md`**.

Goal: a **native Dragon Net menu** (no host overlay) that opens from the hidden entry in the main
menu, shows a placeholder while it is being rebuilt, and allows **entering and exiting normally**
through the game's own logic.

## 1. The ghost: what row 4 did in retail

The hidden entry ("Dragon Net Battle", row 4 of the main menu) uses handler `0x3367A4`, which
**does not change state**: it loads the `fl_ok` resource (`func_336018(0, "fl_ok")`) and calls
`func_124F68(1,1)` → `func_124F88(1,1,0x40,0,0)` → `func_1245F0()` + `func_1252D8(1)`, and with the
resulting id indexes a **table of 76-byte records** (`19*id*4`) plus a `$gp` global.
In other words: **an effect/SE, no screen**. The Dragon Net screen was **cut**.

Additional evidence that it was cut:
- `data/DATA/PZS3US1/DragonNet_UP.pak` and `DragonNet_US.pak` exist but are **64 B stubs**
  (empty `.pak` header: `03 00 00 00` + four `0x40` + zeros).
- The entry is **hidden** in the menu builder (a loop that skips `$s1 == $t0`, with `$t0 = 4`
  hardcoded at `0x335568`) — see the `[bt3 patch: reveal-hidden-entry]` patch.
- There is no network code in the binary (0 references to netcnfif/smap/eenet/dev9/libnet).

## 2. State-hosting infrastructure (from `netmenutest`)

To bring in a screen of our own without an overlay:

1. **Relocated state table**: the state dispatch (`f_336a90`, `0x336c58`) reads `stateObj+0x18`,
   does `(state-1)`, `sltiu <0x46` and `jr`s through the table in RAM **`0x3B1130`**.
   The patches `[netmenutest patch: state-limit]` (`0x46 → 0x60`) and `state-table-hi/lo`
   (base `0x3B0000 → 0x01F80000`, offset `0x1130 → 0`) **relocate it to the top of RAM**
   (verified free: `0x01F1032D..0x01FAFFC2`). The runtime **always builds** that table
   (copies the 70 stock entries) even when the experiment is off, and it has **self-heal** if
   something clobbers it (the first version at `0x00D00000` was clobbered by the heap).
2. **Injected functions** (`runtime/ps2x_injected.h`): an `addr → C++ body` map consulted in
   `lookupFunction()` **before** the generated tables (the tables are dense: an address with no
   real code may have a placeholder that wins the lookup).
3. **Row 4 redirect** (`[netmenutest patch: row4-target]`): handler `0x3367A4` loads
   `$t2 = 0x0F`, `$t0 = 0x300000` (tail: `0x300000-0xEF4 = 0x2FF10C`), `$t1 = <state>` and falls
   through to the common tail `0x3364F4` (writes `stateObj+0x18` and calls `func_10D878`). With the
   env off it runs the stock code (`goto label_3367a8`).
4. **Pad seam**: BT3 **does not use libpad** — the pad the game sees is built in
   `writeNeutralPadPacket` (game_overrides). There we inject Cross/Circle
   (`ps2xNetMenuPress(mask, frames)`) and freeze the guest input (`ps2xNetMenuFreeze`).
5. **Duel fields** (`ps2x_duelfields`): `duelObj = *(0x3B38E8)` → `+0x110` vs mode,
   `+0x114` battle type, `+0x118` DP, `+0x13C` Duel Time; committed copies
   `stateObj+0x620/624/628/630`; time table in RAM `0x2C3480` = `[INF,60,90,180,240]`.

### Lessons (expensive, already paid for)
- An **unknown state** (`0x47`, outside `0x46`) makes the engine fall back to its *fallback*
  (Dragon Net flow → **broken fight**, both at 0 life) and during the FMV the overlay is not drawn
  (the movie present does not go through the UI callback). **That is why we host in a state the
  engine already knows.**
- The dispatch's `jr` has an **inline `case`** for the stock handlers: hooking the overlay table
  does **not** apply to the dispatch (that is why the entry points to an injected address).
- The state machine **loops while `$s2 == 0`** (`0x336E08: beqz $s2, 0x336C58`): the handler must
  set **`$s2 = 1`** to exit (otherwise: freeze / black screen).
- The body must end the dispatch with **`ctx->pc = $ra`** (like the stock handler's `jr $ra`).
- In `dispatchGuestBranch` there are **two** `if (!hasFunction(targetPc))`: injected functions must
  count as "present" in **both** (the `kind == Return` one and the *missing-function policy* one).

## 3. Host state: unused candidates

Those that today map to the generic handler `0x336E08` and do **not** appear as a target in the menu
tables (`0x3B1100`, `0x3B4510/30/40`) nor in the known `$t1` sites:

`0x45` (chosen), `0x44`, `0x43`, `0x39…0x42`, `0x34`, `0x33`, `0x2F`, `0x2E`, `0x2B`, `0x2A`,
`0x25`, `0x24`, `0x1F`, `0x0C`, `0x05`.

**Definitive verification** (pending, gated `[statewrite]` probe): log every write to
`stateObj+0x18` with the caller's `pc` during a playthrough → confirm the chosen state is never
entered; if it is, move on to the next candidate.

## 4. Env

`PS2X_NET_MENU=1` is **the only** switch: it replaces and unifies `PS2X_REVEAL_HIDDEN_MENU_ENTRY`
(the row reveal now reads `ps2x_netmenutest_enabled()`), `PS2X_NETMENUTEST`,
`PS2X_MENU_JUMP`/`PS2X_MENU_AUTO` and `PS2X_ENABLE_DRAGONNET`.

## 5. Phases

- **F1 (current)**: host state `0x45` + **native placeholder** (minimal host text, no page overlay):
  `New Dragon Net Menu / By RexxColder / Work in progress` + `(O) back` (Circle → the game's own
  transition to the main menu).
- **F2**: polish (repeated enter/exit, no freeze), and rewire subtypes/timing on top of the native
  base.
- **F3 (last)**: extract `DragonNet_UP/US.pak` from the **Wii version** (`WZS3US1.AFS`) and fill the
  64 B stubs (drop-in via the folder-backed AFS — see `docs/AFS.md`), with the PS2↔Wii format
  compatibility caveat.

## 6. References
- `docs/MAIN-MENU.md` (entry tables, states, Duel fields).
- `docs/AFS.md` (AFS pipeline and improvements).
- `docs/BT3-MODDING-RESEARCH.md` (unused functions + the J→USA method, formats, modes).
