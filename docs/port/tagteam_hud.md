# The third-fighter experiment: a HUD for the fighters past the two sides

Branch `tt-hud` (from `tagteam-exp` b2146de), 2026-10-14. Everything in the game's source is inside `#ifdef PORT`.

## What the game has

- The fight HUD is seven parts under one manager (`gHud`, src/battle/hud_a.c): gauges, timer, team panel, notice,
  combo counter, prompts, caption. `Hud_PreUpdate` copies the fighters' state into the parts once a frame in a loop
  `for (side = 0; side < 2; side++)`; `Hud_Draw` updates and draws the node trees.
- The gauge part (health, ki, blast stock, powered-up timer, status icons, face; src/battle/hud_a_d.c, hud_b.c,
  hud_c.c) is ONE tree of 12 nodes over 76 sprites, built once and shown twice a frame: `HudGauge_SelectSide(0)`,
  update, draw, then the same for side 1 mirrored (x' = 511 - x). Everything that differs by side is an array of
  two in the work (`HudBWork`, 0x288 bytes), indexed by `gHudGauge->side`.
- A health bar is texture 8 of sheet 0 (160 x 16 texels at (0, 0)); the palette is another entry of the sheet
  (8 + sub): sub 3 for the last bar, 4 with one full bar under it, 1 with two to six, 5 with seven or more, 2 the
  flat colour of the trailing (damage) bar, which the game cuts to the bar's shape with an alpha mask. Health is
  shown in bars of 10000; ki in five lamps of 20000 (texels (16, 16)-(30, 33), sub 1); one marker per health bar
  in reserve (texels (0, 16)-(16, 32), sub 1), six at most.
- The face is texture 0 (left place) or 1 (right place) of the fighter object's own sheet (object + 0x50), sent to
  VRAM blocks 0x2C00 / 0x2CD0 every frame.
- The by-side setters (`HudGauge_Set*`, `HudCombo_Set*`, `HudPrompt_Set*`, `HudTeam_Set*`) are called only from
  `Hud_PreUpdate`'s loop of two and from inside the HUD files; nothing outside hands them a fighter number. So
  the third fighter does not write past these tables today, and nothing here was changed to call them with one.

## What was added

`HudGauge_DrawExtras` (end of src/battle/hud_b.c), called by `Hud_Draw` right behind the second pass of the gauge
tree (the normal branch and replay mode 1). For every roster index `i` from 2 to `BtlChar_GetCount() - 1` (a loop,
not a special case of 2) it draws one small row with the game's own sprite library (`HudSprite_*`, `HudNode_Draw`)
from the same sheet:

    face 14 x 14 | health bar 80 x 8 (the side's bar at half size)
                 | five ki lamps            markers of the health bars in reserve

- Where: under the gauge of the side the fighter is teamed with (`Port_Team(i)`: team 0 left, team 1 right; a team
  of its own, 2, goes under player 2's, right). The first row is at (8, 68) of the 512 x 448 screen in left-hand
  coordinates; rows are 17 apart, four to a column, a second column 106 further in: eight rows fit under one side
  and end at y = 135, above the combo counter. Right-hand rows are mirrored by `HudNode_Draw` like the side's
  gauge; the place goes through `HUD_SCR` (the online swap of sides) and the widescreen marker of its side is set
  for each row. The rows leave the screen with the whole part (`slideAll`).
- Health reads as the side's: 10000 a bar, the palette by the number of bars under the one on show, the bar under
  it drawn behind, a trailing bar with the side's speeds (100 a frame, 120 / 150 / 200 for larger gaps). The
  trailing bar is laid as two rectangles (lower half up to the step at 64 of 160, full height behind it) instead of
  through a mask. Health that is not zero shows at least 2 pixels.
- Knocked out (health 0 or `BtlCtrl_IsActiveDead`): the bar is empty (only the dark shape of it is left) and the
  face, lamps and markers are drawn dark (0x30 of 0x80).
- State: `sHudExtraShown[PORT_FIGHTER_MAX]`, `sHudExtraDrain[PORT_FIGHTER_MAX]` (trailing health by roster index,
  `PORT_FIGHTER_MAX` = 10, defined in hud_b.c), cleared by `HudGauge_ResetExtras` from `Hud_Reset`. The rows taken
  per screen side are counted in a local `used[2]` whose index is 0 or 1 by construction.
- With two fighters the loop does not run: no draw, no marker, no texture upload is added to the frame.

Testing switches (port/src/plat_stub.c):
- `BT3_DEMO_HUD=1`: the attract demo (battle mode 7) draws the fight HUD instead of its caption. Without it a
  `BT3_DEMO` fight shows no gauges at all, so there was nothing to photograph.
- `BT3_HUD_ROWS=<1..8>`: each extra fighter's row is drawn that many times, to see a full stack (the trailing bar
  then steps that many times a frame: for looking at the layout only).

## Checked (2026-10-14, Linux, clang64 build)

1. Build: `link OK`, 0 error lines.
2. Replay regression: last line `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0; port/build/heap_2111.bin` (the
   unmodified tagteam-exp build prints the same line, with the same file name behind it).
3. Windowless demo fights, third fighter 3 107 60 24 0 12 55 100 130 150 x team 0 / 1 / 2, 45 s limit, own empty
   saves directory: 30 of 30 print `bt3: battle finished`. The same 30 again with `BT3_DEMO_HUD=1` (so that the new
   code runs: the plain demo never draws gauges): 30 of 30.
4. Pictures (offscreen GPU renderer, 4:3, `BT3_DEMO_HUD=1`, third fighter 3, `BT3_TEST_HP=0,0,2500`), looked at:
   team 0 the row is under player 1's gauge, teams 1 and 2 under player 2's; the trailing bar falls from full to
   the quarter bar, the bar shrinks as the fighter is hit, and after the knock-out the row is dark and empty.
   Also looked at: `BT3_HUD_ROWS=8` on team 2 (two columns of four on the right), `BT3_HUD_ROWS=4` on team 0 at
   `BT3_ASPECT=16:9` (rows stay at the left edge, clear of the combo counter), 14000 health (two bars: one marker,
   the bar under shows through).
5. No third fighter, picture for picture (every 100th frame, 23 pictures a run, byte comparison of the PPM files):
   - plain demo, unmodified tagteam-exp build against this one: 23 identical, 0 different;
   - `BT3_DEMO_HUD=1`, the build with only that switch (commit cee3f8b) against this one: 23 identical, 0 different;
   - two runs of this build against each other (is a run repeatable at all): 23 identical.
6. PS2 side: src/battle/hud_a.c with the `#ifdef PORT` branches taken out is the same text as at tagteam-exp;
   src/battle/hud_b.c differs by one empty line at the end of the file (in front of the new `#ifdef PORT` block).
   The PS2 matching build itself was NOT run.

## Not checked, and known limits

- A fight started from the menus (battle mode other than 7), a replay shown with HUD mode 1, split screen, the
  pause, the online swap of sides (`Port_HudSwap`), the "long bars" widescreen layout: the code paths are there
  but no picture of them was taken. Only 4:3 and 16:9 were looked at.
- More than one extra fighter: the roster cannot hold one yet. The stack was only seen with the same fighter's
  row repeated.
- Team battles: the team panel (stand-by member's face and switch gauge, node at (12, 66)) lies where the first
  rows are. Not handled.
- The row has no blast stock, no powered-up timer, no status icons, no shake on a hit and no low-health flash.
- The face is sent to the same VRAM blocks as the side's face of that place, after the side's has been drawn; it
  is sent again every frame for every row. Fine in the pictures taken with the GPU renderer; the software and GL
  renderers were not looked at.
- A fighter on a team of its own is put on the right however many there are: a ten-fighter free-for-all gives
  eight rows on the right (they fit), none on the left.
- `BT3_SHOT_FROM` / `BT3_SHOT_TO` runs gave no pictures in two tries here (not looked into; all pictures above
  were taken without them).

## Tables of two in the HUD code (what would have to grow for ten fighters)

By side unless said otherwise. A fighter past the two sides does not reach any of them today.

- Gauge part, `HudBWork` (src/battle/hud_b.c; the same layout again as `HudCGauge` in src/battle/hud_c.c and the
  head of it as `HudGauge` in include/battle/hud_a.h): `shake[2][3]`, `hpShown[2]`, `hp[2]`, `auraAlpha[2]`,
  `lowHpPulse[2]`, `blast[2]`, `ki[2]`, `kiShown[2]`, `kiBarFlash[2]`, `kiFullPulse[2]`, `maxPower[2]`,
  `blastFlash[2]`, `statMask[2]`, `statPos[2]`, `kiReserve[2]`, `statPrev[2]`, `statTo[2][4]`, `statFrom[2][4]`,
  `statRamp[2]`, `statState[2]`, `hpBarFade[2]`, `faceRes[2]`, `obj[2]`, `switching[2]`, `slide[2]`, `hpDrain[2]`,
  `kiDrain[2]`, `kiReservePulse[2]`. Loops of two: `HudGauge_ClearFaceTexMark`, `HudGauge_Init` (two),
  `HudGauge_Reset` (hud_c.c). The tree itself is one set of 76 sprites shown once per side, and side 1 is "mirror
  on": a third pass would need its own position, not only its own slot.
- Team panel, `HudTeam` (include/battle/hud_a.h, src/battle/hud_a_b.c): `hp.cur[2]`, `hp.max[2]`, `sw.gauge[2]`,
  `sw.reserve[2]`, `faceRes[2]`, `flash[2]`, `shown[2]`, `slide[2]`, `switching[2]`, `flipA[2]`, `flipB[2]`,
  `flipDone[2]`, `target.cur[2]`, `target.prev[2]`, `prevHp.cur[2]`, `prevHp.max[2]`; the loop in `HudTeam_Reset`.
- Combo part, `HudCombo` (src/battle/hud_c_b.c): `msgState[2]`, `dmgState[2]`, `hitState[2]`, `textState[2]`,
  `msg[2]`, `damage[2]`, `damageFrom[2]`, `hits[2]`, `hitsRaw[2]`, `hitsPrev[2]`, `text[2]`, `msgRamp[2]`,
  `textRamp[2]`, `dmgRamp[2]`, `hitRamp[2]`, `countRamp[2]`, `lastDigit[2]`, `textPos[2][2]`, `textAlpha[2]`;
  three loops of two.
- Prompt part, `HudPrompt` (include/battle/hud_e.h, src/battle/hud_e_b.c): `btnState[2]`, `btnPulse[2]`,
  `btnRamp[2]`, the button's `value[2]`, `icon[2]`, `flip[2]`, `kind[2]`, the command row's `on[2]`, `state[2]`,
  `cmdRamp[2]`, `count[2]`, `icon[2][4]`, `namePos[2]`, `nameIdx[2]`, `btnBlink[2]`, `cmdBlink[2]`, `nameFade[2]`,
  `btnGroup[2]`; three loops of two. (The second side's prompts are only drawn in split screen.)
- Timer (src/battle/hud_e_c.c): a loop of two over its two nodes, not by side.
- Manager (src/battle/hud_a.c): `Hud_PreUpdate`'s `for (side = 0; side < 2; side++)`, which also shakes "the other
  side's" gauge as `side == 0`; `Hud_Draw` selects sides 0 and 1 by hand in each of its four branches.
- Pause menu (include/battle/hud_0.h): `text[2]` per controller port and `list[2]`: by pad, not by fighter.
- Behind the HUD: every `BtlSide_*` / `BtlCtrl_*` query it uses goes through `BtlChar_FindBySide`, which the
  experiment already answers by roster index for a number past 1; `BtlCtrl_IsActiveDead` takes the roster index.

For ten fighters the cheap way on is the one taken here: leave the two-sided parts alone and give every further
fighter a row from its own state by roster index. The combo counter and the prompts belong to a pad, so they only
need to grow if more than two people play.
