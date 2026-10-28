# The third-fighter experiment: real model changes

Branch `tt-transform`, on top of `tagteam-exp` (b2146de). Everything here is the experiment's
(`BT3_THIRD_FIGHTER`), inside `#ifdef PORT`; the PS2 build sees the same text as before.

## What was there

A fighter that transforms, takes its damaged costume or changes character queues a request
(`BtlChange_RequestChara`, `src/battle/btl_char_ctl_b.c`) and waits. The loader
(`src/battle/battle_load.c`) turns the request into a job, reads the new files, and when the fighter
allows it swaps them in and binds the object to them again. All of that was written for two sides:

- `BtlLoad_PollCharaRequest` and `BtlLoad_PollObjectRequest` looked at sides 0 and 1 only;
- `BtlRes_Reload` (`src/battle/btl_obj.c`) only takes one of the two fixed resource slots, reads the
  new model into the one spare model buffer and the two animation files over the old ones;
- the job asks the battle setup for the side's slot and object (`BattleSide_GetModelSlot`,
  `BattleSide_GetObjId`), and the setup answers for side 2 when asked about fighter 3 (`Side()` in
  `battle_load.c` maps every index past 1 to 1).

So commit 137512b made the third fighter's request count as loaded at once and told the fighter "the
model is there" with the model it had: it "transformed" and stayed what it was.

Two more things were wrong for the third fighter and showed up here:

- it took itself for side 2's character. `BtlChar_Reset` filled its team from the battle setup, which
  gave it side 2's: side 2's health and gauge figures, and, when it asked for "my own model again, the
  damaged one", side 2's model.
- its files (2.4 MB for character 3) were in the game's heap, which then had 1.3 MB left.

## What is there now

### The path of a change, for a fighter that is not a side

1. The fighter queues the request as before; `BtlChange_Update` makes it the active one.
2. `BtlLoad_PollCharaRequest` loops over every fighter of the roster (`Port_LoadFighters()`), finds the
   request, frees the fighter's effect set (unless only the model changes), writes the new form into
   the loader's own record of that fighter (`sPortFighter[i]`; the setup's "current form" would be side
   2's) and pushes a `BtlLoad_StepChara` job with `side` = the roster index.
3. `BtlLoad_StepChara`, state 0: for `side >= 2`, `Port_ResReload(slot, model, anim0, anim1)`
   (`btl_obj.c`) asks for the files. Every new file goes into a buffer of its own; the old files stay
   untouched until the swap. The file ids are the game's: model `0x590 + chara * 10 + costume` (`+4`
   for the damaged variant), animations `0x598 + chara * 10` and `0x599 + chara * 10`. A change of the
   model only (the damaged costume) asks for the model alone.
4. State 1: when the reads are done the request is marked loaded; the fighter sees it and allows the
   swap (all as for a side).
5. State 2: `Port_ResCommit` puts the new files into the slot; `BtlObj_Rebind` binds the object to them
   (it reads the OLD model once more on the way, `BtlObjMdl_Destroy`: that is why the old files are
   still there); `BtlChars_OnModelLoaded` gives the fighter its new character, costume, parameters
   (`BtlMember_LoadParams`: health and gauge maxima of the new character) and binds its tables to the
   new animation files; the AI's controller is reset and the effect set is made again from the new
   files; then `Port_ResReleaseOld` frees the old files.

The removed piece: the block in `BtlChange_Update` that faked the load.

Requests for an extra object (`BtlChange_RequestObject`: a partner's body) are served for every fighter
too: `BtlLoad_PollObjectRequest` has the same loop, and `BtlLoad_StepObject` needed nothing (its model
goes into a heap slot, it is attached with `BtlCtrl_AttachPartner(fighter index)`). Seen once in the
runs below (object 258 for the third fighter), not looked at in pictures.

A restart (`BtlLoad_Reload`) brings such a fighter back to the form it starts with, as it does for the
sides: a change that the dropped jobs left half done is committed, the object bound, and a job of the
"restart" kind loads the starting files if the form differs. **Not run**: nothing in a windowless demo
restarts a fight.

### Where the files are

`Port_ResRequest` (start of a fight) and `Port_ResReload` (a change) take their buffers from the port's
own region below 4 GB (`Port_GameBigAlloc`, `port/src/plat_mem.c`; `Heap_Free` knows such a block), in
the three sizes of a fixed slot's buffers, which hold any character of the disc (a file larger than
that gets a buffer of its own size). That region hands a freed block out again by its size, so the same
three sizes go round. In the 32-bit program, which has no such region, the heap is used as before, and a
change is refused (the fighter keeps its files, the fight goes on) unless the heap's largest free block
keeps 2 MB after the file. That fallback was not run.

### The fighter's own identity

`BtlChar_Reset`: a fighter with `side >= 2` is one member, the character of `BT3_THIRD_FIGHTER`, first
costume. Its health and gauge figures are its own now, and "my own model, damaged" is its own.

### Tools (the experiment's, all in `port/src/plat_stub.c`)

- `BT3_THIRD_TRANSFORM=<frame>[,<frame>...]`: the third fighter transforms at the first chance after
  each of these frames of the fight (in `BtlAct_CheckTransformInput`, where the game takes the button
  for it): its default transformation or else its first possible one, without the cost.
- `BT3_THIRD_DAMAGED=<frame>`: at that frame the third fighter is told it was hit by a finishing
  technique (held flag 0xAB, as the effect scene sets it): action 0x104, which loads the damaged
  costume if it does not wear it.
- `BT3_HEAPCHK=1` additionally prints: the files asked for a change and what was bound, and every five
  seconds how much of the heap is free and its largest free block. Both tools print the picture number
  (`gGsFrame`, what `BT3_SHOT_FROM` / `BT3_SHOT_TO` go by) at which they acted.

## What is not done

- **The voice.** The sound driver has one voice bank per side (masks 0x10 and 0x20, `res->sndChara[2]`,
  one bank buffer `res->bank` for a change) and the streamed voice lines have a player per side; the
  third fighter has no bank (bfba2e6) and stays silent, before and after a change.
- **The 0x1000-byte member block** (`file 8 + chara * 2 + side`: the face shown on the screen) is not
  loaded for it; nothing shows its face.
- **The effect scene's reset by owner.** A fighter's effect tasks are tagged 0x800 (object 0) or 0x1000
  (every other object), and freeing a fighter's set resets every task of its tag
  (`BtlScene_Reset(chr == 0 ? 2 : 3)`). So a change of the third fighter also runs the reset of player
  2's live effects, and the other way round (that direction was already so). No fault came of it in the
  runs below; what it looks like when player 2 has a beam in the air at that moment was not looked at.
- **A team for it.** One member: no switch, no fusion with a team mate.

## What was checked

All on Linux, `BT3_CC=clang64`, the game data of the unmodified disc.

1. Build: `link OK`, 0 error lines.
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0` (the line goes on with
   `; port/build/heap_2111.bin`, as on `tagteam-exp`).
3. A transformation, without a window: `BT3_DEMO=1 BT3_THIRD_FIGHTER=3 BT3_THIRD_TRANSFORM=300
   BT3_HEAPCHK=1` prints

       bt3: the third fighter is made to transform at frame 300 (picture 402): transformation 0, into character 4
       bt3: dbg: fighter change, files asked: fighter / model / animations / given the memory: 2 0x5b8 0x5c0 1
       bt3: dbg: fighter change, bound: fighter / model file of its slot now / only the model / new files: 2 0x5b8 0 1

   (0x5b8 = 0x590 + 4 * 10: character 4's model; 0x5c0 and 0x5c1 its animation files.) Pictures 404 to
   476 of that run, looked at: the black-haired character 3 in the lead-in (420), the golden-haired
   model of character 4 in the closing shot (448 on), and later in the fight that model doing a rush
   on player 2.
   The damaged costume: `BT3_THIRD_DAMAGED=300` prints `files asked: ... 2 0x5b2 0xffffffff 1` and
   `bound: ... 2 0x5b2 0x1 1` (0x5b2 = 0x590 + 3 * 10 + 4, the model only); the action itself is behind
   a white screen, and pictures 1036 and 1064 of that fight show character 3 with torn clothes.
4. Thirty demo fights (third fighter 3 107 60 24 0 12 55 100 130 150, team 0, 1 and 2, 45 s limit,
   each with its own empty saves folder): 30 of 30 print `bt3: battle finished`, exit 0. None of them
   changes a model by itself, so the same thirty were run again with `BT3_THIRD_TRANSFORM=200,700
   BT3_THIRD_DAMAGED=1200`: 30 of 30 finished, 29 with at least one change bound.
   Every character 0..160 as the third fighter with `BT3_THIRD_TRANSFORM=200,500 BT3_THIRD_DAMAGED=800`
   (30 s limit): 161 of 161 finished; 71 characters transformed, 160 had at least one change bound.
   Longer: six demo fights (3 7 14 41 91 141, a transformation asked every 150 frames): all finished,
   3 to 18 changes each. Six runs driven by `BT3_REPLAY` with a third fighter (the fight goes on after
   the replay's end; 75 s limit, a transformation asked every 500 frames): all six still running when
   stopped, at fight frames 40,680 to 43,800, with 1 to 85 changes each (character 7 has no
   transformation it may take on that stage, 91 and 141 reach their last form), no refused change, no
   broken heap. (A demo fight has a fixed length: the replay runs are the long ones.)
5. A demo fight without a third fighter, this build against `tagteam-exp`: `BT3_AT` at ticks 200, 600
   and 1000 prints the same line in both (`hp 40000 36080`, `hp 32090 25130`, `hp 30270 24350`); the
   fight is over before tick 1400 in both. The heap dumps of those ticks differ in about 300 of 28.5
   million bytes (not looked into: addresses of the program's own data would do that).
6. PS2 side: for the five files changed under `src/`, the text with every `#ifdef PORT` branch taken
   out is the same as on `tagteam-exp`. No file under `include/` was changed. The PS2 build itself was
   not run.

One run did something not understood: an offscreen run (`BT3_GS=gpu`, pictures asked from 596 on)
printed `bt3: battle finished` without the line of `BT3_THIRD_DAMAGED=300` and without a picture, as if
the fight had ended before frame 300. Seven more runs of the same command behaved (the line, the
pictures). Not reproduced, not explained.

## For ten fighters

### Tables of two, and loops over two, in the loader and the change path

In `src/battle/battle_load.c`:

| where | what | state |
|---|---|---|
| `BtlLoad_PollCharaRequest`, `BtlLoad_PollObjectRequest` | `side < 2` | now the roster's count |
| `BtlLoad_StepChara` | slot and object from the battle setup; voice bank by `side == 0` / else; member block `chara * 2 + side` | a branch for `side >= 2` (no voice, no member block) |
| `BtlLoad_StepInitial` state 0..3 | `res->sndChara[2]`, banks 0x10 / 0x20 | two only |
| `BtlLoad_StepInitial` state 4, 5 | `BattleSide_SetModelSlot(0 / 1)`; three loops `side < 2` over the member blocks | two only; the third fighter's slot is asked beside them (`gPortThirdSlot`) |
| `BtlLoad_Reload` | two loops `side < 2`; the third fighter's effect set tied to side 2's (`sPortThirdScene`) | a loop over the other fighters added |
| `BtlLoad_FreeAll` | `side < 2` over the member blocks; four banks | two only (the other fighters have neither) |
| `Side()` | `setup->sides[2]`; an index past 1 answers as side 2 | every `BattleSide_*` getter asked with a fighter index gives side 2's value |
| the event sets (`for (i = 0; i < 2; i++)` in `BtlEvent_Update` and the functions around it) | two sets | a fighter's events go to its team's side (b2146de) |
| `sPortFighter[PORT_FIGHTER_MAX]`, `Port_FighterSlot`, `Port_LoadFighters` | new: form per fighter, sized by one constant (10) | `Port_FighterSlot` knows one such fighter (`gPortThirdSlot`) |

In `src/battle/btl_obj.c`:

| what | size | note |
|---|---|---|
| `BtlResWork.fixed[BTL_RES_FIXED_MAX]` | 2 | the sides' slots with preallocated buffers |
| `BtlResWork.spare[3]`, `next`, `pending` | one spare model, one change at a time | `BtlRes_Reload` refuses a second pending change and any slot that is not fixed |
| `BtlResWork.slots[BTL_RES_SLOT_MAX]` | 12 | heap slots: the other fighters, partner objects, anything else loaded by `BtlRes_Request(0, ...)` |
| `sPortResNext[14][3]`, `sPortResPending[14]` | new: one record per slot number | any number of slots can change at once |
| `BtlObjWork.objs[BTL_OBJ_MAX]` | 12 objects | fighters and partners |
| `BtlObjWork.charaWork[3][0x1A0C0]`, `BtlObj_GetCharaWork(id < 3)` | 3 (2 on the PS2) | 107 KB per fighter, inside the work block |

In the change path elsewhere:

| where | what |
|---|---|
| `BtlChangeQueue` (`btl_char_ctl_b.c`) | a ring of 7 requests, ONE active at a time, and one time-stop word for the whole fight: ten fighters transform one after the other and everybody stands still meanwhile |
| `gBtlLoadHandle`, `gBtlLoadObj` (`BtlLoad_StepObject`) | one each (enough while one request is active at a time) |
| `BtlJobPool` | 8 jobs |
| `FILE_REQ_MAX` | 32 file requests in the queue: ten fighters' three files at the start of a fight are 30, beside the stage, the banks and the member blocks; the first load would have to go in two steps |
| effect pools (`btl_pool.c`, `PORT_CHAR_POOL` in `eft_q.c`) | pool 7 for fighter 0, 8 for fighter 1, 10 for EVERY fighter past them (and 9 for the shots of those): a fourth fighter would share the third's, and freeing one's set empties the pool under the other |
| task owner tags (`BtlTask_SetOwnerTag`, 0x800 / 0x1000; `BtlScene_Reset(chr == 0 ? 2 : 3)`) | two tags, set at more than twenty places in the effect files |
| `BtlScene_SetCharCount(gPortThirdObj == 2 ? 3 : 2)` | the effect scene's per-fighter tables, by object id; several places test `gPortThirdObj == 2` |
| `BattleResult.unk1C[2]`, `unk24[2]`, `health[2]` | `BtlChar_Reset` writes `unk1C[chr->side]` and `unk24[chr->side]` = 0: with side 2 that lands on `unk24[0]` and `health[0]`, inside the block and zero at that moment anyway, but it is a write past a table of two (not changed here) |
| `gPortTarget[PORT_FIGHTERS]` (`btl_char_flag_opp.c`) | 4 |
| `Port_ThirdFighter()`, `gPortThirdSlot`, `gPortThirdObj`, `sPortThirdScene`, `BtlChar_Reset` | one extra fighter's character, slot, object and scene flag; `BtlChar_Reset` takes the character of any `side >= 2` from `Port_ThirdFighter()` |
| voice | a bank and a stream player per side |

### Memory

- The game's heap is 28.2 MB (`HEAP_BASE` 0x3BE000 to 0x2000000). In a demo fight of two it has
  3.69 MB free in one block (0x383ddc; printed by `BT3_HEAPCHK`), with the experiment's additions
  already in it (the third object work block and two more effect pools are allocated whether or not
  there is a third fighter).
- A fighter's three files: the game's own buffers for them are 0xCE000 (model, 824 KB), 0x160800
  (first animation file, 1,410 KB) and 0xCE800 (second, 826 KB): 3.06 MB. Character 3's files took
  2.40 MB of the heap (measured: the heap's free room before and after they left it). With them in the
  heap a fight of three had 1.28 MB free; now they are in the port's memory and it has 3.68 MB.
- During a change a fighter holds two sets for a moment (the old one until the object is bound again):
  up to 3.06 MB more, in the port's memory.
- What a fighter costs in the heap besides: its roster block (0x1600 + 2 x 0x34 bytes), its object work
  (0x1A0C0, 107 KB), an effect pool and a shot pool (0x19000 each, 100 KB), its AI controller's block.
  About 0.3 MB.
- Ten fighters: the files of eight more are 24.5 MB at the buffers' sizes. That cannot come from the
  heap (3.7 MB) and does not have to: the port's region is 192 MB of address space (`LOW_ARENA_SIZE`),
  used as it is touched, and a fighter's files are there already. The heap would take about 0.3 MB for
  each of seven more fighters, 2.1 MB of the 3.7 MB free, IF nothing else grows; a partner object (a
  model of up to 824 KB read into the heap by `BtlLoad_StepObject`) still has to fit beside that, and
  the effect scene's own tables by fighter count were not measured. So: the files fit, the heap is the
  tight part, and the per-fighter blocks that are in the heap today (object work, effect pools) are the
  first candidates to follow the files out of it. The 32-bit program has no such region and cannot hold
  even a third fighter's change in its heap with room to spare.
- Sound memory was not looked at (a third voice bank would need it).

## Files

- `src/battle/battle_load.c`: the loader's loops and the job's branch for a fighter that is not a side;
  the restart; the form record.
- `src/battle/btl_obj.c`: `Port_ResRequest`, `Port_ResReload`, `Port_ResCommit`, `Port_ResReleaseOld`,
  `Port_ResCommitAll`, `Port_ResModelFile`; `BtlRes_Term` frees what a change still holds.
- `src/battle/btl_char_ctl_b.c`: the faked load is gone.
- `src/battle/btl_char_mgr.c`: the third fighter is its own character; `BT3_THIRD_DAMAGED`; the heap
  line of `BT3_HEAPCHK`.
- `src/battle/btl_act_j.c`: `BT3_THIRD_TRANSFORM`.
- `port/src/plat_stub.c`: `Port_HeapFree` and the two tools' variables.
