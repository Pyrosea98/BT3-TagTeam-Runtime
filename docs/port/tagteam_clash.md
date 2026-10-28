# The third-fighter experiment: clashes between any two fighters

Notes of the work on branch `tt-clash` (on top of `tagteam-exp`, b2146de). What was found, what was changed, what
was checked and what was not.

## What the game does

There are three kinds of clash, all run by one sequence in `src/battle/btl_char_flag_clash.c` (`BtlClash_Update`,
once a frame after the per-fighter passes):

| kind | how two fighters get into it | actions | sequence steps | what is compared |
|---|---|---|---|---|
| A, the struggle (beam against beam, rush against rush) | `EftDet_ClashPass` (eft_det_a.c): two technique records meet head-on, both fighters get held flag 0xAA | 0x130..0x132 | 1..5 | `clashCountA` every frame from frame 60 on: the lead |
| B, trading blows | `BtlHit_CheckClash` (btl_char_hit.c): both carry flag 0x47 and touch, both get flag 0x61 | 0xFA | 6..8 | `clashCountB` once, after 76 frames |
| C, the vanishing exchange ("mixed") | the same function, a 0x47 / 0x48 mix, both get flag 0x62 | 0xFB then 0xFC | 9..11 | `clashCountB` after seven exchanges |

Every function of that file named the two fighters by number (`BtlChar_Get(0)`, `BtlChar_Get(1)`, node positions of
objects 0 and 1, "player == 0" for the camera's side). A fighter in action 0xFA is only ever let out of it by this
sequence (flag 0xBF or 0xC0), and the sequence only looked at fighters 0 and 1: that is why the third fighter stayed
in 0xFA for ever.

## What was changed

All of it is `#ifdef PORT`; where a line of the game named fighter 0 or 1 it now names a macro whose PS2 definition
is that same 0 or 1 (as `OPPONENT` and `CUT_FIGHTER` of the earlier commits do).

- **The pair** (`btl_char_flag_clash.c`). `gPortClash[2]` holds the two roster indices of the clash, the lower one
  first: it takes fighter 0's part (the camera's side, the first voice, "the lead is positive when it is ahead", the
  first of the two blow motions, the side of the path's point). Nothing is said about index 2: any two indices.
  - `Port_ClashFind`: while no sequence runs, two enemies that are in the same clash action at once become the
    pair. This is where the pair is found "from the fighters".
  - `Port_ClashPair(a, b)`: the code that puts two fighters into a clash names them a few frames before their
    actions start, so that the two are each other's opponent from the first frame (`Port_TargetHit`). It refuses
    two of one team, anything that is not a fighter, and any other pair while a clash runs or is about to
    (`Port_ClashBusy`).
  - `Port_ClashPartner(i)`: used by `Port_UpdateTarget` (btl_char_flag_opp.c) to hold each one's target on the
    other for the whole clash and two seconds on (the loser's damage is "the opponent's count", asked after the
    sequence is over), and by `Port_TargetHit` so that a hit from outside does not turn either away.
  - Step 0 lets out EVERY fighter found alone in a clash action, and while a clash runs any fighter in a clash
    action that is not of the pair is let out too. This alone is what ended the endless 0xFA.
- **Blows and the vanishing exchange** (`BtlHit_CheckClash`). The exclusion of b2146de is gone. With more than two
  fighters a clash is only started between a fighter and its target when they are enemies, no clash runs, and the
  touch is between these two ("blocked by the opponent" is each fighter's push-out against its OWN target; b's
  target may be somebody else, which would have started a clash at a distance).
- **The beam struggle** (`EftDet_ClashPass`, `eft_r.c`). The two fighters are the owners of the two shots that met
  (it was fighters 0 and 1 whoever fired). Shots of two fighters of one team do not meet at all. The struggle
  effect's "record 0 is object 0's", its end ("the other one's beam is taken away") and its "both still in the
  struggle?" test go by the pair.
- **The motions and the placement** (`btl_act_e.c`, `btl_char_ctl_d.c`): the first of the pair plays motion 0x173,
  the other 0x174; the first makes the exchange's spark and sound; the two stand on either side of the path's point.
- **The camera** (`btl_char_cam_cut.c`). The clash cuts run on the first fighter of the pair. A cut's "middle of the
  two" was between fighter 0 and fighter 0's opponent whoever's cut it was; it is now between the cut's own fighter
  and that fighter's opponent (lower index first, so two fighters give the game's own arithmetic). Before this a
  clash between fighters 1 and 2 showed the empty ground between player 1 and its opponent.
- **"The battle's first clash"** (`BtlChar_RaiseFirstClash`, btl_char_flag.c) is asked of every fighter.

## One clash at a time

The clash state is a single global: roster +0x40 `clashState`, +0x50 `clashA` (timer, lead, bias, middle point; the
bias is what `BtlCharApi_GetClashBias()` gives the struggle effect), +0x70 `clashB`, +0x7C `clashC` (also read by
action 0xFC and by `BtlChar_PlaceOnPath`), and the struggle effect is one manager with a list of one task
(`gEftStruggle`, `side[2]`). So a second pair is refused while one clash runs: for blows the hits land as ordinary
hits, for beams the two beams pass each other.

A second simultaneous clash would need: the roster's clash block per pair (state, A, B, C) and `BtlClash_Update`
stepping each; `BtlCharApi_GetClashBias` and the `EftStruggle_*` calls taking a fighter or a pair; the struggle
manager's list and `side[]` per pair; clash C's path, point and pick per pair; and a rule for the camera, which has
one screen (which clash is shown).

## Tools (the experiment's, env-gated, in port/src/plat_stub.c)

- `BT3_CLASH_PAIR_LOG=1`: `bt3: clash: frame N: blows start / blows decided / blows over: fighters a and b[, counts x
  and y, fighter w wins]` (and `struggle`, `vanish`). N is the number of the picture (0 without a renderer). For the
  struggle the counts are those of the deciding frame; the winner is who was ahead for longer.
- `BT3_FORCE_CLASH=n,a,b[,kind]`: from the n-th update on, a and b are put into a clash the next time they are
  within 30 of each other and none runs, and again n updates later. kind 1 blows, 2 the vanishing exchange, 3 the
  struggle whenever both are in a technique of class 2..4 (no beams have met then: the sequence without its effect).

## What was checked

Linux, clang64 build, demo fights (both players and the third are the computer).

1. Build: `link OK`.
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0`.
3. Clashes that start and end, from the log:
   - blows, natural: pairs 0-1, 0-2 and 1-2 (for instance `counts 49 and 49, fighter 0 wins`, `43 and 47, fighter 2
     wins`).
   - the vanishing exchange, forced: pairs 0-1, 0-2, 1-2 (team 1, 2 and 0). Every one ends `counts 0 and 7`, the
     second of the pair winning, for 0-1 as well: the computer never presses the picked button in this clash, and
     the sequence gives the point to the second when the first did not press.
   - the struggle, natural (real beams): 0-2 (third fighter 129 and 99, team 2) and 1-2 (21, 72, 74, 79, team 2),
     and 0-1 with a third fighter in the fight. Forced: 0-2 and 1-2.
   - two of one team: a forced clash is refused (team 0, fighters 0 and 2: none started).
   - Pictures looked at (offscreen renderer): blows 0-2 and 1-2 (the camera circles the two; the third is one of
     them), the vanishing exchange 1-2 (the stage's fixed cameras, the pair appearing with a shock wave at seven
     places), a forced struggle 0-2, and a natural beam struggle 0-2 (each fighter in turn, the ball between the two
     beams, the winner's face, the loser struck).
4. 30 demo fights (ids 3 107 60 24 0 12 55 100 130 150 x team 0/1/2): 30 of 30 `bt3: battle finished`, exit 0;
   again with `BT3_THIRD_LEVEL=29`: 30 of 30. Beyond that, every id 0..160 at level 29 with team 2 and with team 1:
   161 of 161 and 161 of 161 (run before the last small commit).
5. The plain demo fight without a third fighter, against the unmodified tagteam-exp build: health equal at ticks
   400, 700, 900, 1100 and 1250 (40000 25130, 31290 25130, 31230 24350, 29020 24350, 29020 22000). The heap dumps
   at tick 400 differ in 237 words, each by one of three constants (4112, 2752, 4176): addresses of the program,
   which moved.
6. PS2 side: each changed file under src/, preprocessed without PORT, is identical to its b2146de version
   (`cpp -P`, nine files). No matching build was run.

## What was not checked, and what is known to be wrong

- No human player in a clash with the third fighter (mashing from a pad, the Square option).
- No picture of a struggle between fighters 1 and 2 was looked at; only its log (starts, decided, over).
- The end of a struggle resets "the loser's effect layer". Effects are tagged "object 0's" or "not object 0's"
  where they are made (more than twenty places, `objId == 0 ? 0x800 : 0x1000`), so every fighter past the first shares a layer:
  when fighter 2 loses to fighter 0, fighter 1's effects go too; when the pair is 1 and 2 the winner's beam goes
  with the loser's. The damage is dealt by the sequence, not by the beam.
- While fighters 0 and 1 clash, a cut-in of the third fighter still takes the screen (side 2's view carries the
  third's camera when it asks; seen in one picture run). That is the earlier commit's rule, not changed here.
- The clash cut is run on the first fighter of the pair, and a cut reaches the screen through a view; there are two
  views (sides 0 and 1). With three fighters the first of any pair is fighter 0 or 1. A pair with neither would
  have no view to be shown through.
- `BtlOpp_MirrorFlags` (btl_char_flag_opp.c) still mirrors fighter 1 for player 0 and fighter 0 for everyone else.
- One offscreen picture run ended at once with `bt3: battle finished`, no pictures and no clash lines; the same
  command run three more times was normal. Not explained (the attract demo ends when START is pressed on a pad; whether one was is not known).
- macOS and Windows: not built.

## For ten fighters: what still assumes two

Found while reading the clash and targeting code; "done" means it goes by the pair or by teams now.

- btl_char_flag_clash.c: every `BtlChar_Get(0/1)`, node positions of objects 0 / 1, `player == 0` in the orbit cut
  (done: `CLASH_P0` / `CLASH_P1`). The clash state itself: one (see above).
- eft_r.c: `EftStruggle_Start` "`a->objId == 0`", `EftStruggle_End` winner 0 / 1 and layers 2 / 3,
  `EftStruggle_Step` `IsInClashA(0) / (1)` (done); `EftStruggleMgr.side[2]`, `EftStruggleArg.rec[2]`, a list of one
  task; the clash spark's owner is object 0; `EftStruggle_SetPower(side, ...)` writes `objId = side` (no caller).
  Not clash but next to it: a deflected ki blast changes owner with `objId ^ 1` (four places).
- eft_det_a.c: `BtlCharApi_SetHeldFlagAA(0); (1)` (done); the earlier commits' `(u32)owner < 4` and
  `Port_FighterObjId(2)` tests.
- The effect owner tags `objId == 0 ? 0x800 : 0x1000` (more than twenty places in eft_ad, eft_h, eft_i, eft_j, eft_k, eft_l_b,
  eft_l_c, eft_o_b, eft_o_c, eft_q, eft_r, eft_s) and `BtlScene_Reset(2 / 3 / 4)`: one bit per fighter would be needed.
- btl_act_e.c: `chr->player == 0 ? 0x173 : 0x174`, and `chr->player == 0` for the exchange's spark (done).
- btl_char_ctl_d.c `BtlChar_PlaceOnPath`: `chr->player == 0` picks the side of the point (done).
- btl_char_flag.c: `BtlChar_RaiseFirstClash` tested fighters 0 and 1 (done); `BtlChars_UpdateSightFlag` (earlier
  commit).
- btl_char_flag_opp.c: `gPortTarget[PORT_FIGHTERS]` and `sPortTargetHold[PORT_FIGHTERS]` with `PORT_FIGHTERS` 4;
  `Port_Team` gives teams to indices 0, 1 and "the third"; `BtlOpp_MirrorFlags` (not done).
- btl_char_hit.c `BtlHit_CheckClash`: b is a's target only, and the touch test is each fighter's push-out against
  its own target (`BtlMove_PushOut`, btl_char_move.c: one opponent per fighter per frame).
- btl_char_cam_cut.c: the middle of "fighters 0 and 1" (done: the cut's fighter and its opponent). btl_cam.c: two
  views, and `side == 1 && gPortThirdObj == 2` lends side 2's view to the third fighter.
- btl_char_mgr.c: `level[4]` in `BtlChars_UpdateFreeze`; the battle events are kept for two sides
  (`BtlEvent_Side`, battle_load.c).
- btl_char_flag_snd.c: `BtlCharSnd_StopUnrequestedLoops(BtlChar_Get(0)); (1)`.
- Roster index and object id are taken for the same number for fighters (`BtlCharApi_IsInClashA(i)`,
  `BtlCharApi_GetNodePos(i, ...)`, the shots' owners); `gPortThirdObj == 2` is tested where the third is made.
