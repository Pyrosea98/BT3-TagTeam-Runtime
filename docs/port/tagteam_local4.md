# Up to four local players

Branch `tagteam-exp`, 2026-10-16 (4aecb72, 0345abd, 4ed4ad2, 7ef380c). Everything in the game's source is inside
`#ifdef PORT`; the PS2 build matches.

## How to play it

Experiments tab, "Players on this computer": 3 or 4 (setting `local_players`). Then a Team Battle or Free-for-All
of the modes page (SELECT on the main menu) with more than two fighters, chosen for one player or for two (one
player's is made the two players' split-screen fight when it loads: side 2 on controller 2). Players 1 and 2 play
the two leads; controller 3 plays team 1's second member, controller 4 team 2's (a team without one leaves its
player the first fighter nobody plays).
The screen is in four quarters, one a player (with three players the fourth quarter shows a fighter nobody's
quarter shows). For tests, `BT3_HUMAN_PADS=<fighter>:<controller>,...` gives any extra fighter a controller, and
`BT3_TEST_SCREEN=1` now also splits the demo fight (`BT3_DEMO_MODE=0`).

## What the game has, and what was done

- Controllers: two ports at every layer (`PAD_COUNT` 2, `gPad` at a fixed address, the save's key config for two,
  the port's bindings for two). The port's per-player tables (port/src/gs/gs_input.c: bindings, controller slot,
  deadzones, vibration, rumble) are now four (settings `key_p3_*`, `pad_p3_*`, `padslot_p3`, ... ; controller 3 and
  4 have no keyboard keys by default). Controllers 3 and 4 are two port-only `Pad`s (`gPortPad`, src/sys/pad.c):
  once a frame, while a fight has a fighter on one, they are put in `gPad`'s places, the game's own `Pad_Update`
  runs for them, and everything goes back. A fighter's pad number 2 / 3 means one of them (`Port_Pad`,
  `BtlChar_GetPad`).
- A fighter is a player's when it has a controller (`Port_FightPad`): no computer (`injectOn` 0), its pad, player
  1's key config (the save has two; it read past the table for fighter 2 on). Targeting, spectating (R3 on the
  player's own controller), "about a player" (the cut-in and freeze rules) count every such fighter.
- Views: the game has two (`BtlCam.views[2]`, `BTL_OBJ_VIEW_MAX` 2, per-view effect arrays of two). Not made
  larger (the decompiled code reaches into these structures by offset in places). Instead the frame is drawn with
  the two views twice (battle.c, `Port_DrawQuad`; btl_cam.c, `Port_QuadCams` / `Port_QuadViews`): views 0 and 1
  are the top quarters in the update, then the bottom ones while drawing, then the top ones again. The order is
  the game's: the stage of all four first, then the fighters (top pair, bottom pair, visibility made again for
  each), then the effects. Drawing a stage after a view's fighters showed noise for its textures (the fighters'
  textures had replaced them in VRAM).
- A quarter is the full screen's projection at half size (half the screen distance, centred in the quarter,
  scissor 256 x 222): each player sees what a full screen shows.
- The display lists (src/sys/dma.c) are 4 MB each from the port's region: a two-view frame of ten fighters takes
  ~770 KB of the game's 1 MB, and four views ran over into the heap after it (the objects' memory: a crash in
  `ObjDraw_DrawParts`). A list that ever runs past its buffer now stops the game with a message.
- HUD: each quarter has its fighter's gauge in its top corner: the top ones are the game's two
  (`Port_HudFighter`), the bottom ones the same gauge tree drawn half the screen lower with their own animation
  state (hud_b.c, `HudExtra_DrawGauge`, the small gauges' mechanism at full size). No rows of small gauges. Each
  player's target marker is projected through its quarter's camera (`Port_QuarterProject`, a view made for the
  question so that nothing is loaded for drawing in the middle of the HUD).
- A fighter's camera asking for the screen keeps to its quarter; a technique's demo-camera cut (one camera, the
  whole screen) is not started. The intro still uses the whole screen.

## Not done / known

- Not played with four real controllers yet.
- The pause menu, the settings window's controller pages and the results answer to players 1 and 2.
- The combo counters and prompts are the top quarters' only.
- Four views cost about twice the drawing of the game's split screen.
- Online: never (extra fighters are off in an online match).
