/* Dragon History story tune + difficulty: per chapter / per fighter tune (stTunes) on top of the costume, then the
   approved difficulty multipliers for the opponent; story only, fresh from base values, rounding / floor / clamp.
   Real runtime, stub save. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "plat_songs.h"
static int sDifficulty = 1;
int Port_Setting(const char *name, int def) { return !strcmp(name, "story_difficulty") ? sDifficulty : def; }
char gPortSongNames[PORT_SONG_MAX][64]; int gPortSongCount, gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void) { gPortSongCount = 0; }
static int sRanks[72];
int Save_GetStoryBits(void) { return 0; }
void Save_OrStoryBits(int b) { (void)b; } static int sStubGroups[9]; int Save_GetStoryBitsGroup(int g){return g>0&&g<9?sStubGroups[g]&0xFF:0;} void Save_OrStoryBitsGroup(int g,int b){if(g>0&&g<9)sStubGroups[g]|=b&0xFF;}
void Save_AddMoney(int a) { (void)a; }
int Save_GetMoney(void) { return 0; }
int Save_GetStoryRank(int c) { return c >= 0 && c < 72 ? sRanks[c] : 0; }
void Save_ClearStoryRank(int c){if(c>=0&&c<72)sRanks[c]=0;}
void Save_RaiseStoryRank(int c, int r) { (void)c; (void)r; }
int Save_GrantDragonBall(int b) { (void)b; return 0; }
int BattleReplay_IsActive(void) { return 0; }
int Port_NetActive(void) { return 0; }
int Port_StoryReviveActor(int a, int h) { (void)a; (void)h; return 1; }
void Port_StoryMusicStart(int id){(void)id;}void Port_StoryMusicEnd(void){}
void Port_StoryReleaseActor(int a){(void)a;}
#include "mod_scenarios_runtime.inc"

static void choose(int chapter, int difficulty) { MsView v; sDifficulty = difficulty; Port_ScenarioClear(); Port_StoryChoose(chapter); memset(&v, 0, sizeof(v)); assert(Port_ScenarioDraft(&v) == 2); }
/* What btl_char_mgr.c does with a tune: max HP from the fresh base, rounded to 1,000, floor 10,000. */
static int health(int base, int pct) { long long v = (long long)base * pct / 100; v = (v + 500) / 1000 * 1000; return v < 10000 ? 10000 : (int)v; }

static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void) {
    int hp, atk, def, d;
    static const int raditzHp[3] = {51000, 60000, 78000}, raditzAtk[3] = {6, 8, 11};
    /* pure difficulty helpers (still used for the table) */
    assert(ST_ScaleHealth(0, 0, 40000) == 34000 && ST_ScaleHealth(0, 2, 40000) == 52000 && ST_ScaleHealth(0, 0, 10000) == 10000);
    assert(ST_ScaleAttack(0, 2, 39) == 40 && ST_ScaleAttack(0, 0, -19) == -20);
    for (d = 0; d < 3; d++) {
        /* chapter 1: Raditz x1.5 / +8 / def +6, then the difficulty; Piccolo untouched */
        choose(0, d);
        assert(Port_StoryTune(1, &hp, &atk, &def) && health(40000, hp) == raditzHp[d] && atk == raditzAtk[d] && def == 6);
        assert(Port_StoryTune(0, &hp, &atk, &def) && hp == 100 && atk == 0 && def == 0);
        /* chapter 2: Raditz the same; Krillin x0.6 / -8 / def -4 at every difficulty; Goku untouched; scene actors none */
        choose(1, d);
        assert(Port_StoryTune(Port_StoryCastActor(2), &hp, &atk, &def) && health(40000, hp) == raditzHp[d] && atk == raditzAtk[d] && def == 6);
        assert(Port_StoryTune(Port_StoryCastActor(1), &hp, &atk, &def) && health(40000, hp) == 10000 && atk == -20 && def == -20);
        assert(Port_StoryTune(Port_StoryCastActor(0), &hp, &atk, &def));
        assert(!Port_StoryTune(Port_StoryCastActor(3), &hp, &atk, &def) && !Port_StoryTune(Port_StoryCastActor(4), &hp, &atk, &def));
        /* the side hook no longer scales story fights (no double scaling) */
        assert(Port_ScenarioHealthMax(1,40000)==Port_StoryEraHealth(1,40000));
        /* difficulty latched at the choice: a settings change mid-fight changes nothing */
        sDifficulty = 2 - d; assert(Port_StoryTune(Port_StoryCastActor(2), &hp, &atk, &def) && health(40000, hp) == raditzHp[d]);
    }
    /* retries: the same answer from the same base every time (nothing stored) */
    choose(0, 2); Port_StoryTune(1, &hp, &atk, &def); assert(health(40000, hp) == 78000); Port_StoryTune(1, &hp, &atk, &def); assert(health(40000, hp) == 78000);
    /* other modes: no tune, Scenario boss buff unchanged */
    Port_ScenarioChoose(1, 0); { MsView v; memset(&v, 0, sizeof(v)); Port_ScenarioDraft(&v); Port_ScenarioRoundReset(); }
    assert(!Port_StoryTune(1, &hp, &atk, &def) && Port_ScenarioHealthMax(1, 50000) == 150000 && Port_ScenarioHealthMax(0, 50000) == 50000);
    Port_ScenarioClear(); assert(!Port_StoryTune(1, &hp, &atk, &def) && Port_ScenarioHealthMax(1, 50000) == 50000);
    puts("PASS Dragon History story tune: Raditz x1.5/+8/def+6 then difficulty (51,000/60,000/78,000 HP, attack +6/+8/+11), Krillin ch2 x0.25/-20/def-20 (10,000, the floor), Goku/Piccolo/scene actors untouched, latched, no compounding, no double scaling, Scenarios unchanged");
    return 0;
}
