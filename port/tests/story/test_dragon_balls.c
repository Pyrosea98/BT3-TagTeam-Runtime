/* Dragon Balls in the port's Dragon History chapters, offline: the real story runtime against a save stub. A found
   ball (BattleResult.unk44, recorded by Port_StoryFoundBall before the result) is saved once on a successful
   eligible clear and offered to the result screen; failures, restarts, abandoned fights, replays, online fights,
   owned balls and other modes grant nothing. The save helper is the real one (copied by check_dragon_balls.py). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "plat_songs.h"
int Port_Setting(const char *name, int def) { (void)name; return def; }
char gPortSongNames[PORT_SONG_MAX][64]; int gPortSongCount, gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void) { gPortSongCount = 0; }
static int sSlotFlags, sMoney, sRanks[72], sUnlock, sReplay, sNet;
int Save_GetStoryBits(void) { return (sSlotFlags >> 24) & 0xFF; }
void Save_OrStoryBits(int bits) { sSlotFlags |= (bits & 0xFF) << 24; } static int sStubGroups[9]; int Save_GetStoryBitsGroup(int g){return g>0&&g<9?sStubGroups[g]&0xFF:0;} void Save_OrStoryBitsGroup(int g,int b){if(g>0&&g<9)sStubGroups[g]|=b&0xFF;}
void Save_AddMoney(int amount) { sMoney += amount; }
int Save_GetMoney(void) { return sMoney; }
int Save_GetStoryRank(int c) { return c >= 0 && c < 72 ? sRanks[c] : 0; }
void Save_ClearStoryRank(int c){if(c>=0&&c<72)sRanks[c]=0;}
void Save_RaiseStoryRank(int c, int r) { if (c >= 0 && c < 72 && r > sRanks[c]) sRanks[c] = r; }
/* the shipped Save_GrantDragonBall body over a stub save (gSaveData->unlockFlags) */
static struct { int unlockFlags; } sSave, *gSaveData = &sSave;
typedef int s32;
#include "grant_ball_body.inc"
int BattleReplay_IsActive(void) { return sReplay; }
int Port_NetActive(void) { return sNet; }
int Port_StoryReviveActor(int a, int h) { (void)a; (void)h; return 1; }
void Port_StoryMusicStart(int id){(void)id;}void Port_StoryMusicEnd(void){}
void Port_StoryReleaseActor(int a){(void)a;}
#include "mod_scenarios_runtime.inc"

static int fight(int found, int winner, int reason, int eligible) {
    Port_ScenarioRoundReset();          /* round reset: Port_StoryFightStarted */
    Port_StoryFoundBall(found);         /* Port_AchievementBattleResult, before the result */
    Port_ScenarioResult(winner, winner == 0 ? 1 : 2, reason, NULL, 0, eligible);
    return Port_StoryGrantedBall();
}

static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void) {
    int ch, z;
    (void)sUnlock;
    /* the search is on only for a story chapter, not for replays or online */
    Port_ScenarioClear(); assert(!Port_StoryDragonBallSearch());
    Port_StoryChoose(0); assert(Port_StoryDragonBallSearch());
    sReplay = 1; assert(!Port_StoryDragonBallSearch()); sReplay = 0;
    sNet = 1; assert(!Port_StoryDragonBallSearch()); sNet = 0;
    /* Raditz wins (canonical clear) with ball 1 found: saved once, offered to the result screen as 0 */
    assert(fight(1, 1, 1, 1) == 0 && sSave.unlockFlags == 1);
    assert(Port_StoryLastOutcome(&ch, &z) == ST_CANON);
    assert(Port_StoryUnlockedChapter()==1);
    /* acknowledged on the chapter page: nothing left to show */
    Port_StoryAcknowledge(); assert(Port_StoryGrantedBall() == -1&&Port_StoryUnlockedChapter()==-1);
    /* the same result twice (idempotent commit) does not grant twice */
    Port_StoryChoose(0); assert(fight(2, 0, 1, 1) == 1 && sSave.unlockFlags == 3);
    assert(Port_StoryUnlockedChapter()==1); /* the first WIN after the narrative defeat announces the next chapter too (Juan 2026-11-02) */
    Port_StoryFoundBall(3); Port_ScenarioResult(0, 1, 1, NULL, 0, 1); assert(sSave.unlockFlags == 3 && Port_StoryGrantedBall() == 1);
    /* a ball already owned: no reward shown */
    Port_StoryChoose(0); assert(fight(2, 0, 1, 1) == -1 && sSave.unlockFlags == 3);
    assert(Port_StoryUnlockedChapter()==-1); /* later wins: a repeat, no announcement */
    /* failures: time-out, restart (Fight Again), abandon */
    Port_StoryChoose(0); assert(fight(3, 1, 2, 1) == -1 && sSave.unlockFlags == 3);        /* time-out */
    Port_StoryChoose(0); assert(fight(3, -1, 0x8000, 1) == -1 && sSave.unlockFlags == 3);  /* restart */
    Port_StoryChoose(0); Port_ScenarioRoundReset(); Port_StoryFoundBall(3);                 /* abandoned: no result */
    assert(Port_StoryTakeReturn() == 1 && Port_StoryGrantedBall() == -1 && sSave.unlockFlags == 3);
    /* a ball found in an aborted fight does not leak into the next fight */
    Port_StoryChoose(0); Port_ScenarioRoundReset(); Port_StoryFoundBall(3); Port_ScenarioRoundReset();
    Port_ScenarioResult(0, 1, 1, NULL, 0, 1); assert(sSave.unlockFlags == 3 && Port_StoryGrantedBall() == -1);
    /* ineligible (replay / online result): the ending shows, nothing is saved */
    Port_StoryChoose(0); assert(fight(3, 0, 1, 0) == -1 && sSave.unlockFlags == 3);
    /* invalid ids */
    Port_StoryChoose(0); assert(fight(8, 0, 1, 1) == -1 && fight(-1, 0, 1, 1) == -1 && sSave.unlockFlags == 3);
    /* other modes: a Scenario never records or grants a ball */
    Port_ScenarioChoose(3, 0); Port_StoryFoundBall(3); assert(Port_StoryGrantedBall() == -1 && !Port_StoryDragonBallSearch());
    Port_ScenarioClear();
    /* the real save helper: bounds, owned, null save */
    assert(!Save_GrantDragonBall(0) && !Save_GrantDragonBall(8) && !Save_GrantDragonBall(1) && Save_GrantDragonBall(7) && sSave.unlockFlags == (3 | 64));
    /* A new chapter receipt needs a first eligible success, survives result
       re-reads, and clears on acknowledge/choose/restart. */
    sSlotFlags=0;
    Port_StoryChoose(0);fight(0,1,2,1);assert(Port_StoryUnlockedChapter()==-1);
    Port_StoryChoose(0);fight(0,1,1,0);assert(Port_StoryUnlockedChapter()==-1);
    Port_StoryChoose(0);Port_ScenarioRoundReset();Port_StoryTakeReturn();assert(Port_StoryUnlockedChapter()==-1);
    Port_StoryChoose(0);fight(0,0,1,1);assert(Port_StoryUnlockedChapter()==1);
    Port_StoryResult(0,1,1);assert(Port_StoryUnlockedChapter()==1);
    Port_StoryAcknowledge();assert(Port_StoryUnlockedChapter()==-1);
    Port_StoryChoose(1);Port_ScenarioRoundReset();assert(Port_StoryResult(0,1,1)==0); /* not chapter1's trophy */
    assert(Port_StoryUnlockedChapter()==2); /* chapter 2's first clear announces chapter 3 */
    gSaveData = NULL; assert(!Save_GrantDragonBall(4));
    puts("PASS Dragon Balls in story chapters: search only for local story fights, found ball saved once on canonical/alternative clear and offered to the result screen, owned/failed/restarted/abandoned/ineligible/invalid/Scenario grant nothing, no leak across fights, real save helper bounds");
    return 0;
}
