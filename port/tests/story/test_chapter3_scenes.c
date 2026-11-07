void Port_StoryResumeCombatActor(int a,int b){}
/* Chapter 3 (Goku + Piccolo vs Raditz) offline: the real draft, scenario/story runtime and scene driver against stubs
   of the battle calls. Checks the four-actor draft (kid Gohan scene-only), the opening without Gohan, the two authored
   R3 events (offered at ABSOLUTE rival HP whatever the difficulty, shown only as the HUD cue, in story order, each once,
   a later one superseding an earlier one not taken, accepted only when the fighters can be interrupted, the fight
   frozen while they play, Gohan shown only inside his scene, the beam lethal to Goku and Raditz), both endings
   (canonical = a PLAYER win after the beam, Goku down; alternative = a win before it, Gohan safe), losses and
   restarts, receipts, ranks and Zeni, chapters 1-2 outcomes unchanged. */
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
void Save_ClearStoryRank(int c) { if (c >= 0 && c < 72) sRanks[c] = 0; }
void Save_RaiseStoryRank(int c, int r) { if (c >= 0 && c < 72 && r > sRanks[c]) sRanks[c] = r; }
int Save_GrantDragonBall(int b) { (void)b; return 0; }
int BattleReplay_IsActive(void) { return 0; }
int Port_NetActive(void) { return 0; }

#define N 4
static int sMode, sMotionId[N], sMotions[N], sStopped[N], sVisible[N], sTeam[N], sHp[N], sHpMax[N];
static int sInterruptible = 1, sTechnique[N], sAura[N], sSlideOut, sSlideIn;
static Vec4 sPos[N], sRot[N];
static BattleResult sResult;
static BattleWork sWork;
void BtlCtrl_PlayMotion(s32 a, s32 m, s32 loop, f32 b) { (void)loop; (void)b; sMotionId[a] = m; sMotions[a]++; }
void BtlCtrl_StopMotion(s32 a) { sStopped[a]++; }
void BtlCtrl_SetPos(s32 a, Vec4 *p) { sPos[a] = *p; }
void BtlCtrl_SetRot(s32 a, Vec4 *r) { sRot[a] = *r; }
void BtlCharApi_GetPos(s32 obj, Vec4 *p) { *p = sPos[obj]; }
void BtlCharApi_GetRot(s32 obj, Vec4 *r) { *r = sRot[obj]; }
void BtlCharApi_GetNodePos(s32 obj,s32 node,Vec4*out){(void)node;BtlCharApi_GetPos(obj,out);out->y-=7.8f;}
f32 BtlCharApi_GetHeight(s32 obj) { (void)obj; return 10.0f; }
f32 BtlCharApi_GetGroundY(s32 obj) { (void)obj; return 0; }
s32 BtlStage_IsReady(void) { return 0; }
s32 BtlStage_ProbeGround(s32 z, Vec4 *p, StgGroundHit *h) { (void)z; (void)p; h->y = 0; return 0; }
void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { assert(isfinite(p->x) && isfinite(a->y)); }
void BtlFacade_ClearFixedCamera(void) {}
int EftCam_IsActive(void){return 0;}
s32 Port_FighterObjId(s32 i) { return i; }
s32 Port_StoryTeam(s32 actor, s32 team);
s32 Port_Team(s32 i) { return Port_StoryTeam(i, sTeam[i]); }
static int sObjs[N];
void *BtlObj_Get(s32 id) { return id >= 0 && id < N ? &sObjs[id] : NULL; }
void BtlObj_SetPartVisible(void *o, u32 part, s32 on) { (void)part; sVisible[(int *)o - sObjs] = on; }
void ScrXfade_RequestCapture(void) {}
void ScrXfade_Start(s32 r, f32 s) { (void)r; (void)s; }
s32 Battle_GetMode(void) { return sMode; }
BattleWork *Battle_GetWork(void) { return &sWork; }
BattleResult *Battle_GetResult(void) { return &sResult; }
s32 BattleSide_GetObjId(s32 side) { return side; }
s32 BtlCharApi_GetHp(s32 obj) { return sHp[obj]; }
s32 BtlCharApi_GetHpMax(s32 obj) { return sHpMax[obj]; }
s32 BtlStage_CountItemHolders(void) { return 0; }
void Ramp_Start(Ramp *r, f32 seconds, f32 from, f32 to) { r->value = from; r->target = to; r->step = (to - from) / (seconds * 30.0f); }
s32 Ramp_Step(Ramp *r) { r->value += r->step; if (r->value >= r->target) { r->value = r->target; return 1; } return 0; }
s32 BtlCtrl_IsInterruptible(s32 a) { (void)a; return sInterruptible; }
s32 BtlCtrl_UseTechnique(s32 a, s32 k) { sTechnique[a] = k; return 1; }
void BtlCtrl_SetHidden(s32 a) { (void)a; }
void BtlCtrl_SetAuraOn(s32 a) { sAura[a]++; }
/* as btl_capi_b.c: hp = min(hp, max * percent / 100), at least 1 for a positive percentage */
void BtlCtrl_LowerHp(s32 a, s32 m, f32 p) { s32 t = (s32)(sHpMax[a] * p * 0.01f); (void)m; if (t <= 0 && p > 0) t = 1; if (t < sHp[a]) sHp[a] = t; }
s32 BtlCtrl_GetActiveMember(s32 a) { (void)a; return 0; }
void Hud_SlideOut(f32 s) { (void)s; sSlideOut++; }
void Hud_SlideIn(f32 s) { (void)s; sSlideIn++; }
static int sStubChr[N], sSe[N][2], sSoundSeen[N][256],sFxSeen[N][128],sHitEffects[N],sResultSets;
void *BtlChar_Get(s32 i) { return &sStubChr[i]; }
void BtlCharSnd_PlayCommon(void *c, s32 id) { int a = (int *)c - sStubChr; sSoundSeen[a][id]++;sSe[a][0] = id; sSe[a][1]++; }
s32 gPortTarget[N];
void BattleResult_Set(s32 w, s32 r) { sResult.winner = w; sResult.reason = r; sResultSets++; }
static int sBlown[N], sBlownBy[N];
/* as btl_act_e.c: native damage (exact) and the blow-away reaction */
void Port_StoryBeamVictim(int who, int winner, int damage) { sBlown[who]++; sBlownBy[who] = winner; sHp[who] -= damage; if (sHp[who] < 0) sHp[who] = 0; }

s32 BtlStage_GetWaterLevel(f32*y){*y=0;return 0;}
s32 BtlStage_GetStartPlace(s32 a,Vec4*p,Vec4*q,s32 alt){*p=sPos[a];*q=(Vec4){0,0,0,1};return 0;}
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
void BtlChar_SetFxBit(void*c,int f){sFxSeen[(int*)c-sStubChr][f]++;}
int BtlParam_GetChargeLoopSound(void*c){(void)c;return 12;}
void BtlCharSnd_PlayCommonFar(void*c,int id){BtlCharSnd_PlayCommon(c,id);}
static int sChargeOn[N],sChargeOff[N];
void Port_StoryChargeSpark(int actor,int on){if(on)sChargeOn[actor]++;else sChargeOff[actor]++;}
void Port_StoryHitFx(int a){sHitEffects[a]++;}
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
static int sLines[24][3], sN;
static void note(void) {
    int set, line;
    if (sub(&set, &line) && (sN == 0 || (sLines[sN - 1][0] << 8 | sLines[sN - 1][1]) != (set << 8 | line))) {
        sLines[sN][0] = set; sLines[sN][1] = line; sLines[sN][2] = sStRun.actor; sN++;
    }
}
/* A talk scene like states 1/5 run it. */
static void run(BtlSeqTalkCtx *ctx) {
    int frames = 0;
    sN = 0;
    while (ctx->step >= ST_STEP_BEAT && ctx->step <= ST_STEP_HOLD && frames++ < 30 * 200) {
        st_scene_beat(ctx); note();
        if (!st_scene_hold(ctx)) { st_scene_end(); break; }
    }
}
/* The fight (state 3): one frame, with the HUD side first as in Battle_Loop (BtlScript_Update before the sequence). */
static int sCue;
static void frame(int r3) { sCue = Port_StoryEventCue(r3); st_fight_update(); for(int a=0;a<N;a++)Port_StoryFxFrame(a); }
/* Plays an accepted event to its end; returns its frames. */
static int play_event(void) {
    int frames = 0;
    sN = 0;
    while ((sStEventRun >= 0) && frames++ < 30 * 200) { frame(0); note(); }
    return frames;
}
static int actor(int cast) { return Port_StoryCastActor(cast); }

static void fight_start(int goku, int piccolo, int raditz, int gohan, int raditzMax) {
    int i;
    Port_ScenarioRoundReset();
    for (i = 0; i < N; i++) { sHpMax[i] = 40000; sHp[i] = 40000; sTechnique[i] = -1; }
    sHpMax[raditz] = sHp[raditz] = raditzMax;
    for (i = 0; i < N; i++) { sPos[i].x = 0; sPos[i].y = 0; sPos[i].z = i * 40.0f; sPos[i].w = 1; sRot[i].y = 0; }
    sPos[raditz].z = 120; sRot[raditz].y = 3.14159f;
    (void)goku; (void)piccolo; (void)gohan;
    sWork.flags = 0; sInterruptible = 1;
    st_fight_enter();
}

static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void) {
    BtlSeqTalkCtx ctx;
    MsView v;
    int i, goku, piccolo, raditz, gohan, d;
    unsigned awards;

    /* ---- outcome policy: chapters 1-2 unchanged; chapter 3 canonical = player win after the beam ---- */
    for (i = 0; i < 2; i++) {
        assert(ST_OutcomeFor(i, 1, 1, 0) == ST_CANON && ST_OutcomeFor(i, 0, 1, 3) == ST_ALT && ST_OutcomeFor(i, 0, 0x8001, 0) == ST_NONE && ST_OutcomeFor(i, -1, 0, 0) == ST_FAIL);
        assert(ST_WinBits(i) == ST_DONE_ALT && !ST_CanonIsWin(i));
    }
    assert(ST_OutcomeFor(2, 0, 1, 2) == ST_CANON && ST_OutcomeFor(2, 0, 1, 3) == ST_CANON);
    assert(ST_OutcomeFor(2, 0, 1, 0) == ST_ALT && ST_OutcomeFor(2, 0, 1, 1) == ST_ALT);
    assert(ST_OutcomeFor(2, 1, 1, 2) == ST_FAIL && ST_OutcomeFor(2, 0, 0, 2) == ST_FAIL && ST_OutcomeFor(2, 0, 0x10001, 2) == ST_NONE);
    assert(ST_WinBits(2) == (ST_DONE_CANON | ST_DONE_ALT));
    assert(stEvents[0].count == 0 && stEvents[1].count == 0 && stEvents[2].count == 2);
    assert(stEvents[2].event[0].threshold == 20000 && stEvents[2].event[0].kind == ST_EVENT_GOHAN && stEvents[2].event[1].threshold == 10000 && stEvents[2].event[1].kind == ST_EVENT_BEAM);
    assert(!strcmp(stEvents[2].event[0].line[0].speaker, "Gohan") && !strcmp(stEvents[2].event[1].line[3].text[0], "Kakarot! You'll die with me!"));

    /* ---- draft: Goku (human, costume 3) + Piccolo vs Raditz, kid Gohan (yellow) scene-only ---- */
    Port_ScenarioClear(); Port_StoryChoose(2); sMode = 0;
    memset(&v, 0, sizeof(v));
    assert(Port_ScenarioDraft(&v) > 0 && v.total == 4 && v.pick[0].chara == 0 && v.pick[0].pad == 0 && v.pick[0].costume == 3);
    goku = actor(0); piccolo = actor(1); raditz = actor(2); gohan = actor(3);
    assert(goku == 0 && raditz == 1 && piccolo >= 2 && gohan >= 2 && piccolo != gohan);
    for (i = 0; i < N; i++) sTeam[i] = sScTeams[i];
    assert(sTeam[goku] == 0 && sTeam[piccolo] == 0 && sTeam[raditz] == 1 && sTeam[gohan] == 1);
    assert(Port_StorySceneActor(gohan) && !Port_StorySceneActor(piccolo) && Port_StorySceneParked(gohan) && !Port_StorySceneParked(piccolo));
    for (i = 0; i < N; i++) { sPos[i].z = i * 40.0f; sPos[i].w = 1; }

    /* ---- opening SS03_001-003, Gohan is Raditz's prisoner: not shown ---- */
    memset(&ctx, 0, sizeof(ctx));
    assert(st_intro_enter(&ctx) == 1 && Port_StorySceneParked(gohan) && !Port_StorySceneActorShown(gohan));
    run(&ctx);
    assert(sN == 3 && sLines[0][2] == raditz && sLines[1][2] == goku && sLines[2][2] == piccolo);
    for (i = 0; i < 3; i++) assert(sLines[i][0] == ST_SET_INTRO);

    /* ---- canonical path: both R3 events taken ---- */
    fight_start(goku, piccolo, raditz, gohan, 90000);
    for (i = 0; i < 60; i++) frame(1);                       /* R3 before any event: nothing */
    assert(!sCue && sStEventRun < 0 && !(sWork.flags & BATTLE_FLAG_DEMO));
    sHp[raditz] = 20001; frame(0); assert(!sCue);
    sHp[raditz] = 20000; frame(0); frame(0); assert(sCue && sStEventOffer == 0); /* the R3 glyph, no caption */
    { int set, line; assert(!sub(&set, &line) || set == ST_SET_COMBAT); }
    sInterruptible = 0; frame(1); frame(0); assert(sStEventRun < 0 && !sCue); /* unavailable actions never advertise R3 */
    sInterruptible = 1; frame(1);                            /* accepted */
    assert(sStEventRun == 0 && (sWork.flags & BATTLE_FLAG_DEMO) && !Port_StorySceneParked(gohan) && Port_StoryEvents() == 1u);
    assert(Port_StoryEventCue(0) == 0);                      /* the cue goes while it plays */
    play_event();
    assert(sN == 4);
    { static const int who[4] = {3, 2, 0, 1}; /* Gohan Raditz Goku Piccolo */
      for (i = 0; i < 4; i++) assert(sLines[i][0] == ST_SET_EVENT0 && sLines[i][1] == i && sLines[i][2] == actor(who[i])); }
    assert(sHp[raditz] == 16000);                             /* the kid's hit, exact */
    assert(sSoundSeen[gohan][ST_SE_DASH]==1 && sSoundSeen[raditz][ST_SE_STRIKE]==1 && sFxSeen[gohan][0x13]>0 && sFxSeen[gohan][7]>0 && sHitEffects[raditz]==1); /* ki rush, native dash + strike SE */
    assert(sMotionId[raditz] == ST_ANIM_TIRED);                /* hit reaction -> broken-guard stagger -> panting */
    assert(Port_StorySceneParked(gohan) && sVisible[gohan] == 0 && !(sWork.flags & BATTLE_FLAG_DEMO) && sStEventRun < 0);
    for (i = 0; i < 30; i++) frame(1);                        /* taken once */
    assert(!sCue && sStEventRun < 0 && sHp[raditz] == 16000);
    sHp[raditz] = 5000; frame(0); frame(0); assert(sCue && sStEventOffer == 1);
    frame(1); assert(sStEventRun == 1 && Port_StoryEvents() == 3u && sMotionId[piccolo] == ST_MOTION_SBC_IN);
    assert(sMotionId[raditz] == ST_MOTION_SURPRISED);
    play_event();
    assert(sN == 4);
    { static const int who[4] = {0, 1, 0, 2}; /* Goku Piccolo Goku Raditz */
      for (i = 0; i < 4; i++) assert(sLines[i][0] == ST_SET_EVENT1 && sLines[i][1] == i && sLines[i][2] == actor(who[i])); }
    assert(sFxSeen[piccolo][0x13]>0&&sFxSeen[piccolo][7]>0&&sFxSeen[piccolo][0x12]>0);
    assert(sChargeOn[piccolo]==1); /* one native hand glow, not a new task per frame */
    assert(sTechnique[piccolo] == 4 && sStBeamFrames > 0 && !(sWork.flags & BATTLE_FLAG_DEMO));
    { float dx = sPos[goku].x - sPos[raditz].x, dz = sPos[goku].z - sPos[raditz].z; assert(sqrtf(dx * dx + dz * dz) < 5.0f); } /* still gripping him */
    sResult.winner = 0; sResultSets = 0;
    frame(0); frame(0); assert(sHp[goku] > 0 && sHp[raditz] == 5000);
    assert(Port_StoryHeld(goku) && Port_StoryHeld(raditz) && !Port_StoryHeld(piccolo) && gPortTarget[piccolo] == raditz); /* only Piccolo acts */
    assert(Port_Team(goku) == Port_Team(raditz) && Port_Team(piccolo) != Port_Team(goku)); /* Goku switched sides: the beam targets both */
    sPos[goku].x += 30; sPos[raditz].z -= 30; frame(0);       /* each is kept at his own spot */
    { float dx = sPos[goku].x - sPos[raditz].x, dz = sPos[goku].z - sPos[raditz].z; assert(sqrtf(dx * dx + dz * dz) < 5.0f); }
    sHp[raditz] = 4200; frame(0);                             /* the beam lands: both take the native hit */
    assert(sHp[goku] == 0 && sHp[raditz] == 0 && sHp[piccolo] > 0 && sBlown[goku] == 1 && sBlown[raditz] == 1 && sBlownBy[goku] == piccolo);
    assert(!Port_StoryHeld(goku) && !Port_StoryHeld(raditz) && sResultSets == 0); /* released; the fight ends as a KO does */
    sPos[goku].y=sPos[raditz].y=-10;
    for(i=0;i<15;i++)frame(0);assert(sResultSets==0&&Port_StoryEndPending());
    sPos[goku].y=sPos[raditz].y=0;frame(0);
    assert(sResultSets==1&&sResult.winner==1&&sResult.reason==1&&!Port_StoryEndPending());
    assert(sHitEffects[goku]>=1&&sHitEffects[raditz]>=2);
    assert(sChargeOff[piccolo]==1&&!Port_StoryPromptView());
    /* canonical ending (team 0 wins): SS04_005-009, Goku lies defeated, Piccolo stands, no departure caption */
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 1; sResult.reason = 1;
    assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_CANON && sMotionId[goku] == ST_MOTION_WOUNDED && sMotionId[raditz] == ST_MOTION_WOUNDED && sMotionId[piccolo] == 0);
    assert(Port_StorySceneParked(gohan));
    run(&ctx);
    assert(sN == 5);
    { static const int who[5] = {1, 2, 1, 2, 0};
      for (i = 0; i < 5; i++) assert(sLines[i][0] == ST_SET_CANON && sLines[i][1] == i && sLines[i][2] == actor(who[i])); }
    /* result: canonical clear is a real win: Zeni 1000, rank, chapter 4 announced (the saga continues), no ch1 trophies */
    sBits = 0; sMoney = 0; memset(sRanks, 0, sizeof(sRanks));
    awards = Port_StoryResult(0, 1, 1);
    { int ch, z; assert(Port_StoryLastOutcome(&ch, &z) == ST_CANON && ch == 2 && z == 1000 && sMoney == 1000); }
    assert(Port_StoryProgress(2) == ST_DONE_CANON && Port_StoryClearRank(2) == 2 && sStNewChapter == 3);
    assert(!(awards & ((1u << 18) | (1u << 19))));
    Port_StoryAcknowledge();

    /* ---- the beam never lands: at the latest after ST_BEAM_FRAMES both fall anyway ---- */
    Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, 90000);
    sHp[raditz] = 4000; frame(0); frame(0); assert(sCue && sStEventOffer == 1); /* the kid's moment passed untaken */
    frame(1); play_event();
    assert(Port_StoryEvents() == 2u);
    for (i = 0; i < ST_BEAM_FRAMES + 5 && sStBeamFrames > 0; i++) frame(0);
    assert(sHp[goku] == 0 && sHp[raditz] == 0);
    assert(ST_OutcomeFor(2, 0, 1, Port_StoryEvents()) == ST_CANON);

    /* ---- alternative path: Raditz beaten before the beam (here after Gohan's hit) ---- */
    Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, 90000);
    sHp[raditz] = 15000; frame(0); frame(1); play_event();
    assert(Port_StoryEvents() == 1u && sHp[raditz] == 11000);
    sHp[raditz] = 6000; frame(0); assert(!sCue);
    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 1; sResult.reason = 1;
    assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_ALT && !Port_StorySceneParked(gohan)); /* Gohan safe beside Goku */
    { float dx = sPos[gohan].x - sPos[goku].x, dz = sPos[gohan].z - sPos[goku].z; assert(sqrtf(dx * dx + dz * dz) < 12.0f); }
    run(&ctx);
    assert(sN == 2 && sLines[0][0] == ST_SET_ALT && sLines[0][2] == goku && sLines[1][2] == piccolo && Port_StorySceneParked(gohan));
    sBits = ST_DONE_CANON << 4; sMoney = 0; memset(sRanks, 0, sizeof(sRanks)); sRanks[2] = 3; /* chapter 3 = bits 4..5 */
    Port_StoryResult(0, 1, 1);
    { int ch, z; assert(Port_StoryLastOutcome(&ch, &z) == ST_ALT && z == 3000); }
    assert(Port_StoryProgress(2) == (ST_DONE_CANON | ST_DONE_ALT) && Port_StoryClearRank(2) == 3); /* a rank won on the canonical path stays */
    Port_StoryAcknowledge();

    /* ---- no event at all, win: alternative; a loss is a failure (the game's own scene); a restart nothing ---- */
    Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, 90000);
    assert(ST_OutcomeFor(2, 0, 1, Port_StoryEvents()) == ST_ALT);
    /* A direct KO before ANY R3 still runs the actual alternative ending. */
    sHp[raditz]=0;sResult.winner=1;sResult.reason=1;memset(&ctx,0,sizeof(ctx));
    assert(st_ending_enter(&ctx)==1&&sStRun.set==ST_SET_ALT&&!Port_StoryEvents());
    run(&ctx);assert(sN==2&&sLines[0][0]==ST_SET_ALT&&sLines[0][2]==goku&&sLines[1][2]==piccolo);
    assert(!sStRun.active&&!Port_StoryPromptView()&&Port_StorySceneParked(gohan));

    memset(&ctx, 0, sizeof(ctx)); sResult.winner = 2; sResult.reason = 1; assert(st_ending_enter(&ctx) == 0);
    sBits = 0; sMoney = 0; memset(sRanks, 0, sizeof(sRanks));
    assert(Port_StoryResult(1, 1, 1) == 0 && sMoney == 0 && Port_StoryProgress(2) == 0 && Port_StoryClearRank(2) == 0);
    Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, 90000);
    assert(Port_StoryResult(0, 0x8000, 1) == 0 && Port_StoryProgress(2) == 0);

    /* ---- absolute thresholds: the same HP whatever the difficulty's health ---- */
    for (d = 0; d < 3; d++) {
        int max = 60000 * stDifficulty[2].hpPercent[d] / 100;
        Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, max);
        sHp[raditz] = 20500; frame(0); frame(0); assert(!sCue);
        sHp[raditz] = 19999; frame(0); frame(0); assert(sCue && sStEventOffer == 0);
    }
    assert(stDifficulty[2].hpPercent[0] == 90 && stDifficulty[2].hpPercent[2] == 140);

    /* ---- leaving mid-event (restart / quit): the fight is never left frozen, the kid goes back to parking ---- */
    Port_StoryChoose(2); fight_start(goku, piccolo, raditz, gohan, 90000);
    sHp[raditz] = 20000; frame(0); frame(1);
    for (i = 0; i < 10; i++) frame(0);
    assert(sStEventRun == 0 && (sWork.flags & BATTLE_FLAG_DEMO) && !Port_StorySceneParked(gohan));
    st_scene_end();
    assert(sStEventRun < 0 && !(sWork.flags & BATTLE_FLAG_DEMO) && Port_StorySceneParked(gohan));
    Port_ScenarioRoundReset(); st_fight_enter(); assert(Port_StoryEvents() == 0 && sStEventOffer < 0);

    /* ---- paused: nothing advances ---- */
    sHp[raditz] = 20000; sWork.flags = BATTLE_FLAG_PAUSE; frame(0); frame(1); assert(sStEventRun < 0); sWork.flags = 0;

    /* ---- chapters 1-2 have no events: R3 is never taken from the fight ---- */
    Port_StoryChoose(1); sHp[0] = sHp[1] = 1000; st_fight_enter();
    for (i = 0; i < 30; i++) { frame(1); assert(!sCue && sStEventRun < 0); }
    /* ---- next-chapter announcement: on the first clear, and on the first real win after a narrative defeat ---- */
    sBits = ST_DONE_CANON | ST_DONE_ALT; sMoney = 0;                                   /* chapter 1 cleared */
    Port_StoryChoose(1); Port_ScenarioRoundReset();
    Port_StoryResult(1, 1, 1); assert(Port_StoryUnlockedChapter() == 2);              /* Raditz wins: canonical, chapter 3 opens */
    Port_StoryAcknowledge(); Port_StoryChoose(1); Port_ScenarioRoundReset();
    Port_StoryResult(1, 0, 1); assert(Port_StoryUnlockedChapter() == -1);             /* a failure never announces it */
    Port_StoryAcknowledge(); Port_StoryChoose(1); Port_ScenarioRoundReset();
    Port_StoryResult(0, 1, 1); assert(Port_StoryUnlockedChapter() == 2);              /* Juan's case: the first win shows it */
    Port_StoryAcknowledge(); Port_StoryChoose(1); Port_ScenarioRoundReset();
    Port_StoryResult(0, 1, 1); assert(Port_StoryUnlockedChapter() == -1);             /* replays stay quiet */
    Port_StoryAcknowledge();
    sBits = ST_DONE_CANON << 2; Port_StoryChoose(1); Port_ScenarioRoundReset(); sBits |= ST_DONE_CANON << 4; /* chapter 3 already played */
    Port_StoryResult(0, 1, 1); assert(Port_StoryUnlockedChapter() == -1); Port_StoryAcknowledge();
    sBits = 0; Port_StoryChoose(0); Port_ScenarioRoundReset(); Port_StoryResult(0, 1, 1); assert(Port_StoryUnlockedChapter() == 1); /* chapter 1 -> 2 as before */
    Port_StoryAcknowledge();
    Port_ScenarioClear();
    puts("PASS chapter 3: draft, opening without Gohan, R3 events at absolute HP (glyph only, in order, once, superseded, interruptible only, frozen fight), Gohan's hit 4000, beam lethal to Goku and Raditz (landed or timed), canonical player-win ending with Goku down, alternative with Gohan safe, loss/restart, Zeni, ranks, receipts (first clear, and first win after a narrative defeat), chapters 1-2 unchanged");
    return 0;
}

int BtlCharApi_GetChara(int obj){(void)obj;return 80;}





