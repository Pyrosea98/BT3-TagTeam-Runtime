# BT3 main menu / states — complete map

Status: **mechanism verified**, screen names mostly **hypotheses to verify**
(by triggering each state with `PS2X_MENU_JUMP`). Last update: 2026-09.

Everything here is for the `DBZP.BIN` overlay (USA, `SLUS_216.78`). Address conversion:

```
file offset in DBZP.BIN = RAM - 0x334C00        (the overlay loads at RAM 0x334C00)
```

## 1. Main menu mechanism

When a main-menu row is confirmed:

1. The engine reads the active row index and jumps through the **jump table**:

   ```
   0x33643C  lw   $a1, 0xE80($s1)        ; $a1 = menuObj = [0x3B0E80]
   0x336440  lw   $v0, 0x10C($a1)
   0x336444  lw   $v1, 0x148($a1)
   0x336448  lw   $a0, 0x144($a1)
   0x33644C  addu $v1, $v1, $v0
   0x336450  addiu $v1, $v1, 1
   0x33645C  div  $v1, $a0              ; row = (cursor + 0x148 + 1) % 0x144
   0x33646C  lw   $v0, 0x118($v0)        ; menuObj->0x118[row] = index 0..10
   0x336470  sltiu $v1, $v0, 0xB         ; 11 rows
   0x336488  lw   $v1, 0x1100($v1)       ; jump table at RAM 0x3B1100
   0x33648C  jr   $v1
   ```

2. The **row handler** loads `$t1` = **target state** and jumps to the shared tail.

3. **Shared tail `0x3364F4`** (the runtime's `bt3MenuGoto` equivalent):

   ```
   0x3364F4  lui   $a1, 0x3B
   0x3364F8  lw    $v0, 0x108($v1)
   0x336504  ori   $v0, $v0, 1
   0x336508  sw    $v0, 0x108($v1)       ; menuObj->0x108 |= 1
   0x336510  lw    $v0, 0x108($a0)
   0x336514  ori   $v0, $v0, 2
   0x336518  sw    $v0, 0x108($a0)       ; |= 2
   0x336520  sw    $t2, 0x110($v1)       ; $t2 = 0x0F
   0x336524  lw    $v0, 0xF10C($t0)      ; $t0 = 0x2F0000 -> *(0x2FF10C) = stateObj
   0x336528  sw    $t1, 0x18($v0)        ; stateObj+0x18 = target state
   0x336530  jal   func_10D878           ; "go to screen N"
   ```

`func_10D878` (RAM `0x0010D878`) is a **generic "go to screen N"** (nearly every overlay
screen uses it: ~260 call sites). The menu object `[0x3B0E80]` **only exists while the
main menu (0x04) is on screen**: the game frees it on exit.

## 2. Main menu row table

| row | jump-table (RAM) | handler (RAM) | target state |
|----|------------------|---------------|--------------|
| 0 | `0x3B1100` | `0x336498` | `0x06` |
| 1 | `0x3B1104` | `0x3364B0` | `0x0D` |
| 2 | `0x3B1108` | `0x3364C8` | `0x21` |
| 3 | `0x3B110C` | `0x3364E0` | `0x26` (Duel) |
| 4 | `0x3B1110` | `0x3367A4` | — **special** (hidden) |
| 5 | `0x3B1114` | `0x336570` | `0x30` |
| 6 | `0x3B1118` | `0x3365B8` | `0x2C` |
| 7 | `0x3B111C` | `0x336600` | `0x35` |
| 8 | `0x3B1120` | `0x336648` | `0x3C` |
| 9 | `0x3B1124` | `0x336690` | `0x3E` |
| 10 | `0x3B1128` | `0x336720` | `0x46` |

The `$t1` for handlers 0..3 are at `0x3364AC` (0x06), `0x3364C4` (0x0D), `0x3364DC` (0x21),
`0x3364F0` (0x26). For rows 5..10: `0x3365B0` (0x30), `0x3365F8` (0x2C), `0x336640` (0x35),
`0x336688` (0x3C), `0x3366CC` (0x3E), `0x33675C` (0x46).

Row **4 does not jump to any state**: it calls `func_336018(0, "fl_ok"@0x3B10E8)` and
`func_124F68(1,1)`. It is the hidden entry (Network Battle / Dragon Net) — see
`PS2X_REVEAL_HIDDEN_MENU_ENTRY` and `PS2X_ENABLE_DRAGONNET`. It is the one that does nothing on
confirm (it lacks the network backend, which the game does not ship).

Row assets/anims: `fl_on_start` (`0x3B10A0`), `fl_off_start` (`0x3B10B0`), `up` (`0x3B10D8`),
`fl_ok` (`0x3B10E8`).

## 3. Screen states (`*(*(0x2FF10C)+0x18)`)

Live state: `mainPtr = [0x2FF10C]`; the state is `mainPtr+0x18`.

| state | name (hypothesis) | who sets it | notes |
|-------|-------------------|-------------|-------|
| `0x01` | BOOT | — | FMV + title + splash logos |
| `0x04` | MAIN_MENU | — | main menu; `[0x3B0E80]` lives here |
| `0x06` | LOADING | `0x3364AC` | row 0 (Dragon History) |
| `0x07` | ? | `0x340A08`, `0x3990D4` | |
| `0x0B` | ? | `0x35D45C` | |
| `0x0D` | ULTIMATE_BATTLE | `0x3364C4`, `0x35D450` | row 1 |
| `0x0E` | ? | `0x35D46C` | |
| `0x10` | ? | `0x369084` | |
| `0x11` | ? | `0x3690E0`, `0x3A2AA4` | |
| `0x1A` | ? | `0x3690E8` | |
| `0x1D` | ? | `0x3691B4` | |
| `0x20` | ? | `0x3691D8` | |
| `0x21` | DRAGON_WORLD_TOUR | `0x3364DC` | row 2 |
| `0x26` | DUEL_MENU | `0x3364F0` | row 3 — **verified** |
| `0x27` | CHARACTER_SELECT | (duel module) | |
| `0x28` | PREFIGHT_SETUP | — | |
| `0x29` | PREFIGHT_SETUP | — | |
| `0x2C` | ULTIMATE_TRAINING | `0x3365F8` | row 6 |
| `0x2D` | IN_FIGHT | — | |
| `0x30` | EVOLUTION_Z | `0x3365B0` | row 5 |
| `0x35` | DATA_CENTER | `0x336640` | row 7 |
| `0x38` | POST_FIGHT | — | |
| `0x3C` | CHARACTER_REFERENCE | `0x336688` | row 8 |
| `0x3E` | OPTIONS | `0x3366CC` | row 9 |
| `0x46` | EXTRA? | `0x33675C` | row 10 |
| `0xA4` | ? | `0x34248C`, `0x3494C4` | |

The **row→name mapping** comes from crossing the real BT3 main menu order with the row index.
Only row 3 (`0x26` = Duel) is verified at runtime; the rest are hypotheses to confirm by
triggering each state.

Real main menu order (retail): Dragon History → Ultimate Battle → Dragon World Tour →
Duel → *(Network Battle, hidden)* → Evolution Z → Ultimate Training → Data Center →
Character Reference → Options.

## 4. Duel (row 3, state `0x26`)

Real Duel menu structure (2 levels):

- **Level 1** (Duel menu options): `1P VS COM` · `1P VS 2P` · `COM VS COM` · `Battle Settings`.
- **Level 2** (only in the first 3): `Single Battle` (1v1) · `Team Battle` (teams ≤ 5) ·
  `DP Battle` (team ≤ 10 / 15 / 20 Destruction Points).
- **Battle Settings**: Duel Time (60/90/120/180/240/∞) · COM Level (Very Weak→Very Strong) ·
  Referee (Ox King / Videl / Supreme Kai / Shenron / Announcer 1-3) · Transformations (1P·2P ON|OFF) ·
  Map Setting (ON/OFF) · Default.

Known fields (duel object `duelObj = [0x3B38E8]`):

| offset | meaning |
|--------|---------|
| `+0x110` | opponent mode: 0 = 1P vs CPU, 1 = 1P vs 2P, 2 = CPU vs CPU |
| `+0x114` | battle type: 0 = Single, 1 = Team, 2 = DP (provisional) |
| `+0x118` | DP limit (10 / 15 / 20) |
| `+0x13C` | time limit |
| `stateObj+0x620` | copy of the opponent mode (committed on confirm) |
| `stateObj+0x624` | copy of the battle type |
| `stateObj+0x630` | copy of the DP limit |

## 5. Other modes (menu tree)

- **Dragon History**: Select Scenario → Select Difficulty (Easy/Normal/Hard).
- **Ultimate Battle**: Sim Dragon (Start / Battle Record / Explanation) · Mission 100 ·
  Survival (Bardock War / Dragon Classic / All-Star) · Disc Fusion (BT1 / BT2).
- **Dragon World Tour**: World Tour (World Tournament / World Martial Arts Big Tournament /
  Cell Games / Otherworld Tournament / Yamcha Game) and Free (the same 5), levels 1-2-3.
- **Evolution Z**: Character Customize (Custom 1/2/3, Equip Item, Password, Distribute Z Points) ·
  Item Shop (Item Exchange, Item List) · Evolution Z Explanation.
- **Ultimate Training**: Training · Battle Training (Basic/Intermediate/Advanced).
- **Data Center**: Password Input Screen · Fighter Catalog · Replay.
- **Character Reference** · **Options** (Save/Load, Screen, Sound, Controller, Exit).

## 6. How to verify / use

Runtime (`ps2_runtime.cpp`, `[bt3state]` block):

- `PS2X_STATE_NAMES` (ON by default, `=0` turns it off) → reports every transition:
  `[bt3state] 0x3e OPTIONS (was 0x04 MAIN_MENU)`.
  In Duel states it adds the subtype read live:
  `[bt3state] 0x26 DUEL_MENU vs=1P_VS_CPU type=SINGLE dp=0xffffffff`.
- Inside the main menu (`0x04`), the same switch reports which entry the cursor points at, with
  the exact address the game would use on confirm (the game's own formula,
  `0x33643C..0x33648C`):

  ```
  [bt3cursor] row=3 idx=3 entry=0x3B110C handler=0x3364E0 target=0x26 Duel (verified) sel=0x3
  ```

  where `row = (menuObj+0x10C + menuObj+0x148 + 1) % menuObj+0x144`,
  `idx = *(menuObj + 0x118 + 4*row)`, `entry = 0x3B1100 + 4*idx`, `handler = *(entry)`,
  `sel = *(mainPtr+0x2C)`. Only prints when the row changes.
- `PS2X_MENU_JUMP=<decimal state>` + `PS2X_MENU_AUTO=1` → from the main menu runs
  `bt3MenuGoto(state)` (triggers the real transition). Verified: `38` (0x26) enters Duel.
  Can also be triggered with the **P+L** key combo or **left+right click**.
- `PS2X_REVEAL_HIDDEN_MENU_ENTRY=1` → reveals row 4. `PS2X_ENABLE_DRAGONNET=1` → writes
  `0x600` at `0x33408C` so it passes the confirm gate (without the network backend it still
  does nothing).

Pending: trigger `0x06 / 0x0D / 0x21 / 0x30 / 0x2C / 0x35 / 0x3C / 0x3E / 0x46` one by one and
note the screen that comes up → definitive mapping. Then extend `[bt3state]` with each state's
subtype (not just Duel).

## 7. Other data

- `dispPtr = [0x2FF28C]`: `+0x08` display_filter, `+0xA0C` frame_counter.
- `mainStruct = [0x3B38D8]`: `+0x3BC` globalFlags (bit 3 = submenu), `+0x9A4` subStruct,
  `subStruct+0x40` menuState (9 = DISPLAYED, 10 = TRANSITIONING).
- `itemBase = [0x3B38E8]`: `+0x12C` cursor, `+0x138` selection, `+0x13C` itemState.
  itemState 0..8 = PLATE_LOAD, SECOND_PASS, REFERENCE_COUNTER, ANIMATION, CONFIRM_ACCEPT,
  NAVIGATION, CHARACTER_SELECT, VISUAL_RENDER, FINAL_CONFIRM.
- Menu entry handlers (table at RAM `0x3B4290`, file `0x07F690`):
  `0x34DD08, 0x34EA70, 0x34F040, 0x34F630, 0x34F700, 0x34F738, 0x350168, 0x3509B0, 0x351388`.
- Types (RAM `0x3B0FA0`): `[4,4,4,5,5,5,5,4,5,5,4]`.
- Dispatch by type (RAM `0x3B3EF0`): `0x34A040, 0x34A088, 0x34A0D0, 0x34A258, 0x34A230,
  0x34A110, 0x34A158, 0x34A1A0, 0x34A230, 0x34A1E8, 0x34A258`.
- Plate strings: `mc_menu_plate_%d`, `mc_menu_text_on/off`, `mc_icon_off/play`,
  `mc_menu_plate_7`, `mc_menu_yajirusi_up/down`, `mc_dragonball`, `mc_bg_cloud`.

### How this was generated

- The jump table and handlers come from dumping `DBZP.BIN` (see §addr. base) and from the
  generated code in `ps2xRuntime/src/runner_overlay/overlay_functions.cpp` (which carries the
  disassembly in comments).
- The real menu tree comes from a translation of BT3's retail menu and the game's wikis
  (Duel: 3 opponent modes x 3 battle types + Battle Settings).

## 8. Duel setup fields — `ps2x_duelfields` (single source)

Module `ps2xRuntime/{include/runtime,src/lib}/ps2x_duel_fields.{h,cpp}`: all the addresses,
the values, the combinations and the **full write** (what the backend wire will use).

### Addresses (guest RAM)
| field | address | values |
|-------|---------|--------|
| `duelObj` | `*(0x3B38E8)` | Duel module object (lives only in `0x26`) |
| vs mode | `duelObj+0x110` | 0 = 1P vs CPU, 1 = 1P vs 2P, 2 = CPU vs CPU, 3 = Battle Settings |
| battle type | `duelObj+0x114` | 0 = Single, 1 = Team, 2 = DP |
| DP variant | `duelObj+0x118` | 0 = 10, 1 = 15, 2 = 20 |
| time limit / Settings row | `duelObj+0x13C` | time limit; 0..6 as the cursor in Settings |
| vs copy | `stateObj+0x620` | `*(0x2FF10C)+0x620` (commit `0x356234 -> 0x356238`) |
| type copy | `stateObj+0x624` | read by char-select at `0x352DA8` |
| extra | `stateObj+0x628` | (netjump tracks it too) |
| DP copy | `stateObj+0x630` | commit at `0x35625C` |

### Combinations
The **5 battle subtypes** (for any mode): `SINGLE`, `TEAM`, `DP10`, `DP15`, `DP20`
= `(type, dp)` in `{Single/Team/DP} x {10/15/20}` (dp only applies with `type=DP`).
The **3 opponent modes** + Settings: `1P_VS_CPU`, `1P_VS_2P`, `CPU_VS_CPU`, `BATTLE_SETTINGS`.
`Battle Settings` has 6 rows: DUEL_TIME, COM_LEVEL, REFEREE, TRANSFORMATIONS, MAP_SETTING, DEFAULT
(the **value** of each row is not mapped yet; the cursor is `duelObj+0x13C`).

### API
```cpp
ps2x_duelfields::Fields f = ps2x_duelfields::subtype(VsMode::P1VsP2, BattleType::DP, DpVariant::DP15);
ps2x_duelfields::writeDuelObject(rdram, f);   // every frame until confirm (the menu overwrites the cursor)
ps2x_duelfields::writeCommitted (rdram, f);   // re-assert at 0x27/0x28
ps2x_duelfields::apply         (rdram, f);    // both halves (the "full write")
ps2x_duelfields::read(rdram); ps2x_duelfields::format(f, buf, n);
```

### How it is used today
The netmenu's `start` (`Jump -> Stamp -> Advance -> Hold`) uses these methods: `Stamp`/`Advance`
write the duel object, `Hold` re-asserts the copies at `0x27/0x28` and releases on exit.
The confirm is injected through the only seam the game reads: `writeNeutralPadPacket`
(game_overrides), via `ps2xNetMenuPressCross()` (BT3 does not call libpad).
