# The page of extra modes: SELECT on the main menu

> Replaced by docs/port/tagteam_menuflow.md (branch tt-menuflow): the page described here, a window of the port drawn
> over the main menu with a set-up of the extra fighter, is gone. SELECT now switches the main menu own entries, and a
> mode leads to the game own team character select. What follows is kept as the record of that first version.

Notes of the branch `tt-modes` (from `tagteam-exp` at 6d58f3b). Before this the extra fighter of the experiment
could be switched on in the settings window (F1, Experiments: docs/port/tagteam_menu.md) or with environment
variables. Now the game's main menu has a way in of its own, the way players of this kind of fight expect it:
SELECT opens a page that lists the extra modes, and the game's own entries work as always.

## What a player sees

On the main menu (Dragon History, Ultimate Battle, ..., Duel, ...), once the menu takes input, a small plate in
the corner of the message window says **SELECT  More modes**. SELECT on pad 1 opens the page over the menu: the
menu is dimmed behind it, keeps moving (the clouds, the guides) and gets no input.

The page, first screen, **Extra Modes**:

| entry | what it is |
|---|---|
| Team Battle | extra fighters join a team: yours or the opponent's |
| Free-for-All | every fighter is on a team of its own (today: three fighters, each against the other two) |
| Training | the training mode's free practice with extra fighters |
| Back | closes the page |

An entry leads to its **set-up**:

- **Extra fighters**: how many, 1 to `PORT_EXTRA_MAX` (shown as "1 (of 1)" today);
- for each extra fighter (numbered from 3: the two players are 1 and 2): **its character**, by the name the versus
  select shows; **Team** (Yours / The opponent's; in Training also Its own; no such row in a Free-for-All, where
  each is on its own); **Computer level** 0 to 29;
- **Start**, and **Back** (to the list).

With more rows than fit (eight) the list moves with the cursor and says "more above" / "more below"; with one
extra fighter everything fits.

Buttons, everywhere on the page:

| button | does |
|---|---|
| d-pad up / down (or the left stick) | moves the cursor; held, it repeats |
| d-pad left / right | changes the value of the row; L1 / R1 change it by ten (characters, levels) |
| Cross (or START) | chooses: an entry, Start, Back; on a value row, the next value |
| Circle, Triangle or SELECT | one step back: set-up -> list -> closed |
| Esc (keyboard) | the same step back |

The keyboard works the page through the player's own key bindings for those buttons (by default the arrows,
K = Cross, L = Circle, I = Triangle, Right Shift = SELECT, Enter = START), because the page reads the same pad the
game reads. Characters whose files are not in the game data are stepped over; if the character that was stored
is such a one, Start refuses, says which file is missing, and the page stays.

The page plays the main menu's own three sounds (move, choose, cancel), and tells the screen reader the row
under the cursor.

**Start** closes the page and the main menu leaves exactly as it does for one of its own entries (its plates
fly out, the fade), into the game's own character select: Duel's, against the computer, for Team Battle and
Free-for-All; the training mode's for Training. There the player picks their fighter, the opponent, the stage
and the music as always, and the fight starts with the extra fighter(s).

The choice stays for everything that follows from that select: "Fight Again", "Return to Character Select" and
a new pick, going back to the versus menu or the training menu and in again. It ends when the main menu comes
back ("Return to Main Menu", or backing out of the versus / training menu): from then on the game's own Duel
entry is a plain fight of two, unless the Experiments switch is on.

## How it is built

| piece | where | what |
|---|---|---|
| the page's state and rules | port/src/plat_modes.c (new) | the cursor, the draft of the set-up, what the buttons do, Start |
| its interface, and the one constant | port/src/plat_fighters.h (new) | `PORT_EXTRA_MAX`, `PortModesView`, the row kinds |
| the trigger and the hand-over | src/menu/menu_a.c, `MainMenu_PortModes` (inside `#ifdef PORT`) | asks the port on each frame the menu takes input |
| the pads | port/src/plat_stub.c, `scePad2Read` -> `Port_ModesPad` | the page's input; idle pads for the game while it is up |
| the drawing | port/src/gs/ui.cpp, `modes_draw` | the hint and the page, from `Port_ModesView` |
| who is in a fight | port/src/plat_fighters.c, `Port_ExtraSetup` | reads the page's choice beside the Experiments switch |

### The trigger: the main menu, and only while it takes input

`MainMenu_Run` (src/menu/menu_a.c) reads the pads through `MainMenu_Input` only on frames where the screen is
not frozen, not fading and not leaving, and `MainMenu_Input` itself does nothing until the menu's movie says
input is accepted (`gMainMenu->flash[0].flags & MFLASH_PAD`). `MainMenu_PortModes` is called at that same place
under that same condition, and calls `Port_ModesMenu(select pressed)`. That call is the only thing that can open
the page, so SELECT anywhere else (in a fight it changes the camera distance; other menu screens) never reaches
it. The same call is what keeps the hint alive: `Port_ModesMenu` gives the hint three pad reads to live and each
pad read takes one away, so it is gone as soon as the menu stops asking.

Nothing is opened, and no hint is shown, while `Port_NetSession()` or `Port_NetActive()` is true.

### The input: behind the pad read

The settings window keeps the game from the controls by making `Port_PadRead` return nothing
(`gPortOverlayOpen`) and by swallowing the buttons that closed it until they are let go (`PortInput_Swallow`).
The page follows the same two rules one step further in, in `scePad2Read` (port/src/plat_stub.c), after the pad
read has been answered by the controllers and the keyboard **or by a recording** (`BT3_PAD_PLAY`):

- while the page is up, pad 1's buttons are the page's (newly pressed, with a repeat for held directions) and
  both pads are handed to the game idle;
- after it closes, both stay idle until pad 1 has nothing held: Circle that closed the page is not the main
  menu's "cancel" (which would leave for the title screen).

Because of where this sits, the page behaves the same from a controller, from the keyboard and from a pad
recording, and a recording made (`BT3_PAD_REC`) while using the page plays back the same. The page does not set
`gPortOverlayOpen`: that would stop the very reads it lives on. F1 still opens the settings window over the
page; the page then gets no input until the window closes.

The drawing code takes no input at all (it is not an ImGui window: it draws into the picture's background list,
behind the settings window, in the picture's own 512 x 448 units so it is the same at any window size). The one
exception is Esc, which is not a pad button: `Ui_Event` hands it over as "back" (`Port_ModesKey`).

### Start: the main menu's own request

A choice of the main menu is `MAINMENU_CHOOSE(next)`: the flags "chosen" and "leaving", a 15 frame timer,
`gProgress->mode = next`, the movie's "fl_out"; the frame loop then fades out and `Progress_Main` dispatches on
the mode. Duel is mode 38 (the versus menu) and Ultimate Training 44 (the training menu); the character selects
behind them are 39 and 45.

Start makes the same request with `next` = 39 or 45, so the player lands on the select and not on the menu in
front of it. What that menu would have left behind in `gProgress` is set first, in the same function:

| mode | set | the menu's own code |
|---|---|---|
| 39 | `+0x620` (who plays) = 0: 1P vs COM; `+0x624` (battle type) = 0: single | `DuelMenu_Run`: `DUEL_PROG->versus`, `->battleType` (include/menu/menu_g.h) |
| 45 | `+0x7D4` \|= 0x10 | `Train_Input`: `TRAIN_PROG->flags \|= MPTRAIN_SELECT` (include/menu/menu_i.h) |

(The online session enters mode 39 the same way, from port/src/headless.c.) From there on everything is the
game's: `Duel_Main` / `TrainMode_Main` load their archive and run `CharSel_Run`; backing out of the select leads
to the versus menu / the training menu, as it always does; after a fight `Progress_Main` returns to mode 39 / 45.

### The choice: what is stored, and how it relates to the Experiments tab

Start writes, in the settings (bt3_settings.txt):

| name | value |
|---|---|
| `extraN_chara`, `extraN_team`, `extraN_level` | the rows of the extra fighters: **the same rows the Experiments tab edits** (a Free-for-All stores team -1, "its own") |
| `modes_fight` | 1 Team Battle, 2 Free-for-All, 3 Training: "the fights of now were asked on the page" |
| `modes_extras` | how many extra fighters those fights have |
| `modes_count` | the set-up's count, remembered for the next time the page opens |

`Port_ModesLeave`, called where `MainMenu_Run` starts, sets `modes_fight` and `modes_extras` back to 0: that is
"dropped at the main menu". The Experiments switch, `extra_fighters`, is never touched by the page.

So there are two ways a fight gets extra fighters, and one table of who they are:

- the **Experiments switch** (`extra_fighters` > 0): every plain Duel and training fight, until switched off;
- the **page** (`modes_fight` != 0): the fights started from the page's Start, until the main menu is back, and
  only in the kind of fight the mode is (battle mode 0 for Team Battle and Free-for-All, 6 for Training).

`Port_ExtraSetup` looks at the page's choice first, then at the switch; the other rules (docs/port/tagteam_menu.md:
not online, no script, one member a side, one view, not a replay, the character's files) hold for both. With the
page's choice the log says `bt3: extra fighter: character 3, team 0, level 14 (the modes page: Team Battle)`.
The Experiments tab says in a line that the page exists, and in yellow when a page's mode is on. Since the rows
are shared, a character chosen on the page is also what the Experiments row shows afterwards, and the other way
round; a Free-for-All leaves the row's team at "On its own".

`modes_fight` is in the settings file and not only in memory so that it can be seen and tested from outside (an
online session with it set, below). A run that was closed in the middle of such a fight leaves it set; the next
run clears it when it reaches the main menu, which every run does before it can start a Duel or a training
fight. The ways into a fight that do not pass the main menu (an online session started from the command line,
`BT3_DEMO`, `BT3_REPLAY`) are all refused by `Port_ExtraSetup`'s own rules.

"Fight Again" restarts the fight without loading it again, so `Port_ExtraSetup` is not asked and the fight keeps
its fighters (no second log line).

## Testing tools

- `BT3_MODES_LOG=1`: the page says when the main menu begins to take input, and when it opens and closes, with
  the pad read and the frame (for cutting pad recordings and placing pictures).
- `BT3_UI_SHOT=<frame>,<frame>,...:<file.ppm>`: the existing hook, now with a list of frames: several pictures of
  one run, each to `<file>_<frame>.ppm`. (`BT3_SHOT` photographs the game's picture before the port draws on it,
  so the page is not on those.)
- Start and the refusal always log a line (`bt3: modes: ...`), as does the return to the main menu when it drops
  a choice.

## What was checked (2026-10-14, Linux, clang64)

Pad input was scripted: the first 1461 pad reads of a recording that goes from boot through New Game to the main
menu (the menu takes input at read 1459), then presses made by a script, each held four reads. A note on the
recording: `cell.pad` does not enter Duel on this build; its four presses of "up" land on Ultimate Training (the
pictures show the training menu and a fight without a time limit). Its character-select part was reused after
Start; the game's own Duel was entered with presses of my own (down three times from Dragon History).

1. Build: `103 stubs; link OK -> port/build/bt3_64; 0 error lines`.
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0; port/build/heap_2111.bin`, with no settings
   and with a settings file that has the page's choice stored (`modes_fight=1`, `modes_extras=1`; log: `extra
   fighter: not in this fight (a replay is being played)`).
3. The page, with offscreen pictures looked at (`BT3_UI_SHOT` for what the port draws, `BT3_SHOT` for the fights):
   - the main menu with the plate "SELECT  More modes" in the corner of the message window;
   - SELECT: the page "Extra Modes" over the dimmed menu, the cursor on Team Battle; down, down, up: the cursor on
     Training, then on Free-for-All, with each one's text;
   - Circle: the page is gone, the main menu is as it was (the cursor on Dragon History, the hint back); then down
     three times and Cross: the versus menu, 1P VS COM, Single Battle, the select, and a fight of two (Goku and Kid
     Gohan; no `extra fighter` line in the log);
   - Team Battle: the set-up (Extra fighters "1 (of 1)", Fighter 3, Team, Computer level, Start, Back); right
     three times on the character (Goku (End), character 3), right on Team ("The opponent's" -> "Yours"), right on
     the level (14); Start: Duel's character select, and the fight logs `bt3: extra fighter: character 3, team 0,
     level 14 (the modes page: Team Battle)`; the picture has Goku, Cell and the third fighter, whose small bar is
     under player 1's;
   - Free-for-All: its set-up has no Team row; R1 twice and right four times: Nail; the log says `character 24,
     team 2, level 10 (the modes page: Free-for-All)`; the picture has the three, the small bar on the right;
   - Training: right five times on the character, Team "Yours", level 14; `character 5, team 0, level 14 (the modes page: Training)`; the picture is a training
     fight (no time limit) with the three;
   - from the Team Battle fight: pause, Return to Character Select, Yes, Cross through the select: the second
     fight logs the same `extra fighter` line again; pause, Return to Main Menu, Yes: `bt3: modes: back at the main
     menu: fights are the game's own again`; then the game's own Duel (down three times, Cross, 1P VS COM, Single
     Battle, the select): a fight of two, Goku and Cell, no `extra fighter` line, no small bar;
   - a fight left to end (the extra fighter on the opponent's team), then "Fight Again": the fight starts again
     with the third fighter's bar there (no second log line: a restart does not load the fight again);
   - back from a set-up with Circle: the list, the cursor on the mode it was entered from; Circle again: closed.
     (This one through the controllers' and keyboard's path, `BT3_PAD`, with no recording.)
4. SELECT elsewhere: pressed in the Team Battle fight, the camera moves back and stays there (pictures before and
   after), nothing opens; pressed twice in the versus menu and once in the character select, nothing opens (the
   log has one "the page opened" in the whole run, the one on the main menu). The title screen and the other
   modes' screens were not pressed on: the page can only be opened from the call in the main menu's loop.
5. Twelve windowless demo fights, `BT3_THIRD_FIGHTER` 3 / 107 / 60 / 24 with `BT3_THIRD_TEAM` 0 / 1 / 2: all
   twelve print `bt3: battle finished`.
6. Online, the two-copy harness on one machine (in-process session, 50 s), both copies with `modes_fight=1`,
   `modes_extras=1`, `extra0_chara=24` in their settings: `31472 blanks compared (1..31472), 26467 of them in a
   fight; whole state: same; fight values: same`, and both logs say `not in this fight (an online match)`. And
   this build with those settings against the main checkout's program without settings: `fight values: same`
   over 31584 blanks (the whole-state checksum differs from blank 1 there, as it must between two different
   programs). The page was not opened in a session (there is no main menu in one).
7. The Experiments tab: photographed with a page's mode stored (its two new lines are there); and with only
   `extra_fighters=1`, `extra0_chara=24` in the settings the game's own Duel logs `character 24, team 1, level 13
   (the settings)` after the page was opened and closed, and the switch is still 1 in the file afterwards.
8. Game source: src/menu/menu_a.c is the only file under src/ or include/ that changed; all 44 added lines are
   inside `#ifdef PORT`, and with the PORT regions cut out the file is the same as before (a script over the
   diff). No PS2 build was run here.

## Not checked, known limits

- No person and no real controller or keyboard worked the page. The controllers' and keyboard's path
  (`Port_PadRead`) was exercised with scripted buttons (`BT3_PAD`); Esc, the left stick, the repeat of a held
  direction, L1 on a level row, START as "choose" and Triangle as "back" were not pressed.
- A character whose files are missing: the stepping over and Start's refusal were not exercised on the page
  (`Port_ExtraCharaOk`, which decides, was checked in docs/port/tagteam_menu.md).
- The scrolling of the set-up (more than eight rows) cannot be reached with one extra fighter.
- A wide picture (16:9) and the OpenGL back end were not photographed; the page is laid out on the 4:3 page in the
  middle, as the menus are.
- The screen reader's lines were not listened to.
- F1 over the open page, and an online session started while the page is up, were not tried (the code closes the
  page when a session is there).
- Going back from the select to the versus menu and choosing "1P VS 2P" or "Team Battle" there: the fight is left
  as the game's own by `Port_ExtraSetup`'s rules (split picture, more than one member), not tried here.
- Training from the page was checked into the fight; "Return to Main Menu" from it was not (the same call drops
  the choice).
- Windows and macOS builds were not made. No PS2 build was run.

## More than one extra fighter: what changes here

The page is written for extra fighters 0..N-1; what holds it at one is `PORT_EXTRA_MAX` (port/src/plat_fighters.h).
Raising it gives, with no other change in this branch's code: a count row that goes to N, three rows (two in a
Free-for-All) for each fighter with a list that moves, `extra1_*` ... settings (the names are made from the
index), and Start storing all of them and `modes_extras` = the count.

What does not follow by itself:

- `Port_ExtraSetup` keeps ONE character, team and level and reads entry 0 only; the battle code's three questions
  (`Port_ThirdFighter`, `Port_ThirdTeam`, `Port_ThirdLevel`) have no index. docs/port/tagteam_menu.md lists that
  whole path (the loader's one slot, the one object, `Port_Team`). "Its own team" for entry k has to become
  team 2 + k there; the page already stores -1 for each.
- The page's teams are "yours / the opponent's / its own". For five against five that is enough; for a mixed
  fight (three teams of three) the Team row needs team numbers, and `extraN_team` values past 1.
- The Experiments tab still draws one row and a switch.
- Free-for-All's text and the list's "so 3 in a fight" are computed from the constant; the hint and the list need
  nothing.
- Human players past two (pads 3 and 4) are a different matter: the page reads pad 1 only, and every extra
  fighter is the computer's.
