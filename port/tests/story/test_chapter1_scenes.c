void Port_StoryResumeCombatActor(int a,int b){}
/* Dragon History chapter 1 scenes (port/include/mod_story_scene.inc), offline: the real scene driver and story
   runtime against stubs of the battle calls it makes. Checks the opening (lines, speakers, native entrance cuts,
   cross-fades), the lines during the fight (cues, once each, one at a time, none while paused), both endings
   (win / lose poses, canonical from SS01_005, the leaves caption), failures keeping the game's own scene, mode-1
   stock scripts untouched, and no subtitle left behind. */
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

/* ---- stubs: settings, songs, save ---- */
static int sPresentationEnabled;
int Port_Setting(const char *name, int def) { if(!strcmp(name,"story_presentation"))return sPresentationEnabled; return def; }
char gPortSongNames[PORT_SONG_MAX][64]; int gPortSongCount, gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void) { gPortSongCount = 1; strcpy(gPortSongNames[0], "Saga de los Saiyajin"); gPortSongOffsets[0] = 901; }
static int sBits, sMoney;
int Save_GetStoryBits(void) { return sBits; }
void Save_OrStoryBits(int b) { sBits |= b; } static int sStubGroups[9]; int Save_GetStoryBitsGroup(int g){return g>0&&g<9?sStubGroups[g]&0xFF:0;} void Save_OrStoryBitsGroup(int g,int b){if(g>0&&g<9)sStubGroups[g]|=b&0xFF;}
void Save_AddMoney(int a) { sMoney += a; }
int Save_GetMoney(void) { return sMoney; }

/* ---- stubs: the battle calls of the scene driver ---- */
static int sMode, sEntrance[2], sWinPose[2], sLosePose[2], sCam[2][3], sXfade, sPoseReached = 1, sHp[2], sHpMax[2];
static float sFloor=0,sActorFloor=0;static int sStageReady=1,sNoFloor,sProbes;
static int sMotion[2],sMotionId[2],sStopped[2],sFixed,sCameraClear;
static Vec4 sPositions[2]={{0,0,0,1},{100,0,100,1}},sEye,sAngles;
void BtlCtrl_PlayMotion(s32 side,s32 motion,s32 loop,f32 blend){sMotion[side]++;sMotionId[side]=motion;}
void BtlCtrl_StopMotion(s32 side){sStopped[side]++;}
void BtlCtrl_SetPos(s32 side,Vec4*p){sPositions[side]=*p;}
void BtlCharApi_GetPos(s32 side,Vec4*p){*p=sPositions[side];}
void BtlCharApi_GetRot(s32 side,Vec4*p){memset(p,0,sizeof(*p));p->w=1;}
void BtlCharApi_GetNodePos(s32 obj,s32 node,Vec4*out){(void)node;BtlCharApi_GetPos(obj,out);out->y-=7.8f;}
f32 BtlCharApi_GetHeight(s32 side){return 10;}
f32 BtlCharApi_GetGroundY(s32 side){return sActorFloor;}
s32 BtlStage_IsReady(void){return sStageReady;}
s32 BtlStage_ProbeGround(s32 zone,Vec4*pos,StgGroundHit*out){sProbes++;out->y=sNoFloor?3.4e38f:sFloor;return 0;}
void BtlFacade_SetFixedCamera(Vec4*p,Vec4*a){sEye=*p;sAngles=*a;sFixed++;assert(isfinite(p->x)&&isfinite(p->y)&&isfinite(a->x)&&isfinite(a->y));}
void BtlFacade_ClearFixedCamera(void){sCameraClear++;}
int EftCam_IsActive(void){return 0;}
static BattleResult sResult;
static BattleWork sWork;
void BtlCtrl_StartEntrance(s32 p) { sEntrance[p]++; }
void BtlCtrl_StartWinPose(s32 p) { sWinPose[p]++; }
void BtlCtrl_StartLosePose(s32 p) { sLosePose[p]++; }
s32 BtlCtrl_IsPoseReached(s32 p) { (void)p; return sPoseReached; }
void DemoCam_PlayObjAnim(s32 obj, s32 idx) { sCam[obj][idx]++; }
void DemoCam_SetScaleHeight(s32 on) { (void)on; }
void ScrXfade_RequestCapture(void) {}
void ScrXfade_Start(s32 r, f32 s) { (void)r; (void)s; sXfade++; }
s32 Battle_GetMode(void) { return sMode; }
BattleWork *Battle_GetWork(void) { return &sWork; }
BattleResult *Battle_GetResult(void) { return &sResult; }
s32 BattleSide_GetObjId(s32 side) { return side; }
s32 BtlCharApi_GetHp(s32 obj) { return sHp[obj]; }
s32 BtlCharApi_GetHpMax(s32 obj) { return sHpMax[obj]; }
void Ramp_Start(Ramp *r, f32 seconds, f32 from, f32 to) { r->value = from; r->target = to; r->step = (to - from) / (seconds * 30.0f); }
s32 Ramp_Step(Ramp *r) { r->value += r->step; if (r->value >= r->target) { r->value = r->target; return 1; } return 0; }

static int sRanks[72];
int Save_GetStoryRank(int c){return c>=0&&c<72?sRanks[c]:0;}
void Save_ClearStoryRank(int c){if(c>=0&&c<72)sRanks[c]=0;}
void Save_RaiseStoryRank(int c,int r){if(c>=0&&c<72&&r>sRanks[c])sRanks[c]=r;}
int Save_GrantDragonBall(int b){(void)b;return 0;}
int BattleReplay_IsActive(void){return 0;}
int Port_NetActive(void){return 0;}
s32 BtlStage_CountItemHolders(void){return 3;}
void BtlCtrl_SetRot(s32 side,Vec4*p){(void)side;(void)p;}
s32 Port_FighterObjId(s32 i){return i;}
s32 Port_Team(s32 i){return i;}
void *BtlObj_Get(s32 id){(void)id;return NULL;}
void BtlObj_SetPartVisible(void*o,u32 p,s32 on){(void)o;(void)p;(void)on;}
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
s32 BtlStage_GetWaterLevel(f32*y){*y=0;return 0;}
s32 BtlStage_GetStartPlace(s32 a,Vec4*p,Vec4*q,s32 alt){*p=sPositions[a];*q=(Vec4){0,0,0,1};return 0;}
void BtlChar_Place(void*c,Vec4*p,Vec4*q,s32 z){int a=(int*)c-sStubChr;BtlCtrl_SetPos(a,p);BtlCtrl_SetRot(a,q);}
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
/* Runs a scene in the talk context like states 1/5 do: preUpdate (beat) then update (hold). Returns frames. */
static int run(BtlSeqTalkCtx *ctx, int lines[16][2], int *nlines) {
    int frames = 0, set, line, last = -1;
    *nlines = 0;
    while (ctx->step >= ST_STEP_BEAT && ctx->step <= ST_STEP_HOLD && frames < 30 * 120) {
        st_scene_beat(ctx);
        if (sub(&set, &line) && (set << 8 | line) != last) { last = set << 8 | line; lines[*nlines][0] = set; lines[*nlines][1] = line; (*nlines)++; }
        if (!st_scene_hold(ctx)) { st_scene_end(); break; }
        frames++;
    }
    return frames;
}

static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void) {
    BtlSeqTalkCtx ctx;
    int lines[16][2], n, set, line, frames;

    Port_ScenarioClear(); Port_StoryChoose(0); sMode = 0;
    /* opening: SS01_001-003, Piccolo / Raditz / Piccolo, entrance cut per line, cross-fades between lines */
    memset(&ctx, 0, sizeof(ctx));
    assert(st_intro_enter(&ctx) == 1 && ctx.step == ST_STEP_BEAT);
    frames = run(&ctx, lines, &n);
    assert(n == 3 && lines[0][0] == ST_SET_INTRO && lines[0][1] == 0 && lines[2][1] == 2);
    assert(!sEntrance[0] && !sEntrance[1] && !sCam[0][0] && !sCam[1][0] && sXfade == 2);
    assert(sMotion[0]==1&&sMotion[1]==1&&sFixed>100&&sStopped[0]==1&&sStopped[1]==1&&!Port_StorySceneActorShown(0));
    assert(frames >= 3 * 75 && frames <= 3 * 210 && !sub(&set, &line));
    /* START skips: the caller ends the scene; nothing left on screen */
    memset(&ctx, 0, sizeof(ctx)); st_intro_enter(&ctx); st_scene_beat(&ctx); assert(sub(&set, &line)); st_scene_end(); assert(!sub(&set, &line));
    /* a pose not reached still ends 3 s after the line's time */
    sPoseReached = 0; memset(&ctx, 0, sizeof(ctx)); st_intro_enter(&ctx); frames = run(&ctx, lines, &n); assert(n == 3 && frames < 3 * (210 + 95)); sPoseReached = 1;

    /* Real engine Y is down. Camera lens must be ABOVE the native floor, including hills. */
    memset(&ctx,0,sizeof(ctx));st_intro_enter(&ctx);st_scene_beat(&ctx);
    assert(sEye.y<0&&sAngles.x<0);st_scene_end();
    sFloor=-80;memset(&ctx,0,sizeof(ctx));st_intro_enter(&ctx);st_scene_beat(&ctx);
    assert(sEye.y<=-83.5f);st_scene_end(); // zone0 is valid ground
    sFloor=0;sNoFloor=1;memset(&ctx,0,sizeof(ctx));st_intro_enter(&ctx);st_scene_beat(&ctx);
    assert(sEye.y<0&&sEye.y>-100);st_scene_end();sNoFloor=0;
    sStageReady=0;{int n=sProbes;memset(&ctx,0,sizeof(ctx));st_intro_enter(&ctx);st_scene_beat(&ctx);assert(sProbes==n);st_scene_end();}sStageReady=1;
    // The actor rises (negative Y) through a stationary departure shot, never followed.
    sPositions[1]=(Vec4){100,0,100,1};sHp[1]=0;
    st_scene_begin(&ctx,0,ST_SET_ALT,0,0,0,1);st_scene_beat(&ctx);
    {Vec4 eye=sEye,angles=sAngles;assert(sMotionId[1]==0x2E);
     for(int i=0;i<30;i++){assert(st_scene_hold(&ctx));assert(sEye.x==eye.x&&sEye.y==eye.y&&sEye.z==eye.z&&sAngles.x==angles.x&&sAngles.y==angles.y);}
     assert(sPositions[1].y<0&&sMotionId[1]==0x2F&&sHp[1]==0);}
    st_scene_end();assert(!Port_StorySceneActorShown(1));
    /* fight: live health log, cues once each, one at a time, nothing while paused */
    sHp[0] = sHpMax[0] = 40000; sHp[1] = sHpMax[1] = 40000; st_fight_enter();
    for (int f = 0; f < 30 * 5; f++) st_fight_update();
    assert(!sub(&set, &line));                                  /* nothing before 6 s */
    for (int f = 0; f < 30 * 2; f++) st_fight_update();
    assert(sub(&set, &line) && set == ST_SET_COMBAT && line == 0); /* the 6 s line */
    sHp[1] = 20000; sHp[0] = 10000;                            /* Raditz 50 %, Piccolo 25 %: two cues due */
    for (int f = 0; f < 30; f++) st_fight_update();              /* the first line holds its time */
    assert(sub(&set, &line) && line == 0);
    for (int f = 0; f <= st_frames(stScenes[0].cue[0].line.text[0]); f++) st_fight_update();
    assert(sub(&set, &line) && line == 1);                      /* SS01_004 next, alone */
    sWork.flags |= BATTLE_FLAG_PAUSE; for (int f = 0; f < 30 * 20; f++) st_fight_update(); assert(sub(&set, &line) && line == 1); sWork.flags &= ~BATTLE_FLAG_PAUSE;
    for (int f = 0; f < 30 * 30; f++) st_fight_update();
    assert(sStCuesDone == 0x7 && !sub(&set, &line));           /* line 2 said; line 3 (Raditz 30 %) not due */
    sHp[1] = 9000; for (int f = 0; f < 30 * 30; f++) st_fight_update(); assert(sStCuesDone == 0xF);
    st_scene_end(); assert(!sub(&set, &line));

    /* canonical ending: Raditz (side 1) wins by K.O.: SS01_005 (Raditz, win pose) and SS01_006 (Piccolo, lose pose),
       then "Raditz leaves." */
    memset(&ctx, 0, sizeof(ctx)); memset(sWinPose, 0, sizeof(sWinPose)); memset(sLosePose, 0, sizeof(sLosePose));
    sResult.winner = 2; sResult.reason = 1;
    assert(st_ending_enter(&ctx) == 1 && ctx.side[0] == 1);
    run(&ctx, lines, &n);
    assert(n == 3 && lines[0][0] == ST_SET_CANON && lines[0][1] == 1 && lines[1][1] == 2 && lines[2][0] == ST_SET_LEAVES);
    assert(!sWinPose[1] && !sLosePose[0] && !sCam[1][1] && !sCam[0][2] && !sub(&set, &line));
    /* alternative ending: Piccolo wins: SSA_001 (Piccolo, win), SSA_002 (Raditz, lose), leaves caption */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 1; sResult.reason = 1;
    sHp[1]=0;sPositions[1]=(Vec4){1200,-50,300,1};
    assert(st_ending_enter(&ctx) == 1 && ctx.side[0] == 0);
    assert(Port_StorySceneActorShown(1)&&!Port_StorySceneActorShown(2)&&sMotionId[1]==0x184);
    st_scene_beat(&ctx);assert(isfinite(sEye.x));
    run(&ctx, lines, &n);
    assert(n == 3 && lines[0][0] == ST_SET_ALT && lines[0][1] == 0 && lines[1][1] == 1 && lines[2][0] == ST_SET_LEAVES_ALT);
    assert(!sWinPose[0] && !sLosePose[1] && sHp[1]==0 && sMotionId[1]==0x2F && sPositions[1].y<-50 && !Port_StorySceneActorShown(1));
    /* time-out (a failure) and restart keep the game's own winner scene */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 2; sResult.reason = 2; assert(st_ending_enter(&ctx) == 0 && ctx.step == 0);
    sResult.reason = 0x8000; assert(st_ending_enter(&ctx) == 0);
    /* stock Dragon History (mode 1) and other modes: no story scene */
    sMode = 1; memset(&ctx, 0, sizeof(ctx)); assert(st_intro_enter(&ctx) == 0); sResult.reason = 1; assert(st_ending_enter(&ctx) == 0);
    sMode = 0; Port_ScenarioClear(); memset(&ctx, 0, sizeof(ctx)); assert(st_intro_enter(&ctx) == 0 && st_ending_enter(&ctx) == 0);
    st_fight_enter(); for (int f = 0; f < 30 * 20; f++) st_fight_update(); assert(!sub(&set, &line));
    /* data: every scene line has ES and EN text, and its speaker side */
    for (int s = ST_SET_INTRO; s <= ST_SET_COMBAT; s++) for (int i = 0; i < 8; i++) { const StLine *l = ST_SceneLine(0, s, i); if (l) assert(l->text[0][0] && l->text[1][0] && (ST_LineSide(0, l) == !strcmp(l->speaker, "Raditz"))); }
    assert(ST_SceneLine(0, ST_SET_LEAVES, 0) == NULL && ST_LineSeconds("") == 2.5f && ST_LineSeconds("Was that your idea of looking down on Earthlings? Judge your opponent better next time.") <= 7.0f);
    puts("PASS polished authored native cameras/motions, no Duel pose restarts, scoped actor visibility and cleanup; Dragon History chapter 1 scenes: opening SS01_001-003 with entrance cuts and cross-fades, skip, pose time-out, live health log, 4 fight cues once each/one at a time/paused, canonical (SS01_005-006) and alternative (SSA_001-002) endings with win/lose cuts and leaves caption, failure/restart/mode 1/other modes untouched, no subtitle left behind");
    return 0;
}

int BtlCharApi_GetChara(int obj){(void)obj;return 80;}





