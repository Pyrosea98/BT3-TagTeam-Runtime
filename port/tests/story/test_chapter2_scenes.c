void Port_StoryResumeCombatActor(int a,int b){}
/* Chapter 2 (Kame House) and scene-only actors, offline: the real draft, scenario/story runtime and scene driver
   against stubs of the battle calls. Checks the five-actor draft (scene actors included), the scene-only registry
   (parked out of the fight unless a cutscene shows it; re-parked on every exit), that every line films its own
   speaker, Gohan behind Goku in the opening, Gohan carried limp beside Raditz every frame of the canonical ending
   (including the departure), Piccolo's arrival and SS02_013-014, the alternative ending, chapter 1 unchanged. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "sys/math3d.h"
#include "battle/stg_a.h"
#include <math.h>
#include "battle/btl_seq.h"
#include "battle/battle_setup.h"
#include "battle/battle_work.h"
#include "battle/battle.h"
#include "plat_songs.h"

static int sPresentationEnabled;
int Port_Setting(const char *name, int def) { if(!strcmp(name,"story_presentation"))return sPresentationEnabled; return def; }
char gPortSongNames[PORT_SONG_MAX][64]; int gPortSongCount, gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void) { gPortSongCount = 0; }
static int sBits, sMoney, sRanks[72];
int Save_GetStoryBits(void) { return sBits; }
void Save_OrStoryBits(int b) { sBits |= b; } static int sStubGroups[9]; int Save_GetStoryBitsGroup(int g){return g>0&&g<9?sStubGroups[g]&0xFF:0;} void Save_OrStoryBitsGroup(int g,int b){if(g>0&&g<9)sStubGroups[g]|=b&0xFF;}
void Save_AddMoney(int a) { sMoney += a; }
int Save_GetMoney(void) { return sMoney; }
int Save_GetStoryRank(int c) { return c >= 0 && c < 72 ? sRanks[c] : 0; }
void Save_ClearStoryRank(int c){if(c>=0&&c<72)sRanks[c]=0;}
void Save_RaiseStoryRank(int c, int r) { if (c >= 0 && c < 72 && r > sRanks[c]) sRanks[c] = r; }
int Save_GrantDragonBall(int b) { (void)b; return 0; }
int BattleReplay_IsActive(void) { return 0; }
int Port_NetActive(void) { return 0; }

#define N 5
static int sMode, sMotionId[N], sMotions[N], sStopped[N], sVisible[N], sXfade, sTeam[N];
static Vec4 sPos[N], sRot[N];
static int sCameraActor = -1, sFixed, sCleared;
static BattleResult sResult;
static BattleWork sWork;
void BtlCtrl_PlayMotion(s32 a, s32 m, s32 loop, f32 b) { (void)loop; (void)b; sMotionId[a] = m; sMotions[a]++; }
void BtlCtrl_StopMotion(s32 a) { sStopped[a]++; }
void BtlCtrl_SetPos(s32 a, Vec4 *p) { sPos[a] = *p; }
void BtlCtrl_SetRot(s32 a, Vec4 *r) { sRot[a] = *r; }
void BtlCharApi_GetPos(s32 obj, Vec4 *p) { *p = sPos[obj]; }
void BtlCharApi_GetRot(s32 obj, Vec4 *r) { *r = sRot[obj]; }
void BtlCharApi_GetNodePos(s32 obj,s32 node,Vec4*out){(void)node;BtlCharApi_GetPos(obj,out);out->y-=7.8f;}
f32 BtlCharApi_GetHeight(s32 obj) { return obj == 3 ? 6.0f : 10.0f; }
f32 BtlCharApi_GetGroundY(s32 obj) { (void)obj; return 0; }
static int sStageReady,sHasWater,sStartZone[N],sPlacements[N],sProbeZone;static float sWater,sProbeY;static Vec4 sStarts[N],sStartRot[N],sLastEye;
s32 BtlStage_IsReady(void) { return sStageReady; }
s32 BtlStage_GetWaterLevel(f32*y){*y=sWater;return sHasWater;}
s32 BtlStage_GetStartPlace(s32 a,Vec4*p,Vec4*q,s32 alt){assert(a>=0&&a<N);*p=sStarts[a];*q=sStartRot[a];return sStartZone[a];}
s32 BtlStage_ProbeGround(s32 z, Vec4 *p, StgGroundHit *h) { (void)z; (void)p; h->y = sProbeY; return sProbeZone; }
void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { sLastEye=*p;assert(isfinite(p->x) && isfinite(a->y)); sFixed++; }
void BtlFacade_ClearFixedCamera(void) { sCleared++; }
int EftCam_IsActive(void){return 0;}
s32 Port_FighterObjId(s32 i) { return i; }
s32 Port_Team(s32 i) { return sTeam[i]; }
static int sObjs[N];
void *BtlObj_Get(s32 id) { return id >= 0 && id < N ? &sObjs[id] : NULL; }
void *BtlObj_Get(s32 id);
void BtlObj_SetPartVisible(void *o, u32 part, s32 on) { (void)part; sVisible[(int *)o - sObjs] = on; }
void ScrXfade_RequestCapture(void) {}
void ScrXfade_Start(s32 r, f32 s) { (void)r; (void)s; sXfade++; }
s32 Battle_GetMode(void) { return sMode; }
BattleWork *Battle_GetWork(void) { return &sWork; }
BattleResult *Battle_GetResult(void) { return &sResult; }
s32 BattleSide_GetObjId(s32 side) { return side; }
s32 BtlCharApi_GetHp(s32 obj) { (void)obj; return 1; }
s32 BtlCharApi_GetHpMax(s32 obj) { (void)obj; return 1; }
s32 BtlStage_CountItemHolders(void) { return 0; }
void Ramp_Start(Ramp *r, f32 seconds, f32 from, f32 to) { r->value = from; r->target = to; r->step = (to - from) / (seconds * 30.0f); }
s32 Ramp_Step(Ramp *r) { r->value += r->step; if (r->value >= r->target) { r->value = r->target; return 1; } return 0; }

/* chapter 3 event calls (unused here) */
s32 BtlCtrl_IsInterruptible(s32 a) { (void)a; return 1; }
s32 BtlCtrl_UseTechnique(s32 a, s32 k) { (void)a; (void)k; return 1; }
void BtlCtrl_SetHidden(s32 a) { (void)a; }
void BtlCtrl_SetAuraOn(s32 a) { (void)a; }
void BtlCtrl_LowerHp(s32 a, s32 m, f32 p) { (void)a; (void)m; (void)p; }
s32 BtlCtrl_GetActiveMember(s32 a) { (void)a; return 0; }
void Hud_SlideOut(f32 s) { (void)s; }
void Hud_SlideIn(f32 s) { (void)s; }
static int sStubChr[8];
void *BtlChar_Get(s32 i) { return &sStubChr[i & 7]; }
void BtlCharSnd_PlayCommon(void *c, s32 id) { (void)c; (void)id; }
s32 gPortTarget[8];
void BattleResult_Set(s32 w, s32 r) { (void)w; (void)r; }
void Port_StoryBeamVictim(int who, int winner, int damage) { (void)who; (void)winner; (void)damage; }
void BtlChar_Place(void*c,Vec4*p,Vec4*q,s32 z){int a=(int*)c-sStubChr;assert(a>=0&&a<N);sPlacements[a]++;sPos[a]=*p;sRot[a]=*q;}
static unsigned sPresentationClock;static int sPresentationInput,sPresentationBgm;
unsigned Port_StoryPresentationClock(void){return sPresentationClock;}
int Port_StoryPresentationInput(void){return sPresentationInput;}
void Bgm_Play(int id){sPresentationBgm=id;}
int Battle_GetBgm(void){return 3;}
int PortSongs_StoryDurationMs(const char*key,int saga,int title){return 0;}
int PortSongs_StoryBgm(const char*key,int saga,int title){return 900+saga*2+title;}
int Port_StoryReviveActor(int a, int h) { (void)a; (void)h; return 1; }
void Port_StoryMusicStart(int id){(void)id;}void Port_StoryMusicEnd(void){}
void Port_StoryReleaseActor(int a){(void)a;}
#include "mod_scenarios_runtime.inc"
void BtlChar_ClearFlag(void*c,int f){(void)c;(void)f;}
void BtlChar_SetFxBit(void*c,int f){(void)c;(void)f;}
int BtlParam_GetChargeLoopSound(void*c){(void)c;return 12;}
void BtlCharSnd_PlayCommonFar(void*c,int id){BtlCharSnd_PlayCommon(c,id);}
void Port_StoryChargeSpark(int actor,int on){(void)actor;(void)on;}
void Port_StoryHitFx(int a){(void)a;}
s32 BtlCtrl_Transform(s32 a, s32 id) { (void)a; (void)id; return 1; }
void Port_StoryHandControl(int to) { (void)to; }
int Port_StoryResourceIO(int a,int w,int*v){(void)a;(void)w;(void)v;return 0;}
void Port_Dbg3(const char*m,int a,int b,int c,int d){(void)m;(void)a;(void)b;(void)c;(void)d;}
int gPortClash[2]={-1,-1};static int sStoryTestClash;int Port_ClashBusy(void){return sStoryTestClash;}
s32 Port_StoryForceClash(s32 a, s32 b) { (void)a; (void)b; return 0; }
#define Port_StoryUseTechnique BtlCtrl_UseTechnique
int Port_SequencePartner(int a){return -1;}
void Port_StoryControlProbe(int a){(void)a;}
void Port_StoryBeamProbe(s32 a,s32 b,s32 f){(void)a;(void)b;(void)f;}void Port_StoryActorProbe(s32 a,s32 f){(void)a;(void)f;} /* env-gated diagnostics */
void Port_StoryHoldPose(int a,int m){BtlCtrl_PlayMotion(a,m,1,.1f);}
void Port_StoryTailOutfit(int a){(void)a;}
#include "mod_story_scene.inc"

static int sub(int *set, int *line) { int ch; return Port_StorySubtitleView(&ch, set, line); }
/* Runs a scene like states 1/5 do and records, per shown line, its set, line and the actor the camera framed. */
static int sLines[24][3], sN;
static float sGap[2]; /* the carried actor's largest distance from its carrier while carried */
static int run(BtlSeqTalkCtx *ctx) {
    int frames = 0, set, line, last = -1;
    sN = 0; sGap[0] = 0; sGap[1] = 1e9f;
    while (ctx->step >= ST_STEP_BEAT && ctx->step <= ST_STEP_HOLD && frames < 30 * 200) {
        st_scene_beat(ctx);
        if (sub(&set, &line) && (set << 8 | line) != last) { last = set << 8 | line; sLines[sN][0] = set; sLines[sN][1] = line; sLines[sN][2] = sStRun.actor; sN++; }
        if (!st_scene_hold(ctx)) { st_scene_end(); break; }
        if (sStRun.set == ST_SET_CANON && (sStRun.actors >> 3 & 1u)) {
            float dx = sPos[3].x - sPos[2].x, dz = sPos[3].z - sPos[2].z, dy = sPos[3].y - sPos[2].y;
            float d = sqrtf(dx * dx + dz * dz); if (d > sGap[0]) sGap[0] = d; if (dy < sGap[1]) sGap[1] = dy;
        }
        frames++;
    }
    return frames;
}
/* Battle actor of a cast entry, from the real registry. */
static int actor(int cast) { return Port_StoryCastActor(cast); }

static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void) {
    BtlSeqTalkCtx ctx;
    MsView v;
    int i, set, line, goku, krillin, raditz, gohan, piccolo;

    /* ---- chapter 2 draft: Goku (human) + Krillin vs Raditz, Gohan and Piccolo as scene-only actors ---- */
    Port_ScenarioClear(); Port_StoryChoose(1); sMode = 0;
    memset(&v, 0, sizeof(v));
    assert(Port_ScenarioDraft(&v) == 2 && v.total == 5 && v.a == 2 && v.pick[0].chara == 0 && v.pick[0].pad == 0 && v.pick[0].costume == 2 && v.pick[1].costume == 1 && v.pick[3].costume == 3);
    assert(v.pick[1].chara == 25 && v.pick[2].chara == 77 && v.pick[3].chara == 13 && v.pick[4].chara == 22);
    goku = actor(0); krillin = actor(1); raditz = actor(2); gohan = actor(3); piccolo = actor(4);
    assert(goku == 0 && raditz == 1 && krillin >= 2 && gohan >= 2 && piccolo >= 2 && gohan != piccolo && krillin != gohan);
    for (i = 0; i < N; i++) sTeam[i] = sScTeams[i];
    assert(sTeam[goku] == 0 && sTeam[krillin] == 0 && sTeam[raditz] == 1 && sTeam[gohan] == 1 && sTeam[piccolo] == 1);
    /* scene-only registry: only Gohan and Piccolo; parked unless shown */
    assert(Port_StorySceneActor(gohan) && Port_StorySceneActor(piccolo) && !Port_StorySceneActor(goku) && !Port_StorySceneActor(krillin) && !Port_StorySceneActor(raditz));
    assert(Port_StorySceneParked(gohan) && Port_StorySceneParked(piccolo) && !Port_StorySceneParked(goku) && !Port_StorySceneParked(raditz));
    Port_StorySceneShow(goku, 1); assert(!Port_StorySceneParked(goku) && !(sStSceneShown & 1u)); /* a fighter is never "shown" */
    for (i = 0; i < N; i++) { sPos[i].x = 0; sPos[i].y = 0; sPos[i].z = i * 40.0f; sPos[i].w = 1; sRot[i].y = 0; }
    sPos[raditz].z = 80;

    /* ---- opening SS02_001-008: every line films its speaker; Gohan shown behind Goku, Piccolo stays parked ---- */
    memset(&ctx, 0, sizeof(ctx));
    assert(st_intro_enter(&ctx) == 1);
    assert(Port_StorySceneActorShown(goku) && Port_StorySceneActorShown(krillin) && Port_StorySceneActorShown(raditz) && Port_StorySceneActorShown(gohan) && !Port_StorySceneActorShown(piccolo));
    assert(!Port_StorySceneParked(gohan) && Port_StorySceneParked(piccolo) && sVisible[gohan] == 1);
    assert(sPos[gohan].z < sPos[goku].z && fabsf(sPos[gohan].x - sPos[goku].x) > 0.1f); /* behind (facing +z) and to the side */
    run(&ctx);
    assert(sN == 8);
    { static const int who[8] = {0, 3, 2, 0, 2, 0, 2, 0}; /* cast: Goku Gohan Raditz Goku Raditz Goku Raditz Goku */
      for (i = 0; i < 8; i++) assert(sLines[i][0] == ST_SET_INTRO && sLines[i][1] == i && sLines[i][2] == actor(who[i])); }
    assert(Port_StorySceneParked(gohan) && Port_StorySceneParked(piccolo) && sVisible[gohan] == 0 && !sub(&set, &line)); /* re-parked */

    /* ---- canonical ending (Raditz wins): SS02_009-012, Gohan carried, departure caption, Piccolo's bridge ---- */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 2; sResult.reason = 1;
    assert(st_ending_enter(&ctx) == 1);
    assert(!Port_StorySceneParked(gohan) && sMotionId[gohan] == ST_MOTION_LIMP && Port_StorySceneParked(piccolo));
    run(&ctx);
    assert(sN == 7);
    { static const int who[4] = {3, 2, 0, 1}; /* Gohan Raditz Goku Krillin */
      for (i = 0; i < 4; i++) assert(sLines[i][0] == ST_SET_CANON && sLines[i][1] == i && sLines[i][2] == actor(who[i])); }
    assert(sLines[4][0] == ST_SET_LEAVES && sLines[4][2] == raditz);
    assert(sLines[5][0] == ST_SET_BRIDGE && sLines[5][1] == 0 && sLines[5][2] == piccolo);
    assert(sLines[6][0] == ST_SET_BRIDGE && sLines[6][1] == 1 && sLines[6][2] == goku);
    assert(sGap[0] < 10.0f && sGap[1] < 0.0f);  /* at his side, raised (Y down) the whole time he was carried */
    assert(sPos[raditz].y < -10.0f);             /* Raditz flew off upwards */
    assert(sMotionId[piccolo] == ST_MOTION_IDLE); /* landed */
    assert(Port_StorySceneParked(gohan) && Port_StorySceneParked(piccolo) && sVisible[piccolo] == 0 && !sub(&set, &line));
    assert(!strcmp(ST_LeavesText(1, ST_SET_LEAVES, 0), "Raditz takes Gohan away.") && !strcmp(ST_LeavesText(1, ST_SET_LEAVES_ALT, 0), "Raditz leaves."));

    /* ---- alternative ending (Goku + Krillin win): SSA_014 Krillin, DRAFT Raditz; Gohan safe, no bridge ---- */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 1; sResult.reason = 1;
    assert(st_ending_enter(&ctx) == 1 && !Port_StorySceneParked(gohan) && sMotionId[gohan] == 0);
    run(&ctx);
    assert(sN == 3 && sLines[0][0] == ST_SET_ALT && sLines[0][2] == krillin && sLines[1][2] == raditz && sLines[2][0] == ST_SET_LEAVES_ALT);
    assert(Port_StorySceneParked(gohan) && Port_StorySceneParked(piccolo));

    /* ---- skip / abandon mid-scene and a new round re-park the scene actors ---- */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 2; st_ending_enter(&ctx); st_scene_beat(&ctx);
    assert(!Port_StorySceneParked(gohan)); st_scene_end(); assert(Port_StorySceneParked(gohan) && sVisible[gohan] == 0);
    memset(&ctx, 0, sizeof(ctx)); st_intro_enter(&ctx); assert(!Port_StorySceneParked(gohan));
    Port_ScenarioRoundReset(); assert(Port_StorySceneParked(gohan));          /* Fight Again / restart */
    st_scene_end();
    Port_ScenarioClear(); assert(!Port_StorySceneActor(gohan) && !Port_StorySceneParked(gohan)); /* main menu: nothing left */

    /* ---- chapter 1 has no scene actors; Scenarios have none ---- */
    Port_StoryChoose(0); memset(&v, 0, sizeof(v)); assert(Port_ScenarioDraft(&v) == 2 && v.total == 2);
    for (i = 0; i < N; i++) assert(!Port_StorySceneActor(i));
    Port_ScenarioChoose(3, 0); memset(&v, 0, sizeof(v)); assert(Port_ScenarioDraft(&v) > 0);
    for (i = 0; i < N; i++) assert(!Port_StorySceneActor(i) && !Port_StorySceneParked(i));
    Port_ScenarioClear();
    puts("PASS chapter 2 and scene-only actors: 5-actor draft, Gohan/Piccolo parked unless a cutscene shows them and re-parked on every exit, each line films its speaker, Gohan behind Goku, carried limp beside Raditz through the departure, Piccolo lands for SS02_013-014, alternative ending without bridge, chapter 1 and Scenarios without scene actors");
    return 0;
}

int BtlCharApi_GetChara(int obj){(void)obj;return 80;}





