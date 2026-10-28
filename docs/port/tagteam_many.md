# Ten fighters at once

Notes of the branch `tt-many` (from `tagteam-exp` at b00e7d4), 2026-10-14. Before this the fight carried exactly
one extra fighter, "the third". Now it carries up to eight (ten fighters in all): two teams of five, a free-for-all
of ten, or anything between. Everything in the game's source is inside `#ifdef PORT` (or a macro whose PS2
definition is the original text); the PS2 matching build was run on the result and matches.

## The interface

### One number

`port/include/port_fighters.h` (new; C and C++, included by the game's files inside `#ifdef PORT` and by
`port/src/plat_fighters.h`):

| name | value | what |
|---|---|---|
| `PORT_FIGHTER_MAX` | 10 | fighters a fight can have; every table kept by fighter is this long |
| `PORT_EXTRA_MAX` | 8 | extra fighters (roster indices 2..9); the settings rows, the modes page's count, the Experiments tab are held to it |
| `PORT_TEAM_MAX` | 10 | teams are numbered 0..9: 0 player 1's, 1 player 2's, 2.. further teams |

The game's own headers cannot include a port header in the PS2 build, so a few tables in them carry the number
written out (`BTLAI_SIDES 10`, `AITH_SIDES 10`, `PORT_FIGHTERS 10` in btl_char_flag_opp.c, `PORT_SEQ_TEAMS 10` in
btl_seq.c, `level[10]` in BtlChars_UpdateFreeze), each with a comment naming the constant. Tables indexed by OBJECT
id (a fighter's object id is its roster index; partners' and effects' objects come after) are 12 or 20 long, the
object table's size, see below.

### Who the extra fighters are (port/src/plat_fighters.c)

The battle code asks, for the fight being loaded or fought:

    int Port_FightExtras(void);   how many (0..8); extra k is roster index 2 + k
    int Port_FightChara(int k);   its character
    int Port_FightCostume(int k);
    int Port_FightTeam(int k);    its team 0..9
    int Port_FightLevel(int k);   computer level 0..29, -1: the level of player 2's computer (13 where there is none)
    int Port_FightMember(int k);  side * 8 + index of the battle setup's member it was made from, -1: none
    int Port_FightRoster(void);   0, or the fight is of the sides' own team rosters: 1 Team Battle, 2 Free-for-All

`Port_ThirdFighter()`, `Port_ThirdTeam()`, `Port_ThirdLevel()` remain, as extra 0.

`Port_ExtraSetup(mode, members0, members1, split, replay, script)`, called by the loader once per fight (first load,
step 0), decides; the first rule that applies wins:

1. **Online** (`Port_NetActive()`): none, whoever asks.
2. **`BT3_EXTRA_FIGHTERS=<character>:<team>[:<level>],...`** (the tests): any number up to 8, in every kind of fight.
   Teams as above (0 with player 1, 1 with player 2, 2 and up further teams).
3. **`BT3_THIRD_FIGHTER=<character>`** (with `BT3_THIRD_TEAM` 0/1/2 and `BT3_THIRD_LEVEL`): one, as before.
4. **The sides' own team rosters**, when the setting `modes_fight` is 1 (Team Battle) or 2 (Free-for-All):
   - only in a fight of the versus menu (battle mode 0), with no script, one view (not split), not a replay;
     otherwise the log says why and the fight is the game's own;
   - every member the players picked on the game's team select fights AT ONCE: fighter 0 is side 0's first member,
     fighter 1 side 1's, the others are extra fighters in the order side 0's members 1.., then side 1's members 1..;
   - Team Battle: an extra's team is its side (0 or 1). Free-for-All: everyone is alone (fighter 0 team 0, fighter 1
     team 1, extra k team 2 + k);
   - an extra's character, costume and level are its member's; its health, its items' bonus levels and abilities
     and its AI type come from its member's entry too (BtlChar_Reset). A member of a player's own team has no
     computer level (-1): it gets player 2's computer's level, 13 where there is none;
   - the loader then leaves each side ONE member (`memberCount = 1`; the member that starts is moved to index 0):
     the game's own switching has nobody to switch to and sends nobody in after a knock-out, and the team panel
     of the stand-by members is not drawn (the HUD asks the member count). The members' entries stay where they
     are (the extras read theirs);
   - a fight with one member a side is the game's own fight of two.
5. **The settings rows** (`extraN_chara`, `extraN_team` (0, 1, or -1 = a team of its own: team 2 + N),
   `extraN_level`): with `modes_fight` 3 (Training) `modes_extras` of them, only in training (battle mode 6); else with
   the Experiments switch `extra_fighters` > 0 that many, in a plain one against one of Duel or training. As
   before: no script, one member a side, one view, not a replay.

A character whose files are missing is left out (the others still fight), and so is one past the eighth. The log
says, for every extra: `bt3: extra fighter: character 24, team 0, level 13 (Team Battle)`, then
`bt3: extra fighters: 5, so 7 in the fight`.

`BT3_MODES_FIGHT=<n>` stands in for the `modes_fight` setting in a test and then holds for every battle mode (a
demo fight is mode 7). `BT3_TEAM_ROSTER=<id,id,..>/<id,id,..>` (up to five a side) fills the two sides' member
lists of the battle setup when the fight loads (`BattleSide.members`, `memberCount`, `lead` = 0, `startForm`), as
the team select would have left them, before anything is read: the roster path above then runs unchanged.
`BT3_DEMO_MODE=<n>` gives the demo fight that battle mode (0: Duel's sequence with the intro talk, Ready, the win
talk and the result menu, which waits for a pad).

### The end of a fight (src/battle/btl_seq.c)

- With more than two fighters a knock-out ends the fight when one team is left: a Team Battle when every fighter
  of a team is down, a Free-for-All when one fighter is left. The result knows two sides: player 1's team (or
  player 1 alone) left = player 1 wins (the result screen shows side 0), any other = player 2 wins.
- Time up: the team with the most health left in all its fighters wins, by the same rule; level teams leave it
  to the game's own judge of the two sides.
- Ring-out (flag 7) is still asked of fighters 0 and 1 only.

## What broke past three fighters, and what was done

Every item is a table of two, three, four or five, a size the game never outgrew with two fighters, or a rule
that two fighters cannot trip over. Found by running demo fights of 4, 6, 8 and 10 fighters until none crashed or
stood still, with `BT3_HEAPCHK`, gdb and the offscreen pictures.

| what | where | was | now |
|---|---|---|---|
| the extra fighter's slot, object, effect set, controller | battle_load.c, battle_work.c, battle.c, btl_char_mgr.c, btl_scene.c | one (`gPortThirdSlot`, `gPortThirdObj`, `sPortThirdScene`, `AllocAll(3 : 2)`) | `gPortExtraSlot[8]`, `gPortExtraObj[8]`, `gPortExtraObjs`, `sPortExtraScene[8]`; `BtlChar_AllocAll(2 + extras)`; the scene sized `2 + extras` |
| the reader's queue | battle_load.c | `FILE_REQ_MAX` 32; ten fighters need 30 files beside the stage's | the extras' files are asked for in step 5, when the queue is empty, in turns of what fits (eight a turn) |
| computer controllers | btl_ai.h, btl_ai_act.h, btl_ai_think.h, btl_ai_mgr.c | 3 | 10 |
| freeze levels, targets, teams | btl_char_mgr.c, btl_char_flag_opp.c | 4 | 10; a team per extra from the table |
| the knock-out and time-up rules | btl_seq.c | three teams | ten |
| fighter work buffers (107 KB) | btl_obj.c | three in the work block | objects 3.. get one from the port's memory when made |
| the shot layer's arena (pool 3) | btl_pool.c | 5 KB, holds three fighters' records (1.5 KB each): the fourth's ran into the next arena, the wall effect behind it was written over and crashed when drawn | pools 0, 1, 3, 6 grow per fighter past the third (0x100, 0xC000, 0x800, 0x100 bytes); the whole arena block is then in the port's memory |
| the extras' own arenas (shots, effects) | btl_pool.h, btl_pool.c, eft_g.c, eft_q.c | 9 and 10 shared by all extras | extra k has 9 + 2k and 10 + 2k (25 slots); those of extras 1.. in one block of the port's memory |
| per-fighter effect slots | eft_aa.h | five rows of two: the third fighter's kind-0 slot was the first fighter's kind-1 slot; the eighth's kind-3 slot lay past the block (crash in EftCharaFx_Stop) | twelve a row |
| bubbles, wakes, body effect tasks, held discs | eft_b.c, eft_e.c, eft_o.h, eft_p.h (+ eft_o_c.c's block size) | two each, by object id | twelve |
| ground dust | eft_z.h | four | twelve |
| the body test's contact bits | eft_det_a.c, bobj_a.c, btl_char_hit.c | four bits "I touched object n", four "object n touched me" in one word: a fifth fighter's bit was another's, from the ninth the shift left the word | `gPortBodyTouch[12]` (a bit per object, cleared where the game clears its bits); the word keeps the game's bits for objects 0..3 |
| body tests | eft_det_a.c | 0-1 and 0/1 against the third | every pair |
| face states | btl_obj.c | 4 (the fifth fighter's object got a null pointer: crash) | more are made when they run out |
| shadows | gfxm_e_b.c | 5 (the sixth object's was taken for there) | more are made when they run out |
| model parts | btl_obj.c | 512 | 128 more at a time when they run out |
| battle objects | btl_obj.h, btl_obj.c | 12: ten fighters left two for partners and the ki blasts that are models; a volley got "no object" (-1), which nobody checks | 20; the game's `gBtlObjTbl` (12 at a fixed address) is replaced by a table of 20 in btl_obj.c |
| effect textures of a frame | eft_ae.h, eft_ae.c | 128 entries (the 129th was written over the count); 256 VRAM blocks of palettes (used up with ten fighters) | 512 entries; a palette that does not fit its part takes blocks from the end of the images' part (up to 0x3480, where the objects' textures begin); what does not fit at all is left out for that frame, with one log line |
| owner tags of effect tasks | 12 eft_*.c files, eft_ae.c, btl_scene.c, eft_r.c | "object 0's" (0x800) and "anybody else's" (0x1000): one fighter's change of model ended the techniques of all the others past the first | each extra fighter has a tag (0x100000 << k) and a reset group (modes 5..12) of its own; `EFT_OWNER_TAG` / `EFT_OWNER_RESET`, PS2 definitions the original text |
| a reset during a technique's camera cut | btl_scene.c, eft_a.c | the cut was forgotten, the camera stayed on it | a reset of one fighter leaves another fighter's cut alone; a forgotten cut lets the camera go |
| a changed extra fighter's old files | battle_load.c | freed at the swap; a running cut read them, and the next fighter's files were read into the same block (crash in DemoCam_EvalAnim) | kept until that fighter's next change, a restart or the end of the fight |
| a texture record cleared between update and draw | eft_det_a.c | the console sends what address 0 gives; here a fault | not sent |
| the change queue | btl_char_ctl_b.c | a ring of six requests: a seventh was dropped and its fighter waited for ever | what does not fit waits in a list of 32 and moves in as the ring gets room |
| freeze while a model request is active | btl_char_mgr.c, btl_char_ctl_b.c | a fighter that began a transformation during another's request froze that request's owner, who has to act for the queue to move: the fight stood still | while a request is active only its owner holds the others still |
| a rush sequence's pair | btl_act_f.c | the two wait for each other's model loads, which is only seen while the request is active: a victim that reached the step late waited in action 0x13A for ever (and held the clock) | the one that waits for the OTHER's load also goes on when the other is past the step or has left the sequence |
| a rush sequence's pair as targets | btl_char_flag_opp.c, btl_act_f.c | a third fighter's shot that only touched the victim made it the victim's opponent; the victim was placed against it, far off the map, and the game stood still in the angle wrap of its hair | the two keep each other as targets from the sequence's own record (`Port_SequencePartner`); a hit from outside does not change either |
| an angle too large to wrap | port/src/mathf_pc.c | the console's loop never ends | 0 (only where the console would stand still) |
| computer fighters during "Ready!" | btl_input.c | the Ready filter lets steps and dashes through; with two the computer never uses them (it starts at the distance it likes); extra fighters ran at their enemies under "Ready!" (seen by the user), and in a free-for-all the two sides' computers too | with more than two fighters every computer fighter's input is blocked until "Fight!"; a player's own fighter keeps what the game allows |
| start places | stg_a_b.c | V of mates, gaps of a circle (a quarter, then eighths); places pulled in when the stage has no ground could land on a mate | the V's step is at least 2.8 m; the free-for-all circle is shared out evenly (eight: 36 degrees apart); a place is also refused when somebody stands within 2.2 m, and then the later turns' places and the same pulled in are tried |
| HUD rows | hud_b.c | a team of its own went right: ten fighters gave eight rows on the right | player 1's team left, player 2's right, any other team to the side with fewer rows (four and four) |

Kept as they are (they only delay, or are the game's own rule):

- one change request at a time; one clash at a time;
- extras have no voice (one bank per side) and no face block;
- ring-out asked of fighters 0 and 1;
- the alpha rows of bound models (`BtlObjVis_AllocFreeBit`, 15): with ten fighters and their partners' and blasts'
  models more than 15 can be bound; the sixteenth shares row 0. Not seen to matter; not looked into.

## Memory

The user allowed more memory. The game's heap (28.2 MB) was NOT enlarged: what grows with the fighter count was
moved to the port's own region below 4 GB (`Port_GameBigAlloc`), and only when the fight has such fighters, so a
fight of two allocates exactly what it did, apart from a few fixed tables that grew (the object table: 8 objects
of 0x1670 bytes; the effect texture list: 6 KB; the pool manager: 16 slot records).

| | heap block / region | 2 fighters | each extra fighter |
|---|---|---|---|
| fighter files (model, two animation files) | port region (as before) | - | up to 3.06 MB; one set more after its first change |
| fighter work buffer (107 KB) | heap's work block for objects 0..2, port region from 3 on | 2 in the block | 107 KB for the 4th on |
| its two effect arenas | game's arena block (9, 10) / port region | - | 200 KB |
| the shared arenas' growth | port region (the whole arena block, 1.8 MB, moves there with four or more fighters) | - | 0xCA00 bytes |
| face states, shadows, model parts | port region when the game's run out | - | small; a shadow is about 64 KB |
| controller, roster entry, scene tables | heap | | about 10 KB |

The heap's free room (`BT3_HEAPCHK`, every five seconds of a fight; least / most over the batches below):

| fighters | heap free |
|---|---|
| 2 (b00e7d4) | 3.51 MB |
| 2 (this branch) | 3.46 MB |
| 3 | 3.45 MB |
| 4 | 5.27 MB (the arena block left the heap) |
| 6 | 4.52 .. 5.26 MB |
| 8 | 5.25 MB |
| 10 | 4.50 .. 5.23 MB (the low end with transformations: partner bodies are read into the heap) |

The port region's use was not measured; from the sizes above a fight of ten takes about 8 x 3.4 MB plus the arena
block (2.4 MB) plus 8 x 3.06 MB more if every extra has changed once: about 54 MB of its 192 MB of address space.
The port region is part of every saved state (`Port_StateExtra`), and every new table above is a plain static of
the game's files or of plat_stub.c / plat_fighters.c, so it is in the state snapshot like the rest of the
program's data; the heap walker of `BT3_HEAPCHK` is unchanged. Nothing of this runs online (no extras there).

## Tools added

- `BT3_EXTRA_FIGHTERS`, `BT3_MODES_FIGHT`, `BT3_TEAM_ROSTER`, `BT3_DEMO_MODE` (above).
- `BT3_HEAPCHK` now also prints: the arenas' sizes and the most each held, when the scene ends; the most textures,
  palette blocks and image blocks of one frame; the battle's flags and the sequence's state with the fighters'
  actions; a fighter whose place leaves the map; a rush sequence partner that goes on without the other's load;
  who won when a fight ends. Always printed (faults, not measurements): `EFFECT ARENA n IS FULL`,
  `effect textures: more than fit in one frame`.

## What was checked (2026-10-14, Linux, clang64)

1. Build: `103 stubs; link OK -> port/build/bt3_64; 0 error lines` (after every change).
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0; port/build/heap_2111.bin`.
3. Plain demo fight, `BT3_AT=200,600,1000`, this build and b00e7d4: `hp 40000 36080`, `hp 32090 25130`,
   `hp 30270 24350` in both, at the same vertical blanks (1015, 2715, 3585).
4. Demo fights, each with its own empty saves folder, 90 s limit, 12 at a time; "clean" = `battle finished`, exit
   0, no crash, no full arena, no broken heap. Layouts by count: two even teams, everyone alone, all with player 1
   (3 v 1 at four), all with player 2, three teams; random characters, levels default / 29 / 13.
   - the old 30 (one extra, ids 3 107 60 24 0 12 55 100 130 150 x team 0/1/2): 30 of 30; again with
     `BT3_THIRD_TRANSFORM=200,700 BT3_THIRD_DAMAGED=1200`: 30 of 30;
   - 4 fighters: 24 of 24; with every extra made to transform at 200, 500, 800: 24 of 24;
   - 6 fighters: 24 of 24; with transformations 24 of 24;
   - 8 fighters: 24 of 24; with transformations 24 of 24;
   - 10 fighters: 24 of 24; with transformations 24 of 24; with transformations every 150 frames and the damaged
     costume: 24 of 24;
   - team rosters (`BT3_TEAM_ROSTER`, one to five a side, Team Battle and Free-for-All alternating): 48 of 48;
   - ten fighters on every stage 0..34, 5 v 5 and free-for-all: 70 of 70 finished, every extra placed on ground,
     the nearest two extras 24 to 87 units apart (stage 35 crashes with the unmodified b00e7d4 build as well: it is
     not a stage).
5. Team Battle and Free-for-All from the team rosters with the Duel sequence (`BT3_DEMO_MODE=0`, 3 v 2, `BT3_TEST_HP`
   to bring endings quickly): side 1 down -> winner player 1 by knock-out; side 0 down -> player 2; free-for-all
   with fighter 0 the only one at full health -> all others down, player 1; free-for-all with fighter 3 the only one
   at full health -> time up, player 2 by health; the sequence goes on to states 4 (finish), 5 (win talk) and 6 (the
   result menu, which waits for a pad) in every case.
6. Mode 3 from a settings file (`modes_fight=3`, `modes_extras=3`, three rows): three extras with teams 0, 3 (-1 of
   row 1) and 1; the Experiments switch with two rows in a Duel-mode fight: two extras with teams 2 and 3; the same
   in the demo's mode 7: refused (`this mode's fights are left as the game's own`).
7. Pictures (offscreen GPU renderer, looked at): 2 v 2 (one row a side), 3 v 3 (two rows a side), 4 v 4 (three), 5 v 5
   (four), free-for-alls of 6 and 10 (rows split two and two, four and four); the openings of 5 v 5 and of the
   ten-way free-for-all before and after the change to "Ready!" (extras ran under "Ready!" before; now everyone
   stands until "Fight!"), the opposing team standing in a row.
8. PS2 side: for all 50 files changed under src/ and include/, the text with the PORT regions taken out is the
   same as at b00e7d4, or the same after the preprocessor (the macros); and the full matching build in a scratch
   worktree: `[330/330] CHECK build/DBZP.BIN`, `build/DBZP.ok`, `build/SLUS_216.78.ok`.

## Not checked, known limits

- No person played: a human player 1 with extras (the Ready rule leaves a player's fighter alone), split screen
  and online are left as the game's own (refused).
- The real menu flow (`modes_fight` written by the menu code, the team select) was not driven: the roster path was
  run with `BT3_TEAM_ROSTER` / `BT3_MODES_FIGHT`, which go through the same member lists. "Fight Again" keeps the
  lifted setup (one member a side) and does not load again, so it should keep the extras; not run.
- The modes page and the Experiments tab with more than one extra (rows past the first, the page's scrolling) were
  not opened.
- The result screen after a team fight shows the two sides' first fighters; the win talk is the two sides'.
- Sound: extras are silent. The camera follows fighter 0's view; a cut of an extra fighter is shown through side
  1's view as before (the first extra that asks).
- Windows and macOS builds were not made; the 32-bit program (no port region) falls back to the heap for every
  block above and was not run with extras.
- Pictures were taken at 4:3 only.

## Camera of throws, grabs and skills

With more than two fighters, a fighter camera that asks for the whole screen (flag 0xD3: a throw, a grab, a rush, an
ultimate) gets it only when it is a player's own or its fighter's target is a player (fighter 0 or 1 on a side that
is not computer-controlled; in a demo, fighter 0). Before, any such camera took the screen, so a fight elsewhere on
the field cut the player's picture away. `BtlCharApi_HasCamPriority` and `BtlCharApi_AnyCamPriority` ask
`Port_CutShown` (btl_char_api.c); with two fighters every camera passes. `BT3_CAM_LOG=1` prints each decision.

The start of a super (flags 0x125 / 0x126: everyone else held still; 0x12A: everyone else hidden) works the same
way: about a player it is the game's own, otherwise it holds only its own target and hides nobody
(`BtlChars_UpdateFreeze`, `BtlChars_UpdateObjFlag2`; `Port_AboutPlayer` in btl_char_api.c). Before, with the camera
kept on the player, the player stood still and vanished for a second for a super elsewhere. `BT3_CAM_LOG=1` also
prints each such hold.

## Choosing the target

With more than two fighters a player (fighter 0 or 1 on a side that is not computer-controlled) flicks the right
stick right or left to make the next enemy on the screen that way the target (round to the far side past the last).
The choice holds until that enemy is down; then the nearest again, and that holds too: the camera never moves to
another enemy by itself (the user asked for no automatic switching). The camera asks for "the opponent" with
`gPortCamOf` set (btl_char_mgr.c) and gets `Port_CamTarget`: the chosen enemy, except during a rush, throw, clash
or technique of the player's, which it shows between the two of it. The game's own target still turns to whoever
hit the player (what the hit does depends on it); only the camera and the marker stay on the choice. A rush, throw, clash or technique keeps its
target until it is over, and a hit from someone else turns the player to them for the second the game needs.
Held right stick no longer moves the camera to a side with more than two fighters. A triangle over the target's head
(model node 0x30) shows it: yellow when the game picked it, red when the player did (`HudExtra_DrawTargetMark`,
hud_b.c, drawn in the picture's own coordinates). `BT3_TARGET_FLICK=<n>` (testing) makes fighter 0 flick every n
frames by itself; `BT3_CAM_LOG=1` prints the choices.
