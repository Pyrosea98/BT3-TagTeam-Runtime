void Port_StoryResumeCombatActor(int a,int b){}
/* Saiyan saga chapters 4-17 (script nodes 05-17) offline: the real chapter tables, draft, scenario/story runtime and
   scene driver against stubs of the battle calls. For every chapter: table integrity (speakers, counts, masks, events,
   at most ten fighters), the opening, every story event in order (R3 offered at its trigger - HP percentage, absolute
   HP or time - or started by itself; its lines; its KOs, story deaths, damage and side switch), the canonical ending
   reached through the canonical event, the alternative ending from an earlier win, failures, Zeni, ranks, the
   next-chapter announcement and progress stored in the save's chapter groups beyond the first four chapters. */
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
static int sDense;
int Port_Setting(const char *name, int def) { if(!strcmp(name,"story_presentation"))return sPresentationEnabled; if (!strcmp(name, "story_density_full")) return sDense; return def; }
char gPortSongNames[PORT_SONG_MAX][64]; int gPortSongCount, gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void) { gPortSongCount = 0; }
static int sBits, sMoney, sRanks[72], sGroups[9];
int Save_GetStoryBits(void) { return sBits; }
void Save_OrStoryBits(int b) { sBits |= b & 0xFF; }
int Save_GetStoryBitsGroup(int g) { assert(g > 0 && g < 9); return sGroups[g] & 0xFF; }
void Save_OrStoryBitsGroup(int g, int b) { assert(g > 0 && g < 9); sGroups[g] |= b & 0xFF; }
void Save_AddMoney(int a) { sMoney += a; }
int Save_GetMoney(void) { return sMoney; }
int Save_GetStoryRank(int c) { return c >= 0 && c < 72 ? sRanks[c] : 0; }
void Save_ClearStoryRank(int c) { if (c >= 0 && c < 72) sRanks[c] = 0; }
void Save_RaiseStoryRank(int c, int r) { if (c >= 0 && c < 72 && r > sRanks[c]) sRanks[c] = r; }
int Save_GrantDragonBall(int b) { (void)b; return 0; }
int BattleReplay_IsActive(void) { return 0; }
int Port_NetActive(void) { return 0; }

#define N 16
static int sMode, sMotionId[N], sVisible[N], sTeam[N], sHp[N], sHpMax[N],sKi[N],sStocks[N];
static int sInterruptible = 1;
static Vec4 sPos[N], sRot[N];
static BattleResult sResult;
static BattleWork sWork;
void BtlCtrl_PlayMotion(s32 a, s32 m, s32 loop, f32 b) { (void)loop; (void)b; assert(a >= 0 && a < N); sMotionId[a] = m; }
static int sStopCalls[32];
void BtlCtrl_StopMotion(s32 a) { if(a>=0&&a<32)sStopCalls[a]++; }
void BtlCtrl_SetPos(s32 a, Vec4 *p) { sPos[a] = *p; }
void BtlCtrl_SetRot(s32 a, Vec4 *r) { sRot[a] = *r; }
void BtlCharApi_GetPos(s32 obj, Vec4 *p) { *p = sPos[obj]; }
void BtlCharApi_GetRot(s32 obj, Vec4 *r) { *r = sRot[obj]; }
void BtlCharApi_GetNodePos(s32 obj,s32 node,Vec4*out){(void)node;BtlCharApi_GetPos(obj,out);out->y-=7.8f;}
f32 BtlCharApi_GetHeight(s32 obj) { (void)obj; return 10.0f; }
f32 BtlCharApi_GetGroundY(s32 obj) { (void)obj; return 0; }
static int sProbeLedge;static int sReady; s32 BtlStage_IsReady(void) { return sReady; }
s32 BtlStage_ProbeGround(s32 z, Vec4 *p, StgGroundHit *h) { (void)z; h->y = sProbeLedge&&p->x < -180.f ? -64.f : 0.f; return 0; }
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
static int sBlockedActor=-1; s32 BtlCtrl_IsInterruptible(s32 a) { return sInterruptible&&a!=sBlockedActor; }
s32 BtlCtrl_UseTechnique(s32 a, s32 k) { (void)a; (void)k; return 1; }
void BtlCtrl_SetHidden(s32 a) { (void)a; }
void BtlCtrl_SetAuraOn(s32 a) { (void)a; }
void BtlCtrl_LowerHp(s32 a, s32 m, f32 p) { s32 t = (s32)(sHpMax[a] * p * 0.01f); (void)m; if (t <= 0 && p > 0) t = 1; if (t < sHp[a]) sHp[a] = t; }
s32 BtlCtrl_GetActiveMember(s32 a) { (void)a; return 0; }
void Hud_SlideOut(f32 s) { (void)s; }
void Hud_SlideIn(f32 s) { (void)s; }
static int sStubChr[N], sResultSets, sBlown[N];
void *BtlChar_Get(s32 i) { return &sStubChr[i]; }
void BtlCharSnd_PlayCommon(void *c, s32 id) { (void)c; (void)id; }
s32 gPortTarget[N];
void BattleResult_Set(s32 w, s32 r) { sResult.winner = w; sResult.reason = r; sResultSets++; }
void Port_StoryBeamVictim(int who, int winner, int damage) { (void)winner; sBlown[who]++; sHp[who] -= damage; if (sHp[who] < 0) sHp[who] = 0; }
s32 BtlStage_GetWaterLevel(f32 *y) { *y = 0; return 0; }
s32 BtlStage_GetStartPlace(s32 a, Vec4 *p, Vec4 *q, s32 alt) { (void)alt; *p = sPos[a]; *q = (Vec4){0, 0, 0, 1}; return 0; }
void BtlChar_Place(void *c, Vec4 *p, Vec4 *q, s32 z) { int a = (int *)c - sStubChr; (void)z; BtlCtrl_SetPos(a, p); BtlCtrl_SetRot(a, q); }
static unsigned sPresentationClock;static int sPresentationInput,sPresentationBgm;
unsigned Port_StoryPresentationClock(void){return sPresentationClock;}
int Port_StoryPresentationInput(void){return sPresentationInput;}
void Bgm_Play(int id){sPresentationBgm=id;}
int Battle_GetBgm(void){return 3;}
int PortSongs_StoryDurationMs(const char*key,int saga,int title){return 0;}
int PortSongs_StoryBgm(const char*key,int saga,int title){return 900+saga*2+title;}
static int sHanded = -1, events;
void Port_StoryHandControl(int to) { sHanded = to; }
int Port_StoryReviveActor(int a, int h) { if (h <= 0 || h > sHpMax[a]) h = sHpMax[a]; sHp[a] = h; return 1; }
void Port_StoryMusicStart(int id){(void)id;}void Port_StoryMusicEnd(void){}
static int sReleaseCount[N];void Port_StoryReleaseActor(int a){if(a>=0&&a<N)sReleaseCount[a]++;}
#include "mod_scenarios_runtime.inc"
void BtlChar_ClearFlag(void *c, int f) { (void)c; (void)f; }
void BtlChar_SetFxBit(void *c, int f) { (void)c; (void)f; }
int BtlParam_GetChargeLoopSound(void *c) { (void)c; return 12; }
void BtlCharSnd_PlayCommonFar(void *c, int id) { (void)c; (void)id; }
void Port_StoryChargeSpark(int actor,int on){(void)actor;(void)on;}
void Port_StoryHitFx(int a) { (void)a; }
static int sTransformed[N],sRejectTransform;
s32 BtlCtrl_Transform(s32 a, s32 id) { assert(Port_ScenarioFormAllowed(a, id)); if(sRejectTransform>0){sRejectTransform--;return 0;}sTransformed[a] = id; return 1; }
int Port_StoryResourceIO(int a,int w,int*v){if(!w){memset(v,0,9*sizeof(int));v[0]=sTransformed[a]>0?sTransformed[a]:sScForms[a];v[2]=sHp[a];v[3]=sHpMax[a];v[4]=sKi[a];v[5]=100000;v[6]=sStocks[a];v[7]=300000;return 1;}sHp[a]=v[2];sHpMax[a]=v[3];sKi[a]=v[4];sStocks[a]=v[6];return 1;}
void Port_Dbg3(const char*m,int a,int b,int c,int d){(void)m;(void)a;(void)b;(void)c;(void)d;}
int gPortClash[2]={-1,-1};static int sStoryTestClash;int Port_ClashBusy(void){return sStoryTestClash;}
static int sForcedClash[2]={-1,-1};s32 Port_StoryForceClash(s32 a, s32 b) { sForcedClash[0] = a; sForcedClash[1] = b; return 1; }
#define Port_StoryUseTechnique BtlCtrl_UseTechnique
static int sTestSequence=-1; int Port_SequencePartner(int a){return sTestSequence;}
void Port_StoryControlProbe(int a){(void)a;}
void Port_StoryBeamProbe(s32 a,s32 b,s32 f){(void)a;(void)b;(void)f;}void Port_StoryActorProbe(s32 a,s32 f){(void)a;(void)f;} /* env-gated diagnostics */
void Port_StoryHoldPose(int a,int m){BtlCtrl_PlayMotion(a,m,1,.1f);}
void Port_StoryTailOutfit(int a){(void)a;}
#include "mod_story_scene.inc"

static int sub(int *set, int *line) { int ch; return Port_StorySubtitleView(&ch, set, line); }
static int sLines[32][3], sN;
static void note(void) {
    int set, line;
    if (sub(&set, &line) && (sN == 0 || (sLines[sN - 1][0] << 8 | sLines[sN - 1][1]) != (set << 8 | line))) {
        assert(sN < 32);
        sLines[sN][0] = set; sLines[sN][1] = line; sLines[sN][2] = sStRun.actor; sN++;
    }
}
static void run(BtlSeqTalkCtx *ctx) {
    int frames = 0;
    sN = 0;
    while (ctx->step >= ST_STEP_BEAT && ctx->step <= ST_STEP_HOLD && frames++ < 30 * 300) {
        st_scene_beat(ctx); note();
        if (!st_scene_hold(ctx)) { st_scene_end(); break; }
    }
    assert(frames < 30 * 300);
}
static int sCue;
static void frame(int r3) {
 if(sStAtkFrames>0&&sStAtkBy>=0&&sStAtkAt>=0){
  /* The fixture must actually route through Gohan; a first-pass hit is invalid. */
  if(Port_StoryTechniqueDonor(sStAtkBy)>=0&&(sStSbStage==1||sStSbStage==2)){
   if(sStSbStage==2&&sStSbLine>=1&&sStSbLine<=3)st_sb_lines(11);
   Vec4 p=sPos[sStAtkAt];if(sStSbStage==2)BtlCharApi_GetNodePos(st_obj(sStSbGohan),0x11,&p);Port_StoryShotReport(sStAtkBy,p.x,p.y,p.z);
  }else Port_StoryAttackContact(sStAtkBy,sStAtkAt,0x100+st_attack_slot(Port_StoryChapter(),sStAtkEvent));
 }
 sCue=Port_StoryEventCue(r3);st_fight_update();for(int a=0;a<N;a++)Port_StoryFxFrame(a);
}
static void play_event(void) {
    int frames = 0;
    sN = 0;
    while (sStEventRun >= 0 && frames++ < 30 * 300) { frame(0); note(); }
    assert(frames < 30 * 300);
}
static int actor(int cast) { return Port_StoryCastActor(cast); }

/* speakers allowed without a fighter (framed on the lead or the rival): the script's narrative-only characters */
static int off_cast(const char *s) {
    return ST_StrEq(s, "King Kai") || ST_StrEq(s, "Mr. Popo") || ST_StrEq(s, "Roshi") || ST_StrEq(s, "Bulma") || ST_StrEq(s, "Narrator");
}
static void check_line(int ch, const StLine *l) {
    assert(l && l->speaker && l->text[0] && l->text[1] && l->text[0][0] && l->text[1][0]);
    assert(ST_LineCast(ch, l) >= 0 || off_cast(l->speaker) || (ch == 5 && ST_StrEq(l->speaker, "Goku"))); /* chapter 6: Goku calls from far away */
}
static void check_table(int ch) {
    const StChapter *c = &stChapters[ch];
    const StCast *k = &stCasts[ch];
    const StEvents *e = &stEvents[ch];
    unsigned scene = 0, fighting = 0, pure = 0;
    int i, j, teams[2] = {0, 0};
    assert((c->saga == 0 || c->saga == ST_SAGA_RETIRED || c->saga == ST_SAGA_INTERLUDE) && c->sequence >= 1 && c->stage >= 0 && c->stage < 35);
    assert(c->introCount >= 1 && c->introCount <= ST_LINES && (c->canonCount >= 1 || ST_Interlude(ch)) && c->canonCount <= ST_LINES && c->altCount <= ST_LINES);
    if (ST_Interlude(ch)) assert(c->canonCount == 0 && c->altCount == 0 && e->count == 0 && !c->zeniCanonFirst && !c->zeniCanonRepeat && !c->zeniAltFirst && !c->zeniAltRepeat);
    for (i = 0; i < c->introCount; i++) check_line(ch, &c->intro[i]);
    for (i = 0; i < c->canonCount; i++) check_line(ch, &c->canon[i]);
    for (i = 0; i < c->altCount; i++) check_line(ch, &c->alt[i]);
    assert(k->count >= 2 && k->count <= ST_ACTORS && k->count <= MS_MAX);
    for (i = 0; i < k->count; i++) {
        if (k->actor[i].flags & ST_ACTOR_SCENE) scene |= 1u << i;
        if ((k->actor[i].flags & ST_ACTOR_SCENE) && !(k->actor[i].flags & ST_ACTOR_RESERVE)) pure |= 1u << i;
        else { fighting |= 1u << i; teams[k->actor[i].team ? 1 : 0]++; }
        for (j = 0; j < i; j++) assert(!ST_StrEq(k->actor[i].name, k->actor[j].name));
        /* repeated characters (several Saibamen) map to the n-th battle actor of that character and team */
    }
    assert(!(k->actor[0].flags & ST_ACTOR_SCENE) && k->actor[0].team == 0 && teams[0] >= 1 && (teams[1] >= 1||ST_Interlude(ch)));
    assert(k->carried == -1 && k->carrier == -1 && k->arriving == -1 && (k->noLeaves || (k->departMask && k->leavesCanon[0] && k->leavesCanon[1])));
    assert(!((k->introShow | k->canonShow | k->altShow | k->showDown | k->endDown) & ~scene));
    assert(!(k->endHide & ~fighting));
    assert(c->player == k->actor[0].chara);
    for (i = 0; i < stScenes[ch].cueCount; i++) check_line(ch, &stScenes[ch].cue[i].line);
    assert(e->count <= ST_EVENTS);
    for (i = 0; i < e->count; i++) {
        const StEvent *v = &e->event[i];
        assert((v->kind == ST_EVENT_SCENE || v->kind == ST_EVENT_MOON) && v->lineCount >= 1 && v->lineCount <= ST_EVENT_LINES);
        assert(v->transformTo == 0 || (v->transformCast >= 0 && v->transformCast < k->count && (fighting >> v->transformCast & 1u)));
        for (j = 0; j < v->lineCount; j++) check_line(ch, &v->line[j]);
        assert(!((v->killMask | v->removeMask | v->enterMask | v->exitMask) & ~fighting) && !(v->showMask & ~scene));
        assert(v->handTo >= 0 && v->handTo <= k->count && (!v->handTo || (fighting >> (v->handTo - 1) & 1u)));
        assert(v->switchCast < k->count && (v->switchCast < 0 || (fighting >> v->switchCast & 1u)));
        assert(v->trigger >= ST_TRIG_RIVAL_HP && v->trigger <= ST_TRIG_AFTER && v->threshold >= 0);
        assert(v->trigger != ST_TRIG_AFTER || (ST_Phased(ch) && i > 0));
    }
    if (stPolicy[ch] == ST_POL_EVENT) assert(ST_CanonMask(ch) != 0);
    assert(stPolicy[ch] != ST_POL_LEGACY);
}

static void fight_start(void) {
    int i;
    Port_ScenarioRoundReset();
    for (i = 0; i < N; i++) { sHpMax[i] = 40000; sHp[i] = 40000;sKi[i]=60000;sStocks[i]=200000; sPos[i] = (Vec4){i * 30.0f, 0, i * 20.0f, 1}; sRot[i].y = 0; }
    sWork.flags = 0; sInterruptible = 1; sResultSets = 0; memset(&sResult, 0, sizeof(sResult)); memset(sBlown, 0, sizeof(sBlown));
    st_fight_enter();
}
static int rival_actor(int ch) { int i; for (i = 0; i < stCasts[ch].count; i++) if (stCasts[ch].actor[i].team == 1 && !(stCasts[ch].actor[i].flags & ST_ACTOR_SCENE)) return actor(i); return -1; }
/* brings the event's moment about: the rival's HP, or the clock */
static void reach(int ch, const StEvent *v) {
    int r = rival_actor(ch), f;
    if (v->trigger == ST_TRIG_TIME) {
        for (f = 0; f < 30 * 120 && sStFightFrames < v->threshold * 30 && sStEventRun < 0; f++) frame(0);
    } else if (v->trigger == ST_TRIG_RIVAL_PCT) {
        int hp = (int)((long long)sHpMax[r] * v->threshold / 100);
        if (sHp[r] > hp) sHp[r] = hp;
    } else if (sHp[r] > v->threshold) sHp[r] = v->threshold;
}

/* ---- a phased chapter (chapter 7): every phase in order ---- */
static void phased_test(int ch) {
    const StChapter *c = &stChapters[ch];
    const StCast *kc = &stCasts[ch];
    const StEvents *e = &stEvents[ch];
    BtlSeqTalkCtx ctx;
    MsView v;
    int i, k, f, prev = -1;

    Port_ScenarioClear(); Port_StoryChoose(ch); sMode = 0;
    memset(&v, 0, sizeof(v));
    { int expect = 0; for (i = 0; i < kc->count; i++) if (!(kc->actor[i].flags & ST_ACTOR_DENSE) || sDense) expect++;
      assert(Port_ScenarioDraft(&v) > 0 && v.total == expect && v.total <= MS_MAX); }
    for (i = 0; i < N; i++) sTeam[i] = i < sScN ? sScTeams[i] : 0;
    sBits = 0; memset(sGroups, 0, sizeof(sGroups)); sMoney = 0; memset(sRanks, 0, sizeof(sRanks));
    for (i = ch - 1; i >= 0; i--) if (stChapters[i].saga == c->saga) { prev = i; break; }
    if (prev >= 0) { if (prev / 4 == 0) sBits |= 1 << (2 * (prev % 4)); else sGroups[prev / 4] |= 1 << (2 * (prev % 4)); }
    fight_start();
    for (i = 0; i < kc->count; i++)
        if ((kc->actor[i].flags & ST_ACTOR_SCENE) && actor(i) >= 0) assert(Port_StorySceneParked(actor(i)));   /* sidelines / observers */
        else if (kc->actor[i].flags & ST_ACTOR_DENSE) assert(!sDense && actor(i) < 0);              /* reduced density: left out */
    memset(&ctx, 0, sizeof(ctx));
    assert(st_intro_enter(&ctx) == 1);
    run(&ctx);
    assert(sN == c->introCount);
    assert(Port_StoryEndPending());                                                      /* phases still to come */
    sHanded = -1;
    for (k = 0; k < e->count; k++) {
        const StEvent *ev = &e->event[k];
        int r = st_rival_now(ch), j;
        assert(r >= 0);
        if(ch==6&&k==2){
          frame(0);assert(sStEventRun<0&&!sCue);
          sHp[actor(10)]=sHp[actor(11)]=0;frame(0);assert(sStEventRun<0&&!sCue);
          sHp[actor(12)]=sHp[actor(14)]=0;
        }
        if(ch==11&&k==12){int ape=actor(1);assert(Port_Team(ape)!=Port_Team(actor(2))&&Port_Team(ape)!=Port_Team(actor(4)));sHp[ape]=sHpMax[ape]*30/100;}
        if (ev->trigger == ST_TRIG_AFTER && ev->autoStart) {
            for (f = 0; f < 30 * 60 && sStEventRun < 0; f++) frame(0);                  /* an AUTO beat */
        } else if (ev->trigger == ST_TRIG_AFTER) {                                       /* R3 offered after a delay */
            for (f = 0; f < 30 * 60 && !sCue; f++) frame(0);
            assert(sCue && sStEventOffer == k);
            frame(1);
        } else {
            if (ev->trigger == ST_TRIG_RIVAL_PCT) { int hp = (int)((long long)sHpMax[r] * ev->threshold / 100); if (sHp[r] > hp) sHp[r] = hp; }
            else if (ev->trigger == ST_TRIG_RIVAL_HP) { if (sHp[r] > ev->threshold) sHp[r] = ev->threshold; }
            if ((k == 0 && ev->koAlt == 0)||(ch==6&&k==2)) { sHp[r] = 0; frame(0); }                    /* an early KO plays the phase */
            else {
                if (k + 1 < e->count) { /* extra damage never skips ahead */
                    frame(0); frame(0);
                    assert(sCue && sStEventOffer == k);
                }
                frame(0); frame(0);
                assert(sCue && sStEventOffer == k);
                frame(1);
            }
        }
        assert(sStEventRun == k && (sWork.flags & BATTLE_FLAG_DEMO));
        memset(sTransformed, 0, sizeof(sTransformed));
        play_event();
        assert(sN == (ev->attackCast>0?st_event_before(ch,k):ev->lineCount) && sStEventRun < 0 && !(sWork.flags & BATTLE_FLAG_DEMO));
        if (ev->attackCast > 0 || (ch==11&&k==4)) { /* the authored attack: its effects wait until it lands */
            int tgt=sStAtkAt; /* shield contact may differ from the original threatened target */
            assert(sStAtkFrames > 0 && sResultSets == 0 && Port_StoryEndPending());
            if(tgt>=0)assert(Port_StoryHeld(tgt));
            if(ev->choreo==ST_CHOREO_EXECUTE){
                int target=actor(ev->attackTarget-1),kind=0x100+st_attack_slot(ch,k);
                assert(tgt==target&&sMotionId[tgt]==ST_MOTION_IDLE&&!sStAtkFree);
                Port_StoryAttackContact((sStAtkBy+1)%N,tgt,kind);assert(!sStAtkContact&&!sStAtkFree);
                Port_StoryAttackContact(sStAtkBy,tgt,0x55);assert(!sStAtkContact&&!sStAtkFree);
                Port_StoryAttackContact(sStAtkBy,tgt,kind);assert(sStAtkContact&&sStAtkFree&&sMotionId[tgt]==ST_ANIM_HIT);
                sStAtkContact=sStAtkFree=0; /* the live-frame loop below exercises the real contact again */
            }
            if(ev->attackTech>=0){int old=sHp[tgt];sHp[tgt]--;Port_StoryAttackContact((sStAtkBy+1)%N,tgt,0x100+ev->attackTech);Port_StoryAttackContact(sStAtkBy,tgt,0x55);st_attack_update(ch);assert(sStAtkFrames>0&&sResultSets==0);sHp[tgt]=old;}

            for (f = 0; f < 2000 && (sStAtkFrames > 0||sStEventRun>=0); f++) { if (f == 20 && sStAtkFrames>0 && ev->attackTech >= 0 && tgt >= 0) sHp[tgt] -= 100; frame(0);note(); }
            /* This fixture moves the ball in two frames; live beat lines may skip
               the waiting commentary. Their paced order is tested separately. */
            assert(sN==ev->lineCount||(ch==11&&k==10&&sN>=st_event_before(ch,k)&&sN<=ev->lineCount));
            assert(sStAtkFrames == 0);
        }
        for (j = 0; j < kc->count; j++) {
            int a = actor(j);
            if (a < 0) { assert((kc->actor[j].flags & ST_ACTOR_DENSE) && !sDense); continue; }   /* reduced density */
            if (ev->enterMask >> j & 1u) assert(!Port_StorySceneParked(a) && sHp[a] > 0);
            if (ev->exitMask >> j & 1u) assert(Port_StorySceneParked(a));
            if (ev->killMask >> j & 1u) {fprintf(stderr,"VERIFY death ch%d event%d cast%d hp%d noRev%d parked%d\n",ch,k,j,sHp[a],Port_StoryNoRevive(a),Port_StorySceneParked(a));} if (ev->killMask >> j & 1u) assert(sHp[a] == 0 && Port_StoryNoRevive(a) && !Port_StorySceneParked(a)); /* body stays */
            if (ev->removeMask >> j & 1u) assert(Port_StorySceneParked(a) && Port_StoryNoRevive(a));
        }
        if (ev->handTo > 0) assert(sHanded == actor(ev->handTo - 1));
        if (ev->refill) { int nr = st_rival_now(ch); assert(nr >= 0 && sHp[nr] == sHpMax[nr]); }
        if (ev->transformTo > 0) assert(sTransformed[actor(ev->transformCast)] == ev->transformTo);
        if (ev->canon) { assert(sResultSets == 1 && sResult.winner == 1 && !Port_StoryEndPending()); break; }
        assert(sResultSets == 0 && Port_StoryEndPending());
        events++;
    }
    assert(k < e->count);
    memset(&ctx, 0, sizeof(ctx));
    assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_CANON);
    run(&ctx);
    assert(sN == c->canonCount + (kc->departMask ? 1 : 0)); /* chapter 7: Goku and Vegeta leave together (departure caption) */
    Port_StoryResult(0, 1, 1);
    { int oc, z; assert(Port_StoryLastOutcome(&oc, &z) == ST_CANON && oc == ch && z == c->zeniCanonFirst); }
    assert(Port_StoryProgress(ch) == ST_DONE_CANON && Port_StoryUnlockedChapter() == ST_NextVisible(ch));
    Port_StoryAcknowledge();
    /* checkpoints: a failure after reaching one; the retry resumes there with the earlier consequences in place */
    for (k = 0; k < e->count; k++) {
        int j, cp = k,witness=-1;
        if (!e->event[k].checkpoint) continue;
        Port_StoryChoose(ch); fight_start();
        sStEventsDone = (1u << cp) - 1u;
        for(j=0;j<cp;j++){StEvent pe=e->event[j];pe.damage=pe.canon=pe.refill=pe.attackCast=0;if(pe.transformTo>0){int who=actor(pe.transformCast);Port_StoryGrantForm(who,pe.transformTo);BtlCtrl_Transform(who,pe.transformTo);}st_event_effects(ch,&pe,st_rival_now(ch));}
        for(j=0;j<kc->count;j++){int a=actor(j);if(a>=0&&kc->actor[j].team==0&&sHp[a]>0&&!Port_StorySceneParked(a)){witness=a;break;}}
        assert(witness>=0);sHp[witness]=23456;sKi[witness]=34567;sStocks[witness]=123456;Port_StoryFoundBall(3);
        frame(0);                                                    /* the phase becomes pending: recorded */
        Port_StoryResult(1, 1, 1);                                   /* the player's side falls */
        { int oc, z; assert(Port_StoryLastOutcome(&oc, &z) == ST_FAIL && z == 0); }
        Port_StoryAcknowledge();
        Port_StoryRequestCheckpoint();Port_StoryChoose(ch); fight_start();frame(0);                         /* explicit retry and completed resource restore */
        assert(sHp[witness]==23456&&sKi[witness]==34567&&sStocks[witness]==123456&&Port_StoryFoundBallValue()==3);
        assert(Port_StoryResumeEvent() == cp && sStEventsDone == (1u << cp) - 1u && (Port_StoryEvents() & ((1u << cp) - 1u)) == (1u << cp) - 1u);
        for (j = 0; j < cp; j++) {
            const StEvent *pe = &e->event[j];
            int i2;
            for (i2 = 0; i2 < kc->count; i2++) {
                int a = actor(i2);
                if (a < 0) continue;
                if (pe->killMask >> i2 & 1u) assert(Port_StoryNoRevive(a));
                if (pe->removeMask >> i2 & 1u) assert(Port_StorySceneParked(a));
            }
        }
        Port_StoryChoose(ch); fight_start();                         /* no failure in between: from the start */
        assert(Port_StoryResumeEvent() == 0 && sStEventsDone == 0);
        events++;
    }
    /* script section 18: a phase enemy beaten while an event is pending */
    for (k = 0; k < e->count; k++) {
        const StEvent *ev = &e->event[k];
        int r, j;
        if (!ev->koAlt) continue;
        assert(ev->altCount >= 1 && ev->altCount <= 2);
        Port_StoryChoose(ch); fight_start();
        sStEventsDone = (1u << k) - 1u;                            /* the battle has reached this phase */
        r = st_rival_now(ch);
        assert(r >= 0);
        sHp[r] = 0;
        frame(0);
        if (ev->koAlt == 2) {                                       /* the battle ends on the alternative */
            assert(sResultSets == 1 && sResult.winner == 1 && !Port_StoryEndPending() && sStEventRun < 0);
            assert(ST_OutcomeFor(ch, 0, 1, Port_StoryEvents()) == ST_ALT);
            memset(&ctx, 0, sizeof(ctx));
            assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_ALTEV0 + k);
            run(&ctx);
            assert(sN == ev->altCount);
            Port_StoryResult(0, 1, 1);
            { int oc, z; assert(Port_StoryLastOutcome(&oc, &z) == ST_ALT); }
            Port_StoryAcknowledge();
        } else {                                                    /* the phase's alternative: nobody dies */
            assert(sStEventRun == k);
            play_event();
            assert(sN == ev->altCount && sResultSets == 0);
            for (j = 0; j < kc->count; j++) { int a = actor(j); if (a >= 0 && ((ev->killMask | ev->removeMask) >> j & 1u)) assert(kc->actor[j].chara==79||!Port_StoryNoRevive(a)); }
            assert(ev->handTo==0||sHanded==actor(ev->handTo-1));
        }
        events++;
    }
    /* the player's whole side down: the battle may end (a failure) */
    Port_StoryChoose(ch); fight_start();
    assert(Port_StoryEndPending());
    for (i = 0; i < kc->count; i++) if (kc->actor[i].team == 0 && !Port_StorySceneParked(actor(i))) sHp[actor(i)] = 0;
    assert(!Port_StoryEndPending());
    assert(ST_OutcomeFor(ch, 1, 1, 0) == ST_FAIL);
}

static void test_ch4_auto_retry(void){
 MsView v;int a,f;Port_ScenarioClear();Port_StoryChoose(3);memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);fight_start();
 a=actor(1);memset(sTransformed,0,sizeof(sTransformed));sRejectTransform=2;
 /* Even dropping past the later moon HP trigger cannot skip the automatic first beat. */
 sHp[a]=1;for(f=0;f<90&&sStEventRun<0;f++)frame(0);assert(sStEventRun==0&&!sCue);play_event();
 assert(sStFormActor==a&&!sTransformed[a]);
 for(f=0;f<60&&sStFormActor>=0;f++)frame(0);
 assert(sTransformed[a]==180&&sStFormActor<0&&!sResultSets);sRejectTransform=0;
 Port_ScenarioClear();
 puts("PASS chapter4 auto transformation stays before the moon event and retries rejected native admissions");
}
static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

static void test_pending_r3(void){
 MsView v;int enemy;
 Port_ScenarioClear();Port_StoryChoose(6);memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);fight_start();enemy=actor(7);
 sHp[enemy]=sHpMax[enemy]*30/100;frame(0);assert(sStEventOffer==0&&sStEventRun<0);assert(Port_StoryEventCue(0));
 sInterruptible=0;assert(!Port_StoryEventCue(1));frame(0);assert(sStEventRun<0&&sStEventPressed);
 sInterruptible=1;frame(0);assert(sStEventRun==0);st_scene_end();sStEventRun=-1;sWork.flags&=~BATTLE_FLAG_DEMO;Port_ScenarioClear();
 puts("PASS visible R3 press is retained across admission races; hidden offers do not steal assist input");
}
static void test_live_wave_anchors(void){
 MsView v;BtlSeqTalkCtx ctx;int tien,yam,saiba,kr;
 Port_ScenarioClear();Port_StoryChoose(6);memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);fight_start();
 tien=actor(0);yam=actor(1);kr=actor(2);saiba=actor(13);sStEventAlt=0;
 st_event_effects(6,&stEvents[6].event[0],actor(7));
 assert(Port_StorySceneParked(tien)&&!Port_StorySceneParked(yam)&&!Port_StorySceneParked(saiba));
 sPos[tien].y=100000;assert(st_lead_now(6)==yam&&st_rival_now(6)==saiba);
 memset(&ctx,0,sizeof(ctx));sStEventShow=stEvents[6].event[1].showMask;
 st_scene_begin(&ctx,6,ST_SET_EVENT0+1,0,4,-1,0);
 assert(sStRun.rival==saiba&&(sStRun.actors>>yam&1u)&&!(sStRun.actors>>tien&1u));st_scene_end();
 st_event_effects(6,&stEvents[6].event[1],saiba);assert(sHanded==kr&&!Port_StorySceneParked(kr)&&sPos[kr].y<1000);
 Port_ScenarioClear();puts("PASS actual phase0 -> Yamcha -> Krillin: active rival, owned reserve poses and spawn anchors never use parked Tien");
}

static void prepare_ch7_contacts(void){MsView v={0};Port_ScenarioClear();Port_StoryChoose(6);assert(Port_ScenarioDraft(&v));for(int i=0;i<N;i++)sTeam[i]=i<sScN?sScTeams[i]:0;fight_start();for(int i=1;i<=8;i++)Port_StoryEnterActor(actor(i));}
static void test_ch7_contacts(void){BtlSeqTalkCtx ctx={0};int pic,krillin,tien,goku,nappa,gohan;
 prepare_ch7_contacts();st_attack_begin(6,3,actor(8));
 {int target=sStAtkAt,stops=sStopCalls[target];for(int f=0;f<ST_CLING_HOLD+2&&!sStAtkFired;f++){sStAtkFrames++;st_choreo_update(6,&stEvents[6].event[3],sStAtkFrames);}
  assert(sStAtkFired&&Port_StoryHeld(target)&&sStopCalls[target]==stops+1);
  assert(Port_StoryAuthoredVictim(sStAtkBy,target,4));
  assert(!Port_StoryAuthoredVictim(sStAtkBy,target,2)&&!Port_StoryAuthoredVictim((sStAtkBy+1)%N,target,4));
  Port_StoryAttackContact(sStAtkBy,target,0x55);assert(!sStAtkContact&&Port_StoryHeld(target));
  sMotionId[target]=0x15E; /* the native paired victim track owns the grab */
  Port_StoryAttackContact(sStAtkBy,target,0x104);assert(sStAtkContact&&!Port_StoryHeld(target)&&sMotionId[target]==0x15E);}
    /* The sacrifice has exactly one beam victim, and an actual hit changes
       his authored bracing pose once. Unrelated contacts do not trigger it. */
    prepare_ch7_contacts();st_attack_begin(6,7,actor(8));
    {int pic=actor(3),goh=actor(4),n=sStAtkBy;
     assert(!Port_StoryShieldBystander(pic)&&Port_StoryShieldBystander(goh));
     assert(!Port_StoryShieldBystander(-1));
     Port_StoryAttackContact(n,goh,0x104);assert(!sStAtkContact);
     Port_StoryAttackContact(n,pic,0x104);assert(sStAtkContact&&sMotionId[pic]==0xB3);
     sMotionId[pic]=123;Port_StoryAttackContact(n,pic,0x104);assert(sMotionId[pic]==123);
     sStAtkFrames=0;sStEventRun=-1;assert(!Port_StoryShieldBystander(goh));
    }
 prepare_ch7_contacts();st_attack_begin(6,8,actor(8));assert(Port_StoryUltimatePrelude(sStAtkBy)&&!Port_StoryUltimatePrelude(sStAtkAt));sStAtkContact=1;sBlockedActor=actor(8);st_attack_update(6);assert(sStAtkFrames>0&&!sStEventPost);sBlockedActor=-1;st_attack_update(6);assert(!sStAtkFrames&&sStEventPost);
 prepare_ch7_contacts();memset(sReleaseCount,0,sizeof(sReleaseCount));sStAtkBy=actor(6);sStAtkAt=sStAtkRival=actor(8);st_attack_done(6,&stEvents[6].event[9]);assert(sReleaseCount[actor(2)]&&sReleaseCount[actor(3)]&&sReleaseCount[actor(4)]);
 prepare_ch7_contacts();{int v=actor(9);assert(Port_StorySceneParked(v));st_attack_begin(6,0,actor(7));assert(!sStAttackDown[v]&&sHp[v]>0&&Port_StoryOwnAttackCamera(st_obj(v)));sStAtkFrames=0;}
 prepare_ch7_contacts();{int pic=actor(3),kr=actor(2),go=actor(4),n=actor(8);sStEventRun=7;assert(!Port_StoryProtectedDamage(pic,0));assert(Port_StoryProtectedDamage(kr,0)&&Port_StoryProtectedDamage(go,0)&&Port_StoryProtectedDamage(n,0));assert(!Port_StoryProtectedDamage(kr,0x42B));sStAtkFrames=1;sStAtkEvent=7;assert(Port_StoryProtectedDamage(pic,0));assert(!Port_StoryProtectedDamage(pic,0x2000100));assert(Port_StoryProtectedDamage(go,0x800100));sStAtkFrames=0;sStEventRun=-1;assert(!Port_StoryProtectedDamage(kr,0));}
 prepare_ch7_contacts();{int dead=actor(10);Port_StoryEnterActor(dead);sHp[dead]=0;sTestSequence=2;st_fight_update();assert(!(sStGoneActors>>dead&1u));sTestSequence=-1;st_fight_update();assert(sStGoneActors>>dead&1u);}
 prepare_ch7_contacts();{int t=actor(0),k=actor(2);sStRun.chapter=6;sStRun.active=1;sStRun.actors=(1u<<t)|(1u<<k);for(int j=0;j<ST_ACTORS;j++)sStRun.cast[j]=j<stCasts[6].count?actor(j):-1;sHp[t]=0;sStDownActors|=1u<<t;sPos[t]=(Vec4){123,-200,456,1};sReady=1;st_scene_formation();assert(sPos[t].x==123&&sPos[t].z==456);sReady=0;sStRun.active=0;}
 prepare_ch7_contacts();pic=actor(3);krillin=actor(2);tien=actor(0);goku=actor(6);nappa=actor(8);gohan=actor(4);
 sHp[krillin]=0;st_scene_begin(&ctx,6,ST_SET_EVENT0+6,0,1,-1,0);assert(st_down(krillin)&&Port_StorySceneActorShown(krillin)&&sHp[krillin]==0);st_scene_end();
 sHp[tien]=0;sStDownActors|=1u<<tien;sStGoneActors|=1u<<tien;assert(Port_StorySceneActorShown(tien)&&Port_StoryNoRevive(tien));
 sHp[pic]=0;st_attack_begin(6,7,nappa);assert(Port_StoryUltimatePrelude(nappa)&&!Port_StoryUltimatePrelude(pic));assert(sStAtkAt==pic&&sStAttackDown[pic]&&sHp[pic]==1);assert(st_dist(&sPos[nappa],&sPos[gohan])>6*st_height(gohan));assert(st_dist(&sPos[pic],&sPos[gohan])>st_height(gohan)&&st_dist(&sPos[pic],&sPos[gohan])<3*st_height(gohan));
 {Vec4 expected=sStShieldGohanAnchor;sPos[gohan].x+=100;st_attack_update(6);assert(st_dist(&sPos[gohan],&expected)<.001f);}
 Port_StoryAttackContact(nappa,pic,0x104);st_attack_update(6);assert(!sStAtkFrames&&sHp[pic]==0&&!sStAttackDown[pic]&&sStEventPost&&sStEventRun==7);assert(Port_StorySceneActorShown(pic)&&Port_StorySceneActorShown(tien));
 for(int f=0;f<3000&&sStEventRun>=0;f++)frame(0);assert(!sStRun.active&&sHp[krillin]==0&&sHp[pic]==0);
 /* Required Masenko may play if Gohan was down, but cannot revive him for gameplay. */
 sHp[gohan]=0;Port_StoryExitActor(goku,sHp[goku]);st_event_start(6,8);assert(!(sStRun.actors>>goku&1u));play_event();assert(sStAtkFrames&&sStAtkBy==gohan&&sStAttackDown[gohan]);
 Port_StoryAttackContact(gohan,nappa,0x104);st_attack_update(6);assert(sStEventPost&&sStEventRun==8&&sHp[gohan]==0&&!sStAttackDown[gohan]);assert(sHanded==goku&&!Port_StorySceneParked(goku));
 for(int f=0;f<3000&&sStEventRun>=0;f++)frame(0);assert(!sStRun.active&&!Port_StoryHeld(goku)&&!(sWork.flags&BATTLE_FLAG_DEMO));assert(sHp[gohan]==0&&Port_StorySceneActorShown(tien));
 puts("PASS actual Story driver: downed Krillin shown without healing, Tien corpse persists, KO Piccolo still performs shield with KO restored, Nappa distance, Masenko before Goku entry, control/pose fully released");Port_ScenarioClear();}

int main(void) {test_ch4_auto_retry();test_pending_r3();test_live_wave_anchors();test_ch7_contacts();
    BtlSeqTalkCtx ctx;
    MsView v;
    int ch, i, k, chapters = 0;

    for (ch = 3; ch < ST_COUNT; ch++) {
        const StChapter *c = &stChapters[ch];
        const StCast *kc = &stCasts[ch];
        const StEvents *e = &stEvents[ch];
        unsigned prevBits;
        int set, line, lead, rival;
        check_table(ch);
        if (c->saga == ST_SAGA_RETIRED || ST_Interlude(ch)) continue; /* kept in the save, out of the menu / scene-only */
        if (ST_Phased(ch)) {
            int i2, dense = 0;
            for (i2 = 0; i2 < kc->count; i2++) if (kc->actor[i2].flags & ST_ACTOR_DENSE) dense++;
            sDense = 0; phased_test(ch);                                          /* reduced density */
            if (dense) {                                                           /* full density: every enemy at once */
                MsView dv; int a2;
                sDense = 1; Port_ScenarioClear(); Port_StoryChoose(ch); memset(&dv, 0, sizeof(dv));
                assert(Port_ScenarioDraft(&dv) > 0 && dv.total == kc->count && dv.total <= 16);
                for (i2 = 0; i2 < kc->count; i2++) for (a2 = 0; a2 < i2; a2++) assert(actor(i2) != actor(a2) && actor(i2) >= 0);
                phased_test(ch);
                sDense = 0;
            }
            chapters++; continue;
        }

        /* ---- the fixed draft: every cast member, team 0 first, at most ten fighters ---- */
        Port_ScenarioClear(); Port_StoryChoose(ch); sMode = 0;
        memset(&v, 0, sizeof(v));
        assert(Port_ScenarioDraft(&v) > 0 && v.total == kc->count && v.pick[0].pad == 0 && v.pick[0].chara == kc->actor[0].chara);
        for (i = 0; i < N; i++) sTeam[i] = i < sScN ? sScTeams[i] : 0;
        for (i = 0; i < kc->count; i++) {
            int a = actor(i);
            assert(a >= 0 && a < N && sTeam[a] == kc->actor[i].team);
            assert(!!Port_StorySceneActor(a) == !!(kc->actor[i].flags & ST_ACTOR_SCENE));
            for (k = 0; k < i; k++) assert(actor(k) != a);
        }
        lead = actor(0); rival = rival_actor(ch);
        assert(lead == 0 && rival >= 0);

        /* ---- the opening ---- */
        fight_start();
        memset(&ctx, 0, sizeof(ctx));
        assert(st_intro_enter(&ctx) == 1);
        for (i = 0; i < kc->count; i++) {
            int a = actor(i);
            if (kc->actor[i].flags & ST_ACTOR_SCENE) assert(!Port_StorySceneParked(a) == !!(kc->introShow >> i & 1u));
            if (kc->endHide >> i & 1u) {if(ch==3)assert(sVisible[a]&&Port_StorySceneActorShown(a));else assert(sVisible[a] == 0 && !Port_StorySceneActorShown(a));}
        }
        run(&ctx);
        assert(sN == c->introCount);
        for (i = 0; i < sN; i++) assert(sLines[i][0] == ST_SET_INTRO && sLines[i][1] == i);
        for (i = 0; i < kc->count; i++) if (kc->actor[i].flags & ST_ACTOR_SCENE) assert(Port_StorySceneParked(actor(i))); /* re-parked */

        /* ---- canonical path: every event in order ---- */
        sBits = 0; memset(sGroups, 0, sizeof(sGroups)); sMoney = 0; memset(sRanks, 0, sizeof(sRanks));
        if (ch > 0) { if ((ch - 1) / 4 == 0) sBits |= 1 << (2 * ((ch - 1) % 4)); else sGroups[(ch - 1) / 4] |= 1 << (2 * ((ch - 1) % 4)); } /* previous chapter cleared */
        prevBits = Port_StoryProgress(ch - 1);
        Port_StoryChoose(ch); fight_start();
        for (k = 0; k < e->count; k++) {
            const StEvent *ev = &e->event[k];
            int hpBefore, j;
            reach(ch, ev);
            if (!ev->autoStart) {
                frame(0); frame(0);
                assert(sCue && sStEventOffer == k);              /* the R3 glyph: offered at its moment, in order */
                frame(1);
            } else {
                frame(0);                                        /* AUTO: it starts by itself */
                assert(!sCue);
            }
            assert(sStEventRun == k && (sWork.flags & BATTLE_FLAG_DEMO) && (Port_StoryEvents() >> k & 1u));
            for (j = 0; j < kc->count; j++) if (ev->showMask >> j & 1u) assert(!Port_StorySceneParked(actor(j)));
            hpBefore = sHp[rival];
            memset(sTransformed, 0, sizeof(sTransformed));
            if (ev->transformTo > 0) assert(!Port_ScenarioFormAllowed(actor(ev->transformCast), ev->transformTo)); /* not before the event */
            play_event();
            if (ev->transformTo > 0) assert(sTransformed[actor(ev->transformCast)] == ev->transformTo); /* the native form change */
            if (ev->kind == ST_EVENT_MOON) { /* the sky shot, then the tail beam: ends the chapter when it lands */
                int f;
                assert(sResultSets == 0 && sStMoonFrames > 0 && Port_StoryHeld(rival));
                for (f = 0; f < ST_MOON_SKY_FRAMES + 4; f++) frame(0);
                assert(sResultSets == 0);
                for(f=0;f<ST_MOON_BEAM_FRAMES+32&&sStMoonFrames;f++)frame(0);
                assert(sResultSets == 1 && sStMoonFrames == 0 && Port_StoryHeld(rival));
            }
            assert(sN == ev->lineCount);
            for (j = 0; j < sN; j++) assert(sLines[j][0] == ST_SET_EVENT0 + k && sLines[j][1] == j);
            assert((!(sWork.flags&BATTLE_FLAG_DEMO)||(ev->choreo==ST_CHOREO_CLASH&&sStAtkFrames>0)||(ch==3&&ev->kind==ST_EVENT_MOON&&sResultSets==1))&&sStEventRun<0);
            for (j = 0; j < kc->count; j++) {
                int a = actor(j);
                if (kc->actor[j].flags & ST_ACTOR_SCENE) assert(Port_StorySceneParked(a));
                if (ev->removeMask >> j & 1u) assert(sHp[a] == 0 && Port_StorySceneParked(a));     /* a story death: out for good */
                if (ev->killMask >> j & 1u) {fprintf(stderr,"VERIFY death ch%d event%d cast%d hp%d noRev%d parked%d\n",ch,k,j,sHp[a],Port_StoryNoRevive(a),Port_StorySceneParked(a));} if (ev->killMask >> j & 1u) assert(sHp[a] == 0 && sBlown[a] == 1);                 /* a story KO */
            }
            if (ev->damage > 0 && !(ev->removeMask >> 0 & 0) && !((ev->removeMask | ev->killMask) & (1u << 0) && 0)) {
                int r = -1;
                for (j = 0; j < kc->count; j++) if (actor(j) == rival) r = j;
                if (!((ev->removeMask | ev->killMask) >> r & 1u)) assert(sHp[rival] == (hpBefore > ev->damage ? hpBefore - ev->damage : 1));
            }
            if (ev->switchCast >= 0) {
                int a = actor(ev->switchCast);
                assert(ch==11&&k==11 ? Port_Team(a)!=Port_Team(lead)&&Port_Team(a)!=Port_Team(rival) : ev->switchToLead ? Port_Team(a) == Port_Team(lead) : Port_Team(a) == Port_Team(rival));
            }
            if (ev->attackCast > 0 || (ch==11&&k==4)) { /* the authored attack / clash: its effects wait for it */
                int f;
                assert(sStAtkFrames > 0 && sResultSets == 0 && Port_StoryEndPending());
                if (ev->choreo == ST_CHOREO_CLASH) {
                    frame(0);frame(0);assert(sForcedClash[0]==-1&&sForcedClash[1]==-1);
                    assert(Port_StoryHeld(actor(ev->attackCast-1))&&Port_StoryHeld(actor(ev->attackTarget-1)));
                    assert(Port_StorySceneActorShown(actor(ev->attackCast-1))&&Port_StorySceneActorShown(actor(ev->attackTarget-1)));
                    assert(!Port_ClashBusy()&&sResultSets==0);
                }
                for (f = 0; f < 800 && sStAtkFrames > 0; f++) frame(0);
                assert(sStAtkFrames == 0);
            }
            if (ev->canon) { assert(sResultSets == 1 && sResult.winner == 1 && sResult.reason == 1); break; }
            assert(sResultSets == 0);
            events++;
        }
        if (stPolicy[ch] == ST_POL_WIN && !sResultSets) { sResult.winner = 1; sResult.reason = 1; } /* an ordinary win */
        assert(sResult.winner == 1);
        memset(&ctx, 0, sizeof(ctx));
        assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_CANON);
        assert(Port_Team(lead) == 0);                              /* the side switch ends with the fight */
        for (i = 0; i < kc->count; i++) {
            int a = actor(i);
            if (kc->actor[i].flags & ST_ACTOR_SCENE) {
                assert(!Port_StorySceneParked(a) == !!(kc->canonShow >> i & 1u));
                if (kc->canonShow >> i & 1u) assert((sMotionId[a] == ST_MOTION_WOUNDED || sMotionId[a] == ST_MOTION_PRONE) == !!((kc->showDown | kc->endDown) >> i & 1u));
            }
            if (kc->endHide >> i & 1u) assert(sVisible[a] == 0);
        }
        if (kc->canonRivalUp) assert(sMotionId[rival] != ST_MOTION_WOUNDED);
        run(&ctx);
        assert(sN == c->canonCount);
        for (i = 0; i < sN; i++) assert(sLines[i][0] == ST_SET_CANON && sLines[i][1] == i);
        assert(!sub(&set, &line));
        Port_StoryResult(0, 1, 1);
        { int oc, z; assert(Port_StoryLastOutcome(&oc, &z) == ST_CANON && oc == ch && z == c->zeniCanonFirst); }
        assert(Port_StoryProgress(ch) == ST_DONE_CANON && Port_StoryClearRank(ch) == 2 && Port_StoryProgress(ch - 1) == prevBits);
        assert(Port_StoryUnlockedChapter() == ST_NextVisible(ch));
        if (ch >= 4) assert(sGroups[ch / 4] >> (2 * (ch % 4)) & ST_DONE_CANON); /* stored past the first four chapters */
        Port_StoryAcknowledge();

        /* ---- alternative path: a win before the canonical event ---- */
        if (stPolicy[ch] == ST_POL_EVENT && c->altCount > 0) {
            Port_StoryChoose(ch); fight_start();
            assert(ST_OutcomeFor(ch, 0, 1, Port_StoryEvents()) == ST_ALT);
            memset(&ctx, 0, sizeof(ctx)); sResult.winner = 1; sResult.reason = 1;
            assert(st_ending_enter(&ctx) == 1 && sStRun.set == ST_SET_ALT);
            for (i = 0; i < kc->count; i++) if (kc->actor[i].flags & ST_ACTOR_SCENE) assert(!Port_StorySceneParked(actor(i)) == !!(kc->altShow >> i & 1u));
            run(&ctx);
            assert(sN == c->altCount);
            Port_StoryResult(0, 1, 1);
            { int oc, z; assert(Port_StoryLastOutcome(&oc, &z) == ST_ALT && z == c->zeniAltFirst); }
            assert(Port_StoryProgress(ch) == (ST_DONE_CANON | ST_DONE_ALT));
            Port_StoryAcknowledge();
        } else {
            assert(ST_OutcomeFor(ch, 0, 1, 0) == ST_CANON); /* a win is the chapter's clear */
        }
        /* ---- a loss is a genuine failure; a restart nothing ---- */
        assert(ST_OutcomeFor(ch, 1, 1, ST_CanonMask(ch)) == ST_FAIL && ST_OutcomeFor(ch, 0, 0x8001, ST_CanonMask(ch)) == ST_NONE);
        Port_StoryChoose(ch); fight_start(); memset(&ctx, 0, sizeof(ctx)); sResult.winner = 2; sResult.reason = 1;
        assert(st_ending_enter(&ctx) == 0);
        chapters++;
    }
    /* ---- the first four chapters keep slot0's byte; nothing of theirs moved ---- */
    /* A watchdog may retry or fail; it never fabricates native contact / awards a win. */
    Port_StoryChoose(6);fight_start();sStAtkEvent=4;sStAtkBy=0;sStAtkAt=1;sStAtkBack=-1;sStAtkRival=sStAtkAt;sStAtkContact=sStAtkLost=0;sStAtkRetry=0;sStAtkFrames=ST_ATTACK_FRAMES;sResultSets=0;
    st_attack_update(6);assert(sStAtkFrames>0&&sStAtkRetry==1&&sResultSets==0);
    sStAtkFrames=ST_ATTACK_FRAMES;st_attack_update(6);assert(sStAtkFrames==0&&sResultSets==1&&sResult.winner==2);
    sStAtkFrames=ST_ATTACK_FRAMES;sStAtkRetry=0;sStAtkBack=1;gPortClash[0]=0;gPortClash[1]=1;sStoryTestClash=1;sResultSets=0;st_attack_update(6);assert(sStAtkFrames==2&&sStAtkRetry==0&&sResultSets==0);sStoryTestClash=0;
    Port_StoryAttackContact(1,0,0x100+stEvents[6].event[sStAtkEvent].counterTech);st_attack_update(6);assert(sStAtkFrames==0&&sResultSets==1&&sResult.winner==2);
    /* Scene-only interludes after chapter 5: their own stage and cast, the opening, then the battle ends canonically with
       no ending lines, no pre-fight card, no Zeni / progress / rank; chapter 5 -> Lookout -> King Kai -> back. */
    {int ch,i2,oc,z,set,line;unsigned before;
     assert(stInterludeNext[4] == 17 && stInterludeNext[17] == 18 && stInterludeNext[18] < 0 && ST_INTERLUDE_RETURN == 5);
     for (i2 = 0; i2 < ST_COUNT; i2++) if (i2 != 4 && i2 != 17 && i2 != 5 && i2 != 19) assert(stInterludeNext[i2] < 0);
     assert(stChapters[17].stage == 5 && stChapters[18].stage == 7 && stCasts[18].actor[1].chara == 208 && stCasts[18].actor[0].chara == 0);
     Port_StoryChoose(18); assert(Port_StoryStage() == 7);              /* King Kai's planet not installed: the fallback */
     sTestAddedStage = 0x2B; assert(Port_StoryStage() == 0x2B);         /* installed: its added-stage id */
     Port_StoryChoose(17); assert(Port_StoryStage() == 5); sTestAddedStage = -1; Port_StoryAcknowledge();
     for (ch = 17; ch < ST_COUNT; ch++) {
      check_table(ch);
      Port_StoryChoose(ch); fight_start(); sResultSets = 0;
      before = Port_StoryProgress(ch);
      memset(&ctx, 0, sizeof(ctx)); assert(st_intro_enter(&ctx) == 1 && ctx.step != ST_STEP_PREFIGHT && sStRun.set == ST_SET_INTRO);
      run(&ctx); assert(sN == stChapters[ch].introCount);
      st_fight_enter(); frame(0); assert(sResultSets == 1 && sResult.winner == 1 && sResult.reason == 1);
      frame(0); assert(sResultSets == 1);                                  /* once */
      memset(&ctx, 0, sizeof(ctx)); assert(st_ending_enter(&ctx) == 1); run(&ctx); assert(sN == 0 && !sub(&set, &line));
      assert(Port_StoryResult(0, 1, 1) == 0);
      assert(Port_StoryLastOutcome(&oc, &z) == ST_CANON && oc == ch && z == 0 && Port_StoryProgress(ch) == before && Port_StoryClearRank(ch) == 0);
      Port_StoryAcknowledge();
     }
    }
    /* Spirit Bomb: ONE native ball, thrown from Krillin by the hidden helper; Vegeta's dodge (not its enemy meanwhile),
       Gohan's rebound turns the same ball round once, and only the helper's exact contact on Vegeta lands it. */
    {int sb=-1,j,helper,veg,goh,thr;Vec4 ball;float dir[3]={1,0,0};
     for(j=0;j<stEvents[11].count;j++)if(stEvents[11].event[j].choreo==ST_CHOREO_SPIRITBOMB)sb=j;
     assert(sb>=0);
     Port_StoryChoose(11);fight_start();sResultSets=0;
     st_attack_begin(11,sb,actor(4));
     helper=sStAtkBy;veg=sStAtkAt;goh=sStSbGohan;thr=sStSbThrower;
     assert(helper==actor(2)&&thr==helper&&goh==actor(1)&&veg==actor(4)&&!Port_StorySceneParked(helper)&&gPortTarget[helper]==veg);
     assert(Port_StoryTechniqueDonor(helper)==actor(0)&&!Port_StoryHiddenAttackHelper(helper)&&Port_StoryCastActor(5)<0&&stCasts[11].count==5);
     assert(Port_StoryMotionDonor(helper)==actor(0)&&Port_StoryMotionDonor(veg)<0);
     Port_StoryAttackContact(helper,veg,0x104);                     /* before the throw: no landing */
     for(j=0;j<ST_SB_FIRE;j++)st_attack_update(11);
     assert(sStSbStage==1&&!sStAtkContact&&sStAtkFrames>0&&sResultSets==0);
     assert(Port_StorySpiritBallPass(helper));Port_StoryAttackContact(helper,veg,0x104);
     assert(!sStAtkContact); /* a direct first-pass hit is not a valid rebound */
     st_attack_update(11);assert(sStSbStage==1);                   /* no report from the ball: nothing changes */
     ball=sPos[veg];ball.x+=1.0f;Port_StoryShotSteer(helper,&ball.x,dir);st_attack_update(11);
     assert(sStSbStage==2&&Port_StoryTeam(veg,1)==0&&gPortTarget[helper]==goh&&Port_StoryHeld(veg)&&Port_StoryHeld(goh));
     /* the dodge starts the beat lines: Vegeta, Gohan, Goku, in order, over live play */
     assert(st_event_before(11,sb)==1&&sStSbLine==1);
     {int seen=0,l,st,ln;for(l=0;l<900&&sStSbLine>=1&&sStSbLine<=3;l++){st_sb_lines(11);if(sub(&st,&ln)&&st==ST_SET_EVENT0+sb&&ln==seen+1)seen++;}assert(seen==3&&sStSbLine==4);}
     BtlCharApi_GetNodePos(st_obj(goh),0x11,&ball);Port_StoryShotSteer(helper,&ball.x,dir);st_attack_update(11);
     assert(sStSbStage==3&&Port_StoryTeam(veg,1)==1&&gPortTarget[helper]==veg&&Port_StoryHeld(veg)&&Port_StoryHeld(helper)&&!Port_StoryHeld(goh)&&!Port_StorySpiritBallPass(helper));
     assert(Port_StoryMotionDonor(veg)==actor(0)&&!Port_StoryNativeImpactCamera(st_obj(helper)));
     assert(Port_StoryCameraActor()<0);
     Port_StoryShotSteer(helper,&ball.x,dir);assert(dir[0]==-1.0f);   /* turned round once ... */
     Port_StoryShotSteer(helper,&ball.x,dir);assert(dir[0]==-1.0f);   /* ... and only once */
     Port_StoryShotSteer(actor(0),&ball.x,dir);assert(dir[0]==-1.0f); /* ground Goku never casts */
     Port_StoryAttackContact(actor(0),veg,0x104);st_attack_update(11);assert(sStAtkFrames>0);
     Port_StoryAttackContact(helper,veg,0x104);st_attack_update(11);
     assert(sStAtkFrames>0&&sStSbImpactFrames==1&&!Port_StoryHeld(veg));
     assert(Port_StoryNativeImpactCamera(st_obj(helper))&&!Port_StoryOwnAttackCamera(st_obj(helper))&&!Port_StoryFixedCameraOwned());
     assert(Port_StoryCameraActor()==helper);
     for(j=0;j<200&&sStAtkFrames>0;j++)st_attack_update(11);
     assert(sStAtkFrames==0&&sResultSets==0&&!Port_StorySceneParked(helper)&&Port_StoryCastActor(5)<0&&sStSbDodge<0);
     assert(!Port_StoryHiddenAttackHelper(helper));
     assert(Port_StoryCameraActor()<0); /* camera lease ends; player ownership was never changed */
     /* A straight ball (native beam handler: never steers) reported by the detection pass: same dodge, and Gohan sends it
        on to Vegeta, who stands beyond him in its path; still one ball, still only its exact contact lands. */
     Port_StoryChoose(11);fight_start();sResultSets=0;st_attack_begin(11,sb,actor(4));helper=sStAtkBy;veg=sStAtkAt;goh=sStSbGohan;
     for(j=0;j<ST_SB_FIRE;j++)st_attack_update(11);assert(sStSbStage==1);
     ball=sPos[veg];Port_StoryShotReport(helper,ball.x,ball.y,ball.z);st_attack_update(11);assert(sStSbStage==2&&sStSbLine==1);
     {Vec4 before=sPos[veg];BtlCharApi_GetNodePos(st_obj(goh),0x11,&ball);Port_StoryShotReport(helper,ball.x,ball.y,ball.z);st_attack_update(11);
      assert(sStSbStage==2&&!sStAtkContact); /* Goku must finish giving the instruction first. */
      for(int wait=0;wait<900&&!st_sb_return_ready();wait++)st_sb_lines(11);
      assert(st_sb_return_ready());Port_StoryShotReport(helper,ball.x,ball.y,ball.z);st_attack_update(11);
      assert(sStSbStage==3&&!sStSbSteered&&gPortTarget[helper]==veg&&(sPos[veg].x!=before.x||sPos[veg].z!=before.z));}
     Port_StoryShotReport(actor(0),0,0,0);assert(sStSbStage==3); /* donor Goku is never the projectile owner */
     Port_StoryAttackContact(helper,veg,0x104);st_attack_update(11);for(j=0;j<200&&sStAtkFrames>0;j++)st_attack_update(11);assert(sStAtkFrames==0&&sResultSets==0);
     /* the longest wait: an explicit failure, never a second ball */
     Port_StoryChoose(11);fight_start();sResultSets=0;st_attack_begin(11,sb,actor(4));
     for(j=0;j<ST_SB_LIMIT+2&&sStAtkFrames>0;j++)st_attack_update(11);
     assert(sStAtkFrames==0&&sResultSets==1&&sResult.winner==2&&Port_StoryCastActor(5)<0);
    }

    /* CH8: Great Ape Gohan turned against Vegeta (event 12) must really rejoin the heroes even when Goku is down, so the
       lead found is Gohan himself (formerly: he kept his own overridden side and fought Krillin afterwards). */
    {int goh,kri,goku,veg;Port_StoryChoose(11);fight_start();goh=actor(1);kri=actor(2);goku=actor(0);veg=actor(4);
     st_event_effects(11,&stEvents[11].event[11],veg);assert(sHanded==kri&&Port_Team(goh)!=Port_Team(kri)&&Port_Team(goh)!=Port_Team(veg));
     sStFightFrames=sStLastEventEnd+300;sHp[goh]=sHpMax[goh];assert(!st_event_due(&stEvents[11].event[12],veg,sHp[veg]));sHp[goh]=sHpMax[goh]*30/100;assert(st_event_due(&stEvents[11].event[12],veg,sHp[veg]));
     sHp[goku]=0;st_event_effects(11,&stEvents[11].event[12],veg);assert(Port_Team(goh)!=Port_Team(kri)&&Port_Team(goh)!=Port_Team(veg));st_event_effects(11,&stEvents[11].event[13],veg);assert(Port_Team(goh)==Port_Team(kri));sHp[goku]=1000;}

    sBits = 0x3F; memset(sGroups, 0, sizeof(sGroups));
    assert(Port_StoryProgress(0) == 3 && Port_StoryProgress(2) == 3 && Port_StoryProgress(3) == 0 && Port_StoryProgress(4) == 0);
    sGroups[1] = 0x01; assert(Port_StoryProgress(4) == ST_DONE_CANON && Port_StoryProgress(3) == 0);
    sGroups[4] = 0x02; assert(Port_StoryProgress(16) == ST_DONE_ALT);
    assert(!Port_StoryProgress(ST_COUNT) && !Port_StoryProgress(-1));
    Port_ScenarioClear();
    printf("PASS Saiyan saga chapters 4-%d (%d chapters, %d mid-fight events): tables, drafts <=10 fighters, openings, R3/AUTO events at HP%%/HP/time triggers with KOs, story deaths, damage and side switch, canonical endings through the canonical event, alternative endings, failures, Zeni, ranks, next-chapter receipts, progress groups beyond chapter 4\n",
           ST_COUNT, chapters, events);
/* Legacy partial anchors cannot clear the new long battles or grant their trophy. */
    sBits=0;memset(sGroups,0,sizeof(sGroups));memset(sRanks,0,sizeof(sRanks));
    st_group_or(6/4,1u<<(2*(6%4)));sRanks[6]=3;
    assert(Port_StoryProgress(6)==0&&Port_StoryClearRank(6)==0&&!Port_StoryLegacyClear(6));
    for(int old=7;old<=10;old++)st_group_or(old/4,1u<<(2*(old%4)));
    assert(Port_StoryLegacyClear(6)&&Port_StoryProgress(6)==0&&Port_StoryClearRank(6)==0);
    st_group_or(8,1);sRanks[32]=2;assert(Port_StoryProgress(6)==ST_DONE_CANON&&Port_StoryClearRank(6)==2&&sRanks[6]==3);
    /* Ordinary selection never silently resumes after a failure; explicit retry remains available. */
    Port_StoryChoose(6);memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);for(i=0;i<N;i++)sTeam[i]=i<sScN?sScTeams[i]:0;fight_start();Port_StoryCheckpoint(3);Port_StoryResult(1,1,1);Port_StoryAcknowledge();assert(Port_StoryHasCheckpoint());Port_StoryChoose(6);assert(!Port_StoryResumeEvent());
    /* Real order is native round reset -> intro -> fight enter: stale actor masks must clear before any intro pose. */
    Port_StoryChoose(11);memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);for(i=0;i<N;i++)sTeam[i]=i<sScN?sScTeams[i]:0;fight_start();sStDownActors=sStGoneActors=0xffff;sStAtkFrames=19;sStAtkEvent=7;sStEventRun=7;sStFormActor=4;
    memset(&ctx,0,sizeof(ctx));assert(st_intro_enter(&ctx));assert(!sStDownActors&&!sStGoneActors&&!sStAtkFrames&&sStEventRun<0&&sStFormActor<0);assert(sMotionId[actor(0)]!=ST_MOTION_PRONE);run(&ctx);
    /* Energy transfer owns a stable ground anchor despite arbitrary root drift and releases its living receiver. */
    Port_StoryChoose(11);fight_start();{int k;for(k=0;k<stEvents[11].count;k++)if(stEvents[11].event[k].choreo==ST_CHOREO_TRANSFER)break;assert(k<stEvents[11].count);
     int by=actor(2),g=actor(0);Port_StoryEnterActor(by);sHp[by]=30000;sHp[g]=0;sStDownActors|=1u<<g;st_attack_begin(11,k,actor(4));Vec4 anchor=sStTransferAnchor;int releases=sReleaseCount[by];
     for(int f=0;f<ST_XFER_DONE+1&&sStAtkFrames;f++){sPos[g].x+=9999;sPos[g].y-=9999;st_attack_update(11);assert(!memcmp(&sPos[g],&anchor,sizeof(anchor)));}
     assert(!sStAtkFrames&&sReleaseCount[by]>releases&&sHp[g]==0);
    }
    return 0;
}

int BtlCharApi_GetChara(int obj){return obj>=0&&obj<N?(sTransformed[obj]?sTransformed[obj]:sScForms[obj]):-1;}





