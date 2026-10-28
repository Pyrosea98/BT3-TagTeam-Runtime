# The third-fighter experiment from the settings window

Notes of the branch `tt-menu` (from `tagteam-exp` at b2146de). Before this the experiment could only be switched
on with environment variables set before the program starts. Now it is a section of the settings window, and the
variables still work.

## What a player sees

F1 opens the settings window; it has an eighth tab, **Experiments**. (A tab of its own and not a part of Cheats:
Cheats holds things of the game that work; this is unfinished and will get more rows. The window is 690 wide
instead of 640 so that eight tab names fit side by side.)

Section "Extra fighters":

- **Third fighter (experimental)**: the switch. Off by default.
- A table with one row for each extra fighter (one row today, numbered 3):
  - **Character**: a list of the 161 characters by name, with the number after it ("Nail (24)"). The names are
    read at run time from the player's own game data, the versus select's text (below); when they cannot be read
    the list says "Character 24". Nothing of the names is in the repository.
  - **Team**: "Team 1 (player 1's)", "Team 2 (player 2's)", "On its own". No friendly fire inside a team.
  - **Computer level**: 0 to 29 (the game's own five levels are 0, 6, 13, 21, 29). Default 13.
- A line in red when the chosen character cannot be used (its files are not in the game data), and a line that
  says what the last fight did ("The last fight had it." / "... was left as the game's own: <why>").

A change is used by the **next fight that starts**. A fight that is on keeps what it started with.

## Settings (bt3_settings.txt)

| name | values | default |
|---|---|---|
| `extra_fighters` | how many extra fighters: 0 or 1 today | 0 |
| `extra0_chara` | character 0..160 | 0 |
| `extra0_team` | 0 player 1's team, 1 player 2's, -1 a team of its own | 1 |
| `extra0_level` | computer level 0..29 | 13 |

The names carry an index and the count is a number because the aim of the experiment is up to ten fighters
(5 against 5, or everyone for themselves): the second extra fighter is `extra1_*`, and so on. Only entry 0 is read.
"Its own team" is stored as -1 and not as a team number so that it stays right when there are more teams: at
set-up it becomes the first team number no player has (2 for entry 0).

The environment variables win over the settings, each on its own: `BT3_THIRD_FIGHTER=<character>` (0..160; with it
set the settings' switch and character are not looked at, and it applies to **every** kind of fight as before: the
tests use it with `BT3_DEMO`), `BT3_THIRD_TEAM` (1, 0, 2), `BT3_THIRD_LEVEL` (0..29; unset = the side's own level,
as before). `BT3_THIRD_AI` is unchanged.

## Where it is decided

`Port_ExtraSetup` (port/src/plat_fighters.c, new) is called by the loader, `BtlLoad_StepInitial` step 4
(src/battle/battle_load.c), just before the experiment asks for the third fighter's model. It decides once for the
fight and keeps the answers; `Port_ThirdFighter()`, `Port_ThirdTeam()` and `Port_ThirdLevel()` (moved there from
plat_stub.c) return them. Before this they read the environment, the first one once per run, so the answer could
not change between fights.

From the settings a fight gets its extra fighter only when ALL of these hold:

| condition | why |
|---|---|
| no online match (`Port_NetActive()`) | both copies must compute the game's own fight |
| battle mode 0 (the versus menu: Duel) or 6 (training) | story fights are scripted (mode 1), the tournaments, Ultimate Battle and the rest (2..5) have their own rules and result screens, 7 is the attract demo |
| the fight has no script (`Battle_GetScript() < 0`) | a script moves and judges two fighters |
| one member a side | a team battle swaps members through the two sides' slots; the experiment has not met that |
| one view (not split for two players) | the experiment's camera work is for one view |
| no replay is being played (`BattleReplay_IsActive()`) | a replay has two input tracks and was recorded with two fighters |
| the character's three files are in the data folder and not empty | else the loader has nothing to read |

Everything else is left as the game's own. The online rule and the files rule also hold for
`BT3_THIRD_FIGHTER`; the other rules do not (the variable is the tests' tool).

The files rule: character c's model is file id c * 10 + 0x590, its two animation files + 8 and + 9 (what
battle_load.c asks for). `Port_ExtraCharaOk` looks for them through the port's file layer (so `mods/` counts).
The window refuses to take such a character from the list and says which file is missing; a settings file written
by hand that names one leaves fights as they are, with the reason in the log and in the window.

Log lines: `bt3: extra fighter: character 24, team 1, level 13 (the settings)` when a fight has one;
`bt3: extra fighter: not in this fight (<why>)` when the switch is on and the fight was left alone.

### A fault found on the way

After a fight of three, a fight of two crashed in its first frame (`BtlSide_IsComboHitNew`: player 1's target
was still fighter 2, who is not there; the HUD asks before the first frame chooses the targets). Nobody had run two
fights with different numbers of fighters in one run before. `Port_TargetReset` (btl_char_flag_opp.c), called from
the same place of the loader, puts the targets back.

## The names

The versus select shows a character's name and form from two texts of its screen: sections 43 and 44 of member 2
of the first menu archive, the file `gProgress->baseFile` (449 on the USA disc: `pzs3us1/00448.bin`;
src/menu/menu_c_e.c). `Port_CharaName` (plat_fighters.c) reads that file from the data folder the first time the
window needs a name, unpacks the member with the unpacker of menu_labels.c (now also exported as
`MenuLabels_Unpack`), and keeps the 161 lines as "name, form". Every offset is checked against the buffer. This
does not need a select screen to have been opened (the screen reader's name cache does).

## Testing tools added

- `BT3_UI_OPEN=7` opens the window on the Experiments tab (the existing hook; 7 is the new tab's number).
- `BT3_EXTRA_FLIP=1` (the experiment's tool): the switch is turned over after every fight's set-up, as if the
  player had done it during the fight. A run of several fights then has them without and with in turn.

## What was checked (2026-10-14, Linux, clang64)

1. Build: `link OK`.
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0`, with no settings and with a settings file
   that has the switch on (log: `not in this fight (a replay is being played)`).
3. A settings file with `extra_fighters=1`, `extra0_chara=24`, no BT3_THIRD_* variables, pad recordings from boot:
   - Duel (Goku against Perfect Cell): log `extra fighter: character 24, team 1, level 13 (the settings)`, and
     offscreen pictures looked at: Goku, Cell and Nail in the fight. The same with `extra_fighters=0`: no such log
     line, pictures show two fighters.
   - Ultimate Training, free practice (battle mode 6): the same log line; pictures show Goku, the dummy and Nail.
   - Three fights in one run (the Duel recording with pad input appended: pause, Return to Character Select, Yes,
     Cross through the select; `BT3_EXTRA_FLIP=1`, switch off at start): fight 1 two fighters, fight 2 three
     (log line and pictures), fight 3 two again and running (pictures). This is the check of "changes between
     fights, and off again, without a restart"; it is how the fault above was found.
   - A data folder where the model file of character 24 is empty (a scratch folder of links with a `mods/` file):
     the Duel recording starts its fight with two fighters, log `not in this fight (its model file (01663.bin) is
     empty in the game data)`, from the settings and from `BT3_THIRD_FIGHTER=24` alike.
4. The window: offscreen pictures (`BT3_UI_OPEN=7 BT3_UI_SHOT=...`) looked at, with the switch on, and with the
   empty model file (the red line).
5. Online: the two-copy harness on one machine (in-process session, 50 s), both copies with the switch on in their
   settings: `whole state: same; fight values: same` (30133 blanks, 26074 in a fight), and both logs say
   `not in this fight (an online match)`. The same harness without settings says the same. (Two runs of the
   harness do not give the same fight as each other, with or without the setting, so run was not compared with
   run. A 150 s run goes past the session's end and reports a difference from blank 5641, with the main
   checkout's program too: that is the harness, not this.)
6. Twelve windowless demo fights, `BT3_THIRD_FIGHTER` 3 / 107 / 60 / 24 with `BT3_THIRD_TEAM` 0 / 1 / 2: all
   twelve print `bt3: battle finished`.
7. Game source: the changes in src/battle/battle_load.c, btl_ai_cond.c and btl_char_flag_opp.c are all inside
   `#ifdef PORT` (checked by a script over the diff). No PS2 build was run here.

## Not checked, known limits

- Nobody clicked in the window: the list opening, choosing a character, the team list, the slider and the
  refusal message for a character chosen FROM THE LIST were not exercised (the code path that refuses is the same
  function as the checked one). The controller's L1 / R1 round of eight tabs was not exercised.
- Windows and macOS builds were not made.
- The modes that are left alone were checked only through the rule's log line in a replay and in an online
  session (split screen, another mode); story, tournaments and team battles were not entered.
- A Duel against a second player is split screen and so gets none; Duel against the computer does.
- The game records every Duel for "save replay". A replay saved from a fight of three will not play back right:
  playback is the game's own, with two fighters. Saving is not blocked.
- The third fighter still has no health bar and takes no part in clashes (the experiment's own state).
- A session that starts while a three-fighter fight is on was not tried.

## More than one extra fighter: what the set-up path needs

Today everything from the settings to the fight carries exactly one:

- plat_fighters.c: `EXTRA_MAX` and the `kSettings` rows (one entry); `Port_ExtraSetup` keeps one character, team
  and level. It should fill a table per fight (count, and for each: character, team, level) and check each
  character's files.
- The three questions have no index: `Port_ThirdFighter()`, `Port_ThirdTeam()`, `Port_ThirdLevel()`. They become
  "how many" and "entry i's ...".
- battle_load.c step 4 asks for ONE resource slot (`gPortThirdSlot`) and keeps one effect-set flag
  (`sPortThirdScene`); battle_work.c makes ONE object (`gPortThirdObj`); battle.c allocates `3 : 2` fighters.
  Each becomes a loop over the fight's table, and the two globals arrays (btl_cam.c, btl_char_mgr.c, btl_scene.c
  read them too).
- btl_char_flag_opp.c: `Port_Team(i)` is `i < 2 ? i : Port_ThirdTeam()` and `PORT_FIGHTERS` is 4; the team of
  roster entry i has to come from the table, and "its own team" for entry k is team 2 + k.
- btl_ai_cond.c takes the level for `side >= 2` from the one answer.
- The window: the table already has a row per fighter; a count field instead of the switch, a loop over the rows,
  and more team names.
- Memory: each extra fighter is a model and two animation files in the game's heap, beside the stage. Whether
  ten fit was not looked at here.
