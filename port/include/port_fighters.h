/* More than two fighters in a fight: the numbers and questions the game's battle code and the port share.
   (The game's files include this inside #ifdef PORT only; C and C++.) */
#ifndef PORT_FIGHTERS_H
#define PORT_FIGHTERS_H
#ifdef __cplusplus
extern "C" {
#endif

/* How many fighters a fight can have: the two sides' and the extra ones. THE one number: every table that is kept
   by fighter (roster index, which is also the fighter's object id) is this long. */
#define PORT_FIGHTER_MAX 10
#define PORT_EXTRA_MAX (PORT_FIGHTER_MAX - 2)
/* Teams are numbered from 0: 0 is player 1's, 1 player 2's, and in a fight where everyone is alone each fighter
   has its own, so there can be as many as fighters. */
#define PORT_TEAM_MAX PORT_FIGHTER_MAX

/* port/src/plat_fighters.c: the extra fighters of the fight being loaded or fought, 0 <= k < Port_FightExtras().
   Extra k is roster index 2 + k. Decided once per fight (Port_ExtraSetup). */
int Port_FightExtras(void);
int Port_FightChara(int k);   /* character 0..160 */
int Port_FightCostume(int k);
int Port_FightTeam(int k);    /* team number 0..PORT_TEAM_MAX-1 */
int Port_FightLevel(int k);   /* computer level 0..29, -1: not set (its side's own) */
int Port_FightMember(int k);  /* the member of the battle setup it was made from: side * 8 + index, -1: none */
int Port_FightRoster(void);   /* 0, or the fight is of the sides' own team rosters: 1 Team Battle, 2 Free-for-All */

/* What the loader made for them (port/src/plat_stub.c; -1: nothing). gPortExtraObjs: how many have an object, which
   is how many are in the roster (0 until the objects are made). */
extern int gPortExtraSlot[PORT_EXTRA_MAX], gPortExtraObj[PORT_EXTRA_MAX];
extern int gPortExtraObjs;

#ifdef __cplusplus
}
#endif
#endif
