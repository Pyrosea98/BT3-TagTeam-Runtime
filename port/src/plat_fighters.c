#include "tag_tournament_port.h"
/*
 * Extra fighters (the experiment of more than two fighters): who they are, and whether a fight has them.
 *
 * The battle code (src/battle) asks through port/include/port_fighters.h: how many extra fighters the fight has
 * (Port_FightExtras, 0..PORT_EXTRA_MAX) and for extra k its character, costume, team and computer level. Extra k
 * is roster index 2 + k; fighters 0 and 1 are the two sides' own. The answers are decided ONCE PER FIGHT, by
 * Port_ExtraSetup, which the loader calls just before it asks for the fighters' models: a setting changed in the
 * middle of a fight changes the next one, and a fight never sees two answers. (Port_ThirdFighter, Port_ThirdTeam
 * and Port_ThirdLevel are extra 0 by its older name.)
 *
 * Where the answers come from, the first that applies:
 *   - the environment (the tests), in every kind of fight:
 *       BT3_EXTRA_FIGHTERS=<character>:<team>[:<level>],<character>:<team>,...   any number up to PORT_EXTRA_MAX;
 *                         team 0 is player 1's, 1 player 2's, 2 and up are further teams; level 0..29
 *       BT3_THIRD_FIGHTER=<character 0..160>   one, with BT3_THIRD_TEAM (1 with player 2, the default; 0 with
 *                         player 1; 2 on its own) and BT3_THIRD_LEVEL (0..29)
 *   - the sides' own team rosters, when the setting modes_fight says so: 1 Team Battle, 2 Free-for-All (written
 *     by the menu code, cleared at the main menu; BT3_MODES_FIGHT=<n> stands in for it in a test, in any kind of
 *     fight). Every member the players picked on the game's own team select fights AT ONCE: fighter 0 is side 0's
 *     first member, fighter 1 side 1's, and the other members are the extra fighters, side 0's first. In a Team
 *     Battle a fighter's team is its side; in a Free-for-All everyone is alone. The loader hands the members ove
 *     (Port_ExtraAdd) and then leaves each side ONE member, so that the game's own switching of members has
 *     nothing to switch. Only in a fight of the versus menu, with no script, one view, not a replay.
 *   - else the settings' rows (the Experiments tab of the settings window, port/src/gs/ui.cpp, and the Training
 *     entry of the page of extra modes, plat_modes.c):
 *         extra_fighters   how many extra fighters (the Experiments switch), or modes_extras with modes_fight 3
 *         extraN_chara     extra N's characte
 *         extraN_team      its team: 0 player 1's, 1 player 2's, -1 a team of its own (team 2 + N)
 *         extraN_level     its computer level, 0..29
 *     From the settings a fight gets them only when it is a plain one against one of Duel or of the training
 *     mode: no script, one member a side, one view, not a replay.
 *
 * Whatever asked, there are none in an online match (the two copies must compute the game's own fight), and a
 * character whose files are not in the data folder is left out (the loader would wait for them for ever).
 *
 * The characters' names for the window are read from the player's own data (the versus select's text in the
 * first menu archive): nothing of them is in the repository.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gs/menu_labels.h"
#include "plat_beta8.h"
#include "plat_fighters.h"
#include "port_fighters.h"

#define CHARA_COUNT 291  /* characters 0..160 (MCHR_RANDOM, include/menu/menu_d.h) */
#define NAME_COUNT CHARA_COUNT /* Beta8: the mod's named roster goes past the stock 161 */
#define EXTRA_MAX PORT_EXTRA_MAX /* extra fighters a fight can have so far */
#define FILE_CHARA 0x590 /* file ids of character c: c * 10 + this is its model, + 8 and + 9 its two animation files
                            (what battle_load.c asks for) */

extern int Port_Setting(const char *name, int def); /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);
extern int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size); /* plat_file.c */
extern int Port_NetActive(void);                                    /* gs/net.c */
extern int PortA11y_Utf8(char *out, int cap, const uint16_t *str); /* gs/a11y.c: the game's text as UTF-8 */

/* The fight's answers (set by Port_ExtraSetup on the game's thread, read by it). */
typedef struct FightExtra {
    int chara, costume, team, level;
    int member; /* side * 8 + index of the member of the battle setup it was made from, -1: none */
    int pad;    /* the controller that plays it (Local players), -1: the computer */
    int consumed; /* a fusion merged it: it is out of the fight and cannot partner again */
} FightExtra;
static FightExtra sFight[PORT_EXTRA_MAX];
static int sFightCount;
static int sFightRoster; /* PORT_MODE_TEAM or PORT_MODE_FFA while the sides' members are being handed over, and after */
static const char *sFightFrom = ""; /* who asked, for the log */
static int sFightSplit; /* the fight being set up is a split-screen one (two players) */
/* What the last set-up decided, for the window (one of PORT_EXTRA_*; an int, the window is another thread's). */
enum { EXTRA_NONE, EXTRA_IN, EXTRA_MODE, EXTRA_TEAMS, EXTRA_SPLIT, EXTRA_REPLAY, EXTRA_SCRIPT, EXTRA_ONLINE, EXTRA_FILES };
static volatile int sLast = EXTRA_NONE;

/* The name of a setting of entry `index`: "extra0_chara", "extra1_team", ... (into the caller's buffer: the window's
   thread asks too). */
typedef char SettingName[24];
static const char *setting_name(SettingName out, int index, const char *what) {
    snprintf(out, sizeof(SettingName), "extra%d_%s", index, what);
    return out;
}

/* The settings, held to their ranges (the file may have been written by hand). */
int Port_ExtraCount(void) {
    int n = Port_Setting("extra_fighters", 0);
    return n < 0 ? 0 : n > EXTRA_MAX ? EXTRA_MAX : n;
}

/* Local players (the Experiments tab; setting local_players, 2..4): in a two-player Team Battle or Free-for-All of
   the modes page, controller 3 plays team 1's second member and controller 4 team 2's; the screen is then in
   quarters (src/battle/btl_cam.c). 2: the game's own two. */
#include "mod_coop_native.inc"
static int sMatchPlayers;
/* Match selection is transient; never overwrite the saved local setup. */
int Port_LocalPlayersPreference(void) {
    int n=Port_Setting("local_players",2);return n<2?2:n>4?4:n;
}
void Port_LocalPlayersMatchSet(int n) {
    __atomic_store_n(&sMatchPlayers,n<=0?0:n<2?2:n>4?4:n,__ATOMIC_RELAXED);
}
int Port_LocalPlayers(void) {
    int match=__atomic_load_n(&sMatchPlayers,__ATOMIC_RELAXED);
    int n=Port_TagTourActive()?Port_TagTourConfig(0):match?match:Port_LocalPlayersPreference();
    return n<2?2:n>4?4:n;
}

/* The loader asks before anything is loaded (battle_load.c, Port_LoadExtras): is this a fight of three or fou
   local players? A Team Battle or Free-for-All of the modes page with more than two fighters, Local players 3 o
   4, chosen for one player or for two: the loader then makes it a two-player split-screen fight (side 2 played
   on controller 2) and the screen goes into quarters. */
static int fight_mode(int *forced);
int Port_LocalQuadFight(int mode, int members0, int members1, int replay, int script) {
    int forced = 0, page = fight_mode(&forced);

    return !Port_TagTourActive() && !Port_NetActive() && Port_LocalPlayers() >= 3 && (page == PORT_MODE_TEAM || page == PORT_MODE_FFA || Port_CoopTraining()) &&
           members0 + members1 > 2 && (forced || mode == 0 || (mode == 6 && Port_CoopTraining())) && !replay && !script;
}
void Port_LocalPlayersSet(int n) {
    Port_SettingSave("local_players", n < 2 ? 2 : n > 4 ? 4 : n);
    Port_SettingsWrite();
}
int Port_ExtraChara(int index) {
    SettingName name;
    int c = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "chara"), 0) : 0;
    return c < 0 || c >= CHARA_COUNT ? 0 : c;
}
int Port_ExtraTeam(int index) { /* 0, 1: a player's team; -1: its own */
    SettingName name;
    int t = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "team"), 1) : 1;
    return t < -1 || t > 1 ? 1 : t;
}
int Port_ExtraLevel(int index) {
    SettingName name;
    int v = index >= 0 && index < EXTRA_MAX ? Port_Setting(setting_name(name, index, "level"), 13) : 13;
    return v < 0 ? 0 : v > 29 ? 29 : v;
}
void Port_ExtraSetFighter(int index, int chara, int team, int level) { /* (the caller writes the file) */
    SettingName name;

    if (index < 0 || index >= EXTRA_MAX) {
        return;
    }
    Port_SettingSave(setting_name(name, index, "chara"), chara);
    Port_SettingSave(setting_name(name, index, "team"), team);
    Port_SettingSave(setting_name(name, index, "level"), level);
}
void Port_ExtraSet(int count, int index, int chara, int team, int level) {
    if (index < 0 || index >= EXTRA_MAX) {
        return;
    }
    Port_SettingSave("extra_fighters", count);
    Port_ExtraSetFighter(index, chara, team, level);
    Port_SettingsWrite();
}

/* 1 when the three files a fight reads for character `chara` are in the data folder and not empty. `why`: the
   first one that is not, for the window and the log. */
static int extra_costume_ok(int chara,int costume,char *why,int size) {
    static const int kFiles[3] = {0, 8, 9};
    char path[1024];
    int i;

    if (chara < 0 || chara >= CHARA_COUNT || (chara>=161&&chara<=164)) {
        snprintf(why, (size_t)size, "there is no character %d (0..%d)", chara, CHARA_COUNT - 1);
        return 0;
    }
    for (i = 0; i < 3; i++) {
        int id = chara * 10 + FILE_CHARA + kFiles[i]; /* (ids from 1 are the second archive's files, from 0) */
        int volume=1,entry=id-1;
        {int routed=Port_Beta8FileId(chara,costume,0,i);if(routed>=0){volume=(routed>>24)&63;entry=routed&0xffffff;}else if(chara>160){snprintf(why,(size_t)size,"no verified bundle for character %d costume %d",chara,costume);return 0;}}
        FILE *fp = Port_FilePath(volume, entry, NULL, path, sizeof(path)) ? fopen(path, "rb") : NULL;
        long bytes = 0;
        if (fp != NULL) {
            fseek(fp, 0, SEEK_END);
            bytes = ftell(fp);
            fclose(fp);
        }
        if (bytes <= 0) {
            snprintf(why, (size_t)size, "its %s file (%05d.bin) is %s the game data", i == 0 ? "model" : "animation", id - 1,
                     fp != NULL ? "empty in" : "not in");
            return 0;
        }
    }
    return 1;
}

/* One more extra fighter for the fight being set up. 0: not taken (the table is full, there is no such character,
   its files are missing: the reason is logged, and kept for the window). */
int Port_ExtraCharaOk(int chara,char *why,int size){return extra_costume_ok(chara,0,why,size);}

static int fight_add(int chara, int costume, int team, int level, int member) {
    char why[160];
    FightExtra *f;

    if (sFightCount >= PORT_EXTRA_MAX) {
        fprintf(stderr, "bt3: extra fighter: character %d is left out (a fight has %d fighters at most)\n", chara, PORT_FIGHTER_MAX);
        return 0;
    }
    if (!extra_costume_ok(chara,costume,why,sizeof(why))) {
        fprintf(stderr, "bt3: extra fighter: character %d is left out (%s)\n", chara, why);
        sLast = EXTRA_FILES;
        return 0;
    }
    f = &sFight[sFightCount++];
    f->chara = chara;
    f->costume = costume < 0 ? 0 : costume;
    f->team = team < 0 ? 0 : team >= PORT_TEAM_MAX ? PORT_TEAM_MAX - 1 : team;
    f->level = level > 29 ? 29 : level;
    f->member = member;
    f->consumed = 0;
    return 1;
}

/* BT3_EXTRA_FIGHTERS=<character>:<team>[:<level>],... (the tests): any number of extra fighters, in any fight. */
static void fight_from_list(const char *p) {
    while (*p != '\0') {
        char *end;
        long chara = strtol(p, &end, 10), team = 1, level = -1;

        if (end == p) {
            break;
        }
        if (*end == ':') {
            team = strtol(end + 1, &end, 10);
            if (*end == ':') {
                level = strtol(end + 1, &end, 10);
            }
        }
        fight_add((int)chara, 0, (int)team, (int)level, -1);
        p = end;
        while (*p == ',' || *p == ' ') {
            p++;
        }
    }
}

/* The mode the fights of now are of: the page's or the menu flow's setting, or BT3_MODES_FIGHT in a test (which
   then holds for every kind of fight: `forced`). */
static int fight_mode(int *forced) {
    const char *e = getenv("BT3_MODES_FIGHT");

    *forced = e != NULL;
    if (e != NULL) {
        int m = atoi(e);
        return m > PORT_MODE_NONE && m < PORT_MODE_COUNT ? m : PORT_MODE_NONE;
    }
    return Port_TagTourActive()?PORT_MODE_TEAM:Port_ModesFight();
}

static void fight_log(void) {
    int i;

    for (i = 0; i < sFightCount; i++) {
        fprintf(stderr, "bt3: extra fighter: character %d, team %d, level %d (%s)\n", sFight[i].chara, sFight[i].team, sFight[i].level,
                sFightFrom);
    }
    if (sFightCount > 1) {
        fprintf(stderr, "bt3: extra fighters: %d, so %d in the fight\n", sFightCount, sFightCount + 2);
    }
}

/* A fight is being loaded: does it have extra fighters? Called by the loader (BtlLoad_StepInitial) with the
   battle's mode (include/battle/battle_setup.h: 0 the versus menu's, 6 training), each side's number of members,
   and whether the picture is split, a replay is played, the fight has a script.
   Returns 0 when the fight's extra fighters are decided (none, or the table is filled), or PORT_MODE_TEAM /
   PORT_MODE_FFA when they are the sides' own members: the loader then hands those over with Port_ExtraAdd and
   ends with Port_ExtraDone. */
int Port_ExtraSetup(int mode, int members0, int members1, int split, int replay, int script) {
    static const char *const kWhy[] = {"", "", "this mode's fights are left as the game's own", "a side has more than one member",
                                       "the picture is split for two players", "a replay is being played",
                                       "the fight has a script", "an online match", ""};
    const char *list = getenv("BT3_EXTRA_FIGHTERS"), *env = getenv("BT3_THIRD_FIGHTER"), *e;
    int asked = 0, last = EXTRA_NONE, forced = 0, i;
    int page = fight_mode(&forced); /* the mode asked on the page of extra modes or by the menu flow, 0: none */
    int roster = page == PORT_MODE_TEAM || page == PORT_MODE_FFA || page == PORT_MODE_TRAINING;

    sFightCount = 0;
    sFightRoster = 0;
    sFightFrom = "";
    sFightSplit = split;
    sLast = EXTRA_NONE;
    if (Port_NetActive()) {
        /* (nothing of this in an online match, whoever asks) */
        if (list != NULL || env != NULL || (roster ? members0 + members1 > 2 : page != PORT_MODE_NONE ? Port_ModesCount() > 0 : Port_ExtraCount() > 0)) {
            sLast = EXTRA_ONLINE;
            fprintf(stderr, "bt3: extra fighter: not in this fight (%s)\n", kWhy[EXTRA_ONLINE]);
        }
        return 0;
    }
    if (list != NULL) {
        sFightFrom = "BT3_EXTRA_FIGHTERS";
        fight_from_list(list);
        asked = 1;
    } else if (env != NULL) {
        int team = 1, level = -1;

        sFightFrom = "BT3_THIRD_FIGHTER";
        if ((e = getenv("BT3_THIRD_TEAM")) != NULL) {
            team = atoi(e) < 0 || atoi(e) > 2 ? 1 : atoi(e);
        }
        if ((e = getenv("BT3_THIRD_LEVEL")) != NULL) {
            level = atoi(e);
        }
        if (atoi(env) >= 0) {
            fight_add(atoi(env), 0, team, level, -1);
            asked = 1;
        }
    } else if (roster) {
        /* The sides' own members, all at once. (A fight of one member a side is the game's own fight of two.) */
        if (members0 + members1 > 2) {
            asked = 1;
            if (!forced && mode != 0 && !(mode==4&&Port_TagTourActive()) && !(mode==6&&page==PORT_MODE_TRAINING)) {
                last = EXTRA_MODE;
            } else if (script) {
                last = EXTRA_SCRIPT;
            } else if (replay) { /* (split screen: two players, or four in quarters; each part its own HUD) */
                last = EXTRA_REPLAY;
            } else {
                sFightRoster = page;
                sFightFrom = Port_ModesName(page);
                return page;
            }
        }
    } else if (page != PORT_MODE_NONE ? Port_ModesCount() > 0 : Port_ExtraCount() > 0) {
        int count = page != PORT_MODE_NONE ? Port_ModesCount() : Port_ExtraCount();

        asked = 1;
        if (!forced && (page != PORT_MODE_NONE ? mode != 6 : mode != 0 && mode != 6)) {
            last = EXTRA_MODE; /* (the page's choice is for the kind of fight it led into, and no other) */
        } else if (script) {
            last = EXTRA_SCRIPT;
        } else if (members0 != 1 || members1 != 1) {
            last = EXTRA_TEAMS;
        } else if (split) {
            last = EXTRA_SPLIT;
        } else if (replay) {
            last = EXTRA_REPLAY;
        } else {
            sFightFrom = page != PORT_MODE_NONE ? "the modes page: Training" : "the settings";
            for (i = 0; i < count; i++) {
                int team = Port_ExtraTeam(i);
                /* (a team of its own: the first number no player and no extra fighter before it has) */
                fight_add(Port_ExtraChara(i), 0, team < 0 ? 2 + i : team, Port_ExtraLevel(i), -1);
            }
        }
    }
    if (sFightCount > 0) {
        last = EXTRA_IN;
        fight_log();
    } else if (asked) {
        if (sLast == EXTRA_FILES) {
            last = EXTRA_FILES;
        } else {
            fprintf(stderr, "bt3: extra fighter: not in this fight (%s)\n", kWhy[last]);
        }
    }
    sLast = last;
    if (getenv("BT3_EXTRA_FLIP") != NULL) {
        /* (the experiment's tool) Testing without a person at the window: the switch is turned over after every
           set-up, as if the player had done it during the fight, so a run of several fights has them with and
           without in turn. */
        Port_SettingSave("extra_fighters", Port_ExtraCount() > 0 ? 0 : 1);
    }
    return 0;
}

/* The loader hands over a member of a side's team (Port_ExtraSetup returned a roster mode): `side` and `index`
   say which. Its team is its side in a Team Battle and its own in a Free-for-All; its level is the member's, where the member has one (a player's own team has none: -1, and the
   battle code gives it the other side's). Returns 1 if it is in the fight. */
int Port_ExtraAdd(int side, int index, int chara, int costume, int level) {
    if (sFightRoster == 0) {
        return 0;
    }
    return fight_add(chara, costume, sFightRoster == PORT_MODE_FFA ? 2 + sFightCount : side != 0, level < 0 ? -1 : level,
                     side * 8 + index);
}
void Port_ExtraDone(void) {
    int local = Port_LocalPlayers(), pad, k, pass;

    /* Local players 3 and 4 (a split-screen fight of the sides' own members): controller 3 plays team 1's second
       member, controller 4 team 2's; a team without one leaves its player the first fighter nobody plays. */
    for (k = 0; k < sFightCount; k++) {
        sFight[k].pad = -1;
    }
    for (pad = 2; sFightSplit && sFightRoster != 0 && pad < local; pad++) {
        for (pass = 0; pass < 2; pass++) {
            for (k = 0; k < sFightCount; k++) {
                int m = sFight[k].member;
                if (sFight[k].pad < 0 && (pass == 1 || m == (pad - 2) * 8 + 1)) {
                    sFight[k].pad = pad;
                    break;
                }
            }
            if (k < sFightCount) {
                break;
            }
        }
    }
    if(Port_CoopActive())for(k=0;k<sFightCount;k++){int m=sFight[k].member;sFight[k].pad=Port_CoopMemberPad(m/8,m%8);}
    if(Port_TagTourActive())for(k=0;k<sFightCount;k++){int m=sFight[k].member;sFight[k].pad=Port_TagTourMemberPad(m/8,m%8);}
    if (sFightCount > 0) {
        sLast = EXTRA_IN;
        fight_log();
    }
}

/* What the last fight's set-up decided (EXTRA_*), for the window. */
int Port_ExtraLast(void) {
    return sLast;
}

/* The battle code's questions, answered for the fight being loaded or fought. */
int Port_FightExtras(void) {
    return sFightCount;
}
int Port_FightChara(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].chara : -1;
}
int Port_FightCostume(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].costume : 0;
}
int Port_FightTeam(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].team : 1;
}
int Port_FightLevel(int k) { /* -1: its side's own */
    return k >= 0 && k < sFightCount ? sFight[k].level : -1;
}
int Port_FightMember(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].member : -1;
}
int Port_FightRoster(void) {
    return sFightCount > 0 ? sFightRoster : 0;
}
/* Extra k's controller: 0..3 (a player plays it), -1 the computer. For now from BT3_HUMAN_PADS=<fighter>:<pad>,...
   (fighter numbers 2..9 as in the logs, controllers 1..4), e.g. 2:3,6:4: fighter 2 on controller 3, fighter 6 on
   controller 4. */
int Port_FightPad(int k) {
    const char *p = getenv("BT3_HUMAN_PADS");

    if (k < 0 || k >= sFightCount) {
        return -1;
    }
    if (p == NULL) {
        return sFight[k].pad; /* (the setting: Port_ExtraDone) */
    }
    while (*p != '\0') {
        int f = atoi(p), pad = -1;
        const char *c = strchr(p, ':');
        if (c != NULL) {
            pad = atoi(c + 1) - 1;
        }
        if (f == k + 2 && pad >= 0 && pad < 4) {
            return pad;
        }
        p = strchr(p, ',');
        if (p == NULL) {
            break;
        }
        p++;
    }
    return -1;
}

/* (more than two fighters) A fighter played from a controller, said once a fight when it is set up. */
void Port_PlayerLog(int fighter, int pad, int cpu) {
    if (sFightCount > 0 && !cpu) {
        fprintf(stderr, "bt3: fighter %d is played on controller %d\n", fighter, pad + 1);
    }
}

/* A fighter of this fight is on controller 3 or 4 (src/sys/pad.c reads those then). */
int Port_ExtraPadsOn(void) {
    int k;
    if (Port_LocalPlayers() >= 3 || Port_Setting("all_controllers_character_select", 0)) return 1;



    for (k = 0; k < sFightCount; k++) {
        if (Port_FightPad(k) >= 2) {
            return 1;
        }
    }
    return 0;
}

/* (extra 0 by its older name) */
int Port_ThirdFighter(void) {
    return Port_FightChara(0);
}
int Port_ThirdLevel(void) {
    return Port_FightLevel(0);
}
int Port_ThirdTeam(void) {
    return Port_FightTeam(0);
}

/* BT3_DEMO_MODE=<battle mode> (the tests): the demo fight is given that mode when it loads; -1: not set. */
int Port_TestDemoMode(void) {
    const char *e = getenv("BT3_DEMO_MODE");

    return e != NULL && atoi(e) >= 0 && atoi(e) <= 9 ? atoi(e) : -1;
}

/* BT3_TEAM_ROSTER=<character>,<character>,.../<character>,... (the tests): the members of the two sides' teams,
   as the team select would have left them, for a fight that was set up without it (the demo fight). Membe
   `index` of `side`, -1 past the last or when the variable is not set. */
int Port_TestRoster(int side, int index) {
    const char *p = getenv("BT3_TEAM_ROSTER");
    int i;

    if (p == NULL || side < 0 || side > 1 || index < 0 || index >= 5) {
        return -1;
    }
    if (side == 1) {
        p = strchr(p, '/');
        if (p == NULL) {
            return -1;
        }
        p++;
    }
    for (i = 0; i < index; i++) {
        while (*p != ',' && *p != '/' && *p != '\0') {
            p++;
        }
        if (*p != ',') {
            return -1;
        }
        p++;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }
    i = atoi(p);
    return i >= 0 && i < CHARA_COUNT ? i : -1;
}

/* ---- the characters' names -------------------------------------------------------------------------------------- */

/* The versus select shows a character's name and form from two texts of its screen (src/menu/menu_c_e.c: sections
   43 and 44 of member 2 of the first menu archive, the file gProgress->baseFile). The same file is read here, by the
   window's thread, from the data folder: every offset in it is checked against the buffer it is in. */
#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __upt
#define GAME_PTR *__ptr32 __uptr /* (the game's pointers are 32 bits wide in the 64-bit program) */
#else
#define GAME_PTR *
#endif
extern struct { int unk0, baseFile; } GAME_PTR gProgress;

#define NAMES_FILE_DEFAULT 449 /* gProgress->baseFile on the USA disc: pzs3us1/00448.bin */
#define NAMES_MEMBER 2
#define NAMES_UNPACKED_MAX (64u * 1024 * 1024)

static char sNames[NAME_COUNT][72];
static int sNamesState; /* 0 not tried, 1 read, -1 could not be */

static int get32(const uint8_t *p, size_t n, size_t at, uint32_t *v) {
    if (at > n || n - at < 4) {
        return 0;
    }
    *v = (uint32_t)p[at] | (uint32_t)p[at + 1] << 8 | (uint32_t)p[at + 2] << 16 | (uint32_t)p[at + 3] << 24;
    return 1;
}

/* Line `line` of a text section (a table of offsets, then UTF-16 strings that start with a byte order mark). */
static int text_line(const uint8_t *p, size_t n, int line, char *out, int cap) {
    uint16_t str[96];
    uint32_t first, off;
    size_t i;

    out[0] = '\0';
    if (!get32(p, n, 4, &first) || (uint32_t)line + 1 >= first >> 2 || !get32(p, n, ((size_t)line + 1) * 4, &off)) {
        return 0;
    }
    off &= ~3u;
    for (i = 0; i < sizeof(str) / sizeof(str[0]) - 1; i++) {
        if (off + i * 2 + 2 > n) {
            return 0;
        }
        str[i] = (uint16_t)(p[off + i * 2] | p[off + i * 2 + 1] << 8);
        if (str[i] == 0) {
            break;
        }
    }
    str[i] = 0;
    if (off == 0 || str[0] != 0xFEFF) {
        return 0;
    }
    return PortA11y_Utf8(out, cap, str);
}

static void names_read(void) {
    char path[1024], form[64];
    uint8_t *file = NULL, *res = NULL;
    uint32_t member, size, packed, name, formAt;
    long bytes = 0;
    FILE *fp;
    int id = gProgress != NULL ? gProgress->baseFile : 0, c;

    if (id < 1 || id >= 0xD48) { /* (before the game has set it: the USA disc's) */
        id = NAMES_FILE_DEFAULT;
    }
    if (!Port_FilePath(1, id - 1, NULL, path, sizeof(path)) || (fp = fopen(path, "rb")) == NULL) {
        return; /* (the archives are not known yet at the very start: asked again later) */
    }
    sNamesState = -1;
    fseek(fp, 0, SEEK_END);
    bytes = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (bytes > 0 && bytes < (long)NAMES_UNPACKED_MAX && (file = malloc((size_t)bytes)) != NULL &&
        fread(file, 1, (size_t)bytes, fp) == (size_t)bytes && get32(file, (size_t)bytes, NAMES_MEMBER * 4, &member) &&
        (member &= ~3u) < (uint32_t)bytes && get32(file, (size_t)bytes, member, &size) && get32(file, (size_t)bytes, member + 4, &packed) &&
        size > 0 && size <= NAMES_UNPACKED_MAX && (res = malloc(size)) != NULL &&
        MenuLabels_Unpack(file + member, (size_t)bytes - member, res, size) == (long)size && get32(res, size, 43 * 4, &name) &&
        get32(res, size, 44 * 4, &formAt) && (name &= ~3u) < size && (formAt &= ~3u) < size) {
        for (c = 0; c < NAME_COUNT; c++) {
            if (text_line(res + name, size - name, c, sNames[c], 40) > 0) {
                /* as the select shows it: the form after the name when it says something else */
                if (text_line(res + formAt, size - formAt, c, form, 24) > 0 && strcmp(form, sNames[c]) != 0) {
                    size_t len = strlen(sNames[c]);
                    snprintf(sNames[c] + len, sizeof(sNames[c]) - len, ", %s", form);
                }
                sNamesState = 1;
            }
        }
    }
    fclose(fp);
    free(file);
    free(res);
    if (sNamesState < 0) {
        fprintf(stderr, "bt3: extra fighter: the characters' names could not be read from %s (numbers are shown)\n", path);
    }
}

/* The name of a character as the versus select shows it, or "" when the data did not give one. For the settings
   window (its thread only). */
const char *Port_CharaName(int chara) {
    static int tries;

    if (chara == 210) {
        return "Spopovich"; /* the mod's Spopovich cell (costume 0 only) */
    }
    if (chara == 212) {
        return "Yamu"; /* the separated Spopovich costume 2 (Yamu) */
    }
    if (chara == 214) {
        return "Gokhan"; /* the separated Vegetto costume 3/internal 2 */
    }
    if (chara == 215) {
        return "Vegetto GT"; /* the separated Vegetto costume 2/internal 1 */
    }
    if (chara == 216) {
        return "Gokhan Super Saiyajin";
    }
    if (chara == 218) {
        return "Gokhan Super Saiyajin 2";
    }
    if (chara == 219) {
        return "Gokhan Super Saiyajin 3";
    }
    if (chara == 220) {
        return "Vegetto GT Super Saiyajin";
    }
    if (chara == 221) {
        return "Vegetto GT Super Saiyajin 2";
    }
    if (chara == 222) {
        return "Vegetto GT Super Saiyajin 3";
    }
    if (chara == 223) {
        return "Vegetto GT Super Saiyajin 4";
    }
    if (chara == 224) {
        return "Gogetto";
    }
    if (chara == 225) {
        return "Gogetto SSJ";
    }
    if (chara == 226) {
        return "Gogetto SSJ2";
    }
    if (chara == 229) {
        return "Gogetto SSJ3";
    }
    if (chara == 230) {
        return "Gogetto SSJ4";
    }
    if (chara == 234) {
        return "Gogetto Potara";
    }
    if (chara == 235) {
        return "Gogetto Potara SSJ";
    }
    if (chara == 238) {
        return "Gogetto Potara SSJ2";
    }
    if (chara == 239) {
        return "Gogetto Potara SSJ3";
    }
    if (chara == 241) {
        return "Gogetto Potara SSJ4";
    }
    if (chara == 244) {
        return "Vegeta Explorador SSJ4";
    }
    if (sNamesState == 0 && tries++ % 60 == 0) {
        names_read();
    }
    return chara >= 0 && chara < NAME_COUNT ? sNames[chara] : "";
}

/* Fill the game's own team-member list of the active fighter with the port's extra allies of the same side, so
   the stock fusion (BtlMember_FindPresentIndex) can find its partner. The extras are the sides' members handed
   over by Port_ExtraAdd; FightExtra.member is side * 8 + index. Layout (include/battle/btl_char_member.h):
   BtlMemberChr.side +0, memberCount +0x998, members +0x9A4, BtlMember 0xA4 bytes: chara +0, costume +4,
   present +8, gauge +0x40 (health +0, unk30 +0x30). Only the fusion uses this; the port's own extra system keeps
   controlling the live ally. */
static int sFusionMirror[12][5],sFusionOwner[12],sFusionLink[12][5]; /* sFusionMirror[side][slot]=actor+1 for rows the port mirrored in (live allies): the game's tag-switch code must never treat a mirrored row as a bench member (2026-10-26 live regression: KO'd fighter tagged a copy of the already-fighting ally in). */
int Port_IsFusionMirror(const void *chr,int slot){int side=*(const int*)chr;return side>=0&&side<12&&slot>0&&slot<5&&sFusionMirror[side][slot]!=0;}
/* Alive team members except the mirrored fusion rows: in the live mod every same-side fighter is a running
   actor already, so a mirrored row must never read as a bench member for the tag-switch code (live regression
   2026-10-26: a KO'd fighter tagged a fresh copy of the ally who was already on stage). */
int Port_TagBenchAlive(const void *chr){const unsigned char *b=(const unsigned char *)chr;int side=*(const int*)b,i,count,n=0;
 if(side<0||side>=12)return 0;
 count=*(const int*)(b+0x998);
 for(i=0;i<count&&i<5;i++){const unsigned char*m=b+0x9A4+i*0xA4;
  if(sFusionMirror[side][i])continue;
  if(*(const int*)(m+8)&&*(const int*)(m+0x40)>0)n++;}
 return n;}
void Port_TeamFillFusion(void *chr) {
    int side = *(int *)chr;
    int *count = (int *)((unsigned char *)chr + 0x998);
    int i;

    if(side>=2&&side<12){
        extern int BtlChar_GetCount(void),Port_Team(int);extern void*BtlChar_Get(int),*BtlMember_GetActive(void*);
        int k,slot=1;memset(sFusionMirror[side],0,sizeof(sFusionMirror[side]));
        for(k=1;k<5;k++)memset((unsigned char*)chr+0x9a4+k*0xa4,0,0xa4);
        for(k=0;k<BtlChar_GetCount()&&slot<5;k++)if(k!=side&&Port_Team(k)==Port_Team(side)&&!sFusionOwner[k]){
            void*c=BtlChar_Get(k);unsigned char*m=c?(unsigned char*)BtlMember_GetActive(c):NULL;
            if(m&&*(int*)(m+8)&&*(int*)(m+0x40)>0){memcpy((unsigned char*)chr+0x9a4+slot*0xa4,m,0xa4);sFusionMirror[side][slot++]=k+1;}
        }*count=slot;return;
    }
    for (i = 0; i < sFightCount; i++) {
        FightExtra *f = &sFight[i];
        int idx;
        unsigned char *m;

        if (f->member < 0 || f->member / 8 != side) {
            continue;
        }
        idx = f->member % 8;
        if (idx <= 0 || idx >= 5) {
            continue;
        }
        m = (unsigned char *)chr + 0x9A4 + idx * 0xA4;
        if (f->consumed) {
            memset(m, 0, 0xA4); /* a consumed partner must not remain eligible from an earlier mirror */
            sFusionMirror[side][idx] = 0;
            continue;
        }
        { extern void *BtlChar_Get(int);extern void *BtlMember_GetActive(void*);
          void *ally=BtlChar_Get(i+2);void *live=ally?BtlMember_GetActive(ally):NULL;
          if(!live||*(int*)((unsigned char*)live+0x40)<=0){
              memset(m,0,0xA4); /* scrub a stale dead row instead of admitting its old present/health values */
              sFusionMirror[side][idx]=0;
              continue;
          }
          memcpy(m,live,0xA4); /* actual form, costume, HP, Ki and bonuses; no invented 10000HP partner */
          sFusionMirror[side][idx]=i+3; /* mirrored row of live ally actor i+2: never a tag candidate */
        }
        if (*count <= idx) {
            *count = idx + 1;
        }
    }
}

/* ---- what the game can build from a character's model ------------------------------------------------------- */

/* 1 when model file `id` is one the battle object code can bind: the stock container's model entry 3 and texture
   entry 0xC (BObjFile_GetEntry / BtlObjMdl_BindFiles). The added characters' bundles (volumes 3 and 4) hold the
   mod's own layout (raw PlayStation 2 data) here, which the object code cannot bind; those answer 0 so that a
   change to them is refused instead of crashing on a NULL texture. */
int Port_ModelFileOk(int id) {
    static struct {
        int id, ok;
    } cache[64];
    static int cacheCount;
    unsigned char hdr[0x50];
    unsigned w[0x14];
    char path[1024];
    FILE *fp;
    size_t got;
    int pt, flid, i, ok;

    for (i = 0; i < cacheCount; i++) { /* (the AI asks this every frame it considers a change) */
        if (cache[i].id == id) {
            return cache[i].ok;
        }
    }
    if (((unsigned)id & 0xF0000000u) == 0x40000000u) {
        pt = (id >> 24) & 0x0F; /* a bundle volume (plat_beta8.c) */
        flid = id & 0xFFFFFF;
    } else if (id < 0xD48) {
        pt = 1;
        flid = id <= 0 ? 0 : id - 1;
    } else {
        pt = 2;
        flid = id - 0xD48;
    }
    ok = 0;
    if (Port_FilePath(pt, flid, NULL, path, sizeof(path)) && (fp = fopen(path, "rb")) != NULL) {
        got = fread(hdr, 1, sizeof(hdr), fp);
        fclose(fp);
        if (got >= sizeof(hdr)) {
            memcpy(w, hdr, sizeof(w));
            ok = w[3] != w[4] && w[0xC] != w[0xD]; /* model entry, then texture entry */
        }
    }
    if (cacheCount < 64) {
        cache[cacheCount].id = id;
        cache[cacheCount].ok = ok;
        cacheCount++;
    }
    return ok;
}

/* 1 when the character loader (BtlLoad_StepChara, the same rules) can load this character's model: above 160
   through the bundle map, the stock ids by their stock file id. */
int Port_ModelUsable(int chara, int costume, int variant) {
    int model;

    if (chara < 0 || costume < 0) {
        return 0;
    }
    model=Port_Beta8FileId(chara,costume,variant!=0,0);
    if(model>=0)return Port_ModelFileOk(model);
    if(chara>160)return 0;
    return Port_ModelFileOk(chara*10+costume+FILE_CHARA+(variant!=0?4:0));
}

/* Explicit live-actor identity, separate from the copied native member row. */
int Port_FusionPartnerActor(int who,int member){int k;if(who<0||who>=12||member<0||member>=5)return -1;
 if(sFusionLink[who][member])return sFusionLink[who][member]-1;
 if(who>=2)return sFusionMirror[who][member]-1;
 for(k=0;k<sFightCount;k++)if(sFight[k].member==who*8+member)return k+2;return -1;}
int Port_FusionConsumedActor(int who){return who>=0&&who<12&&sFusionOwner[who]!=0;}
int Port_FusionViewOwner(int who){int n=0;while(who>=0&&who<12&&sFusionOwner[who]&&n++<12)who=sFusionOwner[who]-1;return who;}
int Port_FusionViews(int q){extern int BtlChar_GetCount(void),Port_TargetHuman(int);extern void*BtlChar_Get(int);
 int slots[4]={-1,-1,-1,-1},out[4],i,j,n=0,merged=0;extern int Port_FusionRosterReady(void);
 for(i=0;i<12;i++)if(sFusionOwner[i])break;if(i==12||!Port_FusionRosterReady())return -1;
 for(i=0;i<BtlChar_GetCount()&&i<12;i++)if(Port_TargetHuman(i)){
  void*c=BtlChar_Get(i);int pad=c?*(int*)((unsigned char*)c+4):-1,owner=Port_FusionViewOwner(i);
  if(owner!=i)merged=1;if(pad>=0&&pad<4)slots[pad]=owner;}
 if(!merged)return -1;
 for(i=0;i<4;i++)if(slots[i]>=0){for(j=0;j<n&&out[j]!=slots[i];j++);if(j==n)out[n++]=slots[i];}
 return q<0?n:q<n?out[q]:n?out[0]:0;
}
void Port_ExtraRoundReset(void){int k;memset(sFusionMirror,0,sizeof(sFusionMirror));memset(sFusionLink,0,sizeof(sFusionLink));memset(sFusionOwner,0,sizeof(sFusionOwner));for(k=0;k<sFightCount;k++)sFight[k].consumed=0;}
/* The fusion script draws a temporary partner object, not the simultaneous fighter. */
int Port_FusionPartnerInfo(void *actor,int *source,int *object,int *member){
 extern int BtlAct_GetCurrent(void*);int k,action=BtlAct_GetCurrent(actor),side=*(int*)actor;
 unsigned char *c=(unsigned char*)actor;
 if((action!=0xF1&&action!=0xF2)||!*(int*)(c+0x1330))return 0;
 *member=*(int*)(c+0x12F8);*object=*(int*)(c+0x1338);*source=-1;
 if(*member<0||*member>=5||*object<0)return 0;
 {extern int Port_FusionPartnerActor(int,int);*source=Port_FusionPartnerActor(side,*member);}
 return 1;
}

/* A fusion merged the extra fighter that was its partner: hide its model and defeat its fighter, so the ally
   does not stand next to the fused character a second time. The stock fusion already cleared the team entry
   (present = 0); this takes the port's live extra out of the fight. */
void Port_ExtraFusionConsume(int who,int member){
 extern void*BtlChar_Get(int),*BtlMember_GetActive(void*);int target=Port_FusionPartnerActor(who,member);void*c,*m;
 if(target<0||target==who)return;sFusionOwner[target]=who+1;sFusionLink[who][member]=target+1;if(target>=2)sFight[target-2].consumed=1;
 c=BtlChar_Get(target);m=c?BtlMember_GetActive(c):NULL;if(m)*(int*)((unsigned char*)m+0x40)=0;
 fprintf(stderr,"bt3: fusion: actor%d consumed by actor%d member%d\n",target,who,member);
}

/* BT3_DEFUSE_PREVIEW (mod_defuse_cinematic.inc): makes extra `target` member `member` of fighter `who` and merges it
   the way a dance fusion does. 1 when the fusion partner lookup now finds it. */
int Port_DefusePreviewLink(int who,int member,int target){
 if(who<0||who>=2||member<0||member>=5||target<2||target-2>=sFightCount)return 0;
 sFight[target-2].member=who*8+member;Port_ExtraFusionConsume(who,member);
 return Port_FusionPartnerActor(who,member)==target;
}

/* 1 when extra k was merged away by a fusion (the HUD and the fighter update leave it out). */
int Port_FightConsumed(int k) {
    return k >= 0 && k < sFightCount ? sFight[k].consumed : 0;
}

/* Keeps a merged extra out of the picture: a knock-out change rebounds its object's model (which comes back
   visible) and the death animation would leave it lying on the ground. Runs after the fighters' own update,
   before the objects are drawn: hide it, keep its health at zero and put its place far below the stage. */
void Port_ExtraConsumedUpdate(void){
 extern int BtlChar_GetCount(void),Port_FighterObjId(int);extern void*BtlChar_Get(int),*BtlChar_GetPos(void*),*BtlMember_GetActive(void*),*BtlObj_Get(int);
 extern void BtlObj_SetPartVisible(void*,unsigned,int);int i;
 for(i=0;i<BtlChar_GetCount()&&i<12;i++)if(Port_FusionConsumedActor(i)){
  void*c=BtlChar_Get(i),*m=c?BtlMember_GetActive(c):NULL;float*p=c?(float*)BtlChar_GetPos(c):NULL;int id=Port_FighterObjId(i);void*o=id>=0?BtlObj_Get(id):NULL;
  if(o)BtlObj_SetPartVisible(o,0,0);if(m)*(int*)((unsigned char*)m+0x40)=0;if(p){p[0]=0;p[1]=-100000;p[2]=0;}
 }
}

/* ---- the pause menu's fusion list --------------------------------------------------------------------------- */

extern unsigned Port_GameBigAlloc(int size); /* port/src/plat_mem.c: the port's own arena */

/* UTF-8 (the names as the select shows them) into the game's UTF-16 text. */
static int skill_name_units(uint16_t *out, int cap, const char *s) {
    int n = 0;

    while (*s != '\0' && n < cap) {
        unsigned c = (unsigned char)*s++;

        if (c < 0x80) {
            out[n++] = (uint16_t)c;
        } else if ((c & 0xE0) == 0xC0 && *s != '\0') {
            out[n++] = (uint16_t)(((c & 0x1F) << 6) | ((unsigned char)*s++ & 0x3F));
        } else if ((c & 0xF0) == 0xE0 && s[0] != '\0' && s[1] != '\0') {
            out[n++] = (uint16_t)(((c & 0x0F) << 12) | (((unsigned char)s[0] & 0x3F) << 6) | ((unsigned char)s[1] & 0x3F));
            s += 2;
        } else if ((c & 0xF8) == 0xF0 && s[0] != '\0' && s[1] != '\0' && s[2] != '\0') {
            unsigned cp = ((c & 0x07) << 18) | (((unsigned char)s[0] & 0x3F) << 12) |
                          (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);

            s += 3;
            if (cp >= 0x10000 && n + 1 < cap) {
                cp -= 0x10000;
                out[n++] = (uint16_t)(0xD800 + (cp >> 10));
                out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
            }
        } else {
            out[n++] = '?';
        }
    }
    return n;
}

/* Rewrites the transformation/fusion page of a character's skill-list text (the model's entry 0x2D + language,
   UTF-16) from the live BtlParam. The mod added transformation and fusion links, but the disc text still lists
   only the stock ones (and some characters had no fusion lines at all). The page is found structurally: the page
   whose entries are followed by "!F%0Pareja:" lines, else the page titled "Transform...", else the last page.
   The fusion entries take their result's name in the brackets plus the live cost and partners; live
   transformations with no line of their own are appended before the fusions, and live fusions with no line are
   appended at the end. A flat script with no page header (the older synthetic fixture) is patched entry by entry
   and nothing is appended. A new copy is made in the port's arena (the model's text is not touched). */

/* 1 when the first n units of s contain the ASCII string needle. */
static int skill_has(const uint16_t *s, int n, const char *needle) {
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; needle[j] != '\0' && i + j < n; j++) {
            if (s[i + j] != (uint16_t)(unsigned char)needle[j]) {
                break;
            }
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* The distinct partner ids of fusion recipe idx, in recipe order (repeated ids are padding). */
static int skill_partners(const unsigned char *p, int idx, int *out) {
    int n = 0, j, k;

    for (j = 0; j < 4; j++) {
        int id = p[0xBA + 4 * idx + j];

        if (id == 0xFF) {
            continue;
        }
        for (k = 0; k < n; k++) {
            if (out[k] == id) {
                break;
            }
        }
        if (k == n) {
            out[n++] = id;
        }
    }
    return n;
}

/* Writes "!F*" + two icon digits + the cost digit + the name (no line break); returns the units written. */
static int skill_emit_entry(uint16_t *out, int cap, int icon, int cost, const char *name) {
    int n = 0;

    if (cap < 8) {
        return 0;
    }
    out[n++] = '!';
    out[n++] = 'F';
    out[n++] = '*';
    out[n++] = (uint16_t)('0' + (icon / 10) % 10);
    out[n++] = (uint16_t)('0' + icon % 10);
    out[n++] = (uint16_t)('0' + cost % 10);
    n += skill_name_units(out + n, cap - n - 1, name);
    return n;
}

void Port_PatchSkillText(int chara, int costume, void *param, void *textSlot) {
    unsigned char *p = (unsigned char *)param;
    uint16_t *src;
    uint16_t *out;
    int eStart[64], ePage[64], eFusion[64], eSlot[64];
    int transformPage[10];
    int entryCount = 0, overflow = 0, page = -1, pages = 0, lastPage = -1;
    int endLine = -1, prevEntry = -1;
    int liveTrans = 0, liveFusion = 0, tp = -1;
    int transOnPage = 0, fusionOnPage = 0, firstFusion = -1;
    int injectTrans = -1, injectFusion = -1, prevSlot = -1, tail = -1;
    int transRenamed = 0, transIndex = 0;
    int rewriteForms=(chara==51||chara==59||chara==224||chara==225||chara==226||chara==229||chara==230||chara==234||chara==235||chara==238||chara==239||chara==241||chara==244),skipFormDetails=0;
    int len, cap, at, i, j, k, lineStart;

    for (i = 0; i < 10; i++) {
        transformPage[i] = 0;
    }
    if (chara < 0 || param == NULL) {
        return;
    }
    Port_Beta8PatchFormParam(chara, p);
    if (chara == 224 || chara == 234) {
        /* Base Gogetto carries the base aura, not the SSJ1 donor's yellow one. */
        p[0] = 1;
        p[3] = 5;
        p[0xAD] = 0;
    }
    {
        FILE *lg = fopen("bt3_fusion.log", "a");
        if (lg != NULL) {
            fprintf(lg, "form patch: chara=%d costume=%d targets=%d,%d,%d,%d default=%d\n",
                    chara, costume, p[0x98], p[0x99], p[0x9A], p[0x9B], p[0xAC]);
            fclose(lg);
        }
    }
    if (chara == 29 && costume == 4) {
        /* Vegeta (Explorador) costume 5: the Oozaru target becomes the GT costume 3 SSJ4 form. */
        for (i = 0; i < 4; i++) {
            if ((p[0x98 + i] == 30 || p[0x98 + i] == 244)) {
                p[0x98 + i] = 244;
                p[0x9C + i] = 1;
                p[0xA0 + i] = 2;
                p[0xA4 + i] = 4;
                p[0xA8 + i] = 255;
                p[0xAC] = (unsigned char)i;
            }
        }
        {
            FILE *lg = fopen("bt3_fusion.log", "a");
            if (lg != NULL) {
                fprintf(lg, "costume5: chara=29 costume=%d target0=%d target3=%d default=%d\n",
                        costume, p[0x98], p[0x9B], p[0xAC]);
                fclose(lg);
            }
        }
    }
    /* Split hybrid entries must not inherit unrelated fusion recipes from a
       Vegetto/Gogeta animation donor. Only explicitly authored pairs below apply. */
    {static const int hybrid[]={214,215,216,218,219,220,221,222,223,224,225,226,229,230,234,235,238,239,241};
     int n,j;for(n=0;n<(int)(sizeof(hybrid)/sizeof(hybrid[0]));n++)if(chara==hybrid[n]){
      for(j=0;j<3;j++){p[0xB4+j]=p[0xAE + j]=p[0xB1+j]=p[0xB7+j]=255;for(i=0;i<4;i++)p[0xBA+4*j+i]=255;}break;
     }}
    /* Canonical matching Fin forms. The result is the same form, in either
       initiating direction. SSJ4 potara59 costume0 is the new normal Vegetto. */
    {
        static const int pairs[][4]={{3,34,110,51},{4,35,53,52},{5,36,249,166},{6,237,172,149},{200,202,54,59}};
        int n,j,other;
        for(n=0;n<5;n++){
            other=chara==pairs[n][0]?pairs[n][1]:chara==pairs[n][1]?pairs[n][0]:-1;
            if(other<0)continue;
            for(j=0;j<3;j++){
                p[0xB4+j]=p[0xAE + j]=p[0xB1+j]=p[0xB7+j]=255;
                for(i=0;i<4;i++)p[0xBA+j*4+i]=255;
            }
            for(j=0;j<2;j++){
                p[0xB4+j]=(unsigned char)pairs[n][j==0?3:2];p[0xAE + j]=1;p[0xB1+j]=(unsigned char)(2-j);p[0xB7+j]=(unsigned char)other;
                for(i=0;i<4;i++)p[0xBA+j*4+i]=(unsigned char)other;
            }
            /* Preserve the authored SSJ4 cross-family partners on Vegeta Fin. */
            if(chara==202){p[0xBA+1]=10;p[0xBA+2]=233;p[0xBA+5]=10;p[0xBA+6]=233;}
            /* Retain the costume3 Gokhan route beside the two Fin pair routes. */
            if(chara==3&&costume==2){p[0xB4+2]=214;p[0xAE + 2]=1;p[0xB1+2]=2;p[0xB7+2]=21;for(i=0;i<4;i++)p[0xBA+8+i]=21;}
        }
        if(chara==21){
            p[0xB4]=214;p[0xAE]=1;p[0xB1]=2;p[0xB7]=3;for(i=0;i<4;i++)p[0xBA+i]=3;
        }
    }
    /* The GT base pair is not in the stock tables: Goku (GT) costume 3 + Vegeta (GT) fuses into
       Vegetto GT (215). Add the recipe to the live parameter block so the page and CanFuse both see it. */
    if (chara == 7 || chara == 122) {
        int wantPartner = chara == 7 ? 122 : 7;
        int have = -1, freeSlot = -1;

        for (i = 0; i < 3; i++) {
            if (p[0xB4 + i] == 215) {
                have = i;
            }
            if (p[0xB4 + i] == 0xFF && freeSlot < 0) {
                freeSlot = i;
            }
        }
        if (have >= 0) p[0xB7 + have] = (unsigned char)wantPartner;
        if (have < 0 && freeSlot >= 0) {
            p[0xB4 + freeSlot] = 215;
            p[0xAE + freeSlot] = 1;
            p[0xB1 + freeSlot] = 2; /* potara */
            p[0xB7 + freeSlot] = (unsigned char)wantPartner;
            for (i = 0; i < 4; i++) {
                p[0xBA + 4 * freeSlot + i] = (unsigned char)wantPartner;
            }
        }
    }
    /* GT costume-3 Goku family: both fusion styles at matching base/SSJ1/SSJ4.
       Store logical result ids so the loader retains Vegetto GT's own routes. */
    {
        static const int pairs[][4]={{7,122,110,215},{8,240,53,220},{10,38,54,223}};
        int k,j;
        for(k=0;k<3;k++){
            int g=pairs[k][0],v=pairs[k][1],other=-1;
            if(chara==g&&costume==2)other=v;
            if(chara==v&&costume<=1)other=g;
            if(other<0)continue;
            for(j=0;j<2;j++){
                p[0xB4+j]=(unsigned char)pairs[k][j+2];p[0xAE + j]=1;p[0xB1+j]=(unsigned char)(j+1);
                p[0xB7+j]=(unsigned char)other;
                for(i=0;i<4;i++)p[0xBA+j*4+i]=(unsigned char)other;
            }
        }
    }
    /* Fusion of fusions, one recipe per form. The stock data only has the SSJ1 pair; the rest
       are added to the live parameter blocks. Dance -> Metamoran, potara -> Potara. */
    {
        static const struct {
            int a, b, dance, potara;
        } sGogettoPairs[] = {
            {  51, 110, 224, 234 }, /* base */
            {  52,  53, 225, 235 }, /* SSJ1 */
            { 166, 249, 226, 238 }, /* SSJ2 */
            { 149, 172, 229, 239 }, /* SSJ3 */
            {  59,  54, 230, 241 }  /* SSJ4 */
        };
        int pidx;

        for (pidx = 0; pidx < (int)(sizeof(sGogettoPairs) / sizeof(sGogettoPairs[0])); pidx++) {
            int other = -1;

            if (chara == sGogettoPairs[pidx].a) {
                other = sGogettoPairs[pidx].b;
            } else if (chara == sGogettoPairs[pidx].b) {
                other = sGogettoPairs[pidx].a;
            }
            if (other < 0 || costume != 0) {
                continue;
            }
            p[0xB4 + 0] = (unsigned char)sGogettoPairs[pidx].dance;
            p[0xAE + 0] = 1;
            p[0xB1 + 0] = 1; /* dance */
            p[0xB7 + 0] = (unsigned char)other;
            p[0xB4 + 1] = (unsigned char)sGogettoPairs[pidx].potara;
            p[0xAE + 1] = 1;
            p[0xB1 + 1] = 2; /* potara */
            p[0xB7 + 1] = (unsigned char)other;
            for (i = 0; i < 4; i++) {
                p[0xBA + 4 * 0 + i] = (unsigned char)other;
                p[0xBA + 4 * 1 + i] = (unsigned char)other;
            }
        }
    }
    for (i = 0; i < 4; i++) {
        if (p[0x98 + i] != 0xFF) {
            liveTrans++;
        }
    }
    for (i = 0; i < 3; i++) {
        if (p[0xB4 + i] != 0xFF && p[0xBA + 4 * i] != 0xFF) {
            liveFusion++;
        }
    }
    if (liveTrans == 0 && liveFusion == 0) {
        return; /* (nothing to list) */
    }
    /* The object owns a 32-bit PS2 pointer field, even in a 64-bit host. */
    uint32_t textAddress = 0;
    if (textSlot != NULL) memcpy(&textAddress, textSlot, sizeof(textAddress));
    if (textSlot == NULL || (src = (uint16_t *)(uintptr_t)textAddress) == NULL) {
        return;
    }
    for (len = 0; len < 0x8000 && src[len] != 0; len++) {
    }
    if (len <= 0 || len >= 0x8000) {
        return;
    }

    /* Pass 1: the entry lines, their page, and which ones are followed by a "Pareja:" detail line. */
    lineStart = 0;
    for (i = 0; i <= len; i++) {
        int end, tag = 0;

        if (i != len && src[i] != '\n') {
            continue;
        }
        end = i;
        if (end - lineStart >= 3 && src[lineStart] == '!' && src[lineStart + 1] == 'F') {
            tag = src[lineStart + 2];
        }
        if (tag == '$') {
            if (end - lineStart >= 4) {
                int d = src[lineStart + 3] - '0';

                if (d >= 0 && d <= 8) {
                    page = d;
                } else {
                    page++;
                }
                lastPage = page;
                if (page >= 0 && page < 10 &&
                    (skill_has(src + lineStart + 4, end - lineStart - 4, "ransform") ||
                     skill_has(src + lineStart + 4, end - lineStart - 4, "RANSFORM"))) {
                    transformPage[page] = 1;
                }
            }
            pages = 1;
            prevEntry = -1;
        } else if (tag == '@') {
            if (endLine < 0) {
                endLine = lineStart;
            }
            prevEntry = -1;
        } else if (tag == '*') {
            if (entryCount < 64) {
                eStart[entryCount] = lineStart;
                ePage[entryCount] = page;
                eFusion[entryCount] = 0;
                prevEntry = entryCount;
                entryCount++;
            } else {
                overflow = 1;
            }
        } else if (tag == '%') {
            if (prevEntry >= 0 && skill_has(src + lineStart + 3, end - lineStart - 3, "Pareja:")) {
                eFusion[prevEntry] = 1;
            }
            prevEntry = -1;
        } else {
            prevEntry = -1;
        }
        lineStart = i + 1;
    }
    if (overflow) {
        return; /* (a script this long is not one we know how to rewrite) */
    }

    /* The transformation/fusion page, and its entry counts. */
    if (pages) {
        for (i = 0; i < entryCount; i++) {
            if (eFusion[i] && ePage[i] >= 0) {
                tp = ePage[i];
                break;
            }
        }
        if (tp < 0) {
            for (i = 0; i <= lastPage && i < 10; i++) {
                if (transformPage[i]) {
                    tp = i;
                }
            }
            if (tp < 0) {
                tp = lastPage;
            }
        }
        for (i = 0; i < entryCount; i++) {
            eSlot[i] = -1;
            if (ePage[i] != tp) {
                continue;
            }
            if (eFusion[i]) {
                eSlot[i] = fusionOnPage++;
                if (firstFusion < 0) {
                    firstFusion = i;
                }
            } else {
                transOnPage++;
            }
        }
    } else {
        /* Flat script: every entry that is followed by a "Pareja:" line is a fusion entry, in order. */
        for (i = 0; i < entryCount; i++) {
            eSlot[i] = -1;
            if (eFusion[i]) {
                eSlot[i] = fusionOnPage++;
            }
        }
    }

    tail = endLine >= 0 ? endLine : (src[len - 1] == '\n' ? len : -1);
    if (tp >= 0) {
        if (liveTrans > transOnPage) {
            injectTrans = firstFusion >= 0 ? eStart[firstFusion] : tail;
        }
        if (liveFusion > fusionOnPage) {
            injectFusion = tail;
        }
    }

    /* Flat skill scripts also need a newly added third recipe (Gokhan). */
    if(pages==0&&liveFusion>fusionOnPage)injectFusion=tail;
    if(rewriteForms&&tp>=0){
        injectTrans=firstFusion>=0?eStart[firstFusion]:tail;
        for(i=0;i<entryCount;i++)if(!eFusion[i]&&ePage[i]==tp){injectTrans=eStart[i];break;}
    }
    cap = len + 4096;
    out = (uint16_t *)Port_GameBigAlloc(cap * 2);
    if (out == NULL) {
        return;
    }

    /* Pass 2: copy line by line, patching the live entries and appending the ones the disc text lacks. */
    at = 0;
    lineStart = 0;
    for (i = 0; i <= len; i++) {
        int end, idx = -1, slot = -1;

        if (i != len && src[i] != '\n') {
            continue;
        }
        end = i;
        if (lineStart == injectTrans) {
            if(rewriteForms){
                static const char*dirs[]={"←＋Ｒ３","↑＋Ｒ３","→＋Ｒ３","↓＋Ｒ３"};
                for(j=0;j<4&&at+1024<cap;j++)if(p[0x98+j]!=255){
                    int target=Port_Beta8FormTarget(chara,p[0x98+j]),cost=p[0x9c+j];const char*nm=Port_CharaName(target);const char*glyph=(signed char)p[0xac]==j?"Ｒ３":dirs[j];
                    if(!nm[0]||cost>9)continue;
                    at+=skill_emit_entry(out+at,cap-at-2,0,cost,nm);out[at++]='\n';
                    at+=skill_name_units(out+at,cap-at-2,"!F%0Transformación: ");at+=skill_name_units(out+at,cap-at-2,nm);out[at++]='\n';
                    at+=skill_name_units(out+at,cap-at-2,"!D#<PAD=");at+=skill_name_units(out+at,cap-at-2,glyph);out[at++]='>';out[at++]='\n';
                }
            }else
            for (j = transOnPage; j < liveTrans && at + 1024 < cap; j++) {
                int cost = p[0x9C + j];
                int target = Port_Beta8FormTarget(chara, p[0x98 + j]);
                const char *nm = Port_CharaName(target);

                if (nm[0] != '\0' && cost <= 9) {
                    at += skill_emit_entry(out + at, cap - at - 2, 0, cost, nm);
                    if ((target == 224 || target == 234 || (chara == 244 && target == 29)) && j < 4) {
                        static const char *hint[] = { " (R3 + \xE2\x86\x90)", " (R3 + \xE2\x86\x91)",
                                                      " (R3 + \xE2\x86\x92)", " (R3 + \xE2\x86\x93)" };
                        at += skill_name_units(out+at, cap-at-2, hint[j]);
                    }
                    out[at++] = '\n';
                }
            }
            injectTrans = -1;
        }
        if (lineStart == injectFusion) {
            for (j = fusionOnPage; j < liveFusion && at + 1024 < cap; j++) {
                int cost = p[0xAE + j];
                int ids[4];
                int n = skill_partners(p, j, ids);
                int first = 1;
                const char *nm = Port_CharaName(p[0xB4 + j]);

                if (chara == 3 && p[0xBA + 4 * j] == 21 && p[0xB4 + j] == 51) {
                    nm = "Gokhan";
                }
                if (chara == 7 && p[0xBA + 4 * j] == 122 && p[0xB4 + j] == 51) {
                    nm = "Vegetto GT";
                }
                if ((chara == 52 || chara == 53) && p[0xB4 + j] == 54) {
                    nm = "Gogetto SSJ";
                }
                if ((chara == 52 || chara == 53) && p[0xB4 + j] == 59) {
                    nm = "Gogetto Potara SSJ";
                }
                if (nm[0] == '\0' || cost > 9) {
                    continue;
                }
                out[at++] = '!';
                out[at++] = 'F';
                out[at++] = '*';
                out[at++] = '0';
                out[at++] = '0';
                out[at++] = (uint16_t)('0' + cost);
                at += skill_name_units(out + at, cap - at - 2, "Fusi\xC3\xB3n [");
                at += skill_name_units(out + at, cap - at - 2, nm);
                out[at++] = ']';
                out[at++] = '\n';
                if (n > 0) {
                    out[at++] = '!';
                    out[at++] = 'F';
                    out[at++] = '%';
                    out[at++] = '0';
                    at += skill_name_units(out + at, cap - at - 2, "Pareja: ");
                    for (k = 0; k < n; k++) {
                        const char *pn = Port_CharaName(ids[k]);

                        if (pn[0] == '\0') {
                            continue;
                        }
                        if (!first) {
                            out[at++] = ' ';
                            out[at++] = '/';
                            out[at++] = ' ';
                        }
                        at += skill_name_units(out + at, cap - at - 2, pn);
                        first = 0;
                    }
                    out[at++] = '\n';
                }
            }
            injectFusion = -1;
        }
        for (j = 0; j < entryCount; j++) {
            if (eStart[j] == lineStart) {
                idx = j;
                break;
            }
        }
        if (idx >= 0) {
            slot = eSlot[idx];
        }
        if(rewriteForms&&tp>=0){
            if(idx>=0){skipFormDetails=!eFusion[idx]&&ePage[idx]==tp;}
            else if(end-lineStart>=3&&src[lineStart]=='!'&&src[lineStart+1]=='F'&&(src[lineStart+2]=='$'||src[lineStart+2]=='@'))skipFormDetails=0;
            if(skipFormDetails){lineStart=i+1;prevSlot=-1;continue;}
        }
        if (slot >= 0 && slot < 3 && p[0xB4 + slot] != 0xFF && at + 2048 < cap) {
            const char *nm = Port_CharaName(p[0xB4 + slot]);
            int outStart = at;
            int lb = -1, rb = -1;

            /* The mod's custom fusion names (the result id alone cannot name the recipe). */
            if (chara == 3 && p[0xBA + 4 * slot] == 21 && p[0xB4 + slot] == 51) {
                nm = "Gokhan";
            }
            if (chara == 7 && p[0xBA + 4 * slot] == 122 && p[0xB4 + slot] == 51) {
                nm = "Vegetto GT";
            }
            if ((chara == 52 || chara == 53) && p[0xB4 + slot] == 54) {
                nm = "Gogetto SSJ";
            }
            if ((chara == 52 || chara == 53) && p[0xB4 + slot] == 59) {
                nm = "Gogetto Potara SSJ";
            }
            for (k = lineStart + 3; k < end; k++) {
                if (src[k] == '[') {
                    lb = k;
                }
                if (src[k] == ']') {
                    rb = k;
                }
            }
            if (nm[0] != '\0' && lb >= 0 && rb > lb) {
                for (k = lineStart; k <= lb; k++) {
                    out[at++] = src[k]; /* !F*, the icon/cost digits and '[' */
                }
                at += skill_name_units(out + at, cap - at - 2, nm);
                for (k = rb; k < end; k++) {
                    out[at++] = src[k]; /* the rest of the line, ']' included */
                }
            } else if (nm[0] != '\0' && lb < 0) {
                for (k = lineStart; k < lineStart + 6 && k < end; k++) {
                    out[at++] = src[k]; /* !F* and the three digits */
                }
                at += skill_name_units(out + at, cap - at - 2, nm);
            } else {
                for (k = lineStart; k < end; k++) {
                    out[at++] = src[k];
                }
            }
            if (i < len) {
                out[at++] = '\n';
            }
            /* The first two digits select the icon; only the third is the stock cost. Keep it in sync with
               this recipe's live BtlParam instead of the disc's stale text. */
            if (p[0xAE + slot] <= 9 && at - outStart >= 6) {
                out[outStart + 5] = (uint16_t)('0' + p[0xAE + slot]);
            }
        } else if (idx < 0 && prevSlot >= 0 && end - lineStart >= 4 && src[lineStart] == '!' &&
                   src[lineStart + 1] == 'F' && src[lineStart + 2] == '%' && at + 2048 < cap) {
            uint16_t joined[512];
            int found = -1, jn = 0, ids[4], n, first = 1;

            for (k = lineStart + 3; k + 7 < end; k++) {
                if (src[k] == 'P' && src[k + 1] == 'a' && src[k + 2] == 'r' && src[k + 3] == 'e' &&
                    src[k + 4] == 'j' && src[k + 5] == 'a' && src[k + 6] == ':' && src[k + 7] == ' ') {
                    found = k + 7;
                    break;
                }
            }
            if (found >= 0) {
                while (found < end && src[found] == ' ') {
                    found++;
                }
                n = skill_partners(p, prevSlot, ids);
                for (j = 0; j < n; j++) {
                    const char *pn = Port_CharaName(ids[j]);

                    if (pn[0] == '\0') {
                        continue;
                    }
                    if (!first) {
                        joined[jn++] = ' ';
                        joined[jn++] = '/';
                        joined[jn++] = ' ';
                    }
                    jn += skill_name_units(joined + jn, 512 - jn - 2, pn);
                    first = 0;
                }
                if (jn != 0) {
                    for (k = lineStart; k < found; k++) {
                        out[at++] = src[k];
                    }
                    for (k = 0; k < jn; k++) {
                        out[at++] = joined[k];
                    }
                } else {
                    for (k = lineStart; k < end; k++) {
                        out[at++] = src[k];
                    }
                }
            } else {
                for (k = lineStart; k < end; k++) {
                    out[at++] = src[k];
                }
            }
            if (i < len) {
                out[at++] = '\n';
            }
        } else if (idx >= 0 && !eFusion[idx] && ePage[idx] == tp &&
                   (chara == 225 || chara == 226 || chara == 229 || chara == 230 ||
                    chara == 235 || chara == 238 || chara == 239 || chara == 241 || chara == 244) &&
                   transIndex < liveTrans) {
            int t = p[0x98 + transIndex];
            int icon = end - lineStart >= 5 ? (src[lineStart+3]-'0')*10+src[lineStart+4]-'0' : 0;
            at += skill_emit_entry(out+at, cap-at-2, icon, p[0x9C+transIndex], Port_CharaName(t));
            if(t == 224 || t == 234 || (chara == 244 && t == 29)) {
                static const char *hint[] = { " (R3 + \xE2\x86\x90)", " (R3 + \xE2\x86\x91)",
                                              " (R3 + \xE2\x86\x92)", " (R3 + \xE2\x86\x93)" };
                at += skill_name_units(out+at, cap-at-2, hint[transIndex]);
            }
            transIndex++;
            if(i < len)out[at++]='\n';
        } else if (chara == 29 && costume == 4 && !transRenamed && idx >= 0 && !eFusion[idx] && ePage[idx] == tp) {
            /* Costume 5's only form is the GT costume 3 model: rewrite the stock Oozaru line. */
            transRenamed = 1;
            if (end - lineStart >= 6) {
                for (k = lineStart; k < lineStart + 5; k++) {
                    out[at++] = src[k];
                }
                out[at++] = '1';
                at += skill_name_units(out + at, cap - at - 2, "Vegeta Explorador SSJ4");
            } else {
                for (k = lineStart; k < end; k++) {
                    out[at++] = src[k];
                }
            }
            if (i < len) {
                out[at++] = '\n';
            }
        } else {
            for (k = lineStart; k < end; k++) {
                out[at++] = src[k];
            }
            if (i < len) {
                out[at++] = '\n';
            }
        }
        prevSlot = (slot >= 0 && slot < 3) ? slot : -1;
        lineStart = i + 1;
    }
    if (injectTrans >= 0 || injectFusion >= 0) {
        return; /* (the insertion point was not reached; leave the stock text untouched) */
    }
    if (at >= cap) {
        return;
    }
    out[at] = 0;
    textAddress = (uint32_t)(uintptr_t)out;
    memcpy(textSlot, &textAddress, sizeof(textAddress));
}

/* Full dump of the select's master cell/form table (the mod's grouping) for the roster audit: one line pe
   cell with every form and its name. Written when the grid is built. */
typedef struct {
    int id, formCount, form[7];
} PortAuditCell;

void Port_DlcAudit(void *cellsv, int count, int mode) {
    PortAuditCell *cells = (PortAuditCell *)cellsv;
    FILE *fp;
    int i, j;

    if (cells == NULL) {
        return;
    }
    fp = fopen("bt3_dlc.log", "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "=== select: mode %d, %d cells ===\n", mode, count);
    for (i = 0; i < count; i++) {
        fprintf(fp, "cell %3d: %3d (%-38s) forms %d:", i, cells[i].id, Port_CharaName(cells[i].id),
                cells[i].formCount);
        for (j = 0; j < cells[i].formCount && j < 7; j++) {
            fprintf(fp, " %3d (%s)", cells[i].form[j], Port_CharaName(cells[i].form[j]));
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
}

/* One line per (cell base, added form) classified at the select: whether the form is a transformation (kept in
   the base's form reel) or a DLC character the grid promotes to its own cell. Written to bt3_dlc.log for the
   roster check. */
void Port_DlcLog(int base, int candidate, int isForm) {
    static int seen[512][2];
    static int count;
    FILE *fp;
    int i;

    for (i = 0; i < count; i++) {
        if (seen[i][0] == base && seen[i][1] == candidate) {
            return;
        }
    }
    if (count < 512) {
        seen[count][0] = base;
        seen[count][1] = candidate;
        count++;
    }
    fp = fopen("bt3_dlc.log", "a");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "base %d (%s) + %d (%s): %s\n", base, Port_CharaName(base), candidate, Port_CharaName(candidate),
            isForm ? "transformation (stays in the form reel)" : "DLC character (own select cell)");
    fclose(fp);
}

/* Overwrite one line of an unpacked text section (the select's name table) with a name. The line keeps its
   BOM and offset; the new text must fit where the old one was (UTF-16 units). */
void Port_TextPatch(void *section, int line, const char *utf8) {
    unsigned char *p = (unsigned char *)section;
    unsigned int first, off, next = 0, lines, i;
    unsigned short *s;
    unsigned short tmp[128];
    int cap, n;

    if (p == NULL || line < 0) {
        return;
    }
    memcpy(&first, p + 4, 4);
    lines = first >> 2;
    if ((unsigned)(line + 1) >= lines) {
        return;
    }
    memcpy(&off, p + (size_t)(line + 1) * 4, 4);
    off &= ~3u;
    /* The select's text lines are fixed 64-byte slots. The original function stopped at the old NUL,
       so a 3-character placeholder could not become "Yamu"; use the whole slot instead. */
    for (i = (unsigned)line + 2; i < lines; i++) {
        unsigned int o2;

        memcpy(&o2, p + (size_t)i * 4, 4);
        o2 &= ~3u;
        if (o2 > off && (next == 0 || o2 < next)) {
            next = o2;
        }
    }
    cap = next != 0 ? (int)((next - off) / 2) : 32;
    if (cap > 32) {
        cap = 32;
    }
    if (cap < 2) {
        return;
    }
    s = (unsigned short *)(p + off);
    n = skill_name_units(tmp, 127, utf8);
    if (n > cap - 1) {
        n = cap - 1;
    }
    s[0] = 0xFEFF;
    for (i = 0; i < (unsigned)n; i++) {
        s[1 + i] = tmp[i];
    }
    s[1 + n] = 0;
}

/* Restore the real simultaneous partner after the original-body reload completed. */
int Port_ExtraFusionRelease(int who,int member,const void*saved,const void*leader){
 extern void*BtlChar_Get(int),*BtlMember_GetActive(void*),*BtlChar_GetPos(void*),*BtlObj_Get(int);
 extern void Port_ReviveSpectator(int),BtlObj_SetPartVisible(void*,unsigned,int),BtlAct_Request(void*,int);extern int Port_FighterObjId(int);
 int target=Port_FusionPartnerActor(who,member),id;void*c,*m;float*p,*l;
 if(target<0||sFusionOwner[target]!=who+1)return 0;c=BtlChar_Get(target);m=c?BtlMember_GetActive(c):NULL;if(!m)return 0;
 memcpy(m,saved,0xA4);sFusionOwner[target]=0;sFusionLink[who][member]=0;if(target>=2)sFight[target-2].consumed=0;
 p=(float*)BtlChar_GetPos(c);l=(float*)BtlChar_GetPos((void*)leader);
 if(p&&l){
  extern float BtlCharApi_GetRadius(int),cosf(float),sinf(float);
  extern int Port_AssistSafePoint(int,float*),Port_AssistCommitPosition(int,const float*,float);
  extern void BtlChar_ClearFlag(void*,int);
  float point[4],distance=BtlCharApi_GetRadius(Port_FighterObjId(who))+BtlCharApi_GetRadius(Port_FighterObjId(target))+2;
  int attempt,j,placed=0;memcpy(p+4,l+4,4*sizeof(float));memset(p+8,0,12*sizeof(float));p[0x94/4]=l[5];if(distance<6)distance=6;if(distance>60)distance=60;
  for(j=0xF3;j<=0xF8;j++)BtlChar_ClearFlag(c,j);BtlChar_ClearFlag(c,0xFC);BtlChar_ClearFlag(c,0xFD);
  for(attempt=0;attempt<4&&!placed;attempt++){
   float angle=l[5]+(attempt/2)*1.57079633f,sign=(attempt&1)?-1.f:1.f;
   point[0]=l[0]+cosf(angle)*distance*sign;point[1]=l[1];point[2]=l[2]-sinf(angle)*distance*sign;point[3]=1;
   if(Port_AssistSafePoint(target,point))placed=Port_AssistCommitPosition(target,point,l[5]);
  }
  if(!placed){memcpy(point,l,4*sizeof(float));if(!Port_AssistSafePoint(target,point))memcpy(point,l,4*sizeof(float));Port_AssistCommitPosition(target,point,l[5]);}
 }
 id=Port_FighterObjId(target);if(id>=0){void*o=BtlObj_Get(id);if(o){extern void BtlObjBody_Warp(void*,void*);BtlObjBody_Warp(o,p);BtlObj_SetPartVisible(o,0,1);}}
 {extern void Port_DefusionBurst(int);Port_DefusionBurst(who);Port_DefusionBurst(target);}
 Port_ReviveSpectator(target);BtlAct_Request(c,0xB);return 1;
}
int Port_FusionPartnerPad(int who,int member){extern void*BtlChar_Get(int);extern int Port_TargetHuman(int);int target=Port_FusionPartnerActor(who,member);void*c;
 if(target<0||Port_FusionConsumedActor(target)||!Port_TargetHuman(target))return -1;c=BtlChar_Get(target);return c?*(int*)((unsigned char*)c+4):-1;}

#include "mod_revive_native.inc"
#include "mod_assists_native.inc"
#include "mod_training_health_native.inc"
