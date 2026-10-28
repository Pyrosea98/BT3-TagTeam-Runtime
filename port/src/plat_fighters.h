/* Extra fighters (plat_fighters.c) and the extra modes of the main menu that lead to such fights (plat_modes.c). C and C++. */
#ifndef PORT_PLAT_FIGHTERS_H
#define PORT_PLAT_FIGHTERS_H
#ifdef __cplusplus
extern "C" {
#endif

/* How many extra fighters a fight can have in this build, PORT_EXTRA_MAX (eight: ten fighters in all). THE one
   number: the battle code's tables, the settings' rows, the page's count control and the Experiments tab are all
   held to it. */
#include "../include/port_fighters.h"

/* plat_fighters.c: the settings, held to their ranges */
int Port_ExtraCount(void);      /* the Experiments tab's switch: how many extra fighters every Duel / training fight gets */
int Port_LocalPlayers(void);    /* 2..4: players 3 and 4 in a two-player Team Battle / Free-for-All */
void Port_LocalPlayersSet(int n);
int Port_LocalPlayersPreference(void);
void Port_LocalPlayersMatchSet(int n);
int Port_ExtraChara(int index); /* extra fighter `index`: its character 0..160 */
int Port_ExtraTeam(int index);  /* 0, 1: a player's team; -1: its own */
int Port_ExtraLevel(int index); /* its computer level 0..29 */
void Port_ExtraSet(int count, int index, int chara, int team, int level);
void Port_ExtraSetFighter(int index, int chara, int team, int level); /* the same without the switch (not written to the file yet) */
int Port_ExtraCharaOk(int chara, char *why, int size); /* its files are in the game data */
int Port_ExtraLast(void);                              /* what the last fight's set-up decided */
int Port_ExtraSetup(int mode, int members0, int members1, int split, int replay, int script); /* the loader's: a fight is being loaded */
int Port_ExtraAdd(int side, int index, int chara, int costume, int level);
void Port_ExtraDone(void);
int Port_TestRoster(int side, int index);
const char *Port_CharaName(int chara); /* a character's name from the game data ("" if it gave none); the window's thread only */

/* plat_modes.c: the extra modes, the main menu's second set of entries (SELECT there) */
enum { PORT_MODE_NONE, PORT_MODE_TEAM, PORT_MODE_FFA, PORT_MODE_TRAINING, PORT_MODE_COUNT };

typedef struct PortModesView { /* what the main menu shows now, for the code that draws the hint (gs/ui.cpp) */
    int hint;   /* the game's main menu is up and takes input: the hint is shown */
    int set;    /* 1: the menu shows the extra modes' entries, 0: its own */
    int cursor; /* the extra mode the cursor is on, 0..2 (or the number of players less one, 0..3) */
    int players; /* 1: the list is the number of players of the mode chosen (Team Battle, Free-for-All) */
} PortModesView;

void Port_ModesView(PortModesView *out);
int Port_ModesFight(void);     /* PORT_MODE_ the fights of now were chosen with on the main menu, 0: none (and none online) */
int Port_ModesCount(void);     /* ... and how many of the Experiments tab's extra fighters such a fight gets besides */
const char *Port_ModesName(int mode);
const char *Port_ModesPlayersName(int n); /* "1 Player" .. "4 Players" */

#ifdef __cplusplus
}
#endif
#endif
