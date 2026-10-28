/*
 * The extra modes: fights with more than two fighters (Team Battle, Free-for-All, Training), entered from the
 * game's main menu. SELECT there switches the menu ITSELF to a second set of entries: the same plates, cursor,
 * sounds and movie, with the modes' names on three of the plates; SELECT or cancel switches back. Choosing one
 * leaves the menu the way one of its own entries does, for the game's own TEAM character select (Duel, 1P VS COM,
 * Team Battle) or the training mode's select, and everything from there on is the game's.
 *
 * Who does what:
 *   - src/menu/menu_a.c (the main menu) holds the set: which entries it lists, the cursor, the buttons, the
 *     plates that are hidden, the line in the message window. It asks here whether the set is offered
 *     (Port_ModesMenu, on every frame it takes input: that also keeps the hint alive), says what it shows
 *     (Port_ModesShow, Port_ModesCursor) and hands over a choice (Port_ModesChoose).
 *   - gs/menu_text.c draws the words: an entry's name is a row of the menu's label sheets, and while the set is
 *     showing those rows are drawn with the modes' names (MenuText_Relabel).
 *   - gs/ui.cpp draws the small hint in the message window ("SELECT  More modes" / "SELECT  Back").
 *   - src/menu/menu_a_b.c calls Port_ModesFightLog when the menus have set a battle up: one line in the log with
 *     the mode and each side's members.
 *
 * What a choice stores, in the settings: modes_fight (PORT_MODE_: 1 Team Battle, 2 Free-for-All, 3 Training) and
 * modes_extras (how many of the Experiments tab's extra fighters the fight gets besides: one in Training, whose
 * select is one against one; none in the other two, whose fighters are the teams picked on the select).
 * Port_ModesLeave, where the main menu starts, sets both back to 0: the game's own entries are plain again.
 *
 * None of it in an online session: the set is not offered, and Port_ModesFight says "none" during one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_fighters.h"
#include "coop_selection.h"

/* the progress modes a choice asks the main menu for (src/menu/menu_a_b.c) */
#define PROGRESS_TEAM_SELECT 40
#define PROGRESS_TRAINING_SELECT 45

extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern int Port_NetActive(void), Port_NetSession(void);            /* gs/net.c */
extern int MenuText_RelabelSheets(unsigned offEntry, unsigned onEntry); /* gs/menu_text.c */
extern void MenuText_Relabel(int row, const char *text);
extern int Battle_GetMode(void);                    /* the game: the battle the menus have set up */
extern int BattleSide_GetMemberCount(int side);
extern int BattleSide_GetMemberChara(int side, int idx);
extern unsigned gGsFrame; /* gs/gs_core.c: pictures drawn so far (BT3_MODES_LOG) */

static volatile int sHint;   /* pad reads the hint still has: the main menu renews it on every frame it takes input */
static volatile int sSet;    /* the main menu shows the extra modes' entries */
static volatile int sCursor; /* ... and its cursor is on this one (0..2) */
static int sReady;           /* the menu's label sheets can be given the modes' names */
static int sRows[4];         /* the label rows that have them now */
static int sRowCount;
static volatile int sPlayers; /* the list is the number of players */
static int sEntered;         /* the progress mode the main menu left for with the mode that is on, 0: none */
static unsigned sReads;      /* pad 1 reads so far (BT3_MODES_LOG) */

static int logging(void) { /* BT3_MODES_LOG=1: what happened at which pad read and frame (for making test input) */
    static int on = -1;
    if (on < 0) {
        on = getenv("BT3_MODES_LOG") != NULL;
    }
    return on;
}

static int online(void) {
    return Port_NetSession() || Port_NetActive();
}

const char *Port_ModesName(int mode) {
    static const char *const kNames[PORT_MODE_COUNT] = {"", "Team Battle", "Free-for-All", "Training"};
    return mode > PORT_MODE_NONE && mode < PORT_MODE_COUNT ? kNames[mode] : "";
}

/* The mode the fights of now were chosen with on the main menu; none during an online session, whatever is stored. */
int Port_ModesFight(void) {
    int m = Port_Setting("modes_fight", 0);
    return m > PORT_MODE_NONE && m < PORT_MODE_COUNT && !online() ? m : PORT_MODE_NONE;
}

/* ... and how many of the Experiments tab's extra fighters such a fight gets besides its own. */
int Port_ModesCount(void) {
    int n = Port_Setting("modes_extras", 0);
    return Port_ModesFight() == PORT_MODE_NONE || n < 0 ? 0 : n > PORT_EXTRA_MAX ? PORT_EXTRA_MAX : n;
}

void Port_ModesView(PortModesView *out) {
    out->hint = sHint > 0;
    out->set = sSet;
    out->cursor = sCursor;
    out->players = sPlayers;
}

const char *Port_ModesPlayersName(int n) {
    static const char *const kNames[4] = {"1 Player", "2 Players", "3 Players", "4 Players"};
    return n >= 1 && n <= 4 ? kNames[n - 1] : "";
}

/* Pad 1 was read (scePad2Read, plat_stub.c): the hint lives a few reads past the main menu's last frame of input. */
void Port_ModesPad(int socket) {
    if (socket == 0) {
        sReads++;
        if (sHint > 0) {
            sHint--;
        }
    }
}

/* The main menu was set up: its two label sheets (the plates not under the cursor, the plate under it; addresses
   of their texture entries). The set is offered only when the modes' names can be put on them. */
void Port_ModesSheets(int off, int on) {
    sSet = 0;
    sReady = MenuText_RelabelSheets((unsigned)off, (unsigned)on);
    if (!sReady) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "bt3: modes: the main menu's entries cannot be given the extra modes' names (its label sheets are "
                            "not the ones known, or no font of the list can be used): SELECT does nothing there\n");
        }
    }
}

/* The main menu is up and takes input this frame. Returns 1 when the extra modes are offered (SELECT switches). */
int Port_ModesMenu(void) {
    if (!sReady || online()) {
        return 0;
    }
    if (sHint == 0 && logging()) {
        fprintf(stderr, "bt3: modes: the main menu takes input (pad read %u, frame %u)\n", sReads, gGsFrame);
    }
    sHint = 3; /* (some reads' worth: the menu's frames come one read apart) */
    return 1;
}

/* The main menu shows the extra modes' entries on the plates of its own entries rows[0..2] (their label rows get
   the modes' names), or with NULL its own entries again. */
int Port_ModesChoose(int entry); /* (below) */

static void show_names(const int *rows, int n, int players) {
    int i;

    for (i = 0; i < sRowCount; i++) {
        MenuText_Relabel(sRows[i], NULL);
    }
    sRowCount = 0;
    sSet = rows != NULL;
    sPlayers = rows != NULL && players;
    sCursor = 0;
    for (i = 0; i < n && i < 4 && rows != NULL; i++) {
        sRows[sRowCount++] = rows[i];
        MenuText_Relabel(rows[i], players ? Port_ModesPlayersName(i + 1) : Port_ModesName(i + 1));
    }
}

/* The list of the number of players (the main menu, after Team Battle or Free-for-All): rows[0..3] are given
   "1 Player" .. "4 Players". */
void Port_ModesShowPlayers(const int *rows) {
    show_names(rows, 4, 1);
}

/* The number of players `n` (1..4) was chosen for mode entry `entry`: the Experiments tab's Local players is set
   from it (3 or 4: players 3 and 4 play the teams' second members; 1 or 2: the game's own), and the mode is chosen
   as Port_ModesChoose does. */
int Port_ModesChoosePlayers(int entry, int n) {
    Port_LocalPlayersMatchSet(n >= 3 ? n : 2);
    fprintf(stderr, "bt3: modes: %s for %s\n", Port_ModesName(entry + 1), Port_ModesPlayersName(n));
    return Port_ModesChoose(entry);
}

void Port_ModesShow(const int *rows) {
    show_names(rows, PORT_MODE_COUNT - 1, 0);
    if (logging()) {
        fprintf(stderr, "bt3: modes: the main menu shows %s (pad read %u, frame %u)\n", sSet ? "the extra modes" : "its own entries",
                sReads, gGsFrame);
    }
}

void Port_ModesCursor(int entry) {
    sCursor = entry;
}

/* Entry `entry` (0 Team Battle, 1 Free-for-All, 2 Training) was chosen: stored, and the progress mode the main
   menu is to leave for is returned (0: nothing chosen). */
int Port_ModesChoose(int entry) {
    int mode = entry + 1;

    if (mode <= PORT_MODE_NONE || mode >= PORT_MODE_COUNT || online()) {
        return 0;
    }
    Port_SettingSave("modes_fight", mode);
    Port_SettingSave("modes_extras", mode == PORT_MODE_TRAINING ? 1 : 0);
    Port_SettingsWrite();
    fprintf(stderr, "bt3: modes: %s chosen: to the game's %s\n", Port_ModesName(mode),
            "team character select");
    sEntered = PROGRESS_TEAM_SELECT;
    return sEntered;
}

/* The main menu starts: a mode chosen there before is over (and one left by a run that was closed in the middle of
   such a fight goes the same way: every start of the program comes through here before a fight). Returns the
   progress mode that was entered for a mode chosen in THIS run and dropped now, else 0. */
int Port_ModesLeave(void) {
    int entered = sEntered;

    sEntered = 0;
    Port_CoopReset();
    Port_LocalPlayersMatchSet(0);
    if (Port_Setting("modes_fight", 0) != 0 || Port_Setting("modes_extras", 0) != 0) {
        Port_SettingSave("modes_fight", 0);
        Port_SettingSave("modes_extras", 0);
        Port_SettingsWrite();
        fprintf(stderr, "bt3: modes: back at the main menu: fights are the game's own again\n");
    }
    return entered;
}

/* The menus have set a battle up and hand over to it. With a mode on, and the battle of the kind the mode leads to
   (the versus select's, or training's), one line says the mode and who was picked on each side. */
void Port_ModesFightLog(void) {
    char line[256];
    int mode = Port_ModesFight(), side, i, n, at;

    if (mode == PORT_MODE_NONE || Battle_GetMode() != (mode == PORT_MODE_TRAINING ? 6 : 0)) {
        return;
    }
    at = snprintf(line, sizeof(line), "bt3: modes: %s fight:", Port_ModesName(mode));
    for (side = 0; side < 2; side++) {
        n = BattleSide_GetMemberCount(side);
        at += snprintf(line + at, sizeof(line) - (size_t)at, "%s side %d (%d):", side ? ";" : "", side + 1, n);
        for (i = 0; i < n && i < 8 && at < (int)sizeof(line) - 16; i++) {
            at += snprintf(line + at, sizeof(line) - (size_t)at, " %d", BattleSide_GetMemberChara(side, i));
        }
    }
    fprintf(stderr, "%s\n", line);
}
