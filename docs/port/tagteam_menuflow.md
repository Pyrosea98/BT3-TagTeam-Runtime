# The extra modes in the main menu itself, and the game's team select

Notes of the branch `tt-menuflow` (from `tagteam-exp` at b00e7d4). It replaces the page of extra modes of
docs/port/tagteam_modes.md: that page was a window of the port's own, drawn over the main menu, and the extra
fighter was picked there from a list of names. What was wanted instead: SELECT changes the main menu's OWN entries,
and a mode leads to the game's own team character select.

## What a player sees

On the main menu, once it takes input, the small plate in the corner of the message window says
**SELECT  More modes** (as before). SELECT on pad 1 switches the menu to a second set of entries:

| entry | on the plate (and with the icon) of | leads to |
|---|---|---|
| Team Battle | Duel | the game's team character select: up to five fighters a side |
| Free-for-All | Dragon World Tour | the same team select: every picked fighter is to be a team of its own |
| Training | Ultimate Training | the training mode's character select (one against one) |

It is the same menu: the same plates in the same places, the lit plate under the cursor with its animated icon, the
menu's move / choose / cancel sounds, the title, the guides, the clouds. The three names stand where the entries'
names stand. The plates the list of three does not use (the one above the first row, the fourth row, the one below,
the one that scrolls in) and the two scroll arrows are not shown. The guides do not speak in this set (they have
no lines for it): the message window has two lines of text about the mode under the cursor instead, printed with
the game's own font. The plate in the corner reads **SELECT  Back**.

| button | in the set |
|---|---|
| d-pad up / down | moves the cursor over the three; it wraps, nothing scrolls |
| Cross (the game's confirm) | chooses the mode: the plate flashes, the menu leaves as for any entry |
| SELECT, Triangle (the game's cancel) or Circle | back to the game's own entries |

Back puts the menu's own list where it was (the same entry under the cursor, the same scroll position) and a guide
reads that entry out, as after a cursor move. Triangle there is the game's own cancel again (to the title screen).

After the choice everything is the game's: the team select (pick a character, Custom Select, Select Color; the list
1..5 with END), then the same for the computer's side, Map Select, the fight. Back at the main menu ("Return to
Main Menu", or backing out through the versus / training menu) the mode is dropped: the game's own entries start
plain fights, the cursor is on the entry it was on before SELECT, and the versus menu opens on what the player
last chose there himself (not on the team battle the mode was entered with).

## How it is built

| piece | where | what |
|---|---|---|
| the set: list, cursor, buttons, hidden plates, the message line | src/menu/menu_a.c, inside `#ifdef PORT` | `MainMenu_PortModes`, `MainMenu_PortModesSwitch`, `MainMenu_PortModesPlates`, `MainMenu_PortModesHelp` |
| the names on the plates | port/src/gs/menu_text.c | `MenuText_RelabelSheets`, `MenuText_Relabel`, and the rows in `MenuText_Render` |
| decoding a label sheet as the renderer will | port/src/gs/namefont.c | `NameFont_DecodeSheet` (its `sheet_decode`, handed out) |
| what is offered, stored, logged | port/src/plat_modes.c (rewritten, a third of its size) | `Port_ModesMenu`, `Port_ModesShow`, `Port_ModesChoose`, `Port_ModesLeave`, `Port_ModesFightLog` |
| the hint plate, the screen reader's line | port/src/gs/ui.cpp, `modes_draw` | "More modes" / "Back"; the entry under the cursor is said when it changes |
| the log line of a mode fight | src/menu/menu_a_b.c, `Progress_Main`, inside `#ifdef PORT` | calls `Port_ModesFightLog` when the menus hand over to a battle |

Gone: the page and its set-up (the drawing in ui.cpp, the page's input behind the pad read, `Port_ModesKey`,
`Port_ModesRow`, `Port_ModesSound`, the setting `modes_count`). The pad read is no longer touched: `Port_ModesPad`
only counts reads (for the hint's life and for `BT3_MODES_LOG`). `PORT_EXTRA_MAX`, the Experiments tab and
plat_fighters.c are as they were.

### The main menu's own machinery, and why the set is made this way

`MainMenu` is a ring of items (`items[]`, `itemCount`), of which six plates are on screen (`top` is the list index
on the plate above the first row) and four rows can have the cursor. Every frame `MainMenu_Draw` points each
plate's clips at its item's pictures: the name is row `item % 8` of sheet `item / 8` of the label textures
("mc_menu_text_off" for a plate not under the cursor, "mc_menu_text_on" for the one under it), the icon is a cell
of the icon sheets. The guides' spoken line `item * 4 + guide + 0x10` is also the subtitle the message window
shows. There is no text in this menu to give other words to: an entry's name is a picture.

So the set is the menu with another list. `MainMenu_PortModesSwitch(1)` keeps the menu's own `items`, `itemCount`,
`top`, `cursor`, `extra` aside and puts in a list of three ITEM NUMBERS OF THE GAME, 3, 2 and 6 (Duel, Dragon World
Tour, Ultimate Training), with the first on the first row. From there the game's own draw code does everything:
plates, icons, the lit plate, the icon's animation. Two things are added to the draw:

- the plates 1, 5, 6, 7 and the arrows get `Flash_ClipSetFlags(..., 2, 0)` (not visible) on every frame the set is
  up, and once `..., 1` on the first frame the menu's own list is back;
- the message line: `Font_PrintAsciiAt` with a white, drop-shadowed style just before `MsgWin_Draw`, whose
  `Font_FlushAll` then draws it over the window, exactly where the subtitles are. `voiceLine` is -1 meanwhile and
  the voice being spoken is stopped (`Voice_StopWithLip`).

The cursor is moved as the menu moves it (`MainMenu_PlateGoto` "fl_off_start" / "fl_on_start", sound 0), only
without the scrolling branch. A choice is `MAINMENU_CHOOSE(next)` + "fl_ok" + sound 1, the menu's own way out.
The input function is asked on the frames `MainMenu_Input` would be (and only once the menu has lit its first
plate), and returns 1 when it took the frame, so the menu's own input does not also see the button.

Why not new plates or a new item table: the movie has seven plate clips and the textures have eleven names; a
twelfth item would need a new row in a texture the disc does not have, and new voice lines. Reusing three items
and changing what their name rows SHOW needs nothing of the disc changed.

### The names: a row of the label sheet drawn with other words

The port already draws menu labels from a font for the "Menu font" setting: `MenuText_Render` is given every
indexed texture the renderer decodes, knows a label sheet by a hash of its pixels (labels.tsv), and draws the rows
again in the row's place, size, colours and outline, several times the PS2's resolution. The plates' animation,
tint and clipping go on working because the texture keeps its layout.

The set uses exactly that, for three rows of the two main-menu sheets (512 x 256; `tex[8]` "off", `tex[12]` "on"):

- When the menu is set up it hands over the two texture entries (`Port_ModesSheets` -> `MenuText_RelabelSheets`).
  They are decoded the way the renderer will decode them (`NameFont_DecodeSheet`) and hashed: on the USA disc the
  hashes come out as labels.tsv has them (95b191599225f2cd, b36ea735d2c5764d), but **labels.tsv is not needed**:
  the menu's own textures say which sheets they are.
- `MenuText_Relabel(row, text)` sets the words of a row (rows 3, 2, 6 while the set is up) and a key made of them.
  `MenuText_TexKey` mixes the key into the texture cache's identity of 512 x 256 indexed textures, so the switch is
  a different cached texture and going back and forth decodes nothing twice.
- In `MenuText_Render` the sheet's other rows are what they would be anyway (the font's with the setting on, the
  disc's pixels with "Original"), and the three rows are drawn by the same `menu_text_row`, with the measures of
  the row's own disc label when labels.tsv has it (so an edited style in labels.json is followed) and otherwise the
  ones measured on the USA disc (letter height 13 / 17, baseline 22 / 23, margin 8, the "main-menu" and "orange"
  colours).

**With "Menu font" on "Original"** there is no lettering of the game for new words (the names are pictures, and
"Team Battle" is not among the main menu's), so the three names are always drawn from a font: the Menu font when
one is chosen, else the first font of fonts.tsv that can be used, a bold one first (DejaVu Sans Condensed Bold
of the bundled three). The other rows of the sheet stay the disc's pixels. This was preferred over composing the
words from the letters of the disc's name pictures (namefont.c does that for stage and song names, for the two
styles of the stage select only) because it is one way of drawing for both settings and uses the main menu's own
row measures.

If the sheets are not of the layout that is decoded (another edition of the disc), or no font can be used
(`BT3_FONTS=` or a missing fonts folder), the set is **not offered**: no hint, SELECT does nothing, and the log says
once `bt3: modes: the main menu's entries cannot be given the extra modes' names ...`. (Showing the set with the
game's names on the plates would be wrong.) In a run without a picture (`BT3_GS=none`) no font is looked for and
the set works.

### A choice: the game's team select

The versus menu (mode 38) leaves `DUEL_PROG->versus` (+0x620: 0 = 1P VS COM) and `->battleType` (+0x624: 0 single,
1 team, 2 DP) in `gProgress`; `Duel_Main` then runs mode 39 (`CharSel_Run`) or 40 (`TeamSel_Run`), and after a
fight `Progress_Main` comes back to 40 when +0x624 is not 0. Team Battle and Free-for-All therefore set +0x620 = 0
and +0x624 = 1 and choose mode **40**: the player lands on the team select of "Duel, 1P VS COM, Team Battle", picks
his side's list and then the computer's, and all that follows (Map Select, the fight, Fight Again, Return to
Character Select) is the game's. Backing out of the select leads to the versus menu, as always.

The two values the player had there are kept (`sPortModesDuel`) and put back when the main menu is entered again
with such a mode dropped, so the versus menu does not open on "Team Battle" because of a mode.

**Training** chooses mode 45 with `MPTRAIN_SELECT` set, as before. Training has no team variant: `TrainMode_Main`
(src/menu/menu_h_c.c) has two screens, the training menu (44) and `CharSel_Run` (45), the one-against-one select,
and battle mode 6 is "one member per side". So the Training entry is a one-on-one select, and its extra fighter is
the one of the Experiments tab's row (`extra0_chara` / `_team` / `_level`), as before; the message line says so.

### What is stored: the interface to the fight

| setting | value | written | cleared |
|---|---|---|---|
| `modes_fight` | 1 Team Battle, 2 Free-for-All, 3 Training, 0 none | `Port_ModesChoose`, when a mode is chosen | `Port_ModesLeave`, where `MainMenu_Run` starts |
| `modes_extras` | how many of the Experiments rows' fighters the fight gets besides: 1 for Training, 0 for the other two | the same | the same |

`Port_ModesFight()` returns the mode, and **0 during an online session** whatever is stored (so does
`Port_ModesCount()`). With modes 1 and 2 the fighters are the two native team lists (`BattleSide_GetMemberCount`,
`BattleSide_GetMemberChara`): nothing else is stored for them.

What the fight does with it today (plat_fighters.c and the battle code are untouched here): with mode 1 or 2
`Port_ExtraSetup` is asked for no extra fighter, so the fight is the game's ordinary team battle, members coming in
one at a time. Making all members fight at once is the other branch's work (`tt-many`). With mode 3 it adds the
Experiments row's fighter to the training fight; its log line still says "(the modes page: Training)".

When the menus hand over to a battle (`Progress_Main` returning), a mode fight logs one line:

```
bt3: modes: Team Battle fight: side 1 (3): 0 13 22; side 2 (2): 13 22
bt3: modes: Free-for-All fight: side 1 (2): 0 13; side 2 (3): 13 22 0
bt3: modes: Training fight: side 1 (1): 0; side 2 (1): 13
```

(the count in brackets, then the members' character ids in list order). Only when the battle is of the kind the
mode leads to (battle mode 0, or 6 for Training). "Fight Again" restarts without the menus: no second line.

Other lines: `bt3: modes: Team Battle chosen: to the game's team character select`, `bt3: modes: back at the main
menu: fights are the game's own again`; with `BT3_MODES_LOG=1` also when the menu takes input and when it switches
sets, with the pad read and the frame.

### Online

`Port_ModesMenu` answers "not offered" while `Port_NetSession()` or `Port_NetActive()`: no hint, SELECT is the
menu's (which has no use for it). `Port_ModesChoose` refuses, and `Port_ModesFight` is 0, during a session.

## What was checked (2026-10-14, Linux, clang64)

Pad input was scripted (`BT3_PAD_PLAY`): the first 1461 ticks of a recording that goes from boot through New Game
to the main menu, then presses made by a script (each held four ticks). Pictures were made without a window
(`SDL_VIDEODRIVER=offscreen`, `BT3_GS=gpu`), `BT3_UI_SHOT` for the main menu (the hint is the port's), `BT3_SHOT`
for the rest, and looked at.

1. Build: `103 stubs; link OK -> port/build/bt3_64; 0 error lines`.
2. Replay regression: `bt3: tick 2111 (vblank 5431, seq 4): hp 23050 0`, with no settings and with `modes_fight=1`,
   `modes_extras=1` stored.
3. The menu and the modes:
   - the main menu with its own entries (Options above; Dragon History lit; Ultimate Battle, Dragon World Tour,
     Duel; Dragon Net Battle below; the arrows) and the plate "SELECT  More modes";
   - SELECT: the same picture with three plates, Team Battle lit with Duel's icon, Free-for-All, Training, the
     plates above and below and the arrows gone, the message window's two lines, the plate "SELECT  Back";
   - down, down, down: Free-for-All lit, Training lit, Team Battle lit again (wrapped); up: Training;
   - Circle, Triangle and SELECT each: the menu's own entries again, Dragon History lit, a guide's line in the
     window; down then moves to Ultimate Battle;
   - from there the game's own Duel (down three times, Cross, 1P VS COM, Single Battle, the select, Map Select):
     a fight of two, no `modes` line, no settings written;
   - Team Battle: the game's team select; Goku, Kid Gohan, Piccolo on the player's list, END; Kid Gohan, Piccolo on
     the computer's, END; Map Select; the fight with the team rows under the two gauges; log
     `Team Battle fight: side 1 (3): 0 13 22; side 2 (2): 13 22`. Then pause, Return to Main Menu, Yes: `back at the
     main menu`, the settings file has `modes_fight=0`, the menu shows its own entries with Dragon History lit, the
     versus menu opens with Single Battle first, and Duel is a fight of two;
   - Free-for-All: the same select; `Free-for-All fight: side 1 (2): 0 13; side 2 (3): 13 22 0`; the fight has the
     team rows (it runs as a team battle today);
   - Training: the training select; `Training fight: side 1 (1): 0; side 2 (1): 13` and `extra fighter: character
     0, team 1, level 13 (the modes page: Training)`; the picture has no time limit and the third fighter's small
     bar; pause (six rows there), Return to Main Menu, Yes: dropped, and Duel afterwards is a fight of two.
4. SELECT elsewhere: in the versus menu and in the character select nothing changes on screen and nothing is
   logged; in a fight it is the camera distance: in the game's own training fight (two fighters standing still) the
   player is drawn smaller after each of two presses. No port code looks at the button outside the main menu.
5. The Menu font: with "Original" (no settings) and with Liberation Sans Bold (`menu_font_id=628986293`) the three
   names are drawn; in the second case in that font, like the menu's own entries. With `BT3_MENU_FONT=` (no list
   of labels) they are drawn too. With `BT3_FONTS=` (no fonts) the set is not offered and the log says so.
6. Twelve windowless demo fights, `BT3_THIRD_FIGHTER` 3 / 107 / 60 / 24 with `BT3_THIRD_TEAM` 0 / 1 / 2: all twelve
   print `bt3: battle finished`.
7. Online, two copies on one machine (in-process session, 50 s), both with `modes_fight=1`, `modes_extras=1`,
   `extra0_chara=24` in their settings: `whole state: same; fight values: same`, and neither log has an `extra
   fighter` line.
8. Game source: src/menu/menu_a.c and src/menu/menu_a_b.c are the files under src/ and include/ that differ from
   b00e7d4; with the PORT regions removed both are the same text as b00e7d4's (a script). No PS2 build was run.
9. The FFA sequence without a picture (`BT3_GS=none`): the same log lines.

## Not checked, known limits

- No person and no real controller or keyboard used it; the left stick was not tried (the menu itself reads the
  d-pad only, here as for its own entries).
- The guides' mouths and the sounds were not looked at / listened to (the runs had no sound device); the screen
  reader's lines were not listened to.
- The switch has no animation of its own: the names change on the frame of the press, the plates do not fly out
  and in.
- With the renderer on its own thread the names could change one frame before or after the cursor does.
- A wide picture (16:9), the OpenGL back end, Windows and macOS were not tried.
- The team select with five fighters a side, characters that have forms to choose from, and 1P VS 2P / DP Battle
  chosen in the versus menu after backing out of the select while a mode is on, were not tried. (`modes_fight`
  stays set until the main menu, whatever is chosen in the versus menu meanwhile.)
- After a Free-for-All the main menu's cursor is back on the entry it had before SELECT, not on a mode.
- BT3_MODES_LOG prints "the main menu shows its own entries" also when the menu is left through a mode (the names
  are taken off as the screen ends).
- The Experiments tab's wording was changed and not photographed.

## For the merge with the many-fighters branch

- Read the mode with `Port_ModesFight()` (it is 0 online). Modes 1 and 2: the fighters are the setup's two member
  lists; in mode 2 every member is to be its own team. Mode 3: one member a side plus `Port_ModesCount()` (1) of
  the Experiments rows.
- `Port_ExtraSetup`'s own log text "(the modes page: ...)" and its refusal "a side has more than one member" are
  yesterday's; this branch did not touch plat_fighters.c or the battle code.
- `Port_ExtraSetFighter` (plat_fighters.c) has no caller left.
