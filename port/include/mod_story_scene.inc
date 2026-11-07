/* Dragon History chapters in the fight (port/include/mod_story.h), on the versus sequence a chapter runs on:
 *   state 1 (intro talk): the chapter's opening lines in place of the entrance banter;
 *   state 3 (fight): lines said while the fight goes on, each once, when its cue comes (time, health);
 *   state 5 (win talk): the canonical or alternative ending, held through state 6's fade.
 * Native fixed scripted-camera shots framed from the speaking ACTOR (explicit cast references, stCasts: a line's
 * speaker names a cast entry, Port_StoryCastActor gives its battle actor), poses set once per scene, scene-owned
 * visibility for the declared actors only. Scene-only actors (Gohan, Piccolo in chapter 2) are shown only while a
 * scene owns them (Port_StorySceneShow) and are parked out of the fight otherwise. The lines have no recorded voice:
 * their text is the port's subtitle (Port_StorySubtitle). START skips a scene as it skips the native ones.
 * Included by src/battle/btl_seq.c; game thread only. */
#include "mod_story.h"
#include "mod_story_presentation.h"
#include "plat_songs.h"
#include "sys/math3d.h"
#include "battle/stg_a.h"
extern void BtlCtrl_PlayMotion(s32 side,s32 motion,s32 loop,f32 blend);
extern void BtlCtrl_StopMotion(s32 side);
extern void BtlCtrl_SetPos(s32 side,Vec4 *pos);
extern void BtlCtrl_SetRot(s32 side,Vec4 *rot);
extern void BtlCharApi_GetPos(s32 obj,Vec4 *pos);
extern void BtlCharApi_GetRot(s32 obj,Vec4 *rot);
extern f32 BtlCharApi_GetHeight(s32 obj);
extern f32 BtlCharApi_GetGroundY(s32 obj);
extern s32 BtlStage_IsReady(void);
extern void BtlFacade_SetFixedCamera(Vec4 *pos,Vec4 *rot);
extern void BtlFacade_ClearFixedCamera(void);
extern s32 Port_FighterObjId(s32 i);
extern s32 Port_Team(s32 i);
extern void BtlObj_SetPartVisible(void *obj, u32 part, s32 on);
extern int Port_StoryActive(void), Port_StoryChapter(void);
extern void Port_StorySubtitle(int chapter, int set, int line);
extern void Port_StoryLogStats(int chapter, int side, int obj, int health, int healthMax);
extern void Port_StoryLogBallSearch(int chapter, int holders);
extern int Port_StoryCastActor(int cast), Port_StorySceneActor(int actor);
extern void Port_StorySceneShow(int actor, int on);
extern s32 BtlStage_CountItemHolders(void);
extern s32 BtlCharApi_GetHp(s32 objId);
extern s32 BtlCharApi_GetHpMax(s32 objId);
extern BattleResult *Battle_GetResult(void);
extern void Port_StoryEventDone(int event);
extern unsigned Port_StoryEvents(void);

#define ST_STEP_BEAT 40 /* BtlSeqTalkCtx.step of a story scene: start a line */
#define ST_STEP_HOLD 41 /*                                       hold it */
#define ST_GOHAN_FLIGHT 24 /* frames of the headbutt tracking shot */
#define ST_MOTION_IDLE 0x181
#define ST_ANIM_HIT 0xB3       /* native in-place heavy hit reaction (action 0xC2, btl_act_a.c BtlAct_HitStaggerHandler) */
#define ST_MOTION_WOUNDED 0x184
#define ST_MOTION_PRONE 0xD5 /* native grounded down-rest loop, never a lose/get-up pose */
#define ST_MOTION_TAKEOFF 0x18
#define ST_MOTION_FLIGHT 0x19
#define ST_MOTION_LIMP 0xC3 /* the held / dragged loop of the native carry pair (btl_act_h_b.c BtlAct_DraggedDown) */

typedef struct StSceneRun {
    s32 chapter, set, index, count, actor, winner, leaves, frames, active, departing, finishing, bridge;
    s32 cast[ST_ACTORS];   /* battle actor of each cast entry, -1 absent */
    u32 actors;            /* actors this scene owns (visibility, scene motions) */
    s32 rival, carried, carrier, arriving;
    Vec4 departure,exitEye,exitRot,arrival;
    Vec4 endPos[ST_ACTORS];
    f32 cameraFloor;
    s32 cameraActor;
    s32 elapsed,shot,shotActor,shotLine;
} StSceneRun;
static StSceneRun sStRun;
static u32 sStCuesDone;
static s32 sStFightFrames, sStCueFrames;
/* Authored R3 events (st_event_update below): the offered / running event, its scene and the beam that follows. */
static BtlSeqTalkCtx sStEventCtx;
static s32 sStEventOffer = -1, sStEventRun = -1, sStEventPressed, sStEventFrames, sStEventHit;
static u32 sStEventsDone;
static s32 sStBeamFrames, sStBeamHp, sStBeamLead, sStBeamRival, sStBeamPartner;
static Vec4 sStEventFrom, sStEventTo, sStBeamPos, sStBeamRot, sStBeamLeadPos, sStBeamLeadRot;
static s32 sStBeamLanded, sStSwitchActor = -1, sStSwitchTeam;
/* Scene requests are delivered after BeginFrame clears the native FX/SE queues. */
static int sStGohanFrames;
static unsigned sStPresentation[16];
static int sStAuraActive[16],sStBeamChargeActive[16];
static void st_presentation_queue(int actor,unsigned bits){if(actor>=0&&actor<16)sStPresentation[actor]|=bits;}
static s32 st_down(s32 actor);static s32 st_hp(s32 actor);
static s32 st_lead_now(s32 ch);
/* Generic story events: actors an event knocked out (shown down in the ending) or took out for good, and the
   scene-only cast an event shows (bit = battle actor / cast index). */
static u32 sStDownActors, sStGoneActors, sStEventShow;
/* The departure shot of several actors (StCast.departMask): who flies, and from where. */
static u32 sStDepartActors;
static Vec4 sStDepartFrom[ST_ACTORS];
/* Chapter 4's moon shot: frames since its lines ended (0 = idle), the shooter, the Great Ape and his HP at the beam. */
static s32 sStMoonTailAt;
static s32 sStMoonFrames, sStMoonShooter = -1, sStMoonTarget = -1, sStMoonHp;
/* Phased chapters: the fight frame the previous event ended on (ST_TRIG_AFTER). */
static s32 sStLastEventEnd;
/* Script section 18 inside a phased battle: the event whose alternative ends the battle (-1 none), and whether the
   running event plays as its phase's alternative. */
static s32 sStAltEnd = -1, sStEventAlt;
/* An authored attack after an event's lines: frames since it began (0 idle), the event, attacker, counter, target, the
   target's HP when it was fired. */
static s32 sStAtkFrames, sStAtkEvent = -1, sStAtkBy = -1, sStAtkBack = -1, sStAtkAt = -1, sStAtkHp;
static s32 sStAtkContact,sStAtkRetry,sStAtkRival,sStAtkLost,sStShieldWait;
static Vec4 sStShieldGohanAnchor,sStTransferAnchor;
static int sStEndingExecutionHit,sStEndingFall;
static Vec4 sStEndingVegetaAnchor;
static Vec4 sStEndingDrawAnchor[2];static int sStEndingAnchorReady;
static Vec4 sStNappaIntroAnchor;static int sStNappaIntroAnchorReady;
/* A scene can have queued action4 without entering it yet. Release the whole
   live CH7 cast after its final action dispatch, not just the last shot's mask. */
static int sStCh7ReleaseFrames;
static unsigned sStPairChargeReady;
/* Choreographed attacks (StEvent.choreo): the native technique has been fired (a cling's hold is over), and the target
   is free again (a tail cut has landed). */
static s32 sStAtkFired,sStAtkFree;
/* CH8 (Juan): Goku's "Kaioken times four!" is said DURING the live beam struggle, not after it. */
static s32 sStX4Wait,sStX4Frames,sStX4Said;
/* CH8 Spirit Bomb (Codex spec: throw, miss and return are separate beats): event lines 1..3 follow the real ball. */
static s32 sStSbLine,sStSbLineFrames;
/* 1 once the ball reported through a homing path (it can be turned round); a straight beam never does. */
static s32 sStSbSteered;
/* straight ball: its previous position and closest distance to Gohan so far (the bat happens at closest approach) */
static Vec4 sStSbPrev;static f32 sStSbBest;
/* Actors whose tail was cut in this fight: its model parts stay hidden (native renderer gate, bobj_a.c). */
static u32 sStTailCut;
/* A Great Ape that turned back mid-fight (CH8 Vegeta after the cut, Gohan at the end) stays in the native tired loop a moment. */
#define ST_MOTION_TIRED 1
#define ST_TIRED_FRAMES 75
static s32 sStTiredActor=-1,sStTiredFrames;
static s32 sStEventPost,sStIntroFormFrames;
extern s32 Port_StoryUseTechnique(s32,s32);
/* A scene-only interlude has ended its battle. */
static s32 sStInterludeEnded;
/* ST_CHOREO_CLASH: the native clash has been seen running. */
static s32 sStClashSeen;
extern s32 Port_StoryForceClash(s32 a, s32 b); /* btl_char_flag_clash.c: the pair into the trading-blows clash */
/* ST_CHOREO_SPIRITBOMB: stage (0 wind-up, 1 at Vegeta, 2 dodged: on to Gohan, 3 sent back), Krillin, Gohan, the actor
   not an enemy while he dodges, a rebound to apply to the ball, its position this frame and whether it reported. */
static s32 sStSbStage,sStSbThrower=-1,sStSbGohan=-1,sStSbDodge=-1,sStSbFlip,sStSbSeen,sStSbDodgeFrames,sStSbImpactFrames;
static Vec4 sStSbBall;
static Vec4 sStSbDodgePos;
extern int Port_ClashBusy(void);extern s32 gPortClash[];
/* Nappa shields scene: Break Cannon (ultimate), not the short-range Volcano Explosion. */
static s32 st_attack_slot(s32 ch,s32 event){return ch==6&&event==7?4:stEvents[ch].event[event].attackTech;}
extern void Port_Dbg3(const char*,int,int,int,int);
/* A native hit is reported with its actual source and victim. Other fighters cannot complete this event. */
void Port_StoryAttackContact(int by,int at,int kind){
 int ch=Port_StoryChapter();const StEvent*ev;
 if(ch==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&by==Port_StoryCastActor(9)&&at==Port_StoryCastActor(8)&&kind==0x102){
  if(!sStEndingExecutionHit){sStEndingExecutionHit=1;BtlCtrl_PlayMotion(at,0xB3,0,.05f);Port_Dbg3("story execution native contact",by,at,kind,sStRun.frames);}return;
 }
 if(ch<0||ch>=ST_COUNT||sStAtkFrames<1||sStAtkEvent<0)return;
 ev=&stEvents[ch].event[sStAtkEvent];
 if(ev->choreo==ST_CHOREO_SPIRITBOMB&&sStSbStage!=3)return; /* the first pass must not complete the attack */
 {extern char*getenv(const char*);if(ch==6&&(sStAtkEvent==7||sStAtkEvent==8)&&getenv("BT3_STORY_WAVE_PROBE"))Port_Dbg3("story authored native contact",by,at,kind,sStAtkFrames);}
 if(by==sStAtkBy&&at==sStAtkAt&&kind==0x100+st_attack_slot(ch,sStAtkEvent)){
  /* Action4's bracing pose deliberately blocks native forced reactions.
     Change to the native heavy-hit motion once, at REAL beam contact. */
  if(!sStAtkContact&&ch==6&&sStAtkEvent==7)BtlCtrl_PlayMotion(at,0xB3,0,.05f);
  /* Keep Nappa braced through the Chiaotzu charge; only native contact frees him. */
  /* The first contact admits a paired native rush, not its final explosion.
     Forcing action4 here evicts Yamcha from the victim track while the
     Saibaman continues hugging the now-empty attachment point. */
  if(ev->choreo==ST_CHOREO_CLING)sStAtkFree=1;
  /* The Saibaman execution is also authored: hold its idle pose so CPU evasion
     cannot spoil Vegeta's scripted strike, then show the hit on real contact. */
  if(ev->choreo==ST_CHOREO_EXECUTE){sStAtkFree=1;BtlCtrl_PlayMotion(at,ST_ANIM_HIT,0,.05f);}
  sStAtkContact=1;
 }
 if(by==sStAtkBack&&at==sStAtkBy&&kind==0x100+ev->counterTech)sStAtkLost=1;
}
/* Both native charge loops must be visible before either authored beam fires.
   Return0 outside this exact Story pair,1 hold,2 release both. */
int Port_StoryBeamChargeGate(int actor){
 unsigned pair;
 if(Port_StoryChapter()<0||sStAtkFrames<1||sStAtkBack<0||
    (actor!=sStAtkBy&&actor!=sStAtkBack))return 0;
 sStPairChargeReady|=1u<<actor;pair=(1u<<sStAtkBy)|(1u<<sStAtkBack);
 return (sStPairChargeReady&pair)==pair?2:1;
}
#define ST_ATTACK_FRAMES 600 /* native ultimates may have a long wind-up; a watchdog is never a hit */
extern void Port_StoryCheckpoint(int event);
extern int Port_StoryResumeEvent(void);
extern void Port_Dbg3(const char*,s32,s32,s32,s32);
extern int Port_StoryResourceIO(int actor,int write,int*values),Port_StoryReserveHealth(int actor),Port_StoryFoundBallValue(void);
extern void Port_StoryFoundBall(int ball);
static s32 st_obj(s32),st_chapter(void),st_event_ready(s32);
static void st_set_hp(s32,s32);
static void st_face(s32,s32);
extern s32 gPortTarget[];
extern void Port_StoryEnterActor(int),Port_StoryExitActor(int,int);
extern void BtlCtrl_SetHidden(s32 player); /* (the Spirit Bomb helper is never seen) */
typedef struct StActorSave {int valid,v[9],parked,reserve;Vec4 pos,rot;} StActorSave;
static struct {int chapter,event,difficulty,dense,ball,frames,last;unsigned events,cues,down,gone;int switchActor,switchTeam;StActorSave actor[ST_ACTORS];} sStCheckpoint={.chapter=-1};
static int sStRestoreFrames;
static int sStFormActor=-1,sStFormTarget,sStFormWait;
static void st_checkpoint_capture(int chapter,int event){
 int i,a;
 if(sStCheckpoint.chapter==chapter&&sStCheckpoint.event==event&&Port_StoryResumeEvent()==0&&sStCheckpoint.frames>0)return;
 sStCheckpoint.chapter=chapter;sStCheckpoint.event=event;sStCheckpoint.difficulty=Port_StoryDifficulty();sStCheckpoint.dense=Port_Setting("story_density_full",0);
 sStCheckpoint.ball=Port_StoryFoundBallValue();sStCheckpoint.frames=sStFightFrames;sStCheckpoint.last=sStLastEventEnd;
 sStCheckpoint.events=sStEventsDone;sStCheckpoint.cues=sStCuesDone;sStCheckpoint.down=sStDownActors;sStCheckpoint.gone=sStGoneActors;
 sStCheckpoint.switchActor=sStSwitchActor;sStCheckpoint.switchTeam=sStSwitchTeam;
 for(i=0;i<ST_ACTORS;i++){StActorSave*v=&sStCheckpoint.actor[i];a=Port_StoryCastActor(i);v->valid=a>=0&&Port_StoryResourceIO(a,0,v->v);
 if(v->valid){v->parked=Port_StorySceneParked(a);v->reserve=Port_StoryReserveHealth(a);BtlCharApi_GetPos(st_obj(a),&v->pos);BtlCharApi_GetRot(st_obj(a),&v->rot);}}
 Port_StoryCheckpoint(event);
}
static int st_checkpoint_restore(void){
 int i,a,ready=1,ch=st_chapter(),v[9];
 if(!sStRestoreFrames)return 0;
 for(i=0;i<ST_ACTORS;i++)if(sStCheckpoint.actor[i].valid){a=Port_StoryCastActor(i);if(a>=0&&Port_StoryResourceIO(a,0,v)&&v[0]!=sStCheckpoint.actor[i].v[0])ready=0;}
 if(!ready&&++sStRestoreFrames<150)return 1;
 if(!ready){sStRestoreFrames=0;Port_Dbg3("story checkpoint form restore failed",ch,0,0,0);BattleResult_Set(2,1);return 1;}
 for(i=0;i<ST_ACTORS;i++){StActorSave*v=&sStCheckpoint.actor[i];a=Port_StoryCastActor(i);if(!v->valid||a<0)continue;
 if(v->parked)Port_StoryExitActor(a,v->reserve);else Port_StoryEnterActor(a);
 Port_StoryResourceIO(a,1,v->v);if(!v->parked){BtlCtrl_SetPos(a,&v->pos);BtlCtrl_SetRot(a,&v->rot);}}
 sStEventsDone=sStCheckpoint.events;sStCuesDone=sStCheckpoint.cues;sStDownActors=sStCheckpoint.down;sStGoneActors=sStCheckpoint.gone;
 sStFightFrames=sStCheckpoint.frames;sStLastEventEnd=sStCheckpoint.last;sStSwitchActor=sStCheckpoint.switchActor;sStSwitchTeam=sStCheckpoint.switchTeam;
 Port_StoryFoundBall(sStCheckpoint.ball);sStRestoreFrames=0;return 1;
}

static s32 st_rival_now(s32 chapter);
static void st_event_effects(s32 chapter, const StEvent *ev, s32 rival);
static int st_event_before(int,int);
extern void Port_StoryEnterActor(int actor), Port_StoryExitActor(int actor, int hp), Port_StoryHandControl(int to);
extern int Port_StorySceneParked(int actor), Port_StoryReviveActor(int actor, int hp);
extern s32 BtlCtrl_Transform(s32 player, s32 id);
extern void Port_StoryGrantForm(int actor, int target);
#define ST_MOON_SKY_FRAMES 46   /* the shot into the sky, the flash at its middle */
#define ST_MOON_BEAM_FRAMES 150 /* longest wait for the tail beam to land */
/* ST_CHOREO_CLING: frames to reach the back, frame the native technique fires (the hold in between); where on the
   target (in its heights: behind, up) */
#define ST_CLING_FLY 4
#define ST_CLING_HOLD 12
#define ST_CLING_BACK (-0.22f)
#define ST_CLING_UP 0.3f
/* ST_CHOREO_TAILCUT: where the sneak starts and the tail is (in the giant's heights), the stroke, the end */
#define ST_TAIL_FROM (-0.9f)
#define ST_TAIL_AT (-0.32f)
#define ST_TAIL_UP 0.28f
#define ST_TAIL_SNEAK 16
#define ST_TAIL_SLASH 18
#define ST_TAIL_DONE 46
/* ST_CHOREO_TRANSFER: Krillin reaches Goku's side, receives the energy, the end; where (Goku's heights, to his right) */
#define ST_XFER_REACH 20
#define ST_XFER_GIVE 70
#define ST_XFER_DONE 84
#define ST_XFER_SIDE 0.45f
/* ST_CHOREO_SPIRITBOMB: the throw, Gohan's place beyond Vegeta, the dodge (distance, sidestep), the rebound distance (all
   in heights), the longest wait (a native ultimate's wind-up and a slow ball) */
#define ST_SB_FIRE 6
#define ST_SB_GOHAN_BEYOND 2.2f
#define ST_SB_DODGE_AT 1.4f
#define ST_SB_DODGE_FRAMES 10
#define ST_SB_DODGE_SIDE 1.2f
#define ST_SB_RETURN_AT 0.35f
#define ST_SB_LIMIT 1200
/* ST_CHOREO_CLASH: the gap between the two (the target's heights in front of the attacker), the longest wait */
#define ST_CLASH_GAP 0.6f
#define ST_CLASH_LIMIT 600
#define ST_ANIM_SWORD 0x3B /* native rush combo step (animations 0x37 + step, btl_tech_a.h): Yajirobe's sword stroke */
extern void Port_StoryRemoveActor(int actor);
extern int Port_SequencePartner(int actor);

/* Only the declared actors of the running cutscene may bypass KO model hiding and take scene motions.
   HP, result, revival eligibility and every other fighter remain untouched. */
int Port_StoryHiddenAttackHelper(int actor){
 return 0; /* The throw now belongs to Krilin; no extra fighter is admitted. */
}
int Port_StoryTechniqueDonor(int actor){
 int ch=st_chapter();
 return ch==11&&sStAtkFrames>0&&sStAtkEvent>=0&&actor==sStSbThrower&&
  stEvents[ch].event[sStAtkEvent].choreo==ST_CHOREO_SPIRITBOMB?Port_StoryCastActor(0):-1;
}
int Port_StoryMotionDonor(int actor){
 int donor=Port_StoryTechniqueDonor(actor);
 if(donor<0&&actor==sStAtkAt&&sStSbStage==3)donor=Port_StoryTechniqueDonor(sStSbThrower);
 return donor;
}
int Port_StoryNativeImpactCamera(int obj){
 return sStAtkContact&&sStSbStage==3&&Port_StoryTechniqueDonor(sStSbThrower)>=0&&obj==st_obj(sStSbThrower);
}
int Port_StoryCameraActor(void){
 return sStAtkContact&&sStSbStage==3&&Port_StoryTechniqueDonor(sStSbThrower)>=0?sStSbThrower:-1;
}
int Port_StoryAuthoredVictim(int by,int at,int slot){
 int ch=st_chapter();
 return ch>=0&&sStAtkFrames>0&&(!sStAtkFree||(ch==11&&sStSbStage==3&&Port_StoryTechniqueDonor(by)>=0))&&sStAtkBack<0&&sStAtkEvent>=0&&
  by==sStAtkBy&&at==sStAtkAt&&slot==st_attack_slot(ch,sStAtkEvent);
}
s32 Port_StorySceneActorShown(s32 actor) {
    int ch=st_chapter();
    if(ch<0||actor<0||actor>=32)return 0;
    if(sStRun.active&&(sStRun.actors>>actor&1u))return 1;
    if((sStDownActors>>actor&1u)&&!Port_StorySceneParked(actor))return 1; /* Yamcha/Tien/Piccoro bodies remain visible. */
    if(Port_StorySceneParked(actor)||(sStGoneActors>>actor&1u))return 0;
    if(sStMoonFrames>0&&(actor==sStMoonShooter||actor==sStMoonTarget))return 1;
    if(sStAtkFrames>0&&sStAtkEvent>=0){const StEvent*e=&stEvents[ch].event[sStAtkEvent];
      if(e->choreo==ST_CHOREO_CLASH)return actor==sStAtkBy||actor==sStAtkAt;
      if(e->choreo==ST_CHOREO_CLING&&!sStAtkFired)return actor==sStAtkBy||actor==sStAtkAt;
      if(e->choreo==ST_CHOREO_TAILCUT||e->choreo==ST_CHOREO_TRANSFER)return actor==sStAtkBy||actor==sStAtkAt;
      return actor!=sStAtkBy&&actor!=sStAtkBack; /* held spectators and the staged target, never interrupt the actual shooter */
    }
    return 0;
}

static s32 st_obj(s32 actor) {
    s32 obj = Port_FighterObjId(actor);
    return obj >= 0 ? obj : BattleSide_GetObjId(actor == 1);
}

static f32 st_height(s32 actor) {
    f32 h = BtlCharApi_GetHeight(st_obj(actor));
    return h > 0.1f && h <= 10000.0f ? h : 10.0f;
}

/* Takes a scene-only actor into the scene (visible, posed) or gives it back to its parking. */
static void st_scene_take(s32 actor, s32 on) {
    void *o;
    if (actor < 0 || actor >= 32) {
        return;
    }
    if (on) {
        sStRun.actors |= 1u << actor;
    } else {
        sStRun.actors &= ~(1u << actor);
    }
    if (!Port_StorySceneActor(actor)) {
        return;
    }
    Port_StorySceneShow(actor, on);
    o = (void *)BtlObj_Get(st_obj(actor)); /* (declared by the including file) */
    if (o != NULL) {
        BtlObj_SetPartVisible(o, 0, on); /* Port_ExtraConsumedUpdate hides it again once parked */
    }
}

static void st_scene_ground_actor(s32 actor);
static void st_stage_actor(s32 a){
 Vec4 pos={0,0,0,1},rot={0,0,0,1};StgGroundHit hit;int zone;
 extern void *BtlChar_Get(s32);extern void BtlChar_Place(void*,Vec4*,Vec4*,s32);
 if(a<0||!BtlStage_IsReady())return;
 if((st_chapter()==6||st_chapter()==11)&&!Port_StorySceneParked(a)&&((sStDownActors>>a&1u)||st_hp(a)<=0)){st_scene_ground_actor(a);return;}
 zone=BtlStage_GetStartPlace(a,&pos,&rot,0);if(zone<0)return;
 hit.y=3.4e38f;{Vec4 q=pos;q.y-=100;
 if(BtlStage_ProbeGround(zone,&q,&hit)>=0&&hit.y>-1e9f&&hit.y<1e9f)pos.y=hit.y;}
 if(BtlChar_Get(a))BtlChar_Place(BtlChar_Get(a),&pos,&rot,zone);
}
/* Pick nearby rocky perches from real collision geometry; keep the original start if no valid higher rock exists. */
static void st_rock_perch(s32 actor){
 Vec4 base={0,0,0,1},rot={0,0,0,1},best,probe;StgGroundHit hit;int zone,j;float h;
 extern void *BtlChar_Get(s32);extern void BtlChar_Place(void*,Vec4*,Vec4*,s32);
 if(actor<0||!BtlStage_IsReady())return;zone=BtlStage_GetStartPlace(actor,&base,&rot,0);if(zone<0)return;
 best=base;h=st_height(actor);
 for(j=0;j<12;j++){float a=j*6.2831853f/12;probe=base;probe.x+=cosf(a)*h*4;probe.z+=sinf(a)*h*4;probe.y-=h*30;hit.y=3.4e38f;
  if(BtlStage_ProbeGround(-1,&probe,&hit)>=0&&hit.y<best.y&&hit.y>base.y-h*20){best=probe;best.y=hit.y;}}
 if(BtlChar_Get(actor))BtlChar_Place(BtlChar_Get(actor),&best,&rot,zone);
}
/* Places `actor` beside `anchor`: forward / right in the anchor's facing, up in native units (Y is DOWN). */
static void st_place_near(s32 actor, s32 anchor, f32 forward, f32 right, f32 up) {
    Vec4 at = {0, 0, 0, 1}, facing = {0, 0, 0, 1}, p;
    f32 h;
    if (actor < 0 || anchor < 0) {
        return;
    }
    h = st_height(anchor);
    BtlCharApi_GetPos(st_obj(anchor), &at);
    BtlCharApi_GetRot(st_obj(anchor), &facing);
    p = at;
    p.x += sinf(facing.y) * forward * h + cosf(facing.y) * right * h;
    p.z += cosf(facing.y) * forward * h - sinf(facing.y) * right * h;
    p.y -= up * h;
    BtlCtrl_SetPos(actor, &p);
    BtlCtrl_SetRot(actor, &facing);
}

/* Re-stage a waterlogged cast or an off-ground defeated body before a shot/event path is computed. Native placement
   is immediate (pose/object/velocity/camera), unlike BtlCtrl_SetPos's next-frame warp.
   Only actual cutscenes call this; underwater gameplay/dialogue cues stay untouched. */
extern void *BtlChar_Get(s32 i);
extern void BtlChar_Place(void*,Vec4*,Vec4*,s32);
extern void BtlChar_ClearFlag(void*,int);
static int st_finite_pos(const Vec4*p){return p->x>-1e9f&&p->x<1e9f&&p->y>-1e9f&&p->y<1e9f&&p->z>-1e9f&&p->z<1e9f;}
static void st_scene_dry_placement(void){
 Vec4 start[ST_ACTORS],rot[ST_ACTORS],at;int zone[ST_ACTORS],actor[ST_ACTORS],i,a,wet=0,offGround=0,safe=-1,hasWater;float water=0;
 const StCast*c=&stCasts[sStRun.chapter];
 if(!BtlStage_IsReady())return;
 hasWater=BtlStage_GetWaterLevel(&water)&&water>-1e9f&&water<1e9f;
 for(i=0;i<c->count;i++){
  a=sStRun.cast[i];actor[i]=a;
  if(a<0||(c->actor[i].flags&ST_ACTOR_SCENE)||Port_StorySceneParked(a))continue;
  BtlCharApi_GetPos(st_obj(a),&at);
  if(!st_finite_pos(&at)||(hasWater&&at.y>water))wet=1;
  if(st_down(a)&&st_finite_pos(&at)){
   float floor=BtlCharApi_GetGroundY(st_obj(a)),tolerance=st_height(a)*.20f;
   if(!(floor>-1e9f&&floor<1e9f)){
    StgGroundHit below;Vec4 probe=at;below.y=3.4e38f;probe.y-=st_height(a)*4;
    if(BtlStage_ProbeGround(-1,&probe,&below)>=0&&below.y>-1e9f&&below.y<1e9f)floor=below.y;
   }
   if(tolerance<2.f)tolerance=2.f;
   if(floor>-1e9f&&floor<1e9f&&(floor-at.y>tolerance||at.y-floor>tolerance))offGround=1;
  }
 }
 if(!wet&&!offGround)return;
 for(i=0;i<c->count;i++){
  zone[i]=-1;if(actor[i]<0||(c->actor[i].flags&ST_ACTOR_SCENE)||Port_StorySceneParked(actor[i]))continue;
  start[i]=(Vec4){0,0,0,1};rot[i]=(Vec4){0,0,0,1};
  zone[i]=BtlStage_GetStartPlace(actor[i],&start[i],&rot[i],0);
  if(zone[i]>=0&&st_finite_pos(&start[i])&&(!hasWater||start[i].y<=water)&&safe<0)safe=i;
 }
 ScrXfade_RequestCapture();ScrXfade_Start(1,0.35f);
 for(i=0;i<c->count;i++){
  void*chr;float h;StgGroundHit hit;Vec4 probe;
  a=actor[i];if(a<0||(c->actor[i].flags&ST_ACTOR_SCENE)||Port_StorySceneParked(a))continue;
  h=st_height(a);
  if(zone[i]<0||!st_finite_pos(&start[i])||(hasWater&&start[i].y>water)){
   if(safe>=0){
    /* A dry start for the rest of the cast; separated, never piled on one actor. */
    start[i]=start[safe];rot[i]=rot[safe];
    start[i].x+=cosf(rot[i].y)*(i+1)*h*2.0f;
    start[i].z-=sinf(rot[i].y)*(i+1)*h*2.0f;
    probe=start[i];probe.y-=h*4;hit.y=3.4e38f;
    zone[i]=BtlStage_ProbeGround(-1,&probe,&hit);
    if(zone[i]>=0&&hit.y>-1e9f&&hit.y<1e9f&&(!hasWater||hit.y<=water))start[i].y=hit.y;
    else if(hasWater)start[i].y=water-h*.5f; /* water-only/offshore fallback: hover safely above the surface */
   }else{
    if(zone[i]<0||!st_finite_pos(&start[i]))BtlCharApi_GetPos(st_obj(a),&start[i]);
    if(!st_finite_pos(&start[i]))start[i]=(Vec4){0,0,0,1};
    if(hasWater)start[i].y=water-h*.5f;
    else {float floor=BtlCharApi_GetGroundY(st_obj(a));if(floor>-1e9f&&floor<1e9f)start[i].y=floor;}
   }
  }
  if(offGround&&st_down(a)){
   /* Native starts can themselves be airborne: put the defeated actor's feet on the floor there. */
   probe=start[i];probe.y-=h*4;hit.y=3.4e38f;
   {int groundZone=BtlStage_ProbeGround(-1,&probe,&hit);
    if(groundZone>=0&&hit.y>-1e9f&&hit.y<1e9f&&(!hasWater||hit.y<=water)){zone[i]=groundZone;start[i].y=hit.y;}}
  }
  if(!st_finite_pos(&rot[i]))rot[i]=(Vec4){0,0,0,1};
  chr=(void*)BtlChar_Get(a);if(!chr)continue;
  BtlChar_ClearFlag(chr,0xFC);BtlChar_ClearFlag(chr,0xFD);
  BtlChar_Place(chr,&start[i],&rot[i],zone[i]);
 }
 sStRun.cameraActor=-1;
}

/* Ground visible reserves too: a parked position must never become a camera anchor. */
static void st_pose(s32 actor);
/* WIN disables Stage_IsReady for simulation, although the ending still owns
   the loaded map. Permit only synchronous geometry queries in this CH7 scene;
   restore the simulation flag before returning (never advance a frame here). */
static u32 st_scene_geometry_open(void){
 u32 held=0;
 if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON){held=Battle_GetWork()->flags&0x2000u;Battle_GetWork()->flags&=~held;}
 return held;
}
static void st_scene_geometry_close(u32 held){Battle_GetWork()->flags|=held;}
static void st_scene_ground_actor(s32 actor){
 Vec4 p,probe;StgGroundHit hit;float water,h;int zone;u32 held=st_scene_geometry_open();
 if(actor<0||!BtlStage_IsReady()){st_scene_geometry_close(held);return;}
 h=st_height(actor);BtlCharApi_GetPos(st_obj(actor),&p);
 if(!st_finite_pos(&p)){st_stage_actor(actor);BtlCharApi_GetPos(st_obj(actor),&p);}
 probe=p;probe.y-=h*12.f+100.f;hit.y=3.4e38f;
 zone=BtlStage_ProbeGround(-1,&probe,&hit);
 if(zone>=0&&hit.y>-1e9f&&hit.y<1e9f)p.y=hit.y;
 if(BtlStage_GetWaterLevel(&water)&&water>-1e9f&&water<1e9f&&p.y>water){
  int i;Vec4 q,rot;int found=0;
  for(i=0;i<stCasts[sStRun.chapter].count;i++){
   int other=sStRun.cast[i];if(other<0||!(sStRun.actors>>other&1u))continue;
   if(BtlStage_GetStartPlace(other,&q,&rot,0)>=0&&st_finite_pos(&q)&&q.y<=water){p=q;found=1;break;}}
  if(!found)p.y=water-h*.3f;
 }
 BtlCtrl_SetPos(actor,&p);
 st_scene_geometry_close(held);
}
/* Valid individual floors can still be different cliff tiers. Choreographed
   close pairs need two nearby floor samples at compatible heights. Search the
   local patch; never force a shared Y that buries an actor in a hillside. */
static int st_scene_flat_pair_query(int a,int b,float gap){
 Vec4 center,pa,pb,probe;StgGroundHit hit;float h,t,ra;int ring,dir;
 if(st_chapter()!=6||a<0||b<0||!BtlStage_IsReady())return 0;
 BtlCharApi_GetPos(st_obj(a),&center);h=st_height(a);if(st_height(b)>h)h=st_height(b);
 for(ring=0;ring<4;ring++)for(dir=0;dir<16;dir++){
  t=dir*6.2831853f/16;ra=ring*h*2.f;pa=center;
  pa.x+=cosf(t+.7f)*ra;pa.z+=sinf(t+.7f)*ra;probe=pa;probe.y-=st_height(a)*12.f+100.f;hit.y=3.4e38f;
  if(BtlStage_ProbeGround(-1,&probe,&hit)<0||hit.y<=-1e9f||hit.y>=1e9f)continue;pa.y=hit.y;
  pb=pa;pb.x+=sinf(t)*st_height(a)*gap;pb.z+=cosf(t)*st_height(a)*gap;probe=pb;probe.y-=st_height(b)*12.f+100.f;hit.y=3.4e38f;
  if(BtlStage_ProbeGround(-1,&probe,&hit)<0||hit.y<=-1e9f||hit.y>=1e9f)continue;pb.y=hit.y;
  if(fabsf(pa.y-pb.y)>h*.15f)continue;
  BtlCtrl_SetPos(a,&pa);BtlCtrl_SetPos(b,&pb);
  /* Confirm through the actual placement query too: FindZoneNear depends on
     probe height, so a ray from far outside the map can select another tier. */
  st_scene_ground_actor(a);st_scene_ground_actor(b);BtlCharApi_GetPos(st_obj(a),&pa);BtlCharApi_GetPos(st_obj(b),&pb);
  if(fabsf(pa.y-pb.y)>h*.15f)continue;
  st_face(a,b);st_face(b,a);return 1;
 }
 Port_Dbg3("story flat pair unavailable",a,b,0,0);return 0;
}
static int st_scene_flat_pair(int a,int b,float gap){
 u32 held=st_scene_geometry_open();int result=st_scene_flat_pair_query(a,b,gap);st_scene_geometry_close(held);return result;
}
/* Gather once per actual cutscene, never on fighting subtitle cues or every spoken line. */
static void st_scene_formation(void){
 int i,a,lead=-1,enemy=-1,rank[2]={0,0};float h;
 if(!BtlStage_IsReady())return;
 for(i=0;i<stCasts[sStRun.chapter].count;i++){
  a=sStRun.cast[i];if(a<0||!(sStRun.actors>>a&1u))continue;
  if(sStRun.chapter==6&&st_down(a)){st_scene_ground_actor(a);continue;}
  if(stCasts[sStRun.chapter].actor[i].team){if(enemy<0||st_down(enemy))enemy=a;}
  else if(lead<0||st_down(lead))lead=a;
 }
 if(lead<0)lead=enemy;if(lead<0)return;
 st_stage_actor(lead);st_scene_ground_actor(lead);
 h=st_height(lead);if(enemy>=0&&st_height(enemy)>h)h=st_height(enemy);
 if(enemy>=0&&enemy!=lead){
  st_place_near(enemy,lead,7.f*h/st_height(lead),0,0);st_scene_ground_actor(enemy);
  st_face(lead,enemy);st_face(enemy,lead);
 }
 for(i=0;i<stCasts[sStRun.chapter].count;i++){
  int team,anchor,other,n;float right,back;
  a=sStRun.cast[i];if(a<0||a==lead||a==enemy||!(sStRun.actors>>a&1u))continue;
  if(sStRun.chapter==6&&st_down(a)){st_scene_ground_actor(a);continue;}
  team=stCasts[sStRun.chapter].actor[i].team?1:0;anchor=team?enemy:lead;
  other=team?lead:enemy;if(anchor<0)anchor=lead;
  n=rank[team]++;right=(n&1?-1.f:1.f)*(1.35f+1.25f*(n/4));back=-.85f-1.3f*((n/2)%2);
  st_place_near(a,anchor,back,right,0);st_scene_ground_actor(a);
  if(other>=0&&other!=a)st_face(a,other);
 }
}
/* Lookout guides observe behind the fighters, never in a combat formation slot.
   Search only on scene admission, not each frame or each subtitle. */
static int st_scene_observer_place(s32 actor,s32 anchor){
 int i,j,ch=sStRun.chapter,kind=-1;float h;
 if(ch!=5||actor<0||anchor<0)return 0;
 for(i=0;i<stCasts[ch].count;i++)if(sStRun.cast[i]==actor){kind=stCasts[ch].actor[i].chara;break;}
 if(kind!=187&&kind!=280)return 0;
 h=st_height(actor);if(st_height(anchor)>h)h=st_height(anchor);
 for(j=0;j<16;j++){
  Vec4 p,q;int clear=1;float back=-4.f-2.f*(j/2),right=(kind==187?-1.f:1.f)*(3.f+2.f*(j&1));
  st_place_near(actor,anchor,back*h/st_height(anchor),right*h/st_height(anchor),0);
  BtlCharApi_GetPos(st_obj(actor),&p);
  for(i=0;i<stCasts[ch].count;i++){
   int other=sStRun.cast[i];float dx,dz,gap;
   if(other<0||other==actor||!(sStRun.actors>>other&1u))continue;
   BtlCharApi_GetPos(st_obj(other),&q);dx=p.x-q.x;dz=p.z-q.z;gap=.65f*(h+st_height(other));
   if(dx*dx+dz*dz<gap*gap){clear=0;break;}
  }
  if(clear)return 1;
 }
 return 1;
}
/* A declared speaker is visible and grounded before taking its shot. */
static void st_scene_admit_speaker(s32 actor){
 int i,anchor=-1,other=-1,team=0;
 if(actor<0||actor>=ST_ACTORS||((sStGoneActors>>actor&1u)&&!(sStDownActors>>actor&1u)))return;
 if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+2&&actor==Port_StoryCastActor(8)){
  /* Nappa is still a parked reserve during these lines. Use the real opposing
     start, not an extra-fighter/observer placement relative to parked Vegeta. */
  Vec4 p,rot;int zone=BtlStage_GetStartPlace(1,&p,&rot,0);
  st_scene_take(actor,1);
  if(zone>=0)BtlCtrl_SetPos(actor,&p);
  st_scene_ground_actor(actor);other=st_lead_now(6);if(other>=0)st_face(actor,other);
  st_pose(actor);BtlCharApi_GetPos(st_obj(actor),&sStNappaIntroAnchor);sStNappaIntroAnchorReady=1;
  return;
 }
 if(sStRun.actors>>actor&1u){
  if(sStRun.chapter==6){
   st_scene_ground_actor(actor);
   other=actor==Port_StoryCastActor(8)||actor==Port_StoryCastActor(9)?st_lead_now(6):Port_StoryCastActor(8);
   if(other>=0&&other!=actor&&!Port_StorySceneParked(other))st_face(actor,other);
  }
  return;
 }
 for(i=0;i<stCasts[sStRun.chapter].count;i++)if(sStRun.cast[i]==actor){team=stCasts[sStRun.chapter].actor[i].team?1:0;break;}
 if(i==stCasts[sStRun.chapter].count)return;
 for(i=0;i<stCasts[sStRun.chapter].count;i++){
  int a=sStRun.cast[i],t=stCasts[sStRun.chapter].actor[i].team?1:0;
  if(a<0||a==actor||!(sStRun.actors>>a&1u))continue;
  if(t==team&&anchor<0)anchor=a;else if(t!=team&&other<0)other=a;
 }
 st_scene_take(actor,1);st_stage_actor(actor);
 if(anchor>=0&&!(sStRun.chapter==6&&st_down(actor))&&!st_scene_observer_place(actor,anchor))st_place_near(actor,anchor,-.65f,1.45f,0);
 st_scene_ground_actor(actor);if(other>=0)st_face(actor,other);st_pose(actor);
}
/* Native head node46 is already evaluated for rendering. Reject stale parked/warp
   coordinates; the fallback is used only until the first fresh skeleton sample. */
extern void BtlCharApi_GetNodePos(s32,s32,Vec4*);
static Vec4 st_scene_face(s32 actor){
 Vec4 root,head;float h=st_height(actor),x,y,z,d;
 BtlCharApi_GetPos(st_obj(actor),&root);head=root;
 BtlCharApi_GetNodePos(st_obj(actor),0x2E,&head);
 x=head.x-root.x;y=head.y-root.y;z=head.z-root.z;d=x*x+y*y+z*z;
 if(!st_finite_pos(&head)||d<h*h*.01f||d>h*h*4||
    (sStRun.chapter==6&&!st_down(actor)&&(y>-h*.15f||y<-h*1.3f||x*x+z*z>h*h*.4f))){head=root;head.y-=h*(sStRun.chapter==6?.7f:.45f);}
 head.w=1;return head;
}
/* Every shot, including wide/interlude shots, obeys terrain/water clearance. */
static void st_camera_finish(Vec4 eye,Vec4 at,float h,int actor,int kind){
 Vec4 rot={0,0,0,1};float dx,dz;
 if(sStRun.cameraActor!=actor||sStRun.frames%6==0){
  float floor=BtlCharApi_GetGroundY(st_obj(actor));
  if(!(floor>-1e9f&&floor<1e9f))floor=at.y;
  if(BtlStage_IsReady()){
   StgGroundHit hit;Vec4 probe=eye;probe.y-=h*12+100;hit.y=3.4e38f;
   if(BtlStage_ProbeGround(-1,&probe,&hit)>=0&&hit.y>-1e9f&&hit.y<1e9f)floor=hit.y;
  }
  sStRun.cameraFloor=floor;sStRun.cameraActor=actor;
 }
 if(eye.y>sStRun.cameraFloor-h*.4f)eye.y=sStRun.cameraFloor-h*.4f;
 {float water;if(BtlStage_IsReady()&&BtlStage_GetWaterLevel(&water)&&water>-1e9f&&water<1e9f&&eye.y>water-h*.4f)eye.y=water-h*.4f;}
 dx=at.x-eye.x;dz=at.z-eye.z;
 rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));
 {static int trace=-1;extern char*getenv(const char*);
  if(trace<0)trace=getenv("BT3_STORY_SCENE_LOG")!=0;
  if(trace&&(sStRun.shot!=kind||sStRun.shotActor!=actor||sStRun.shotLine!=sStRun.index))Port_Dbg3("story camera shot",sStRun.chapter,actor,kind,sStRun.index);}
 sStRun.shot=kind;sStRun.shotActor=actor;sStRun.shotLine=sStRun.index;
 BtlFacade_SetFixedCamera(&eye,&rot);sStRun.exitEye=eye;sStRun.exitRot=rot;
}
/* Establishing views, close faces, reverse shoulders, solo charge and Gohan tracking. */
static void st_scene_camera(void){
 Vec4 at={0,0,0,1},facing={0,0,0,1},eye,p;float h,distance;int i,n=0,other=-1,actor=sStRun.actor;
 if(!sStRun.active||sStRun.chapter<0||sStRun.chapter>=ST_COUNT)return;
 if(sStRun.finishing||(sStRun.departing&&sStRun.frames>0)){BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return;}
 if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+2&&actor==Port_StoryCastActor(8)&&sStNappaIntroAnchorReady){
  h=st_height(actor);BtlCharApi_GetRot(st_obj(actor),&facing);
  at=sStNappaIntroAnchor;at.y-=h*.7f;eye=at;
  eye.x+=(sinf(facing.y)*1.4f+cosf(facing.y)*.3f)*h;
  eye.z+=(cosf(facing.y)*1.4f-sinf(facing.y)*.3f)*h;eye.y-=h*.25f;
  st_camera_finish(eye,at,h,actor,1);return;
 }
 if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)&&sStRun.index>=sStRun.count){
  int ape=Port_StoryCastActor(1),pic=Port_StoryCastActor(0);Vec4 q;float ah;
  if(ape>=0&&pic>=0){
   BtlCharApi_GetPos(st_obj(ape),&at);BtlCharApi_GetPos(st_obj(pic),&q);BtlCharApi_GetRot(st_obj(pic),&facing);
   ah=st_height(ape);h=st_height(pic);if(ah<h*5)ah=h*5;
   distance=sqrtf((q.x-at.x)*(q.x-at.x)+(q.z-at.z)*(q.z-at.z));
   at.x=(at.x+q.x)*.5f;at.z=(at.z+q.z)*.5f;at.y=(at.y+q.y)*.5f-ah*.45f;
   eye=at;eye.x+=sinf(facing.y+.7f)*(ah*2.1f+distance);eye.z+=cosf(facing.y+.7f)*(ah*2.1f+distance);eye.y-=ah*.35f;
   st_camera_finish(eye,at,h,ape,0);return;
  }
 }
 if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index<=1){
  int v=sStRun.cast[9],n=sStRun.cast[8];Vec4 vp,np,vr;float h=st_height(v),span;
  if(st_height(n)>h)h=st_height(n);
  BtlCharApi_GetPos(st_obj(v),&vp);BtlCharApi_GetPos(st_obj(n),&np);BtlCharApi_GetRot(st_obj(v),&vr);
  if(sStRun.index==1&&sStRun.frames>40){
   /* Over Vegeta's shoulder, looking at the victim: the blast should not be
      a distant landscape shot merely to fit both complete bodies. */
   float vh=st_height(v);at=np;at.y-=st_height(n)*.45f;
   eye=vp;eye.x+=(-sinf(vr.y)*1.35f+cosf(vr.y)*.8f)*vh;
   eye.z+=(-cosf(vr.y)*1.35f-sinf(vr.y)*.8f)*vh;eye.y-=vh*.8f;
   st_camera_finish(eye,at,vh,v,5);return;
  }
  at=vp;at.x=(vp.x+np.x)*.5f;at.y=(vp.y+np.y)*.5f-h*.55f;at.z=(vp.z+np.z)*.5f;
  span=h*1.5f+fabsf(vp.y-np.y)*.3f;
  eye=at;eye.x+=cosf(vr.y)*span;eye.z-=sinf(vr.y)*span;eye.y-=h*.25f;
  st_camera_finish(eye,at,h,v,5);return;
 }
 if(actor<0||actor>=ST_ACTORS||!(sStRun.actors>>actor&1u)){
  for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];if(a>=0&&(sStRun.actors>>a&1u)){actor=a;break;}}
 }
 if(actor<0||actor>=ST_ACTORS||!(sStRun.actors>>actor&1u))return;
 h=st_height(actor);BtlCharApi_GetPos(st_obj(actor),&at);BtlCharApi_GetRot(st_obj(actor),&facing);
 for(i=0;i<stCasts[sStRun.chapter].count;i++){
  int a=sStRun.cast[i];if(a<0||!(sStRun.actors>>a&1u))continue;n++;
  if(a!=actor&&Port_Team(a)!=Port_Team(actor)&&!st_down(a)&&other<0)other=a;
 }
 if(sStEventRun>=0&&stEvents[sStRun.chapter].event[sStEventRun].kind==ST_EVENT_GOHAN&&actor==(int)sStEventTo.w&&sStGohanFrames>0&&sStGohanFrames<=ST_GOHAN_FLIGHT){
  float x=sStEventTo.x-sStEventFrom.x,z=sStEventTo.z-sStEventFrom.z,len=sqrtf(x*x+z*z);
  if(len>.01f){x/=len;z/=len;}else{x=sinf(facing.y);z=cosf(facing.y);}
  at=st_scene_face(actor);eye=at;eye.x+=(-x*1.9f+z*1.1f)*h;eye.z+=(-z*1.9f-x*1.1f)*h;eye.y-=h*.15f;
  at.x+=x*h*.7f;at.z+=z*h*.7f;st_camera_finish(eye,at,h,actor,4);return;
 }
 if(ST_Interlude(sStRun.chapter)||(n>=4&&sStRun.index==0&&sStRun.set<ST_SET_EVENT0)){
  float radius=0,maxH=h;int count=0;at.x=at.y=at.z=0;
  for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];float ah;if(a<0||!(sStRun.actors>>a&1u))continue;
   BtlCharApi_GetPos(st_obj(a),&p);at.x+=p.x;at.y+=p.y;at.z+=p.z;count++;ah=st_height(a);if(ah>maxH)maxH=ah;}
  if(count){at.x/=count;at.y/=count;at.z/=count;}
  for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];float d;if(a<0||!(sStRun.actors>>a&1u))continue;
   BtlCharApi_GetPos(st_obj(a),&p);d=sqrtf((p.x-at.x)*(p.x-at.x)+(p.z-at.z)*(p.z-at.z));if(d>radius)radius=d;}
  distance=maxH*3.1f+radius*1.45f;eye=at;
  eye.x+=sinf(facing.y+.45f)*distance;eye.z+=cosf(facing.y+.45f)*distance;eye.y-=maxH*1.9f;
  at.y-=maxH*.6f;st_camera_finish(eye,at,maxH,actor,0);return;
 }
 if(sStRun.chapter==2&&sStEventRun>=0&&stEvents[2].event[sStEventRun].kind==ST_EVENT_BEAM&&actor==sStBeamPartner){
  at=st_scene_face(actor);eye=at;eye.x+=(sinf(facing.y)*1.15f+cosf(facing.y)*.45f)*h;eye.z+=(cosf(facing.y)*1.15f-sinf(facing.y)*.45f)*h;
  eye.y-=h*.08f;at.y+=h*.17f;st_camera_finish(eye,at,h,actor,3);return;
 }
 if(sStRun.chapter!=6&&!sStRun.departing&&!st_down(actor)&&other>=0&&(sStRun.index&1)&&
    (sStEventRun<0||stEvents[sStRun.chapter].event[sStEventRun].kind!=ST_EVENT_GOHAN)){
  Vec4 target;float x,z,len,side=(sStRun.index&2)?-1.f:1.f;
  BtlCharApi_GetPos(st_obj(other),&target);x=target.x-at.x;z=target.z-at.z;len=sqrtf(x*x+z*z);
  if(len>h*2.f){x/=len;z/=len;eye=st_scene_face(actor);eye.x+=(-x*.95f+z*.7f*side)*h;eye.z+=(-z*.95f-x*.7f*side)*h;eye.y-=h*.08f;
   target=st_scene_face(other);st_camera_finish(eye,target,h,actor,2);return;}
 }
 distance=h*(sStRun.departing?4.8f:st_down(actor)?2.6f:.72f);
 if(!sStRun.departing&&!st_down(actor)){at=st_scene_face(actor);eye=at;eye.y-=h*.08f;
  eye.x+=sinf(facing.y)*distance+cosf(facing.y)*h*.25f;eye.z+=cosf(facing.y)*distance-sinf(facing.y)*h*.25f;
  st_camera_finish(eye,at,h,actor,1);return;}
 eye=at;eye.x+=sinf(facing.y)*distance+cosf(facing.y)*h*.4f;eye.z+=cosf(facing.y)*distance-sinf(facing.y)*h*.4f;
 eye.y-=h*.9f;at.y-=h*(st_down(actor)?.32f:.78f);st_camera_finish(eye,at,h,actor,sStRun.departing?6:st_down(actor)?5:1);
}

/* The chapter of a story fight on the versus sequence, else -1 (stock mode-1 scripts keep theirs). */
static s32 st_chapter(void) {
    return Battle_GetMode() != 1 && Port_StoryActive() ? Port_StoryChapter() : -1;
}

static s32 st_frames(const char *text) {
    {static int probe=-1;extern char*getenv(const char*);if(probe<0)probe=getenv("BT3_STORY_FAST_PROBE")!=0;if(probe)return 15;}
    return (s32)(ST_LineSeconds(text) * 30.0f);
}

/* The battle actor a line belongs to: its speaker's cast entry, else the rival / the human lead. */
static s32 st_line_actor(const StLine *line) {
    s32 cast = ST_LineCast(sStRun.chapter, line);
    s32 actor = cast >= 0 ? sStRun.cast[cast] : -1;
    if (actor >= 0) {
        return actor;
    }
    return ST_LineSide(sStRun.chapter, line) ? sStRun.rival : 0;
}

/* Whether an actor is shown defeated in the current scene: the losing team in an ending, and the human lead in a
   canonical ending that costs him (StCast.canonLeadDown, chapter 3). */
static s32 st_down(s32 actor) {
    const StCast *c = &stCasts[sStRun.chapter];
    s32 i;
    if(actor>=0&&actor<32&&((sStDownActors>>actor&1u)||(!Port_StorySceneActor(actor)&&BtlCharApi_GetHp(st_obj(actor))<=0)))return 1;
    if (sStRun.set == ST_SET_INTRO || sStRun.set == ST_SET_BRIDGE ||
        (sStRun.set >= ST_SET_EVENT0 && sStRun.set < ST_SET_EVENT0 + ST_EVENTS)) {
        return 0;
    }
    if (sStRun.set == ST_SET_CANON && c->canonLeadDown && actor == sStRun.cast[0]) {
        return 1;
    }
    if (actor >= 0 && actor < 32 && (sStDownActors >> actor & 1u)) {
        return 1; /* a story event knocked him out */
    }
    for (i = 0; i < c->count; i++) { /* scene-only cast shown lying down (hurt, asleep) */
        if (sStRun.cast[i] == actor && (c->actor[i].flags & ST_ACTOR_SCENE)) {
            return ((c->showDown >> i) & 1u) || ((sStRun.set == ST_SET_CANON || sStRun.set == ST_SET_ALT) && ((c->endDown >> i) & 1u));
        }
    }
    if (sStRun.set == ST_SET_CANON && c->canonRivalUp && actor == sStRun.rival) {
        return 0;
    }
    return Port_Team(actor) != sStRun.winner;
}

/* Pose of one actor for the scene: idle when standing (opening, bridge, events, winners), wounded when defeated. */
static void st_pose(s32 actor) {
    s32 standing;
    if (actor < 0) {
        return;
    }
    if((sStRun.chapter==6||sStRun.chapter==11)&&st_down(actor)){st_scene_ground_actor(actor);BtlCtrl_PlayMotion(actor,ST_MOTION_PRONE,1,.15f);return;}
    if(sStRun.chapter==3&&(sStRun.set==ST_SET_CANON||sStRun.set==ST_SET_ALT)&&(actor==sStRun.cast[1]||actor==sStRun.cast[2])){
        BtlCtrl_PlayMotion(actor,ST_MOTION_PRONE,1,.15f);return;
    }
    standing = sStRun.chapter==4&&sStRun.set==ST_SET_CANON ? 1 : !st_down(actor);
    BtlCtrl_PlayMotion(actor, standing ? (actor == sStRun.rival ? ST_MOTION_IDLE : 0) : ST_MOTION_WOUNDED, standing, 0.15f);
}

/* A long chapter's current hero, not its first (possibly parked) cast member. */
static s32 st_lead_now(s32 ch){int i,a;
    if(!ST_Phased(ch))return Port_StoryCastActor(0);
    for(i=0;i<stCasts[ch].count;i++){if(stCasts[ch].actor[i].team)continue;a=Port_StoryCastActor(i);
        if(a>=0&&!Port_StorySceneParked(a)&&!(sStGoneActors>>a&1u)&&BtlCharApi_GetHp(st_obj(a))>0)return a;}
    return Port_StoryCastActor(0);
}
/* Starts a scene of `count` lines of `set` from line `first`; the talk context runs it. */
static void st_scene_begin(BtlSeqTalkCtx *ctx, s32 chapter, s32 set, s32 first, s32 count, s32 winner, s32 leaves) {
    const StCast *cast = &stCasts[chapter];
    s32 i;

    {extern void Port_StoryInterludeShow(int);if(ST_Interlude(chapter))Port_StoryInterludeShow(1);}
    if(set==ST_SET_CANON||set==ST_SET_ALT){extern void Port_StoryMusicStart(int);
     if(chapter==4)Port_StoryMusicStart(PortSongs_StoryBgm(stChapters[chapter].id,stChapters[chapter].saga,0));
     else if(chapter==5)Port_StoryMusicStart(-1);
    }
    sStRun.chapter = chapter;
    sStRun.set = set;
    sStRun.index = first;
    sStRun.count = first + count;
    sStRun.winner = winner;
    sStRun.leaves = leaves;
    sStRun.active=1;sStRun.departing=sStRun.finishing=sStRun.bridge=0;sStRun.frames=0;sStRun.elapsed=0;sStRun.cameraActor=-1;
    sStRun.actors=0;sStRun.rival=1;sStRun.shot=sStRun.shotActor=sStRun.shotLine=-1;sStEndingAnchorReady=0;sStNappaIntroAnchorReady=0;
    if(chapter==11&&set==ST_SET_CANON){int j;for(j=0;j<2;j++){int a=Port_StoryCastActor(j?4:1);if(a>=0){sStDownActors|=1u<<a;st_set_hp(a,0);}}
      Port_Dbg3("story final knockout state",Port_StoryCastActor(1),st_hp(Port_StoryCastActor(1)),Port_StoryCastActor(4),st_hp(Port_StoryCastActor(4)));}
    for (i = 0; i < ST_ACTORS; i++) {
        sStRun.cast[i] = i < cast->count ? Port_StoryCastActor(i) : -1;
    }
    for (i = 0; i < cast->count; i++) { /* no draft record (a test, a direct start): the two team leads are actors 0 and 1 */
        s32 j, lead = 1;
        for (j = 0; j < i; j++) lead &= cast->actor[j].team != cast->actor[i].team;
        if (sStRun.cast[i] < 0 && lead && !(cast->actor[i].flags & ST_ACTOR_SCENE)) sStRun.cast[i] = cast->actor[i].team;
    }
    for (i = 0; i < cast->count; i++) { /* the rival: the first fighting team-1 actor */
        if (sStRun.cast[i] >= 0 && cast->actor[i].team == 1 && !(cast->actor[i].flags & ST_ACTOR_SCENE)) {
            sStRun.rival = sStRun.cast[i];
            break;
        }
    }
    if(ST_Phased(chapter)){s32 r=st_rival_now(chapter);if(r>=0)sStRun.rival=r;}
    sStRun.carried = cast->carried >= 0 ? sStRun.cast[cast->carried] : -1;
    sStRun.carrier = cast->carrier >= 0 ? sStRun.cast[cast->carrier] : -1;
    sStRun.arriving = cast->arriving >= 0 ? sStRun.cast[cast->arriving] : -1;
    ctx->side[0]=0;ctx->side[1]=1;
    st_scene_dry_placement(); /* ground defeated bodies / avoid water before companions, paths or camera snapshots */
    /* Every fighting cast member is owned and posed once; scene-only actors join where the scene needs them. */
    for (i = 0; i < cast->count; i++) {
        s32 a = sStRun.cast[i];
        if(a<0||a>=32||Port_StorySceneParked(a)||((sStGoneActors>>a&1u)&&!(sStDownActors>>a&1u))){
            continue;
        }
        sStRun.actors |= 1u << a;
        st_pose(a);
    }
    st_scene_formation();
    if(chapter==11&&set==ST_SET_INTRO){st_rock_perch(Port_StoryCastActor(0));st_rock_perch(Port_StoryCastActor(4));st_face(Port_StoryCastActor(0),Port_StoryCastActor(4));st_face(Port_StoryCastActor(4),Port_StoryCastActor(0));}
    if (sStRun.carried >= 0) {
        if ((set == ST_SET_INTRO && !cast->noIntroCompanion) || set == ST_SET_ALT) { /* Gohan beside his father: hiding behind him, or safe */
            st_scene_take(sStRun.carried, 1);
            st_place_near(sStRun.carried, sStRun.cast[0], set == ST_SET_INTRO ? -0.45f : 0.1f, set == ST_SET_INTRO ? 0.35f : 0.6f, 0.0f);
            BtlCtrl_PlayMotion(sStRun.carried, 0, 1, 0.15f);
        } else if (set == ST_SET_CANON && sStRun.carrier >= 0) { /* taken: limp at Raditz's side */
            st_scene_take(sStRun.carried, 1);
            BtlCtrl_PlayMotion(sStRun.carried, ST_MOTION_LIMP, 1, 0.15f);
        }
    }
    {   /* scene-only cast this scene shows (StCast masks, or the event's): beside their own side's lead, spread out */
        u32 show = set == ST_SET_INTRO ? cast->introShow : set == ST_SET_CANON ? cast->canonShow :
                   set == ST_SET_ALT ? cast->altShow : (set >= ST_SET_EVENT0 && set < ST_SET_EVENT0 + ST_EVENTS) ? sStEventShow : 0;
        s32 n[2] = {0, 0};
        for (i = 0; i < cast->count; i++) {
            s32 a = sStRun.cast[i], t = cast->actor[i].team ? 1 : 0, anchor = t ? sStRun.rival : st_lead_now(chapter);
            if (!(show >> i & 1u) || a < 0 || !(cast->actor[i].flags & ST_ACTOR_SCENE) || a == sStRun.carried) {
                continue;
            }
            st_scene_take(a, 1);
            n[t]++;
            if(!st_scene_observer_place(a,anchor))st_place_near(a, anchor, -0.55f - 0.25f * (n[t] >> 1), (n[t] & 1 ? 0.75f : -0.75f) * (1 + (n[t] - 1) / 2), 0.0f);
            st_scene_ground_actor(a);if(anchor>=0&&anchor!=a)st_face(a,t?sStRun.cast[0]:sStRun.rival);
            st_pose(a); /* includes the visible sleeping child, not just the hidden fighter */
        }
    }
    if (set == ST_SET_INTRO || set == ST_SET_CANON || set == ST_SET_ALT) { /* hidden outside the fight (a giant before / after) */
        for (i = 0; i < cast->count; i++) {
            void *o;
            if (!(cast->endHide >> i & 1u) || sStRun.cast[i] < 0) continue;
            if(chapter==3&&set==ST_SET_INTRO){
                o=(void*)BtlObj_Get(st_obj(sStRun.cast[i]));if(o)BtlObj_SetPartVisible(o,0,1);
                continue; /* Gohan must stay visible during the native opening transformation. */
            }
            o = (void *)BtlObj_Get(st_obj(sStRun.cast[i]));
            if (o != NULL) BtlObj_SetPartVisible(o, 0, 0);
            sStRun.actors &= ~(1u << sStRun.cast[i]);
        }
    }
    if(chapter==6&&set==ST_SET_CANON){int g=Port_StoryCastActor(6),v[9];
      if(g>=0&&Port_StoryResourceIO(g,0,v)&&v[0]==231){
        BtlCtrl_StopMotion(g);sStRun.actors&=~(1u<<g);Port_StoryGrantForm(g,0);BtlCtrl_Transform(g,0);
        sStFormActor=g;sStFormTarget=0;sStFormWait=0;
      }
      {int n=Port_StoryCastActor(8),veg=Port_StoryCastActor(9);
       /* This long chapter's generic rival is the first Saibaman, not Nappa.
          Its ending mask therefore does not admit Nappa automatically. */
       st_scene_take(n,1);st_scene_take(veg,1);
       st_stage_actor(veg);st_scene_ground_actor(veg);st_face(veg,g);
       st_place_near(n,veg,1.3f,0,0);st_scene_ground_actor(n);
       st_scene_flat_pair(veg,n,1.3f);
       BtlCtrl_PlayMotion(n,ST_MOTION_PRONE,1,.1f);
       BtlCharApi_GetPos(st_obj(veg),&sStEndingDrawAnchor[0]);BtlCharApi_GetPos(st_obj(n),&sStEndingDrawAnchor[1]);sStEndingAnchorReady=1;
       {extern char*getenv(const char*);if(getenv("BT3_STORY_SCENE_LOG"))Port_Dbg3("story ending staged pair",(int)(sStEndingDrawAnchor[0].y*100),(int)(sStEndingDrawAnchor[1].y*100),BtlStage_IsReady(),Battle_GetWork()->flags);}
      }}
    if(chapter==5&&(set==ST_SET_INTRO||set==ST_SET_CANON||set==ST_SET_ALT)){
      int lead=sStRun.cast[0],j;for(j=11;j<=12;j++){int who=sStRun.cast[j];if(who>=0){st_place_near(who,lead,-3.f,6.f+(j-11)*1.7f,0);st_scene_ground_actor(who);st_face(who,lead);}}
    }
    if(chapter==6&&sStEventPost&&set==ST_SET_EVENT0+8){int g=Port_StoryCastActor(6);
      sStRun.arriving=g;BtlCharApi_GetPos(st_obj(g),&sStRun.arrival);
      {Vec4 p=sStRun.arrival;p.y-=st_height(g)*6;BtlCtrl_SetPos(g,&p);}BtlCtrl_PlayMotion(g,0x2F,1,.1f);st_presentation_queue(g,1u);
    }
    ctx->step = ST_STEP_BEAT;
}

/* Every frame of a canonical scene: the carried actor follows the carrier (the native carry pair's own technique:
   the held fighter's position is set from the holder's each frame), at his side and at hip height. */
static void st_scene_follow(void) {
    if(sStRun.active&&sStRun.chapter==6&&!(sStRun.set==ST_SET_CANON&&sStRun.index==1)){
      int v=Port_StoryCastActor(9),lead=st_lead_now(6);
      if(v>=0&&lead>=0&&v!=lead&&(sStRun.actors>>v&1u))st_face(v,lead);
    }
    if((sStRun.chapter==17||sStRun.chapter==18)&&sStRun.elapsed==12){int i,lead=sStRun.cast[0];
     for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];if(a<0)continue;
      if(i>0)st_place_near(a,lead,sStRun.chapter==17?-1.2f:1.1f,(i-2)*1.4f,0);
      BtlCtrl_PlayMotion(a,sStRun.chapter==18&&i==1?ST_MOTION_IDLE:(i&1?0x174:0x173),1,.1f);
     }
    }
    if (sStRun.set == ST_SET_CANON && sStRun.carried >= 0 && sStRun.carrier >= 0 && sStRun.carried < 32 &&
        (sStRun.actors >> sStRun.carried & 1u)) {
        st_place_near(sStRun.carried, sStRun.carrier, 0.12f, 0.32f, 0.45f);
    }
}

/* The canonical bridge after the departure: the carried actor leaves the picture with the carrier, the arriving actor
   lands beside the human lead and the bridge lines follow (chapter 2: Piccolo, SS02_013-014). */
static s32 st_bridge_begin(BtlSeqTalkCtx *ctx) {
    const StCast *cast = &stCasts[sStRun.chapter];
    if (sStRun.set != ST_SET_CANON || sStRun.bridge || cast->bridgeCount <= 0 || sStRun.arriving < 0) {
        return 0;
    }
    if (sStRun.carried >= 0) {
        st_scene_take(sStRun.carried, 0);
    }
    sStRun.bridge = 1;
    sStRun.departing = 0;
    sStRun.set = ST_SET_BRIDGE;
    sStRun.index = 0;
    sStRun.count = cast->bridgeCount;
    sStRun.leaves = 0;
    st_scene_take(sStRun.arriving, 1);
    st_place_near(sStRun.arriving, sStRun.cast[0], 1.6f, -0.7f, 0.0f);
    BtlCharApi_GetPos(st_obj(sStRun.cast[0]), &sStRun.arrival);
    {   /* the landing spot, computed like st_place_near (the warp is taken next frame) */
        Vec4 facing = {0, 0, 0, 1};
        f32 h = st_height(sStRun.cast[0]);
        BtlCharApi_GetRot(st_obj(sStRun.cast[0]), &facing);
        sStRun.arrival.x += sinf(facing.y) * 1.6f * h - cosf(facing.y) * 0.7f * h;
        sStRun.arrival.z += cosf(facing.y) * 1.6f * h + sinf(facing.y) * 0.7f * h;
    }
    BtlCtrl_PlayMotion(sStRun.arriving, ST_MOTION_FLIGHT, 1, 0.15f);
    ScrXfade_RequestCapture();
    ScrXfade_Start(1, 1.0f);
    ctx->step = ST_STEP_BEAT;
    return 1;
}

/* preUpdate of a story scene: the camera cut and subtitle of the next line. */
static void st_scene_beat(BtlSeqTalkCtx *ctx) {
    const StLine *line;

    if (ctx->step != ST_STEP_BEAT) {
        return;
    }
    if (sStRun.index >= sStRun.count && ST_Interlude(sStRun.chapter)) { /* an interlude: no departure, nothing after */
        sStRun.frames = 0;
        ctx->step = ST_STEP_HOLD;
        return;
    }
    if (sStRun.index >= sStRun.count && !sStRun.leaves) {ctx->step=ST_STEP_HOLD;sStRun.frames=0;Ramp_Start(&ctx->timer,1.f/30.f,0,1);return;}
    if (sStRun.index >= sStRun.count) { /* the departure caption stays on the last shot */
        sStRun.actor=sStRun.rival;sStRun.departing=1;
        BtlCharApi_GetPos(st_obj(sStRun.rival),&sStRun.departure);
        sStDepartActors=0;
        if(sStRun.set==ST_SET_CANON&&stCasts[sStRun.chapter].departMask){ /* several leave together (chapter 7: Goku and Vegeta) */
            s32 i,first=-1;
            for(i=0;i<stCasts[sStRun.chapter].count&&i<ST_ACTORS;i++){s32 a=sStRun.cast[i];if(a<0||a>=32||!(stCasts[sStRun.chapter].departMask>>i&1u))continue;
                if(first<0)first=a;sStDepartActors|=1u<<a;BtlCharApi_GetPos(st_obj(a),&sStDepartFrom[i]);BtlCtrl_PlayMotion(a,0x2E,0,0.10f);st_presentation_queue(a,1u);}
            if(first>=0)sStRun.actor=first;
        }else{
        /* Native start -> flight loop, followed by a visible upward escape. */
        BtlCtrl_PlayMotion(sStRun.rival,0x2E,0,0.10f);
        st_presentation_queue(sStRun.rival,1u);
        }
        Port_StorySubtitle(sStRun.chapter, sStRun.set == ST_SET_CANON ? ST_SET_LEAVES : ST_SET_LEAVES_ALT, 0);
        Ramp_Start(&ctx->timer, 2.0f, 0.0f, 1.0f);
        sStRun.frames = 0;
        st_scene_camera(); /* save the fixed exit framing before he moves */
        ctx->step = ST_STEP_HOLD;
        return;
    }
    line = ST_SceneLine(sStRun.chapter, sStRun.set, sStRun.index);
    sStRun.actor = st_line_actor(line);
    st_scene_admit_speaker(sStRun.actor);sStRun.cameraActor=-1;
    st_scene_follow();
    st_scene_camera();
    {int set=sStRun.set,index=sStRun.index;
     if(sStRun.chapter==6&&line&&line->text[0]&&strstr(line->text[0],"Yamcha... Tien...")&&st_hp(Port_StoryCastActor(1))>0){set=ST_SET_YAMCHA_ALIVE;index=0;}
     Port_StorySubtitle(sStRun.chapter,set,index);}
    Ramp_Start(&ctx->timer, (f32)st_frames(line->text[0]) / 30.0f, 0.0f, 1.0f);
    sStRun.frames = 0;
    ctx->step = ST_STEP_HOLD;
}

/* update of a story scene: 1 while it runs, 0 when its last line (and its caption / bridge) has been held. */
static s32 st_scene_hold(BtlSeqTalkCtx *ctx) {
    s32 timeUp;

    if (ctx->step != ST_STEP_HOLD) {
        return 1;
    }
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStFormActor>=0){int v[9],g=sStFormActor;
      if(Port_StoryResourceIO(g,0,v)&&v[0]==0){sStFormActor=-1;sStRun.actors|=1u<<g;st_pose(g);}
      else if(++sStFormWait<300){if(sStFormWait%15==0&&BtlCtrl_IsInterruptible(g))BtlCtrl_Transform(g,0);return 1;}
      else {Port_Dbg3("story ending base form failed",6,g,0,0);sStFormActor=-1;}
    }
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index<=1&&sStEndingAnchorReady){
      /* Reapply our last authored positions BEFORE reading them again. The
         previous native frame may have changed its output root; adopting that
         output would turn a hold into a one-frame-late follower of the drift. */
      BtlCtrl_SetPos(Port_StoryCastActor(9),&sStEndingDrawAnchor[0]);
      BtlCtrl_SetPos(Port_StoryCastActor(8),&sStEndingDrawAnchor[1]);
    }
    sStRun.frames++;sStRun.elapsed++;
    if(sStRun.departing&&sStDepartActors){
        s32 i;f32 t=(sStRun.frames-8)/30.0f;if(t<0)t=0;if(t>1)t=1;
        for(i=0;i<stCasts[sStRun.chapter].count&&i<ST_ACTORS;i++){s32 a=sStRun.cast[i];Vec4 p;f32 h;if(a<0||a>=32||!(sStDepartActors>>a&1u))continue;
            p=sStDepartFrom[i];h=st_height(a);if(sStRun.frames==9)BtlCtrl_PlayMotion(a,0x2F,1,0.10f);
            p.y-=h*(2.0f*t+18.0f*t*t);p.x+=h*4.0f*t*(i&1?1.0f:-1.0f)*0.0f;BtlCtrl_SetPos(a,&p);}
    }else if(sStRun.departing){
        Vec4 p=sStRun.departure;f32 h=st_height(sStRun.rival);
        f32 t=(sStRun.frames-8)/30.0f;if(t<0)t=0;if(t>1)t=1;
        if(sStRun.frames==9)BtlCtrl_PlayMotion(sStRun.rival,0x2F,1,0.10f);
        p.y-=h*(2.0f*t+18.0f*t*t);BtlCtrl_SetPos(sStRun.rival,&p);
    }
    if(sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8&&sStRun.arriving>=0&&sStRun.index==2){
      Vec4 p=sStRun.arrival;int f=sStRun.frames;float t=f/40.f;if(t>1)t=1;
      p.y-=st_height(sStRun.arriving)*6*(1-t)*(1-t);BtlCtrl_SetPos(sStRun.arriving,&p);
      if(f==40)BtlCtrl_PlayMotion(sStRun.arriving,ST_MOTION_IDLE,1,.15f);
    }
    if(sStRun.bridge && sStRun.arriving>=0){ /* the arrival: down from the sky, then standing */
        Vec4 p=sStRun.arrival;f32 h=st_height(sStRun.arriving);
        s32 f=sStRun.index==0?sStRun.frames:60;f32 t=f/40.0f;if(t>1)t=1;
        p.y-=h*6.0f*(1.0f-t)*(1.0f-t);BtlCtrl_SetPos(sStRun.arriving,&p);
        if(sStRun.index==0&&f==40)BtlCtrl_PlayMotion(sStRun.arriving,ST_MOTION_IDLE,1,0.2f);
    }
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1){
      int n=Port_StoryCastActor(8),v=Port_StoryCastActor(9),f=sStRun.frames;Vec4 at;
      if(f==1){
        sStEndingExecutionHit=sStEndingFall=0;
        if(st_hp(n)<=0)Port_StoryReviveActor(n,1);
        BtlCtrl_SetPos(v,&sStEndingDrawAnchor[0]);BtlCtrl_SetPos(n,&sStEndingDrawAnchor[1]);st_face(v,n);st_face(n,v);
        BtlCharApi_GetPos(st_obj(v),&sStEndingVegetaAnchor);
        BtlCharApi_GetPos(st_obj(n),&sStRun.endPos[1]);
        BtlCtrl_PlayMotion(v,ST_MOTION_IDLE,1,.1f);BtlCtrl_PlayMotion(n,ST_MOTION_PRONE,1,.1f);
      }
      if(f==31)BtlCtrl_PlayMotion(v,ST_ANIM_SWORD,0,.1f);
      if(f==40)BtlCtrl_PlayMotion(n,ST_ANIM_HIT,0,.05f);
      if(f>40&&f<=64){st_place_near(n,v,1.3f,0,(f-40)*.05f);}
      if(f==72){BtlCharApi_GetPos(st_obj(n),&sStRun.endPos[0]);st_scene_ground_actor(n);BtlCharApi_GetPos(st_obj(n),&sStRun.endPos[1]);BtlCtrl_SetPos(n,&sStRun.endPos[0]);
        sStRun.actors&=~(1u<<v);BtlCtrl_StopMotion(v);st_face(v,n);gPortTarget[v]=n;Port_StoryUseTechnique(v,2);st_presentation_queue(n,2u|4u);}
      if(f>72){
        /* Keep the shooter planted while the real beam plays. Do not warp its
           victim over his native hit reaction before contact. */
        BtlCtrl_SetPos(v,&sStEndingVegetaAnchor);st_face(v,n);
        if(sStEndingExecutionHit){Vec4 p=sStRun.endPos[0];float t=++sStEndingFall/24.f;if(t>1)t=1;p.y+=(sStRun.endPos[1].y-p.y)*t;BtlCtrl_SetPos(n,&p);if(sStEndingFall==24){st_scene_ground_actor(n);BtlCtrl_PlayMotion(n,ST_MOTION_PRONE,1,.1f);st_set_hp(n,0);}}
      }
    }
    st_scene_follow();
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index<=1){
      BtlCharApi_GetPos(st_obj(Port_StoryCastActor(9)),&sStEndingDrawAnchor[0]);
      BtlCharApi_GetPos(st_obj(Port_StoryCastActor(8)),&sStEndingDrawAnchor[1]);sStEndingAnchorReady=1;
    }
    st_scene_camera();
    timeUp = Ramp_Step(&ctx->timer);
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&timeUp){int v=Port_StoryCastActor(9);
      /* Subtitle timing cannot skip the firing cue or cut off the actual impact/fall. */
      if((sStRun.frames<72||!sStEndingExecutionHit||sStEndingFall<24||!BtlCtrl_IsInterruptible(v))&&sStRun.frames<360)return 1;
      if(!sStEndingExecutionHit)Port_Dbg3("story execution missed",6,v,Port_StoryCastActor(8),sStRun.frames);
      st_scene_ground_actor(Port_StoryCastActor(8));
      sStRun.actors|=1u<<v;BtlCtrl_StopMotion(v);st_scene_ground_actor(v);st_face(v,Port_StoryCastActor(6));st_pose(v);
    }
    if (!timeUp) {
        return 1;
    }
    if (sStRun.index >= sStRun.count) {
        if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)){
          int a=Port_StoryCastActor(1),v[9];
          sStRun.actor=a;st_scene_camera(); /* stay on Gohan throughout the opening transformation */
          if(!sStIntroFormFrames){sStIntroFormFrames=1;st_stage_actor(a);BtlCtrl_StopMotion(a);
            sStRun.actors&=~(1u<<a);Port_StoryGrantForm(a,180);BtlCtrl_Transform(a,180);
            Port_StorySubtitle(3,ST_SET_EVENT0,0);}
          if(Port_StoryResourceIO(a,0,v)&&v[0]==180&&BtlCtrl_IsInterruptible(a)){sStEventsDone|=1u;Port_StoryEventDone(0);sStIntroFormFrames=0;}
          else if(++sStIntroFormFrames<300){if(sStIntroFormFrames%15==0&&BtlCtrl_IsInterruptible(a))BtlCtrl_Transform(a,180);return 1;}
          else {Port_Dbg3("story intro form failed",3,a,180,0);BattleResult_Set(2,1);sStIntroFormFrames=0;}
        }
        if (st_bridge_begin(ctx)) {
            return 1;
        }
        Port_StorySubtitle(-1, 0, 0);
        return 0;
    }
    sStRun.index++;
    if (sStRun.index >= sStRun.count && !sStRun.leaves) {
        if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)){
          ctx->step=ST_STEP_HOLD;Ramp_Start(&ctx->timer,1.f/30.f,0,1);return 1;
        }
        Port_StorySubtitle(-1, 0, 0);
        return 0;
    }
    if (sStRun.index < sStRun.count) {
        ScrXfade_RequestCapture();
        ScrXfade_Start(1, 1.0f);
    }
    ctx->step = ST_STEP_BEAT;
    return 1;
}

#define ST_STEP_PREFIGHT 42
extern unsigned Port_StoryPresentationClock(void);
extern int Port_StoryPresentationInput(void);
extern void Bgm_Play(int);
static int sStPreludePhase,sStPreludeChapter=-1,sStPreludeElapsed,sStPreludeJustFinished,sStPreludeDuration;
static unsigned sStPreludePrevious;
static void st_prefight_stop(int restore){
 if(sStPreludePhase&&restore)Bgm_Play(PORT_BGM_FILE(Battle_GetBgm()));
 sStPreludePhase=0;sStPreludeChapter=-1;sStPreludeElapsed=0;
 Port_StoryPresentation(-1,0,0);
}
static int st_prefight_begin(BtlSeqTalkCtx*ctx,int chapter){
 if(ST_Interlude(chapter)||!Port_Setting("story_presentation",1)||Port_NetActive()||BattleReplay_IsActive())return 0;
 sStPreludePhase=1;sStPreludeChapter=chapter;sStPreludeElapsed=0;sStPreludeJustFinished=0;sStPreludePrevious=Port_StoryPresentationClock();sStPreludeDuration=STP_RecapMs(chapter);
 Port_StorySubtitle(-1,0,0);Port_StoryPresentation(chapter,1,0);ctx->step=ST_STEP_PREFIGHT;ctx->side[0]=0;ctx->side[1]=1;
 Bgm_Play(PortSongs_StoryBgm(stChapters[chapter].id,stChapters[chapter].saga,0));return 1;
}
static void st_prefight_tick(BtlSeqTalkCtx*ctx){
 unsigned now=Port_StoryPresentationClock(),delta=now-sStPreludePrevious;int pressed=Port_StoryPresentationInput(),duration,ch=sStPreludeChapter;
 sStPreludePrevious=now;if(delta>200)delta=200; /* no unseen time jump after an unfocused/stalled window */
 if(ch<0||ch>=ST_COUNT||!Port_StoryActive()){st_prefight_stop(0);return;}
 sStPreludeElapsed+=(int)delta;duration=sStPreludeDuration;
 if(sStPreludeElapsed>=duration||(sStPreludeElapsed>=350&&(pressed&0x1200))){
  if(sStPreludePhase==1&&!(pressed&0x1000)){
   sStPreludePhase=2;sStPreludeElapsed=0;sStPreludeDuration=PortSongs_StoryDurationMs(stChapters[ch].id,stChapters[ch].saga,1);if(sStPreludeDuration<=0||sStPreludeDuration>30000)sStPreludeDuration=STP_TitleMs(stChapters[ch].saga);
   Bgm_Play(PortSongs_StoryBgm(stChapters[ch].id,stChapters[ch].saga,1));
  }else{
   st_prefight_stop(1);st_scene_begin(ctx,ch,ST_SET_INTRO,0,stChapters[ch].introCount,-1,0);
   st_scene_beat(ctx); /* frame the first shot before the opaque presentation disappears */
   sStPreludeJustFinished=1;return;
  }
 }
 duration=sStPreludeDuration;
 Port_StoryPresentation(ch,sStPreludePhase,(int)((long long)sStPreludeElapsed*65535/duration));
}

/* Ready cannot release chapter4's controls while its opening form is pending. */
static s32 st_opening_ready(void){
 int ch=st_chapter(),a,v[9];if(ch!=3)return 1;a=Port_StoryCastActor(1);
 if(a<0)return 0;
 if(Port_StoryResourceIO(a,0,v)&&v[0]==180&&BtlCtrl_IsInterruptible(a)){
  sStEventsDone|=1u;Port_StoryEventDone(0);return 1;
 }
 Battle_GetWork()->flags|=BATTLE_FLAG_DEMO;
 if(BtlCtrl_IsInterruptible(a)){Port_StoryGrantForm(a,180);BtlCtrl_StopMotion(a);BtlCtrl_Transform(a,180);}
 return 0;
}

/* These authored closures go directly to their Story ending, without the
   Duel PERFECT/FINISH wait that otherwise releases a defeated pose for3.5s. */
static s32 st_direct_ending(void){
 int ch=st_chapter();return ch==4||(ch==3&&sStMoonTarget>=0&&sStMoonTarget<32&&(sStDownActors>>sStMoonTarget&1u));
}

/* State 1 enter of a story fight: the opening instead of the entrance banter. 1 when it took the state. */
static s32 st_intro_enter(BtlSeqTalkCtx *ctx) {
    s32 chapter = st_chapter();

    if (chapter < 0 || stChapters[chapter].introCount <= 0) {
        return 0;
    }
    /* A new native round starts before the intro. Never reuse prior actor-index masks. */
    sStRun.active=0;sStDownActors=sStGoneActors=sStEventShow=sStDepartActors=0;sStTailCut=0;sStTiredActor=-1;sStTiredFrames=0;
    sStAtkFrames=sStBeamFrames=sStMoonFrames=sStEventPost=sStIntroFormFrames=0;
    sStEventRun=sStEventOffer=sStAtkEvent=sStAtkBy=sStAtkAt=sStAtkBack=-1;
    sStSwitchActor=-1;sStFormActor=-1;sStSbStage=0;sStSbDodge=-1;
    memset(sStPresentation,0,sizeof(sStPresentation));memset(sStAuraActive,0,sizeof(sStAuraActive));memset(sStBeamChargeActive,0,sizeof(sStBeamChargeActive));
    Port_StoryPrompt(0);
    if(st_prefight_begin(ctx,chapter))return 1;
    st_scene_begin(ctx, chapter, ST_SET_INTRO, 0, stChapters[chapter].introCount, -1, 0);
    return 1;
}

/* State 5 enter of a story fight: the ending of the outcome that the result records. Draws and failures keep the
   game's own winner scene. 1 when it took the state. */
static s32 st_ending_enter(BtlSeqTalkCtx *ctx) {
    BattleResult *res = Battle_GetResult();
    s32 chapter = st_chapter();
    s32 winner, outcome;
    const StScene *sc;

    sStSwitchActor = -1; /* everyone back on his own side for the ending */
    sStBeamFrames = sStBeamLanded = 0;
    if (chapter < 0) {
        return 0;
    }
    winner = res->winner == 1 ? 0 : res->winner == 2 ? 1 : -1;
    outcome = ST_OutcomeFor(chapter, winner, res->reason, Port_StoryEvents());
    sc = &stScenes[chapter];
    if (outcome == ST_CANON) {
        st_scene_begin(ctx, chapter, ST_SET_CANON, sc->canonFirst, stChapters[chapter].canonCount - sc->canonFirst,
                       winner, !stCasts[chapter].noLeaves);
    } else if (outcome == ST_ALT && sStAltEnd >= 0 && sStAltEnd < stEvents[chapter].count && stEvents[chapter].event[sStAltEnd].altCount > 0) {
        st_scene_begin(ctx, chapter, ST_SET_ALTEV0 + sStAltEnd, 0, stEvents[chapter].event[sStAltEnd].altCount, winner, 0);
    } else if (outcome == ST_ALT) {
        st_scene_begin(ctx, chapter, ST_SET_ALT, 0, stChapters[chapter].altCount, winner, !stCasts[chapter].noLeaves && !stCasts[chapter].departMask);
    } else {
        return 0;
    }
    ctx->side[0] = winner;
    return 1;
}

/* State 3 enter of a story fight: no line said yet; logs every cast actor's live health once (the cast audit). */
static void st_fight_enter(void) {
    s32 chapter = st_chapter();
    s32 i;

    sStRestoreFrames=0;if(Port_StoryResumeEvent()==0)sStCheckpoint.chapter=-1;
    sStInterludeEnded = 0;
    sStCuesDone = 0;
    sStFightFrames = sStCueFrames = 0;sStFormActor=-1;sStCh7ReleaseFrames=0;
    Port_StoryPrompt(0);
    sStEventOffer = sStEventRun = -1;
    sStEventPressed = sStBeamFrames = sStBeamLanded = 0;
    sStSwitchActor = -1;
    sStEventsDone = Port_StoryEvents(); /* preserve events actually completed during the opening */
    sStDownActors = sStGoneActors = sStEventShow = 0;sStTailCut = 0;sStTiredActor = -1;sStTiredFrames = 0;
    sStMoonFrames = sStMoonTailAt = 0;
    sStMoonShooter = sStMoonTarget = -1;
    sStLastEventEnd = 0;
    sStAltEnd = -1;
    sStEventAlt = 0;
    sStAtkFrames = 0;sStEventPost=sStIntroFormFrames=0;
    sStAtkEvent = sStAtkBy = sStAtkBack = sStAtkAt = -1;
    sStSbStage = 0; sStSbThrower = sStSbGohan = sStSbDodge = -1; sStSbFlip = sStSbSeen = 0;
    memset(sStPresentation,0,sizeof(sStPresentation));memset(sStAuraActive,0,sizeof(sStAuraActive));memset(sStBeamChargeActive,0,sizeof(sStBeamChargeActive));
    if (chapter < 0) {
        return;
    }
    for (i = 0; i < stCasts[chapter].count; i++) {
        s32 actor = Port_StoryCastActor(i), obj = actor >= 0 ? st_obj(actor) : -1;
        if (obj >= 0) {
            Port_StoryLogStats(chapter, actor, obj, (int)BtlCharApi_GetHp(obj), (int)BtlCharApi_GetHpMax(obj));
        }
    }
    Port_StoryLogBallSearch(chapter, (int)BtlStage_CountItemHolders()); /* a stage without holders hides no ball */
    if (ST_Phased(chapter) && Port_StoryResumeEvent() > 0) {
        s32 k, n = Port_StoryResumeEvent(), rival;
        for (k = 0; k < n && k < stEvents[chapter].count; k++) {
            StEvent e = stEvents[chapter].event[k];
            e.damage = 0; e.canon = 0; e.refill = 0; e.attackCast = 0; /* the consequences, not the blows */
            sStEventsDone |= 1u << k;
            Port_StoryEventDone(k);
            if (sStCheckpoint.chapter!=chapter&&e.transformTo > 0 && e.transformCast >= 0 && e.transformCast < stCasts[chapter].count) {
                s32 who = Port_StoryCastActor(e.transformCast);
                if (who >= 0) { Port_StoryGrantForm(who, e.transformTo); BtlCtrl_Transform(who, e.transformTo); }
            }
            rival = st_rival_now(chapter);
            st_event_effects(chapter, &e, rival);
        }
        sStLastEventEnd = 0;
        if(sStCheckpoint.chapter==chapter&&sStCheckpoint.event==n){
         int v[9];for(k=0;k<ST_ACTORS;k++){StActorSave*p=&sStCheckpoint.actor[k];int a=Port_StoryCastActor(k);if(p->valid&&a>=0&&Port_StoryResourceIO(a,0,v)&&v[0]!=p->v[0]){Port_StoryGrantForm(a,p->v[0]);BtlCtrl_Transform(a,p->v[0]);}}
         sStRestoreFrames=1;
        }
        Port_StorySubtitle(-1, 0, 0);
    }
}

static s32 st_percent(s32 side) {
    s32 obj = BattleSide_GetObjId(side);
    s32 max = BtlCharApi_GetHpMax(obj);

    return max > 0 ? (s32)((long long)BtlCharApi_GetHp(obj) * 100 / max) : 100;
}

/* Authored R3 events of a chapter (stEvents, chapter 3), in the running fight: offered in story order when the rival's
   HP falls to the event's ABSOLUTE value (no difficulty scaling), shown only as the native HUD R3 cue (no caption),
   accepted with R3 (battle event 0x50, read where the native mission script reads it: BtlScript_UpdateHud) when every
   fighting cast member can be interrupted. A later event's moment supersedes an earlier one not taken. The scene
   freezes the fight as the opening talk does (BATTLE_FLAG_DEMO) and runs on its own talk context.
     Gohan (ST_EVENT_GOHAN): the scene-only kid flies into Raditz (ST_GOHAN_DAMAGE HP), lines SS03_011-014, parked again.
     Beam (ST_EVENT_BEAM): Goku holds Raditz from behind, Piccolo charges (native SKILL_005 in / charge, aura), lines
     SS04_001-004; then the fight resumes for Piccolo's native ultimate (BtlCtrl_UseTechnique 4, as mission command
     "Trigger Ultimate Blast") and when it lands - or after ST_BEAM_FRAMES - Goku and Raditz both fall. */
extern s32 BtlCtrl_IsInterruptible(s32 player);
extern s32 Port_StoryUseTechnique(s32 player, s32 kind);
extern void BtlCtrl_SetAuraOn(s32 player);
extern void BtlCtrl_LowerHp(s32 player, s32 member, f32 percent);
extern s32 BtlCtrl_GetActiveMember(s32 player);
extern void Hud_SlideOut(f32 seconds);
extern void Hud_SlideIn(f32 seconds);
static void st_scene_end(void);
extern void BtlCharSnd_PlayCommon(void *chr, s32 id);
extern s32 gPortTarget[];
#define ST_SE_DASH 0x1C   /* native common SE of a dash / burst start (btl_tech_b.c BtlParam_GetDashSound) */
#define ST_SE_STRIKE 0x45 /* native common SE of a level-4 strike landing (btl_char_fx.c BtlChar_SpawnHitFx) */
#define ST_ANIM_STAGGER 0xAE   /* native long stagger of a broken guard (actions 0xCB / 0xCC, BtlAct_ActionCBtoCC) */
#define ST_ANIM_TIRED 1        /* native GROUND_TIRED: out of breath (mission 510's animation list) */
#define ST_HIT_FRAMES 16       /* hit reaction, then the stagger */
#define ST_STAGGER_FRAMES 46   /* stagger, then panting */
#define ST_FINISH_FRAMES 300   /* after the beam's hits: the natural end of the fight, else the native result */
extern void Port_StoryBeamVictim(int who, int winner, int damage); /* btl_act_e.c: native damage + blow-away (0xD5) */

#define ST_MOTION_GROUND_TIRED 1
#define ST_MOTION_SURPRISED 406
#define ST_MOTION_SBC_IN 329
#define ST_MOTION_SBC_CHARGE 330

#define ST_GOHAN_DAMAGE 4000    /* the hit (absolute, like the thresholds) */
#define ST_BEAM_FRAMES 150      /* longest wait for the ultimate to land */


/* The `skip`-th cast actor of `team`, fighting (scene 0) or scene-only (scene 1); -1 when absent. */
static s32 st_cast_find(s32 chapter, s32 team, s32 scene, s32 skip) {
    const StCast *c = &stCasts[chapter];
    s32 i;
    for (i = 0; i < c->count; i++) {
        if (c->actor[i].team != team || !(c->actor[i].flags & ST_ACTOR_SCENE) != !scene) {
            continue;
        }
        if (skip-- == 0) {
            return Port_StoryCastActor(i);
        }
    }
    return -1;
}

static s32 st_hp(s32 actor) {
    return actor >= 0 ? (s32)BtlCharApi_GetHp(st_obj(actor)) : 0;
}

/* Turns `actor` to face `target` (native facing: forward = (sin y, cos y)). */
static void st_face(s32 actor, s32 target) {
    if(actor<0||actor>=ST_ACTORS||target<0||target>=ST_ACTORS)return;
    Vec4 a = {0, 0, 0, 1}, b = {0, 0, 0, 1}, rot = {0, 0, 0, 1};
    BtlCharApi_GetPos(st_obj(actor), &a);
    BtlCharApi_GetPos(st_obj(target), &b);
    BtlCharApi_GetRot(st_obj(actor), &rot);
    rot.y = atan2f(b.x - a.x, b.z - a.z);
    BtlCtrl_SetRot(actor, &rot);
}

/* Sets an actor's health to `hp` (never raises it); 0 knocks it out. */
static void st_set_hp(s32 actor, s32 hp) {
    s32 max = actor >= 0 ? (s32)BtlCharApi_GetHpMax(st_obj(actor)) : 0;
    if (max <= 0) {
        return;
    }
    BtlCtrl_LowerHp(actor, BtlCtrl_GetActiveMember(actor), hp <= 0 ? 0.0f : ((f32)hp + 0.5f) * 100.0f / (f32)max); /* +0.5: exact after the native truncation */
}

/* The HUD side (btl_script.c BtlScript_UpdateHud): 1 while an event is offered (the cue shows the R3 glyph); a fresh R3
   press is remembered for the fight update. */
s32 Port_StoryEventCue(s32 pressed) {
    extern int Port_StoryPromptView(void);s32 was=Port_StoryPromptView(),ready;
    Port_StoryPrompt(0);
    if (sStEventOffer < 0 || sStEventRun >= 0 || sStBeamFrames > 0 || st_chapter() < 0) {
        return 0;
    }
    ready=st_event_ready(st_chapter());
    if(pressed&&(was||ready))sStEventPressed=1; /* retain a visible prompt press across one-frame admission races */
    if(!ready)return 0;
    Port_StoryPrompt(1);
    return 1;
}

static s32 st_event_ready(s32 chapter) {
    const StCast *c = &stCasts[chapter];
    s32 i, a;
    for (i = 0; i < c->count; i++) {
        a = Port_StoryCastActor(i);
        if (a >= 0 && a < 32 && ((sStGoneActors >> a & 1u) || Port_StorySceneParked(a) || (ST_Phased(chapter) && st_hp(a) <= 0))) {
            continue; /* gone, waiting on the sidelines, or (phased) the phase enemy just knocked out */
        }
        if (a >= 0 && (st_hp(a) <= 0 || !BtlCtrl_IsInterruptible(a))) {
            return 0;
        }
    }
    return 1;
}

/* Speech after an attack must observe its actual impact, not predict it. */
static int st_event_before(int ch,int k){
 const StEvent*e=&stEvents[ch].event[k];
 if(ch==6){switch(k){case 0:return 1;case 1:return 2;case 3:return 3;case 4:return 3;case 7:return 0;}}
 if(ch==6&&k==8)return 2; /* revenge -> real Masenko -> Goku arrival */
 if(ch==11&&k==1)return 2; /* Kaioken x4: said during the live struggle (st_attack_update); after it only if no struggle ran */
 if(ch==11&&k==13)return 0; /* "He's breathing" / "I can't get up" only after the cut and the fall */
 if(ch==11&&k==10)return 1; /* Spirit Bomb: "Now!" before the throw; the rest follow the ball (st_sb_lines) */
 return e->lineCount;
}
static void st_event_start(s32 chapter, s32 k) {
    Port_StoryPrompt(0);
    const StEvent *ev = &stEvents[chapter].event[k];
    s32 lead, rival, kid, partner;
    Vec4 facing = {0, 0, 0, 1}, at = {0, 0, 0, 1};
    f32 h;

    sStEventOffer = -1;
    sStEventRun = k;
    Port_Dbg3("story event starts",chapter,k,ev->transformTo,ev->attackCast);
    sStEventFrames = sStEventHit = sStGohanFrames = 0;sStEventPost=0;sStX4Wait=sStX4Frames=sStX4Said=0;sStSbLine=sStSbLineFrames=0;
    sStEventsDone |= 1u << k;
    Port_StoryEventDone(k);
    sStCueFrames = 0;
    Port_StorySubtitle(-1, 0, 0);
    Battle_GetWork()->flags |= BATTLE_FLAG_DEMO; /* frozen as in the opening talk */
    Hud_SlideOut(0.5f);
    sStEventShow = ev->showMask;
    if (sStEventAlt) {
        st_scene_begin(&sStEventCtx, chapter, ST_SET_ALTEV0 + k, 0, ev->altCount, -1, 0);
    } else {
        st_scene_begin(&sStEventCtx, chapter, ST_SET_EVENT0 + k, 0, st_event_before(chapter,k), -1, 0);
    }
    lead = st_lead_now(chapter);
    rival = sStRun.rival;
    if (ev->kind == ST_EVENT_GOHAN) {
        kid = st_cast_find(chapter, 1, 1, 0);
        if (kid < 0) {
            kid = st_cast_find(chapter, 0, 1, 0);
        }
        if (lead >= 0) {
            BtlCtrl_PlayMotion(lead, ST_MOTION_GROUND_TIRED, 1, 0.15f);
        }
        if (kid >= 0 && rival >= 0 && lead >= 0) {
            st_scene_take(kid, 1);
            st_place_near(kid, lead, 0.3f, 0.6f, 0.4f);
            h = st_height(lead); /* the path starts from that spot (computed: the warp lands next frame) */
            BtlCharApi_GetPos(st_obj(lead), &sStEventFrom);
            BtlCharApi_GetRot(st_obj(lead), &facing);
            sStEventFrom.x += sinf(facing.y) * 0.3f * h + cosf(facing.y) * 0.6f * h;
            sStEventFrom.z += cosf(facing.y) * 0.3f * h - sinf(facing.y) * 0.6f * h;
            sStEventFrom.y -= 0.4f * h;
            h = st_height(rival);
            BtlCharApi_GetPos(st_obj(rival), &sStEventTo);
            BtlCharApi_GetRot(st_obj(rival), &facing);
            sStEventTo.x += sinf(facing.y) * 0.45f * h;
            sStEventTo.z += cosf(facing.y) * 0.45f * h;
            sStEventTo.y -= 0.35f * h;
            BtlCtrl_PlayMotion(kid, ST_MOTION_FLIGHT, 1, 0.1f);
            /* Aura/charge are refreshed by Port_StoryFxFrame after native per-frame clears. */
            /* Rush begins on Gohan's own line, while he is actually on camera. */
        }
        sStEventTo.w = (f32)kid;
    } else if (ev->kind == ST_EVENT_BEAM) {
        partner = st_cast_find(chapter, 0, 0, 1);
        if (rival >= 0 && lead >= 0) {
            BtlCharApi_GetPos(st_obj(rival), &sStBeamPos);
            BtlCharApi_GetRot(st_obj(rival), &sStBeamRot);
            if (partner >= 0) { /* Raditz faces Piccolo; Goku grips him from behind */
                st_face(rival, partner);
                BtlCharApi_GetPos(st_obj(rival), &at);
                BtlCharApi_GetPos(st_obj(partner), &facing);
                sStBeamRot.y = atan2f(facing.x - at.x, facing.z - at.z);
            }
            st_place_near(lead, rival, -0.32f, 0.0f, 0.0f);
            BtlCtrl_PlayMotion(rival, ST_MOTION_SURPRISED, 1, 0.15f);
            BtlCtrl_PlayMotion(lead, ST_MOTION_IDLE, 1, 0.15f);
        }
        if (partner >= 0 && rival >= 0) {
            st_face(partner, rival);
            BtlCtrl_PlayMotion(partner, ST_MOTION_SBC_IN, 0, 0.15f);
            /* Native aura and charge FX are delivered by the presentation hook. */
        }
        sStBeamLead = lead;
        sStBeamRival = rival;
        sStBeamPartner = partner;
    }
    if (ev->kind == ST_EVENT_MOON && lead >= 0 && rival >= 0) {
        st_face(lead, rival);
        BtlCtrl_PlayMotion(lead, ST_MOTION_IDLE, 1, 0.15f); /* the native charge pose; FX from the presentation hook */
        BtlCtrl_PlayMotion(rival, ST_MOTION_SURPRISED, 1, 0.15f);
        sStMoonShooter = lead;
        sStMoonTarget = rival;
    }
    if(ev->counterCast>0&&ev->attackCast>0&&!sStEventAlt){
      int by=Port_StoryCastActor(ev->attackCast-1),back=Port_StoryCastActor(ev->counterCast-1);Vec4 p;
      st_stage_actor(by);st_stage_actor(back);st_face(by,back);st_place_near(by,back,9.f,0,0);
      BtlCharApi_GetPos(st_obj(back),&p);p.y-=st_height(back)*5;BtlCtrl_SetPos(back,&p);st_face(by,back);st_face(back,by);
      BtlCtrl_PlayMotion(by,0x106+(ev->attackTech-2)*0x22,1,.1f);
      BtlCtrl_PlayMotion(back,0x106+(ev->counterTech-2)*0x22,1,.1f);
    }
    if(ev->choreo==ST_CHOREO_EXECUTE&&ev->attackCast>0&&ev->attackTarget>0){
      int by=Port_StoryCastActor(ev->attackCast-1),at=Port_StoryCastActor(ev->attackTarget-1),i;
      if(by>=0&&at>=0){st_scene_admit_speaker(by);st_scene_admit_speaker(at);
      st_place_near(by,at,1.8f,.65f,0);st_scene_ground_actor(by);st_face(by,at);st_face(at,by);
      for(i=0;i<stCasts[chapter].count;i++){int a=sStRun.cast[i];if(a>=0&&a!=by&&a!=at&&(sStRun.actors>>a&1u))st_face(a,at);}}
    }
    /* ST_EVENT_SCENE: the fighting cast stands for its lines (st_pose), the shown scene-only cast beside them. */
}

/* Every frame of an event scene (after the talk context's own work). */
static void st_event_frame(s32 chapter) {
    const StEvent *ev = &stEvents[chapter].event[sStEventRun];
    s32 kid = (s32)sStEventTo.w, rival = sStRun.rival;
    sStEventFrames++;
    if (ev->kind == ST_EVENT_GOHAN && kid >= 0) {
        if(sStRun.index<1)return;
        if(++sStGohanFrames==1){BtlCtrl_PlayMotion(kid,ST_MOTION_FLIGHT,1,0.1f);st_presentation_queue(kid,1u);}
        if (sStGohanFrames <= ST_GOHAN_FLIGHT) {
            f32 t = (f32)sStGohanFrames / ST_GOHAN_FLIGHT;
            Vec4 p = sStEventFrom;
            p.x += (sStEventTo.x - sStEventFrom.x) * t;
            p.y += (sStEventTo.y - sStEventFrom.y) * t;
            p.z += (sStEventTo.z - sStEventFrom.z) * t;
            /* Aim the animated head at Raditz's chest, not the child's root
               at a cached adult-root offset. The flight pose bends him forward. */
            {Vec4 root,head,chest;float rh=st_height(rival);
             st_face(kid,rival);BtlCharApi_GetPos(st_obj(kid),&root);
             head=st_scene_face(kid);chest=st_scene_face(rival);chest.y+=rh*.22f;
             sStEventTo.x=chest.x-(head.x-root.x);
             sStEventTo.y=chest.y-(head.y-root.y);
             sStEventTo.z=chest.z-(head.z-root.z);
             p.x=sStEventFrom.x+(sStEventTo.x-sStEventFrom.x)*t;
             p.y=sStEventFrom.y+(sStEventTo.y-sStEventFrom.y)*t;
             p.z=sStEventFrom.z+(sStEventTo.z-sStEventFrom.z)*t;
            }
            p.w = 1;
            BtlCtrl_SetPos(kid, &p);
            st_face(kid, rival);
            {int speaker=sStRun.actor;sStRun.actor=kid;st_scene_camera();sStRun.actor=speaker;}
        }
        if (sStEventHit && sStGohanFrames == ST_GOHAN_FLIGHT + ST_HIT_FRAMES) {
            BtlCtrl_PlayMotion(rival, ST_ANIM_STAGGER, 0, 0.1f);
        } else if (sStEventHit && sStGohanFrames == ST_GOHAN_FLIGHT + ST_HIT_FRAMES + ST_STAGGER_FRAMES) {
            BtlCtrl_PlayMotion(rival, ST_ANIM_TIRED, 1, 0.2f);
        }
        if (sStGohanFrames == ST_GOHAN_FLIGHT && !sStEventHit) { /* the headbutt lands */
            sStEventHit = 1;
            st_set_hp(rival, st_hp(rival) > ST_GOHAN_DAMAGE ? st_hp(rival) - ST_GOHAN_DAMAGE : 1);
            BtlCtrl_PlayMotion(rival, ST_ANIM_HIT, 0, 0.05f);
            BtlCtrl_PlayMotion(kid, ST_MOTION_IDLE, 1, 0.2f);
            st_presentation_queue(rival,2u|4u);
        }
    } else if (ev->kind == ST_EVENT_BEAM) {
        if (sStBeamLead >= 0 && rival >= 0) {
            BtlCtrl_SetPos(rival, &sStBeamPos);
            BtlCtrl_SetRot(rival, &sStBeamRot);
            st_place_near(sStBeamLead, rival, -0.32f, 0.0f, 0.0f);
        }
        (void)0;
    }
    if (ev->kind == ST_EVENT_MOON && sStMoonShooter >= 0) {
        if (sStEventFrames == 30 || (sStEventFrames > 30 && sStRun.actor == sStMoonShooter && sStRun.frames <= 1)) {
            BtlCtrl_PlayMotion(sStMoonShooter, ST_MOTION_IDLE, 1, 0.15f);
        }
    }
    if (ev->kind == ST_EVENT_BEAM) {
        if (sStBeamPartner >= 0 && (sStEventFrames == 30 || (sStEventFrames>30 && sStRun.actor==sStBeamPartner && sStRun.frames<=1))) {
            BtlCtrl_PlayMotion(sStBeamPartner, ST_MOTION_SBC_CHARGE, 1, 0.15f);
        }
    }
}

/* A SCENE event's effects, once its lines are over (or skipped): the side switch, damage to the rival, the story KOs
   and story deaths, then - for the canonical event - the end of the fight through the native result (the player's
   side the winner, as a mission's scripted ending sets it). */
static void st_event_effects(s32 chapter, const StEvent *ev, s32 rival) {
    const StCast *c = &stCasts[chapter];
    s32 i,a,formationLead=st_lead_now(chapter);
    /* Transfer the controller while Gohan still shares Krilin's allegiance.
       HandControl intentionally rejects transfers between enemy teams. */
    int giantHandover=chapter==11&&ev==&stEvents[11].event[11];
    if(giantHandover)Port_StoryHandControl(Port_StoryCastActor(2));
    if (!(chapter==11&&ev==&stEvents[11].event[12]) && ev->switchCast >= 0 && ev->switchCast < c->count && (a = Port_StoryCastActor(ev->switchCast)) >= 0) {
        s32 lead = formationLead;
        if (ev->switchToLead && (lead < 0 || lead == a)) { /* never the switched actor's own (overridden) side */
            s32 j, h; lead = -1;
            for (j = 0; j < c->count && lead < 0; j++) if (!c->actor[j].team && (h = Port_StoryCastActor(j)) >= 0 && h != a) lead = h;
        }
        sStSwitchTeam = ev->switchToLead ? Port_Team(lead >= 0 ? lead : 0) : Port_Team(rival);
        /* The uncontrolled giant is a third faction, hostile to both survivors. */
        if(chapter==11&&ev==&stEvents[11].event[11])sStSwitchTeam=2;
        sStSwitchActor = a; /* until the ending (st_ending_enter) */
    }
    if(chapter==11&&ev==&stEvents[11].event[13]){
      int g=Port_StoryCastActor(1),v=Port_StoryCastActor(4);sStSwitchActor=-1;
      if(g>=0)sStDownActors|=1u<<g;if(v>=0)sStDownActors|=1u<<v;
    }
    if (ev->damage > 0 && rival >= 0) {
        st_set_hp(rival, st_hp(rival) > ev->damage ? st_hp(rival) - ev->damage : 1);
        st_presentation_queue(rival, 2u | 4u);
    }
    for (i = 0; i < c->count; i++) {
        a = Port_StoryCastActor(i);
        if (a < 0 || a >= 32) continue;
        if (sStEventAlt) { /* an early win saves the hero, but the defeated wave enemy still leaves */
            if((ev->removeMask>>i&1u)&&c->actor[i].team==1&&st_hp(a)<=0&&Port_SequencePartner(a)<0){sStGoneActors|=1u<<a;Port_StoryRemoveActor(a);}continue;
        }
        if (ev->removeMask >> i & 1u) { /* a story death: out of the fight for good, never revived */
            sStGoneActors |= 1u << a;
            st_set_hp(a, 0);
            Port_StoryRemoveActor(a);
        } else if (ev->killMask >> i & 1u) { /* knocked out by the story, shown down in the ending */
            sStDownActors |= 1u << a;
            sStGoneActors |= 1u << a;
            Port_StoryBeamVictim(a, rival >= 0 && rival != a ? rival : Port_StoryCastActor(0), st_hp(a));
            if (st_hp(a) > 0) st_set_hp(a, 0);
        }
    }
    for (i = 0; i < c->count; i++) { /* the phase changes: heroes step back, others join, the next enemy of the wave */
        a = Port_StoryCastActor(i);
        if (a < 0 || a >= 32) continue;
        if (ev->exitMask >> i & 1u) Port_StoryExitActor(a, st_hp(a));
    }
    {
        s32 n = 0, lead = formationLead;
        for (i = 0; i < c->count; i++) {
            a = Port_StoryCastActor(i);
            if (a < 0 || a >= 32 || !(ev->enterMask >> i & 1u)) continue;
            Port_StoryEnterActor(a);
            n++;
            if (c->actor[i].team == 1 && rival >= 0 && !(sStGoneActors>>rival&1u) && a != rival) st_place_near(a, rival, -0.8f, 1.2f * n, 0.0f);
            else if (lead >= 0 && a != lead) st_place_near(a, lead, -1.0f, (n & 1 ? 1.2f : -1.2f) * ((n + 1) / 2), 0.0f);
        }
    }
    if (ev->refill && rival >= 0 && !(sStGoneActors >> rival & 1u)) {
        Port_StoryReviveActor(rival, (s32)BtlCharApi_GetHpMax(st_obj(rival))); /* the next of his kind, at full health */
    }
    if (!giantHandover && ev->handTo > 0 && ev->handTo <= c->count && (a = Port_StoryCastActor(ev->handTo - 1)) >= 0) {
        Port_StoryHandControl(a);
    }
    if (ST_Phased(chapter) && !ev->canon && !ev->refill && rival >= 0 && !(sStGoneActors >> rival & 1u) && st_hp(rival) <= 0) {
        /* a phase enemy knocked out before his story moment gets back up for the next phase: just above the next
           event's HP moment (or a quarter of his health), never a fight left waiting on a dead enemy */
        const StEvents *evs = &stEvents[chapter];
        s32 k = (s32)(ev - evs->event) + 1, pct = 25, mx = (s32)BtlCharApi_GetHpMax(st_obj(rival));
        if (k < evs->count && evs->event[k].trigger == ST_TRIG_RIVAL_PCT) pct = evs->event[k].threshold + 5;
        if (pct > 100) pct = 100;
        Port_StoryReviveActor(rival, (s32)((long long)mx * pct / 100));
    }
    if (ev->canon && st_event_before(chapter,(s32)(ev-stEvents[chapter].event))>=ev->lineCount) {
        BattleResult_Set(1, 1);
    }
}

/* The attack of an event (StEvent.attackCast ...): fired once the scene has released the fight. */
static int sStAttackDown[ST_ACTORS];
static void st_attack_admit_down(int actor){
 if(actor>=0&&actor<ST_ACTORS&&!Port_StorySceneParked(actor)&&st_hp(actor)<=0){
  sStAttackDown[actor]=1;Port_StoryReviveActor(actor,1); /* native contact requires a live slot; never released to gameplay */
 }
}
static void st_attack_restore_down(void){int i;for(i=0;i<ST_ACTORS;i++)if(sStAttackDown[i]){st_set_hp(i,0);sStAttackDown[i]=0;}}
static void st_attack_begin(s32 chapter, s32 k, s32 rival) {
    const StEvent *ev = &stEvents[chapter].event[k];
    sStAtkEvent = k;
    sStAtkBy = Port_StoryCastActor(chapter==11&&k==4?4:ev->attackCast-1);
    sStAtkAt = chapter==11&&k==4?Port_StoryCastActor(0):ev->attackTarget > 0 ? Port_StoryCastActor(ev->attackTarget - 1) : rival;
    sStAtkBack = ev->counterCast > 0 ? Port_StoryCastActor(ev->counterCast - 1) : -1;
    sStAtkFrames = 1;sStAtkContact=sStAtkLost=0;sStAtkRetry=0;sStShieldWait=0;sStAtkRival=rival;sStPairChargeReady=0;
    memset(sStAttackDown,0,sizeof(sStAttackDown));
    st_attack_admit_down(sStAtkBy);
    sStAtkHp = st_hp(sStAtkAt);
    {int i,a;for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);
      if(a<0||Port_StorySceneParked(a)||(sStGoneActors>>a&1u))continue;
      st_stage_actor(a);if(a!=sStAtkBy&&a!=sStAtkAt&&a!=sStAtkBack)BtlCtrl_PlayMotion(a,0,1,.1f);}}
    BtlCtrl_StopMotion(sStAtkBy);BtlCtrl_StopMotion(sStAtkAt);
    /* The incoming phase can have ended in the air. Authored attacks start on
       each actor's real ground, before their offset and native paired movie. */
    if(chapter==6){
      st_stage_actor(sStAtkAt);st_scene_ground_actor(sStAtkAt);
      if(k==8){ /* Masenko away from Tien's preserved body. */
        st_stage_actor(sStAtkBy);st_scene_ground_actor(sStAtkBy);
        st_face(sStAtkBy,sStAtkAt);st_place_near(sStAtkAt,sStAtkBy,7.f,2.f,0);st_scene_ground_actor(sStAtkAt);
      }
    }
    if(sStAtkBack>=0)BtlCtrl_StopMotion(sStAtkBack);
    if(sStAtkBy>=0&&sStAtkAt>=0){st_face(sStAtkBy,sStAtkAt);
      st_place_near(sStAtkBy,sStAtkAt,ev->counterCast>0?35.f:1.5f,0,ev->counterCast>0?-0.0f:0);
      st_face(sStAtkBy,sStAtkAt);st_face(sStAtkAt,sStAtkBy);}
    if(chapter==6){st_scene_ground_actor(sStAtkBy);st_scene_ground_actor(sStAtkAt);}
    if(sStAtkBack>=0){Vec4 q;BtlCharApi_GetPos(st_obj(sStAtkBack),&q);q.y-=st_height(sStAtkBack)*5;BtlCtrl_SetPos(sStAtkBack,&q);st_face(sStAtkBack,sStAtkBy);st_face(sStAtkBy,sStAtkBack);}
    if(sStAtkBy<0||sStAtkAt<0){sStAtkFrames=0;st_attack_restore_down();Port_Dbg3("story authored attack missing participant",chapter,k,0,0);BattleResult_Set(2,1);return;}

    if(chapter==6&&k==7){
      st_stage_actor(sStAtkAt);st_scene_ground_actor(sStAtkAt);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStShieldGohanAnchor);
      st_place_near(sStAtkBy,sStAtkAt,10.f,0,0);st_scene_ground_actor(sStAtkBy);st_face(sStAtkAt,sStAtkBy);st_face(sStAtkBy,sStAtkAt);
    }
    {   /* a fighter the attack will cost his life steps in front of its target (Piccolo shields Gohan) */
        const StCast *c = &stCasts[chapter];
        s32 i, a;
        for (i = 0; i < c->count; i++) {
            a = Port_StoryCastActor(i);
            if (a < 0 || a == sStAtkBy || a == sStAtkAt || !(ev->killMask >> i & 1u) || ev->attackTech < 0) continue;
            st_face(sStAtkAt, sStAtkBy);
            st_place_near(a, sStAtkAt, chapter==6&&k==7?1.8f:.7f, 0.0f, 0.0f);
            st_face(a, sStAtkBy);
            st_scene_ground_actor(a);st_attack_admit_down(a);BtlCtrl_PlayMotion(a,ST_MOTION_IDLE,1,.1f);
            sStAtkAt=a; /* observe the shield's real contact, never Gohan's unrelated HP */
            break;
        }
    }
    if(chapter==6&&k==7){Vec4 at,eye,go,by;int g=Port_StoryCastActor(4);float h=st_height(sStAtkAt),dx,dz,len;
      BtlCharApi_GetPos(st_obj(g),&go);BtlCharApi_GetPos(st_obj(sStAtkBy),&by);
      dx=by.x-go.x;dz=by.z-go.z;len=sqrtf(dx*dx+dz*dz);if(len<.01f){dx=0;dz=1;len=1;}dx/=len;dz/=len;
      eye=go;eye.x+=(-dx*1.7f+dz*.7f)*h;eye.z+=(-dz*1.7f-dx*.7f)*h;eye.y-=h*1.2f;
      BtlCharApi_GetPos(st_obj(sStAtkAt),&at);at.y-=h*.6f;
      st_camera_finish(eye,at,h,g,5);
    }
    if(ev->choreo==ST_CHOREO_EXECUTE){
      /* A player KO already left the phase enemy down. The authored execution
         still needs a hittable native victim, strictly inside this owned shot. */
      if(st_hp(sStAtkAt)<=0)Port_StoryReviveActor(sStAtkAt,1);
      Port_StoryEnterActor(sStAtkBy);st_place_near(sStAtkBy,sStAtkAt,-1.5f,.5f,0);st_scene_ground_actor(sStAtkBy);st_scene_ground_actor(sStAtkAt);
      if(chapter==6)st_scene_flat_pair(sStAtkBy,sStAtkAt,1.3f);
      {extern void Port_StoryHoldPose(int,int);Port_StoryHoldPose(sStAtkAt,ST_MOTION_IDLE);}
      {Vec4 by,at,eye,rot;float h=st_height(sStAtkBy);
       BtlCharApi_GetPos(st_obj(sStAtkBy),&by);BtlCharApi_GetPos(st_obj(sStAtkAt),&at);BtlCharApi_GetRot(st_obj(sStAtkBy),&rot);
       at.x=(at.x+by.x)*.5f;at.z=(at.z+by.z)*.5f;at.y-=h*.6f;
       eye=by;eye.x+=sinf(rot.y+1.15f)*h*3.5f;eye.z+=cosf(rot.y+1.15f)*h*3.5f;eye.y-=h*1.2f;
       st_camera_finish(eye,at,h,sStAtkBy,5);}

    }
    st_face(sStAtkBy, sStAtkAt);
    gPortTarget[sStAtkBy] = sStAtkAt;
    sStAtkFired = sStAtkFree = 0;
    if(chapter==11&&k==4){
      st_stage_actor(sStAtkBy);st_scene_ground_actor(sStAtkBy);st_scene_ground_actor(sStAtkAt);
      BtlCtrl_StopMotion(sStAtkAt);Port_StoryGrantForm(sStAtkAt,0);BtlCtrl_Transform(sStAtkAt,0);
      sStFormActor=sStAtkAt;sStFormTarget=0;sStFormWait=0;
      BtlCtrl_PlayMotion(sStAtkBy,0x96,1,.1f); /* native thrower's grip */
      return;
    }
    if (ev->choreo == ST_CHOREO_SPIRITBOMB) {
        s32 helper = sStAtkBy, i; /* Krilin owns the throw; ground Goku supplies only its data/motion. */
        Vec4 a = {0, 0, 0, 1}, b = {0, 0, 0, 1}, g;
        f32 dx, dz, len, h = st_height(sStAtkAt);
        sStSbThrower = sStAtkBy;sStSbSteered = 0;sStSbLine = sStSbLineFrames = sStSbImpactFrames = 0;sStSbBest = 1e30f;sStSbPrev.w = 0.0f;
        sStSbGohan = -1;
        for (i = 0; i < stCasts[chapter].count; i++) /* who sends it back (the script's Gohan) */
            if (!strcmp(stCasts[chapter].actor[i].name, "Gohan")) sStSbGohan = Port_StoryCastActor(i);
        if (helper < 0 || sStSbGohan < 0) { sStAtkFrames = 0; Port_Dbg3("story spirit bomb missing helper", chapter, k, 0, 0); BattleResult_Set(2, 1); return; }
        st_face(sStSbThrower, sStAtkAt);
        st_face(helper, sStAtkAt);
        BtlCharApi_GetPos(st_obj(sStSbThrower), &a); /* Gohan beyond Vegeta, in the ball's line */
        BtlCharApi_GetPos(st_obj(sStAtkAt), &b);
        dx = b.x - a.x; dz = b.z - a.z; len = sqrtf(dx * dx + dz * dz);
        if (len < 0.01f) { dx = 0.0f; dz = 1.0f; len = 1.0f; }
        /* Reset the three actors onto one clear throw line. Vegeta is the
           midpoint, with seven body heights to each end rather than combat's
           arbitrary close-range positions. Ground Goku is left untouched. */
        a=b;a.x-=dx/len*h*7.f;a.z-=dz/len*h*7.f;
        g=b;g.x+=dx/len*h*7.f;g.z+=dz/len*h*7.f;
        BtlCtrl_SetPos(sStSbThrower,&a);BtlCtrl_SetPos(sStSbGohan,&g);
        st_scene_ground_actor(sStSbThrower);st_scene_ground_actor(sStSbGohan);st_scene_ground_actor(sStAtkAt);
        BtlCharApi_GetPos(st_obj(sStSbThrower),&a);BtlCharApi_GetPos(st_obj(sStSbGohan),&g);BtlCharApi_GetPos(st_obj(sStAtkAt),&b);
        /* Each end keeps its own ground height: this terrain is not flat. */
        BtlCtrl_SetPos(sStSbThrower,&a);BtlCtrl_SetPos(sStAtkAt,&b);BtlCtrl_SetPos(sStSbGohan,&g);
        sStSbDodgePos=b;sStSbDodgePos.x+=dz/len*h*3.f;sStSbDodgePos.z-=dx/len*h*3.f;
        st_face(sStSbThrower,sStAtkAt);
        st_face(sStSbGohan, sStAtkAt);
        sStAtkBy = helper; /* the ball is his: his exact native contact lands the event */
        gPortTarget[helper] = sStAtkAt;
        sStSbStage = 0; sStSbDodge = -1; sStSbFlip = sStSbSeen = 0;
        BtlCtrl_PlayMotion(sStSbThrower, ST_MOTION_IDLE, 1, 0.15f);
        return; /* st_choreo_update */
    }
    if (ev->choreo == ST_CHOREO_CLASH) { Battle_GetWork()->flags|=BATTLE_FLAG_DEMO; /* face to face, close: the clash is asked for in st_choreo_update */
        st_face(sStAtkBy, sStAtkAt);
        st_place_near(sStAtkAt, sStAtkBy, ST_CLASH_GAP, 0.0f, 0.0f);
        st_face(sStAtkAt, sStAtkBy);
        st_face(sStAtkBy, sStAtkAt);
        sStClashSeen = 0;
        BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_FLIGHT, 1, 0.1f);
        BtlCtrl_PlayMotion(sStAtkAt, ST_MOTION_FLIGHT, 1, 0.1f);
        st_presentation_queue(sStAtkBy, 1u);
        return;
    }
    if(ev->choreo==ST_CHOREO_TRANSFER){
      st_scene_ground_actor(sStAtkAt);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStTransferAnchor);
      BtlCtrl_PlayMotion(sStAtkAt,ST_MOTION_PRONE,1,.1f);
      st_scene_ground_actor(sStAtkBy);
    }
    if (ev->choreo == ST_CHOREO_CLING || ev->choreo == ST_CHOREO_TAILCUT || ev->choreo == ST_CHOREO_TRANSFER) {
        if (ev->choreo == ST_CHOREO_TAILCUT) { /* he comes out from the sidelines behind the giant */
            st_scene_take(sStAtkBy, 1);
            st_place_near(sStAtkBy, sStAtkAt, ST_TAIL_FROM, 0.25f, ST_TAIL_UP);
            st_face(sStAtkBy, sStAtkAt);
        }
        BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_FLIGHT, 1, 0.1f);
        st_presentation_queue(sStAtkBy, 1u);
        return; /* st_choreo_update */
    }

    if (ev->attackTech >= 0) {
        if(!Port_StoryUseTechnique(sStAtkBy,st_attack_slot(chapter,k))){sStAtkFrames=0;st_attack_restore_down();Port_Dbg3("story technique admission failed",chapter,k,0,0);BattleResult_Set(2,1);return;}
    } else {
        BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_FLIGHT, 1, 0.1f); /* the dash / lunge of a strike */
        st_presentation_queue(sStAtkBy, 1u);
    }
    if (sStAtkBack >= 0) { /* the clash: both beams at once */
        st_face(sStAtkBack, sStAtkBy);
        gPortTarget[sStAtkBack] = sStAtkBy;
        Port_StoryUseTechnique(sStAtkBack, ev->counterTech);
    }
}

/* A point beside `anchor` in its facing (forward / right / up in its heights; native Y is down). */
static void st_point_near(s32 anchor, f32 forward, f32 right, f32 up, Vec4 *p) {
    Vec4 facing = {0, 0, 0, 1};
    f32 h = st_height(anchor);
    BtlCharApi_GetPos(st_obj(anchor), p);
    BtlCharApi_GetRot(st_obj(anchor), &facing);
    p->x += sinf(facing.y) * forward * h + cosf(facing.y) * right * h;
    p->z += cosf(facing.y) * forward * h - sinf(facing.y) * right * h;
    p->y -= up * h;
}

static f32 st_dist(const Vec4 *a, const Vec4 *b) {
    f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* Moves `actor` a share of the way to `to`. */
static void st_move_toward(s32 actor, const Vec4 *to, f32 share) {
    Vec4 a = {0, 0, 0, 1};
    BtlCharApi_GetPos(st_obj(actor), &a);
    a.x += (to->x - a.x) * share; a.y += (to->y - a.y) * share; a.z += (to->z - a.z) * share;
    BtlCtrl_SetPos(actor, &a);
}

/* Goku's instruction must be readable BEFORE Gohan sends the ball back. */
static int st_sb_return_ready(void){
    int total=st_frames(stEvents[11].event[10].line[3].text[0])+8;
    return sStSbLine>3||(sStSbLine==3&&sStSbLineFrames<=total/2);
}

/* The choreographed part of an attack, before (a cling) or instead of (a tail cut) the plain strike / technique.
   Returns 1 while it owns the frame. */
static void st_attack_done(s32,const StEvent*);
static s32 st_choreo_update(s32 chapter, const StEvent *ev, s32 f) {
    Vec4 p = {0, 0, 0, 1};
    if(chapter==11&&sStAtkEvent==4){
      /* Native throw clips:96 is the holder,97 the held fighter,98 releases.
         Keep the body attached to the hand, then lower it onto actual terrain. */
      Vec4 hand,root;float h=st_height(sStAtkBy);
      BtlCharApi_GetPos(st_obj(sStAtkBy),&root);hand=root;
      BtlCharApi_GetNodePos(st_obj(sStAtkBy),0x23,&hand); /* right hand, same native hand node used by the muzzle anchors */
      if(!st_finite_pos(&hand)||st_dist(&hand,&root)>h*2.f||st_dist(&hand,&root)<h*.1f)st_point_near(sStAtkBy,.45f,.25f,.55f,&hand);
      if(f<=54){extern void Port_StoryHoldPose(int,int);Vec4 ownRoot,ownGrip;
        Port_StoryHoldPose(sStAtkAt,0x97);BtlCharApi_GetNodePos(st_obj(sStAtkAt),0,&ownRoot);BtlCharApi_GetNodePos(st_obj(sStAtkAt),0x10,&ownGrip);
        /* Native carry alignment: root - held-body anchor, rather than putting his feet at the hand. */
        if(st_finite_pos(&ownRoot)&&st_finite_pos(&ownGrip)&&st_dist(&ownRoot,&ownGrip)<st_height(sStAtkAt)*2){hand.x+=ownRoot.x-ownGrip.x;hand.y+=ownRoot.y-ownGrip.y;hand.z+=ownRoot.z-ownGrip.z;}
        BtlCtrl_SetPos(sStAtkAt,&hand);}
      if(f==55){BtlCtrl_PlayMotion(sStAtkBy,0x98,0,.1f);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStTransferAnchor);st_scene_ground_actor(sStAtkAt);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStRun.endPos[2]);}
      if(f>=55){float t=(f-55)/24.f;Vec4 q=sStTransferAnchor;if(t>1)t=1;q.y+=(sStRun.endPos[2].y-q.y)*t;BtlCtrl_SetPos(sStAtkAt,&q);}
      if(f>=80){BtlCtrl_PlayMotion(sStAtkAt,ST_MOTION_PRONE,1,.1f);st_attack_done(chapter,ev);}
      return 1;
    }
    if (ev->choreo == ST_CHOREO_CLING && !sStAtkFired) {
        /* Chiaotzu: onto Nappa's back, and he holds on there while Nappa struggles to shake him off */
        st_point_near(sStAtkAt, ST_CLING_BACK, 0.0f, ST_CLING_UP, &p);
        if (f <= ST_CLING_FLY) {
            st_move_toward(sStAtkBy, &p, 0.25f);
            st_face(sStAtkBy, sStAtkAt);
        } else {
            st_place_near(sStAtkBy, sStAtkAt, ST_CLING_BACK, 0.0f, ST_CLING_UP);
        }
        /* Placement copies the victim's rotation: restore the attacker's aim.
           Otherwise the Saibaman launches away and waits for a watchdog retry. */
        st_face(sStAtkBy, sStAtkAt);
        if (f == ST_CLING_FLY + 1) { /* he grabs on */
            BtlCtrl_PlayMotion(sStAtkAt, ST_ANIM_STAGGER, 1, 0.1f);
            st_presentation_queue(sStAtkAt, 2u);
        }
        if (f == (ST_CLING_FLY + ST_CLING_HOLD) / 2) st_presentation_queue(sStAtkAt, 1u); /* Nappa thrashes */
        if (f >= ST_CLING_HOLD) { /* the native ultimate, from his back: only its exact contact lands the event */
            sStAtkFired = 1;
            /* Native paired attacks need action4 released. The exact authored
               contact is protected from evasion in BtlColl_TryDodge. */
            BtlCtrl_StopMotion(sStAtkAt);
            if(chapter==6){extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(sStAtkAt);}
            sStAtkContact = 0;
            sStAtkFrames = 1;
            if (!Port_StoryUseTechnique(sStAtkBy, ev->attackTech)) {
                sStAtkFrames = 0;
                Port_Dbg3("story technique admission failed", chapter, sStAtkEvent, 0, 0);
                BattleResult_Set(2, 1);
            }
        }
        return 1;
    }
    if (ev->choreo == ST_CHOREO_TAILCUT) {
        if (f <= ST_TAIL_SNEAK) { /* Yajirobe closes in from behind, low, at the tail */
            st_point_near(sStAtkAt, ST_TAIL_AT, 0.0f, ST_TAIL_UP, &p);
            st_move_toward(sStAtkBy, &p, 0.22f);
            st_face(sStAtkBy, sStAtkAt);
        }
        if (f == ST_TAIL_SNEAK - 3) BtlCtrl_PlayMotion(sStAtkBy, ST_ANIM_SWORD, 0, 0.05f); /* the stroke */
        if (f == ST_TAIL_SLASH) { /* it lands: the tail is cut, the giant reels and shrinks back (native form change) */
            s32 who = ev->transformCast >= 0 && ev->transformCast < stCasts[chapter].count ? Port_StoryCastActor(ev->transformCast) : -1;
            st_presentation_queue(sStAtkBy, 2u);
            st_presentation_queue(sStAtkAt, 2u | 4u);
            BtlCtrl_PlayMotion(sStAtkAt, ST_ANIM_HIT, 0, 0.05f);
            sStAtkFree = 1;
            st_event_effects(chapter, ev, sStAtkRival);
            if (who >= 0 && ev->transformTo > 0) {
                Port_StoryGrantForm(who, ev->transformTo);
                sStFormActor=who;sStFormTarget=ev->transformTo;sStFormWait=0;BtlCtrl_Transform(who, ev->transformTo);
            }
        }
        if (f == ST_TAIL_SLASH + 6) { /* and he scrambles away */
            BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_FLIGHT, 1, 0.1f);
            st_presentation_queue(sStAtkBy, 1u);
        }
        if (f > ST_TAIL_SLASH + 6) {
            Vec4 q = {0, 0, 0, 1};
            BtlCharApi_GetPos(st_obj(sStAtkBy), &q);
            st_point_near(sStAtkAt, ST_TAIL_FROM * 1.6f, 0.6f, 0.0f, &p);
            p.y = q.y;
            st_move_toward(sStAtkBy, &p, 0.15f);
        }
        if (f >= ST_TAIL_DONE) {
            st_scene_take(sStAtkBy, 0); /* back to the sidelines */
            sStAtkFrames = 0;
        }
        return 1;
    }
    if (ev->choreo == ST_CHOREO_SPIRITBOMB) {
        s32 helper = sStAtkBy, seen = sStSbSeen;
        Vec4 v = {0, 0, 0, 1};
        if(!sStAtkContact){int subject=sStSbStage==2?sStSbGohan:sStSbStage==3?sStAtkAt:sStSbThrower;
         Vec4 at,eye,rot;float h=st_height(subject);
         BtlCharApi_GetPos(st_obj(subject),&at);BtlCharApi_GetRot(st_obj(subject),&rot);
         eye=at;eye.x+=sinf(rot.y+.65f)*h*3;eye.z+=cosf(rot.y+.65f)*h*3;eye.y-=h*1.1f;at.y-=h*.6f;
         st_camera_finish(eye,at,h,subject,40+sStSbStage);}
        sStSbSeen = 0;
        if (sStSbStage == 0 && f >= ST_SB_FIRE) { /* Krillin throws: the native ball */
            sStSbStage = 1;
            sStAtkContact = 0;
            if (!Port_StoryUseTechnique(helper, ev->attackTech)) {
                sStAtkFrames = 0;
                Port_Dbg3("story technique admission failed", chapter, sStAtkEvent, 0, 0);
                BattleResult_Set(2, 1);
                return 1;
            }
        }
        if (sStSbStage == 1 && seen) { /* close to Vegeta: he gets out of the way (not an enemy while he does) */
            BtlCharApi_GetPos(st_obj(sStAtkAt), &v);
            if (st_dist(&v, &sStSbBall) < st_height(sStAtkAt) * ST_SB_DODGE_AT) {
                sStSbStage = 2;
                sStSbDodge = sStAtkAt;
                sStAtkFree = 0;
                sStSbDodgeFrames = 0;
                if (chapter == 11 && !sStSbLine) { sStSbLine = 1; sStSbLineFrames = 0; } /* Vegeta sees it coming */
                BtlCtrl_PlayMotion(sStAtkAt, ST_MOTION_FLIGHT, 1, 0.05f);
                st_presentation_queue(sStAtkAt, 1u);
            }
        }
        if (sStSbStage == 2) {
            if (++sStSbDodgeFrames <= ST_SB_DODGE_FRAMES) { /* the sidestep */
                st_move_toward(sStAtkAt, &sStSbDodgePos, 0.3f);
            } else {
                BtlCtrl_SetPos(sStAtkAt,&sStSbDodgePos);
                if(sStSbDodgeFrames==ST_SB_DODGE_FRAMES+1)BtlCtrl_PlayMotion(sStAtkAt,ST_MOTION_IDLE,1,.05f);
            }
            gPortTarget[helper] = sStSbGohan; /* it carries on towards Gohan */
            if (seen) {
                f32 gd, hg = st_height(sStSbGohan);
                s32 bat;
                BtlCharApi_GetNodePos(st_obj(sStSbGohan),0x11,&v);
                gd = st_dist(&v, &sStSbBall);
                /* a homing ball comes right at him; a straight one is batted at its closest pass */
                bat = st_sb_return_ready()&&(gd < hg * ST_SB_RETURN_AT || (!sStSbSteered && sStSbBest < hg * 3.0f && gd > sStSbBest));
                if (gd < sStSbBest) sStSbBest = gd;
                if (!bat) { sStSbPrev = sStSbBall; sStSbPrev.w = 1.0f; }
                if (bat) { /* Gohan knocks it back */
                    sStSbStage = 3;
                    Port_Dbg3("story genkidama rebound reached Gohan",sStSbGohan,(int)(gd*100),f,0);
                    sStSbFlip = 1;
                    gPortTarget[helper] = sStAtkAt; /* back at Vegeta */
                    sStSbDodge = -1;
                    sStAtkFree = 0;
                    BtlCtrl_StopMotion(sStAtkAt); /* action4 rejects the native paired Genkidama reaction */
                    st_face(sStSbGohan, sStAtkAt);
                    BtlCtrl_PlayMotion(sStSbGohan, 0xF0, 0, 0.05f); /* native frontal guard-impact / return pose */
                    st_presentation_queue(sStSbGohan, 2u | 4u);
                    if (!sStSbSteered) { /* a straight ball cannot turn: Vegeta is where Gohan sends it */
                        Vec4 g, t; f32 hv = st_height(sStAtkAt), ddx, ddy, ddz, dl;
                        /* ahead of the ball on its own travel line (falls back to thrower -> Gohan) */
                        if (sStSbPrev.w > 0.0f) { t = sStSbPrev; g = sStSbBall; } else { BtlCharApi_GetPos(st_obj(sStSbThrower), &t); BtlCharApi_GetPos(st_obj(sStSbGohan), &g); }
                        ddx = g.x - t.x; ddy = g.y - t.y; ddz = g.z - t.z; dl = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                        if (dl < 0.01f) { ddx = 0.0f; ddy = 0.0f; ddz = 1.0f; dl = 1.0f; }
                        g = sStSbBall; g.x += ddx / dl * hv * 1.5f; g.y += ddy / dl * hv * 1.5f; g.z += ddz / dl * hv * 1.5f;
                        BtlCtrl_SetPos(sStAtkAt, &g); st_face(sStAtkAt, sStSbGohan);
                        Port_Dbg3("story spirit bomb sent on to vegeta", sStSbStage, sStAtkAt, sStSbGohan, 0);
                    }
                }
            }
        } else {
            gPortTarget[helper] = sStAtkAt;
        }
        if (sStSbStage == 3 && sStAtkContact) { /* only the returned ball may play Vegeta's native impact */
            sStSbStage=3;sStAtkFree=1; /* native hit animation may carry Vegeta away */
            {extern int EftCam_IsActive(void);
             BtlFacade_ClearFixedCamera();
             if(++sStSbImpactFrames<45||((!BtlCtrl_IsInterruptible(sStSbThrower)||!BtlCtrl_IsInterruptible(sStAtkAt)||EftCam_IsActive())&&sStSbImpactFrames<600))return 1;}
            sStAtkFrames = 0;
            sStSbDodge = -1;
            st_presentation_queue(sStAtkAt, 2u | 4u);
            st_event_effects(chapter, ev, sStAtkRival);
            BtlCtrl_StopMotion(sStSbThrower);
            BtlFacade_ClearFixedCamera();
            return 1;
        }
        if (f > ST_SB_LIMIT) { /* never a second ball, never a faked hit */
            Port_Dbg3("story spirit bomb failed no contact", chapter, sStAtkEvent, sStSbStage, 0);
            sStAtkFrames = 0;
            sStSbDodge = -1;
            BtlCtrl_StopMotion(sStSbThrower);
            BtlFacade_ClearFixedCamera();
            BattleResult_Set(2, 1);
        }
        return 1;
    }
    if (ev->choreo == ST_CHOREO_CLASH) {
        /* Cinematic sparring only: no clash action, score, damage or pad admission. */
        st_face(sStAtkBy,sStAtkAt);st_face(sStAtkAt,sStAtkBy);
        {Vec4 left={0,0,0,1},right={0,0,0,1},eye={0,0,0,1},rot={0,0,0,1};float h=st_height(sStAtkBy),dx,dz;
         BtlCharApi_GetPos(st_obj(sStAtkBy),&left);BtlCharApi_GetPos(st_obj(sStAtkAt),&right);
         eye.x=(left.x+right.x)*.5f+h*2.1f;eye.y=(left.y+right.y)*.5f-h*.8f;eye.z=(left.z+right.z)*.5f+h*1.7f;
         dx=(left.x+right.x)*.5f-eye.x;dz=(left.z+right.z)*.5f-eye.z;rot.y=atan2f(dx,dz);BtlFacade_SetFixedCamera(&eye,&rot);}
        if(f==2||f==26||f==50){BtlCtrl_PlayMotion(sStAtkBy,0x173,1,.1f);BtlCtrl_PlayMotion(sStAtkAt,0x174,1,.1f);}
        if(f==2||f==24||f==48){
            st_place_near(sStAtkBy,sStAtkAt,-.8f,0,0);
            /* The native punch/kick loops run independently of clash scoring. */
            st_presentation_queue(sStAtkBy,1u|4u);st_presentation_queue(sStAtkAt,2u|4u);
        }
        if(f>=90) {
            BtlCtrl_StopMotion(sStAtkBy);BtlCtrl_StopMotion(sStAtkAt);BtlFacade_ClearFixedCamera();Battle_GetWork()->flags&=~BATTLE_FLAG_DEMO;
            sStAtkFrames = 0;
            st_event_effects(chapter, ev, sStAtkRival);
        }
        return 1;
    }
    if (ev->choreo == ST_CHOREO_TRANSFER) {
        extern void Port_StoryHoldPose(int,int);
        BtlCtrl_SetPos(sStAtkAt,&sStTransferAnchor);Port_StoryHoldPose(sStAtkAt,ST_MOTION_PRONE);
        if (f <= ST_XFER_REACH) { /* Krillin comes down beside him */
            st_point_near(sStAtkAt, 0.0f, ST_XFER_SIDE, 0.0f, &p);
            st_move_toward(sStAtkBy, &p, 0.2f);
            st_face(sStAtkBy, sStAtkAt);
        }
        if (f == ST_XFER_REACH) BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_IDLE, 1, 0.15f);
        if (f == ST_XFER_GIVE) { /* it passes to him: flash + impact, and he keeps the aura (Port_StoryFxFrame) */
            st_presentation_queue(sStAtkBy, 2u | 4u);
            ScrXfade_RequestCapture();
            ScrXfade_Start(1, 0.6f);
        }
        if (f >= ST_XFER_DONE) {
            st_attack_done(chapter,ev); /* includes release of Krillin's held flight pose */
        }
        return 1;
    }
    return 0;
}

static void st_attack_done(s32 chapter,const StEvent*ev){
 int k=(int)(ev-stEvents[chapter].event),i,a,before=st_event_before(chapter,k);
 if(chapter==11&&k==1&&sStX4Said){before=ev->lineCount;sStX4Frames=0;Port_StorySubtitle(-1,0,0);} /* already said in the struggle */
 sStAtkFrames=0;st_attack_restore_down();
 for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0&&!Port_StorySceneParked(a))BtlCtrl_StopMotion(a);}
 if(ev->choreo==ST_CHOREO_EXECUTE)Port_StoryExitActor(sStAtkBy,st_hp(sStAtkBy));
 st_event_effects(chapter,ev,sStAtkRival);
 for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0&&!Port_StorySceneParked(a))st_stage_actor(a);}
 if(before<ev->lineCount){
   sStEventRun=k;sStEventPost=1;Battle_GetWork()->flags|=BATTLE_FLAG_DEMO;
   st_scene_begin(&sStEventCtx,chapter,ST_SET_EVENT0+k,before,ev->lineCount-before,-1,0);
 }else { /* No post-dialogue scene will release the bystanders for us. */
  for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0&&!Port_StorySceneParked(a)&&!(sStGoneActors>>a&1u)){extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(a);}}
  BtlFacade_ClearFixedCamera();
 }
}
/* The Spirit Bomb's beat lines, one after another, over the live play (no pause): they never predict the contact. */
static void st_sb_lines(s32 chapter){
 if(chapter!=11||sStSbLine<1||sStSbLine>3)return;
 /* Never skip Gohan's warning or put Goku's instruction after the rebound. */
 if(sStSbLineFrames==0){const StEvent*e=&stEvents[11].event[10];Port_StorySubtitle(11,ST_SET_EVENT0+10,sStSbLine);sStSbLineFrames=st_frames(e->line[sStSbLine].text[0])+8;Port_Dbg3("story spirit bomb line",sStSbLine,sStSbStage,0,0);}
 else if(--sStSbLineFrames==0){if(++sStSbLine>3)Port_StorySubtitle(-1,0,0);}
}
static void st_attack_update(s32 chapter) {
    const StEvent *ev = &stEvents[chapter].event[sStAtkEvent];
    s32 f = ++sStAtkFrames, landed;
    {extern char*getenv(const char*);extern void Port_StoryBeamProbe(s32,s32,s32);static int bl=-1,n;if(bl<0)bl=getenv("BT3_STORY_BEAM_LOG")!=0;
     if(bl&&chapter==11&&sStAtkEvent==1&&sStAtkBy>=0&&sStAtkBack>=0&&n<900){n++;Port_StoryBeamProbe(sStAtkBy,sStAtkBack,n|Port_ClashBusy()<<12);}}

    if(chapter==6&&sStAtkEvent==7){int g=Port_StoryCastActor(4);extern void Port_StoryHoldPose(int,int);
     BtlCtrl_SetPos(g,&sStShieldGohanAnchor);Port_StoryHoldPose(g,ST_MOTION_IDLE);st_face(g,sStAtkBy);
     if(!sStAtkContact){st_place_near(sStAtkAt,g,1.8f,0,0);st_scene_ground_actor(sStAtkAt);st_face(sStAtkAt,sStAtkBy);}
    }
    {extern char*getenv(const char*);if(chapter==6&&sStAtkEvent==7&&f<=120&&f%30==0&&getenv("BT3_STORY_WAVE_PROBE")){Vec4 p;BtlCharApi_GetPos(st_obj(sStAtkBy),&p);Port_Dbg3("story shield shooter position",f,(int)p.x,(int)p.y,(int)p.z);BtlCharApi_GetPos(st_obj(sStAtkAt),&p);Port_Dbg3("story shield victim position",f,(int)p.x,(int)p.y,(int)p.z);}}
    if(sStAtkBy<0||sStAtkBy>=ST_ACTORS||sStAtkAt<0||sStAtkAt>=ST_ACTORS){sStAtkFrames=0;st_attack_restore_down();BattleResult_Set(2,1);return;}
    if(sStAtkLost){sStAtkFrames=0;st_attack_restore_down();BattleResult_Set(2,1);return;}
    if(sStAtkBack>=0&&Port_ClashBusy()&&((gPortClash[0]==sStAtkBy&&gPortClash[1]==sStAtkBack)||(gPortClash[1]==sStAtkBy&&gPortClash[0]==sStAtkBack))){
     sStAtkFrames=2; /* the player's real struggle may last4x longer */
     if(chapter==11&&sStAtkEvent==1){ /* the struggle is really on: Kaioken x4 now, while the beams push */
      if(!sStX4Said&&++sStX4Wait>=45){sStX4Said=1;sStX4Frames=st_frames(ev->line[2].text[0])+30;Port_StorySubtitle(chapter,ST_SET_EVENT0+1,2);Port_Dbg3("story x4 line during struggle",chapter,sStAtkBy,sStAtkBack,sStX4Wait);}
      else if(sStX4Frames>0&&--sStX4Frames==0)Port_StorySubtitle(-1,0,0);
     }
     return;}
    if (st_choreo_update(chapter, ev, f)) return;
    if (chapter == 11 && sStAtkEvent == 13) { /* Vegeta cuts the Great Ape's tail; the giant falls on him (the HP drop) */
        Vec4 p;
        if (f <= 10) { st_point_near(sStAtkAt, ST_TAIL_AT, 0.0f, ST_TAIL_UP, &p); st_move_toward(sStAtkBy, &p, 0.3f); st_face(sStAtkBy, sStAtkAt); }
        if (f == 8) BtlCtrl_PlayMotion(sStAtkBy, ST_ANIM_SWORD, 0, 0.05f); /* the stroke */
        if (f == 14) { /* the tail is cut: the giant reels, and Vegeta is right where he falls */
            st_presentation_queue(sStAtkAt, 2u | 4u); BtlCtrl_PlayMotion(sStAtkAt, 0xB3, 0, 0.05f);
            st_place_near(sStAtkBy, sStAtkAt, 0.5f, 0.0f, 0.0f); st_scene_ground_actor(sStAtkBy); st_face(sStAtkBy, sStAtkAt);
            Port_Dbg3("story ape tail cut", chapter, sStAtkBy, sStAtkAt, f);
        }
        if (f == 26) BtlCtrl_PlayMotion(sStAtkAt, ST_MOTION_PRONE, 1, 0.1f); /* he topples forward */
        if (f == 34) { /* onto Vegeta */
            st_presentation_queue(sStAtkBy, 2u | 4u); BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_PRONE, 1, 0.1f);
            if (sStAtkBy >= 0 && sStAtkBy < 32) sStDownActors |= 1u << sStAtkBy;
            Port_Dbg3("story ape falls on vegeta", chapter, sStAtkBy, sStAtkAt, f);
        }
        if (f == 38 && ev->transformTo > 0 && ev->transformCast >= 0) { /* and only then shrinks back to the boy */
            s32 who = Port_StoryCastActor(ev->transformCast);
            if (who >= 0) { Port_StoryGrantForm(who, ev->transformTo); sStFormActor = who; sStFormTarget = ev->transformTo; sStFormWait = 0; BtlCtrl_Transform(who, ev->transformTo); }
        }
        if (f >= 46) st_attack_done(chapter, ev); /* the event's damage (the HP drop) and Gohan's reversion */
        return;
    }
    if (ev->attackTech < 0) {
        if (f <= 12) { /* the lunge: close the distance */
            Vec4 a = {0, 0, 0, 1}, b = {0, 0, 0, 1};
            BtlCharApi_GetPos(st_obj(sStAtkBy), &a);
            BtlCharApi_GetPos(st_obj(sStAtkAt), &b);
            a.x += (b.x - a.x) * 0.2f; a.y += (b.y - a.y) * 0.2f; a.z += (b.z - a.z) * 0.2f;
            BtlCtrl_SetPos(sStAtkBy, &a);
        }
        landed = f == 14;
        if (landed) {
            st_presentation_queue(sStAtkAt, 2u | 4u); /* the strike / blast lands: flash + impact */
            BtlCtrl_PlayMotion(sStAtkAt, ST_ANIM_HIT, 0, 0.05f);
        }
    } else {
        gPortTarget[sStAtkBy] = sStAtkAt;
        if (sStAtkBack >= 0) gPortTarget[sStAtkBack] = sStAtkBy;
        landed=sStAtkContact&&BtlCtrl_IsInterruptible(sStAtkBy)&&BtlCtrl_IsInterruptible(sStAtkAt);
        if(chapter==6&&sStAtkEvent==7&&!landed&&f>30&&BtlCtrl_IsInterruptible(sStAtkBy)){
         if(!sStShieldWait)BtlCtrl_PlayMotion(sStAtkBy,ST_MOTION_IDLE,1,.1f); /* no free CPU attacks while awaiting this shot */
         sStShieldWait++;
        }
        if(!sStAtkContact&&!landed&&(f>ST_ATTACK_FRAMES||sStShieldWait>20)){
         if(!sStAtkRetry++){sStAtkFrames=1;sStShieldWait=0;Port_Dbg3("story attack retry no contact",chapter,sStAtkEvent,0,0);
          if(chapter==6&&sStAtkEvent==7){st_face(sStAtkAt,sStAtkBy);st_place_near(sStAtkBy,sStAtkAt,6.2f,0,0);st_scene_ground_actor(sStAtkBy);st_face(sStAtkBy,sStAtkAt);}
          Port_StoryUseTechnique(sStAtkBy,st_attack_slot(chapter,sStAtkEvent));return;}
         Port_Dbg3("story attack failed no contact",chapter,sStAtkEvent,0,0);sStAtkFrames=0;st_attack_restore_down();BattleResult_Set(2,1);return;
        }
        if(landed)st_presentation_queue(sStAtkAt,chapter==6&&sStAtkEvent==7?2u:2u|4u);
    }
    if (landed) {
        st_attack_done(chapter,ev);
    }
}

static void st_event_finish(s32 chapter) {
    sStLastEventEnd = sStFightFrames;
    const StEvent *ev = &stEvents[chapter].event[sStEventRun];
    s32 rival = sStRun.rival;
    if(sStEventPost){sStEventRun=-1;sStEventPost=0;st_scene_end();Battle_GetWork()->flags&=~BATTLE_FLAG_DEMO;Hud_SlideIn(.5f);if(ev->canon)BattleResult_Set(1,1);return;}
    sStEventRun = -1;
    if (ev->kind == ST_EVENT_GOHAN && !sStEventHit) { /* skipped before the hit: the hit still counts */
        sStEventHit = 1;
        st_set_hp(rival, st_hp(rival) > ST_GOHAN_DAMAGE ? st_hp(rival) - ST_GOHAN_DAMAGE : 1);
    }
    st_scene_end(); /* the kid goes back to parking */
    Battle_GetWork()->flags &= ~BATTLE_FLAG_DEMO;
    if(ev->kind!=ST_EVENT_MOON)Hud_SlideIn(0.5f); /* moon/tail/reversion stays cinematic through the ending */
    if (ev->transformTo > 0 && ev->transformCast >= 0 && ev->transformCast < stCasts[chapter].count &&
        !(ev->choreo == ST_CHOREO_TAILCUT && ev->attackCast > 0 && !sStEventAlt) && /* (a tail cut: when the stroke lands) */
        !(chapter == 11 && ev == &stEvents[11].event[13] && !sStEventAlt)) { /* (Great Ape Gohan: after he falls on Vegeta) */
        s32 who = Port_StoryCastActor(ev->transformCast); /* the native form change, granted for this fight */
        if (who >= 0) {
            Port_StoryGrantForm(who, ev->transformTo);
            sStFormActor=who;sStFormTarget=ev->transformTo;sStFormWait=0;BtlCtrl_Transform(who, ev->transformTo);
        }
    }
    if (ev->kind == ST_EVENT_SCENE && !sStEventAlt && (ev->attackCast > 0 || (chapter==11&&ev==&stEvents[11].event[4]))) {
        st_attack_begin(chapter, (s32)(ev - stEvents[chapter].event), rival); /* effects when it lands */
    } else if (ev->kind == ST_EVENT_SCENE) {
        st_event_effects(chapter, ev, rival);
    }
    if (ev->kind == ST_EVENT_MOON && sStMoonShooter >= 0 && sStMoonTarget >= 0) {
        sStMoonFrames = 1;
    }
    sStEventAlt = 0;
    if (ev->kind == ST_EVENT_BEAM && sStBeamLead >= 0 && sStBeamRival >= 0) {
        sStBeamFrames = 1;
        BtlCharApi_GetPos(st_obj(sStBeamLead), &sStBeamLeadPos);
        BtlCharApi_GetRot(st_obj(sStBeamLead), &sStBeamLeadRot);
        sStBeamHp = st_hp(sStBeamRival);
        sStBeamLanded = 0;
        if (ev->switchCast >= 0 && ev->switchCast < stCasts[chapter].count) { /* he changes sides for this attack */
            s32 who = Port_StoryCastActor(ev->switchCast);
            sStSwitchTeam = Port_Team(sStBeamRival);
            sStSwitchActor = who;
        }
        if (sStBeamPartner >= 0) {
            BtlCtrl_PlayMotion(sStBeamLead,ST_MOTION_IDLE,1,0.0f);
            BtlCtrl_PlayMotion(sStBeamRival,ST_MOTION_SURPRISED,1,0.0f);
            Port_StoryUseTechnique(sStBeamPartner, 4);
        }
    }
}

/* The beam is on its way: Goku keeps his grip; when it lands (or at the latest after ST_BEAM_FRAMES) both fall. */
static void st_beam_update(void) {
    s32 hp = st_hp(sStBeamRival);
    sStBeamFrames++;
    if (sStBeamLanded) { /* both blown away: the fight ends as any KO does; the native result if it has not */
        Vec4 a,b;
        BtlCharApi_GetPos(st_obj(sStBeamLead),&a);BtlCharApi_GetPos(st_obj(sStBeamRival),&b);
        if ((sStBeamFrames>=12 && a.y>=BtlCharApi_GetGroundY(st_obj(sStBeamLead))-1.0f && b.y>=BtlCharApi_GetGroundY(st_obj(sStBeamRival))-1.0f) || sStBeamFrames > ST_FINISH_FRAMES) {
            sStBeamFrames = 0;
            st_set_hp(sStBeamLead, 0);
            st_set_hp(sStBeamRival, 0);
            BattleResult_Set(1, 1);
        }
        return;
    }
    if (hp > 0 && hp >= sStBeamHp && st_hp(sStBeamLead) > 0 && sStBeamFrames <= ST_BEAM_FRAMES) {
        /* Goku and Raditz are both held (Port_StoryHeld: the native freeze), each at his own spot; Piccolo, the only
           one acting, aims at Raditz - and through him at Goku. */
        BtlCtrl_SetPos(sStBeamRival, &sStBeamPos);
        BtlCtrl_SetRot(sStBeamRival, &sStBeamRot);
        BtlCtrl_SetPos(sStBeamLead, &sStBeamLeadPos);
        BtlCtrl_SetRot(sStBeamLead, &sStBeamLeadRot);
        if (sStBeamPartner >= 0) {
            gPortTarget[sStBeamPartner] = sStBeamRival;
        }
        return;
    }
    /* The beam lands (or its time is up): released, Goku and Raditz both take the native hit - damage to KO and the
       blow-away reaction, from Piccolo - and fall as any KO'd fighter does. Goku is on Raditz's side now, so their
       team is down and the fight ends with Piccolo's side the winner. */
    sStBeamLanded = 1;
    sStBeamFrames = 1;
    Port_StoryBeamVictim(sStBeamLead, sStBeamPartner >= 0 ? sStBeamPartner : sStBeamRival, st_hp(sStBeamLead));
    Port_StoryBeamVictim(sStBeamRival, sStBeamPartner >= 0 ? sStBeamPartner : sStBeamLead, st_hp(sStBeamRival));
    st_presentation_queue(sStBeamLead,2u|4u);st_presentation_queue(sStBeamRival,2u|4u);
    if (st_hp(sStBeamLead) > 0) st_set_hp(sStBeamLead, 0);
    if (st_hp(sStBeamRival) > 0) st_set_hp(sStBeamRival, 0);
}

/* Chapter 4, after the lines: the shot into the sky (camera up, flash and impact sound at its middle: the moon), then the
   native beam at the Great Ape (the tail). When it lands - or after ST_MOON_BEAM_FRAMES - the chapter ends canonically:
   Gohan reverts in the ending (the sleeping child), nobody is knocked out. */
/* The normal blast destroys the moon; it never targets or damages Gohan. */
static void st_moon_update(void) {
    s32 f=++sStMoonFrames;
    if(f==2){
        Vec4 eye={0,0,0,1},rot={0,0,0,1},facing={0,0,0,1};f32 h=st_height(sStMoonShooter);
        BtlCtrl_PlayMotion(sStMoonTarget,ST_MOTION_SURPRISED,1,.15f);
        st_stage_actor(sStMoonShooter);BtlCharApi_GetPos(st_obj(sStMoonShooter),&eye);BtlCharApi_GetRot(st_obj(sStMoonShooter),&facing);
        {Vec4 at=eye;
         eye.x+=(-sinf(facing.y)*3.2f+cosf(facing.y)*2.f)*h;eye.z+=(-cosf(facing.y)*3.2f-sinf(facing.y)*2.f)*h;eye.y-=h*1.4f;
         at.y-=h*2.2f;st_camera_finish(eye,at,h,sStMoonShooter,3);}

        BtlCtrl_StopMotion(sStMoonShooter);Port_StoryUseTechnique(sStMoonShooter,2);
    }
    if(f==ST_MOON_SKY_FRAMES)st_presentation_queue(sStMoonShooter,2u|4u);
    if(!sStMoonTailAt&&f>=ST_MOON_SKY_FRAMES+20&&BtlCtrl_IsInterruptible(sStMoonShooter)){
        sStMoonTailAt=f;
        BtlCtrl_StopMotion(sStMoonShooter);st_stage_actor(sStMoonTarget);
        st_place_near(sStMoonShooter,sStMoonTarget,-.4f,.35f,0);st_face(sStMoonShooter,sStMoonTarget);
        BtlCtrl_PlayMotion(sStMoonShooter,ST_ANIM_SWORD,0,.1f);
        {Vec4 at,eye,rot={0,0,0,1};float h=st_height(sStMoonShooter),dx,dz;
         BtlCharApi_GetPos(st_obj(sStMoonShooter),&at);eye=at;eye.x+=h*2.5f;eye.z+=h*2.5f;eye.y-=h*1.1f;at.y-=h*.5f;
         dx=at.x-eye.x;dz=at.z-eye.z;rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));BtlFacade_SetFixedCamera(&eye,&rot);}
    }
    if(sStMoonTailAt&&f==sStMoonTailAt+12){
        st_presentation_queue(sStMoonTarget,2u|4u);BtlCtrl_StopMotion(sStMoonTarget);
        Port_StoryGrantForm(sStMoonTarget,13);BtlCtrl_Transform(sStMoonTarget,13);
    }
    if(sStMoonTailAt&&f>=sStMoonTailAt+13){int v[9];
        if(Port_StoryResourceIO(sStMoonTarget,0,v)&&v[0]==13&&BtlCtrl_IsInterruptible(sStMoonTarget)){
            st_stage_actor(sStMoonTarget);sStDownActors|=1u<<sStMoonTarget;
            BtlCtrl_PlayMotion(sStMoonTarget,ST_MOTION_PRONE,1,.1f);
            Battle_GetWork()->flags|=BATTLE_FLAG_DEMO;
            sStMoonFrames=0;BattleResult_Set(1,1);
        }else if(f<600){if(f%15==0&&BtlCtrl_IsInterruptible(sStMoonTarget))BtlCtrl_Transform(sStMoonTarget,13);}
        else {Port_Dbg3("story moon reversion failed",3,sStMoonTarget,13,0);sStMoonFrames=0;BattleResult_Set(2,1);}
    }
}
s32 Port_StorySkyAim(s32 obj,float*out){
    if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&obj==st_obj(Port_StoryCastActor(9))){
     Vec4 a,b;float d,x,y,z;int by=Port_StoryCastActor(9),at=Port_StoryCastActor(8);
     BtlCharApi_GetPos(st_obj(by),&a);BtlCharApi_GetPos(st_obj(at),&b);a.y-=st_height(by)*.6f;b.y-=st_height(at)*.6f;
     x=b.x-a.x;y=b.y-a.y;z=b.z-a.z;d=sqrtf(x*x+y*y+z*z);if(d>.01f){out[0]=x/d;out[1]=y/d;out[2]=z/d;out[3]=0;return 1;}
    }

    if(sStMoonFrames>0&&sStMoonShooter>=0&&obj==st_obj(sStMoonShooter)&&st_chapter()==3){out[0]=0;out[1]=-1;out[2]=0;out[3]=0;return 1;}
    if(st_chapter()>=0&&sStAtkFrames>0&&(sStAtkBack>=0||(st_chapter()==6&&sStAtkEvent==7))){int from=-1,to=-1;Vec4 a,b;float x,y,z,d;
      if(obj==st_obj(sStAtkBy)){from=sStAtkBy;to=sStAtkBack>=0?sStAtkBack:sStAtkAt;}else if(obj==st_obj(sStAtkBack)){from=sStAtkBack;to=sStAtkBy;}
      if(from>=0){BtlCharApi_GetPos(st_obj(from),&a);BtlCharApi_GetPos(st_obj(to),&b);if(st_chapter()==6&&sStAtkEvent==7){a.y-=st_height(from)*.6f;b.y-=st_height(to)*.6f;}x=b.x-a.x;y=b.y-a.y;z=b.z-a.z;d=sqrtf(x*x+y*y+z*z);
        if(d>.01f){out[0]=x/d;out[1]=y/d;out[2]=z/d;out[3]=0;return 1;}}
    }return 0;
}

/* The team of a fighter while a story event has switched him to the other side (StEvent.switchCast; btl_char_flag_opp.c
   Port_Team), else `team`. */
/* The native homing functions (eft_a.c EftAim_Home, eft_det_a.c EftVolleyAim_Steer) report each frame of a homing
   shot: the Spirit Bomb choreography follows its ball and turns it round once for Gohan's rebound. */
/* Every live projectile, each frame (eft_det_a.c EftDet_FighterPass): the Spirit Bomb's real position. */
void Port_StoryShotReport(s32 owner, f32 x, f32 y, f32 z) {
    if (sStAtkFrames <= 0 || sStSbStage < 1 || sStAtkBy < 0 || owner != sStAtkBy || st_chapter() < 0) return;
    sStSbBall.x = x; sStSbBall.y = y; sStSbBall.z = z; sStSbBall.w = 1.0f;
    sStSbSeen = 1;
}
void Port_StoryShotSteer(int objId, float *pos, float *dir) {
    if(Port_StorySkyAim(objId,dir))return;
    if (sStAtkFrames <= 0 || sStSbStage < 1 || sStAtkBy < 0 || st_chapter() < 0 || objId != st_obj(sStAtkBy)) return;
    sStSbBall.x = pos[0]; sStSbBall.y = pos[1]; sStSbBall.z = pos[2]; sStSbBall.w = 1.0f;
    sStSbSeen = 1;sStSbSteered = 1;
    {static int n;Vec4 v;if(n<400&&sStAtkAt>=0){n++;BtlCharApi_GetPos(st_obj(sStAtkAt),&v);Port_Dbg3("story spirit bomb steer",sStSbStage,(int)(st_dist(&v,&sStSbBall)*10.0f),(int)(st_height(sStAtkAt)*10.0f),sStAtkFrames);}}
    if (sStSbFlip) { dir[0] = -dir[0]; dir[1] = -dir[1]; dir[2] = -dir[2]; sStSbFlip = 0; }
}

/* The small Genkidama is a native type-9 projectile, not a homing blast.
 * Route that one live projectile through Gohan before admitting its Vegeta hit. */
int Port_StorySpiritBallVelocity(int objId, float *pos, float *vel, float *dir) {
    Vec4 target;float x,y,z,d,speed;int actor;
    if(sStAtkFrames<=0||sStSbStage<1||sStAtkBy<0||objId!=st_obj(sStAtkBy)||
       Port_StoryTechniqueDonor(sStAtkBy)<0)return 0;
    actor=sStSbStage==2?sStSbGohan:sStAtkAt;
    BtlCharApi_GetNodePos(st_obj(actor),0x11,&target);
    x=target.x-pos[0];y=target.y-pos[1];z=target.z-pos[2];d=sqrtf(x*x+y*y+z*z);
    speed=sqrtf(vel[0]*vel[0]+vel[1]*vel[1]+vel[2]*vel[2]);
    {extern char*getenv(const char*);if((sStAtkFrames%15)==0&&getenv("BT3_STORY_ACTOR_LOG"))
       Port_Dbg3("story live genkidama route",sStSbStage,(int)(d*100),(int)(pos[1]*100),(int)(target.y*100));}
    {float cap=st_height(actor)*.22f;if(speed>cap||speed<.001f)speed=cap;} /* resume the same ball after the instruction beat */
    if(sStSbStage==2&&d<st_height(actor)*.7f&&!st_sb_return_ready())speed=0;
    if(d>.001f){if(speed>d)speed=d;dir[0]=x/d;dir[1]=y/d;dir[2]=z/d;
        vel[0]=dir[0]*speed;vel[1]=dir[1]*speed;vel[2]=dir[2]*speed;}
    sStSbSteered=1;sStSbFlip=0;
    Port_StoryShotReport(sStAtkBy,pos[0],pos[1],pos[2]);
    return 1;
}

int Port_StorySpiritBallPass(int owner){
    return Port_StoryTechniqueDonor(owner)>=0&&sStSbStage>=1&&sStSbStage<3;
}
int Port_StorySpiritBallPending(int owner){
    return Port_StoryTechniqueDonor(owner)>=0&&sStSbStage>=1&&!sStAtkContact;
}

s32 Port_StoryTeam(s32 actor, s32 team) {
    /* Ending cinematic: Vegeta's beam can hit Nappa; normal team allegiance returns with the next shot. */
    if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==Port_StoryCastActor(9))return 0;
    /* Vegeta executes his own Saibaman: admit this native attack only while the authored shot is active. */
    if(sStAtkFrames>0 && actor==sStAtkBy && st_chapter()>=0 && sStAtkEvent>=0 && stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_EXECUTE)return 0;
    if (sStSbDodge >= 0 && actor == sStSbDodge && st_chapter() >= 0) return 0; /* dodging the ball: not its enemy */
    return sStSwitchActor >= 0 && actor == sStSwitchActor && st_chapter() >= 0 ? sStSwitchTeam : team;
}

/* Fighters the story holds still (the native freeze, btl_char_get.c BtlChar_IsFrozen): Goku and Raditz while the
   beam is on its way. */
/* Permanently removed Story bodies may finish an already-issued resource load,
   but can never rejoin the fight. Waiting on their paused action blocks all later loads. */
/* Nappa's sacrifice shot belongs to Piccolo alone, including any lingering AoE.
   Exact script health edits use42B and must still remove/KO the intended actors. */
int Port_StoryShieldBystander(int actor){
 return st_chapter()==6&&actor>=0&&actor<ST_ACTORS&&
  ((sStAtkFrames>0&&sStAtkEvent==7)||sStEventRun==7)&&actor!=Port_StoryCastActor(3);
}
int Port_StoryProtectedDamage(int actor,int flags){
 {static int inv=-1;extern char*getenv(const char*);if(inv<0)inv=getenv("BT3_STORY_INVULN_PROBE")!=0; /* test fixture only: heroes take no damage */
  if(inv&&st_chapter()>=0&&actor>=0&&actor<ST_ACTORS&&flags!=0x42B&&Port_Team(actor)==0&&!(sStAtkFrames>0&&actor==sStAtkAt))return 1;}
 if(st_chapter()!=6||actor<0||actor>=ST_ACTORS||flags==0x42B)return 0;
 if((sStAtkFrames>0&&sStAtkEvent==7)||sStEventRun==7){
  extern char*getenv(const char*);if(getenv("BT3_STORY_WAVE_PROBE"))Port_Dbg3("story shield damage admission",actor,flags,st_hp(actor),sStAtkFrames);
  return actor!=Port_StoryCastActor(3)||(sStAtkFrames>0&&!(flags&0x2000000));
 }
 return 0;
}

/* A melee hit of a listed ally on the chapter's rival (mod_story.h stPunchArmor): damage stays, the reaction does not.
   Cutscenes and authored attacks keep the reactions their staging needs. */
static int st_punch_armor(int by,int at){
 int ch=st_chapter(),i,bc=-1,ac=-1;
 if(ch<0||by<0||at<0||by>=ST_ACTORS||at>=ST_ACTORS||by==at)return 0;
 if(sStRun.active)return 0;
 if(sStAtkFrames>0&&(by==sStAtkBy||by==sStAtkBack))return 0;
 for(i=0;i<ST_ACTORS;i++){int a=Port_StoryCastActor(i);if(a<0)continue;if(a==by&&bc<0)bc=i;if(a==at&&ac<0)ac=i;}
 return ST_PunchArmor(ch,bc,ac);
}
int Port_StoryPunchArmor(int by,int at,int kind,int react,int byChara){
 int armour=st_punch_armor(by,at)&&ST_PunchArmorForm(st_chapter(),byChara);
 {static int log=-1;extern char*getenv(const char*);if(log<0)log=getenv("BT3_STORY_PUNCH_LOG")!=0;
  if(log&&st_chapter()>=0)Port_Dbg3(armour?"story punch armor":"story punch native",by,at,kind,react);}
 return armour;
}

/* Tail part node ids (standard BT3 skeleton ids; BT3_STORY_TAIL_NODES=lo-hi overrides, BT3_STORY_TAIL_ALL=1 applies it
   to every Story fighter, both only for locating/verifying the parts). */
#define ST_TAIL_NODE_LO (-1)
#define ST_TAIL_NODE_HI (-1)
int Port_StoryNodeHidden(void*obj,int node){
 static int lo=-2,hi=-1,all=0;int a;
 if(lo==-2){extern char*getenv(const char*);const char*e=getenv("BT3_STORY_TAIL_NODES");
  lo=ST_TAIL_NODE_LO;hi=ST_TAIL_NODE_HI;all=getenv("BT3_STORY_TAIL_ALL")!=0;
  if(e){int v=0,which=0;lo=hi=0;for(;*e;e++){if(*e>='0'&&*e<='9')v=v*10+(*e-'0');else if(*e=='-'&&!which){lo=v;v=0;which=1;}}if(which)hi=v;else lo=hi=-1;}}
 if((!sStTailCut&&!all)||node<lo||node>hi||st_chapter()<0||!obj)return 0;
 for(a=0;a<ST_ACTORS;a++)if((all||(sStTailCut>>a&1u))&&Port_FighterObjId(a)>=0&&(void*)BtlObj_Get(Port_FighterObjId(a))==obj)return 1;
 return 0;
}

s32 Port_StoryRetired(s32 actor){return st_chapter()>=0&&actor>=0&&actor<32&&(sStGoneActors>>actor&1u);}

s32 Port_StoryHeld(s32 actor) {
    if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==Port_StoryCastActor(9))return 1;
    if(actor==sStSbThrower&&Port_StorySpiritBallPending(actor))return 1;
    if(st_chapter()==3&&actor>=0&&actor<32&&(sStDownActors>>actor&1u))return 1;
    if(st_chapter()==11&&actor>=0&&actor<32&&(sStDownActors>>actor&1u)&&actor==Port_StoryCastActor(4))return 1; /* Vegeta under the fallen giant: he can't get up */
    if(sStRun.active&&actor>=0&&actor<32&&(sStRun.actors>>actor&1u))return 1;
    if(sStAtkFrames>0&&actor>=0&&actor!=sStAtkBy&&actor!=sStAtkAt&&actor!=sStAtkBack&&!(sStSbStage==3&&actor==sStSbGohan))return 1;
    if(sStAtkFrames>0&&sStAtkEvent>=0&&st_chapter()>=0&&stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_CLASH&&(actor==sStAtkBy||actor==sStAtkAt))return 1;
    if (sStMoonFrames > 0 && (!sStMoonTailAt||sStMoonFrames<sStMoonTailAt+12) && actor >= 0 && actor == sStMoonTarget && st_chapter() >= 0) {
        return 1; /* the Great Ape stays where the shot finds him */
    }
    if (sStAtkFrames > 0 && sStSbStage >= 1 && sStSbStage < 3 && actor >= 0 && actor == sStSbGohan && st_chapter() >= 0) {
        return 1; /* Gohan waits for the ball */
    }
    if (sStAtkFrames > 0 && !sStAtkFree && actor >= 0 && actor == sStAtkAt && st_chapter() >= 0 && sStAtkEvent >= 0 &&
        stEvents[st_chapter()].event[sStAtkEvent].choreo != ST_CHOREO_CLASH) {
        return 1; /* an authored attack's target stays put until it lands */
    }
    return sStBeamFrames > 0 && !sStBeamLanded && actor >= 0 && (actor == sStBeamLead || actor == sStBeamRival) && st_chapter() >= 0;
}

/* Native restart/path/saved-placement passes run AFTER the motion hold. The
   ending owns these two roots until its next shot; apply that ownership last,
   immediately before pose-to-object, without interrupting the beam animation. */
int Port_StoryScenePlacementOwned(int actor){
 return st_chapter()==6&&sStRun.active&&actor>=0&&actor<32&&(sStRun.actors>>actor&1u);
}
void Port_StoryRestoreEndingPlacement(int actor){
 int slot=-1;
 if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_EVENT0+2&&sStNappaIntroAnchorReady&&actor==Port_StoryCastActor(8)){
  BtlCtrl_SetPos(actor,&sStNappaIntroAnchor);return;
 }
 if(st_chapter()!=6||!sStRun.active||sStRun.set!=ST_SET_CANON||sStRun.index>1||!sStEndingAnchorReady)return;
 if(actor==Port_StoryCastActor(9))slot=0;else if(actor==Port_StoryCastActor(8))slot=1;
 if(slot>=0)BtlCtrl_SetPos(actor,&sStEndingDrawAnchor[slot]);
 {extern char*getenv(const char*);if(slot==1&&sStRun.frames%30==0&&getenv("BT3_STORY_SCENE_LOG")){
   Vec4 p,head;int v=Port_StoryCastActor(9);
   BtlCharApi_GetPos(st_obj(actor),&p);Port_Dbg3("story ending nappa root",sStRun.frames,(int)(p.x*100),(int)(p.y*100),(int)(p.z*100));
   BtlCharApi_GetNodePos(st_obj(actor),0x2E,&head);Port_Dbg3("story ending nappa head",(int)(st_height(actor)*100),(int)(head.x*100),(int)(head.y*100),(int)(head.z*100));
   BtlCharApi_GetPos(st_obj(v),&p);Port_Dbg3("story ending vegeta root",sStRun.index,(int)(p.x*100),(int)(p.y*100),(int)(p.z*100));
   Port_Dbg3("story ending camera eye",sStRun.index,(int)(sStRun.exitEye.x*100),(int)(sStRun.exitEye.y*100),(int)(sStRun.exitEye.z*100));
 }}
}

/* A grip is NOT a full simulation freeze: frozen targets reject projectiles.
   The scripted pose holds movement; only its collision immunity is removed. */
extern void BtlChar_SetFxBit(void*,int),BtlChar_ClearFlag(void*,int);
extern void BtlCharSnd_PlayCommonFar(void*,int);
extern int BtlParam_GetChargeLoopSound(void*);
extern void Port_StoryHitFx(int);
extern void Port_StoryChargeSpark(int,int);
void Port_StoryFxFrame(int actor){
 int charge=0,beamCharge=0,kid=-1;unsigned pending;void*c;
 if(actor<0||actor>=16)return;
 c=BtlChar_Get(actor);if(!c)return;
 if((sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==Port_StoryCastActor(8))||(sStAtkFrames>0&&actor==sStAtkAt)||(sStBeamFrames>0&&(actor==sStBeamLead||actor==sStBeamRival))){int f;for(f=0x42;f<=0x46;f++)BtlChar_ClearFlag(c,f);BtlChar_ClearFlag(c,0x128);}
 if(st_chapter()>=0&&sStEventRun>=0){
  const StEvent*ev=&stEvents[st_chapter()].event[sStEventRun];
  if(ev->kind==ST_EVENT_GOHAN){kid=(int)sStEventTo.w;charge=actor==kid&&sStGohanFrames>0&&!sStEventHit;}
  else if(ev->kind==ST_EVENT_BEAM)beamCharge=charge=actor==sStBeamPartner;
  else if(ev->kind==ST_EVENT_MOON)beamCharge=charge=actor==sStMoonShooter;
  else if(ev->kind==ST_EVENT_SCENE&&!sStEventAlt){int ch=st_chapter();int who=ev->chargeCast>0?Port_StoryCastActor(ev->chargeCast-1):
   ev->attackCast>0&&ev->attackTech>=0?Port_StoryCastActor(ev->attackCast-1):-1;(void)ch;beamCharge=charge=who>=0&&actor==who;
   if(ev->counterCast>0&&actor==Port_StoryCastActor(ev->counterCast-1)&&!sStEventPost)beamCharge=charge=1;}
 }
 if(sStAtkFrames>0&&!sStAtkFired&&sStAtkEvent>=0&&actor==sStAtkBy&&st_chapter()>=0&&
    stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_CLING)beamCharge=charge=1; /* Chiaotzu gathers it all, holding on */
 if(sStAtkFrames>0&&sStSbStage==0&&actor==sStSbThrower&&sStAtkEvent>=0&&st_chapter()>=0&&
    stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_SPIRITBOMB)beamCharge=charge=1;
 if(sStAtkFrames>0&&sStAtkEvent>=0&&st_chapter()>=0&&stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_TRANSFER){
  if(actor==sStAtkAt&&sStAtkFrames>ST_XFER_REACH&&sStAtkFrames<=ST_XFER_GIVE)beamCharge=1;      /* Goku's hands glow */
  if(actor==sStAtkBy&&sStAtkFrames>=(ST_XFER_REACH+ST_XFER_GIVE)/2)beamCharge=charge=1;        /* Krillin takes it in */
 }
 if(beamCharge){BtlChar_SetFxBit(c,0x12);}
 if(beamCharge!=sStBeamChargeActive[actor]){Port_StoryChargeSpark(actor,beamCharge);sStBeamChargeActive[actor]=beamCharge;}
 if(sStRun.active&&sStRun.arriving==actor&&sStRun.frames<=40&&(sStRun.bridge||(sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8&&sStRun.index==2))){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);charge=1;}
 if(sStRun.active&&sStRun.departing&&actor==sStRun.rival&&sStRun.frames>=9){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);}
 if(charge){BtlChar_SetFxBit(c,0x13);BtlChar_SetFxBit(c,7);BtlCharSnd_PlayCommonFar(c,BtlParam_GetChargeLoopSound(c));}
 else if(sStAuraActive[actor])BtlChar_SetFxBit(c,0x14);
 sStAuraActive[actor]=charge;
 pending=sStPresentation[actor];sStPresentation[actor]=0;
 if(pending&1u)BtlCharSnd_PlayCommonFar(c,ST_SE_DASH);
 if(pending&2u)BtlCharSnd_PlayCommonFar(c,ST_SE_STRIKE);
 if(pending&4u)Port_StoryHitFx(actor);
}
int Port_StoryEndPending(void){
 s32 ch=st_chapter(),i,a,alive=0;
 if(Port_StoryActive()&&sStBeamFrames>0&&sStBeamLanded)return 1;
 if(Port_StoryActive()&&(sStAtkFrames>0||sStMoonFrames>0||sStIntroFormFrames>0||sStEventRun>=0))return 1;
 if(ch<0||!ST_Phased(ch)||sStAltEnd>=0||(sStEventsDone&ST_CanonMask(ch))==ST_CanonMask(ch))return 0;
 for(i=0;i<stCasts[ch].count;i++){a=Port_StoryCastActor(i);if(a>=0&&a<32&&stCasts[ch].actor[i].team==0&&!(sStGoneActors>>a&1u)&&!Port_StorySceneParked(a)&&st_hp(a)>0)alive=1;}
 return alive; /* the next phase is coming; with the player's whole side down the battle ends (a failure) */
}
/* Story-dead fighters (a story KO, a story death) are never revived by an ally (mod_revive_native.inc). */
int Port_StoryNoRevive(int actor){int ch=st_chapter(),k;if(ch<0||actor<0||actor>=32)return 0;
 if((sStDownActors|sStGoneActors)>>actor&1u)return 1;
 for(k=0;k<stCasts[ch].count;k++)if(Port_StoryCastActor(k)==actor&&stCasts[ch].actor[k].chara==79)return 1;
 return 0;}

/* The enemy the player is facing now in a phased chapter: the first team-1 cast fighter in the fight (not a mere
   observer, not waiting on the sidelines, not taken out by the story). A knocked-out phase enemy still counts. */
static s32 st_rival_now(s32 chapter) {
    const StCast *c = &stCasts[chapter];
    s32 i, a;
    for (i = 0; i < c->count; i++) {
        if (c->actor[i].team != 1 || ((c->actor[i].flags & ST_ACTOR_SCENE) && !(c->actor[i].flags & ST_ACTOR_RESERVE))) continue;
        a = Port_StoryCastActor(i);
        if (a < 0 || a >= 32 || (sStGoneActors >> a & 1u) || Port_StorySceneParked(a)) continue;
        return a;
    }
    if(chapter==6&&(sStEventsDone&7u)==3u)return Port_StoryCastActor(10); /* last wave defeated: retain only the phase reference, not its model */
    return -1;
}

/* Whether an event's moment has come: the rival's absolute HP, his HP percentage, or the fight's elapsed time. */
static s32 st_event_due(const StEvent *ev, s32 rival, s32 hp) {
    s32 max;
    if(st_chapter()==11&&ev==&stEvents[11].event[12]){
        int ape=Port_StoryCastActor(1),mx=ape>=0?BtlCharApi_GetHpMax(st_obj(ape)):0;
        return mx>0&&st_hp(ape)*100LL<=mx*30LL&&sStFightFrames-sStLastEventEnd>=ev->threshold*30;
    }
    switch (ev->trigger) {
    case ST_TRIG_TIME:
        return sStFightFrames >= ev->threshold * 30;
    case ST_TRIG_AFTER:
        return sStFightFrames - sStLastEventEnd >= ev->threshold * 30;
    case ST_TRIG_RIVAL_PCT:
        max = rival >= 0 ? (s32)BtlCharApi_GetHpMax(st_obj(rival)) : 0;
        return hp > 0 && max > 0 && (long long)hp * 100 <= (long long)ev->threshold * max;
    default:
        return hp > 0 && hp <= ev->threshold;
    }
}

/* Fight frame of the events: 1 while an event owns it (scene, beam), else 0 (offers only). */
static s32 st_event_update(s32 chapter) {
    const StEvents *evs = &stEvents[chapter];
    s32 i, hp, rival;

    if (sStEventRun >= 0) {
        st_scene_beat(&sStEventCtx);
        if (!st_scene_hold(&sStEventCtx)) {
            st_event_finish(chapter);
        } else {
            st_event_frame(chapter);
        }
        return 1;
    }
    if (sStBeamFrames > 0) {
        st_beam_update();
        return 1;
    }
    if (sStMoonFrames > 0) {
        st_moon_update();
        return 1;
    }
    if (sStAtkFrames > 0) {
        st_attack_update(chapter);
        return 1;
    }
    if (chapter == 11 && sStSbLine >= 1 && sStSbLine <= 3) return 1; /* the next scene waits for the Spirit Bomb line to be read */
    sStEventOffer = -1;
    rival = ST_Phased(chapter) ? st_rival_now(chapter) : st_cast_find(chapter, 1, 0, 0);
    if (evs->count <= 0 || rival < 0) {
        sStEventPressed = 0;
        return 0;
    }
    hp = st_hp(rival);
    if (ST_Phased(chapter) || chapter==3) { /* strictly in order; a knocked-out phase enemy brings his event at once */
        for (i = 0; i < evs->count && (sStEventsDone >> i & 1u); i++) {
        }
        if(i<evs->count&&evs->event[i].checkpoint){if(sStCheckpoint.chapter!=chapter||sStCheckpoint.event!=i)st_checkpoint_capture(chapter,i);}
        if (i >= evs->count) {
            sStEventPressed = 0;
            return 0;
        }
        /* Explicit isolated native regression probe: bring the first two wave enemies to their R3 moments. */
        {extern char*getenv(const char*);if(chapter==6&&i<2&&getenv("BT3_STORY_WAVE_PROBE")){
            int mx=BtlCharApi_GetHpMax(st_obj(rival)),at=mx*(evs->event[i].threshold-5)/100;
            if(hp>at){st_set_hp(rival,at);hp=st_hp(rival);}
            if(sStFightFrames%30==0)Port_Dbg3("story wave actual hp",i,st_lead_now(chapter),rival,hp);
        }}
        {extern char*getenv(const char*);if(chapter==6&&i==0&&getenv("BT3_STORY_WAVE_KO_PROBE")){st_set_hp(rival,0);hp=0;}}
        if(chapter==6&&i==2){int j;
          for(j=10;j<=14;j++){int foe=Port_StoryCastActor(j);if(j==13||foe<0)continue;
           if(!(sStGoneActors>>foe&1u)&&!Port_StorySceneParked(foe)&&st_hp(foe)>0){sStEventPressed=0;return 0;}}
        }
        if (!st_event_due(&evs->event[i], rival, hp) && !(hp <= 0 && evs->event[i].trigger != ST_TRIG_AFTER)) {
            sStEventPressed = 0;
            return 0;
        }
        if (hp <= 0 && evs->event[i].koAlt == 2) { /* beaten before the story moment: the battle ends on the alternative */
            sStAltEnd = i;
            sStEventPressed = 0;
            BattleResult_Set(1, 1);
            return 0;
        }
        if (hp <= 0 && st_event_ready(chapter)) {
            sStEventAlt = evs->event[i].koAlt == 1 && evs->event[i].altCount > 0;
            st_event_start(chapter, i);
            return 1;
        }
    } else {
        for (i = evs->count - 1; i >= 0; i--) {
            if (st_event_due(&evs->event[i], rival, hp)) {
                break;
            }
        }
    }
    if (i < 0 || (sStEventsDone >> i & 1u)) {
        sStEventPressed = 0;
        return 0;
    }
    if (evs->event[i].autoStart) { /* an AUTO beat: no R3, it starts as soon as the fighters can be interrupted */
        sStEventPressed = 0;
        if (st_event_ready(chapter)) {
            st_event_start(chapter, i);
            return 1;
        }
        return 0;
    }
    sStEventOffer = i;
    if (sStEventPressed) {
        if (st_event_ready(chapter)) {
            sStEventPressed = 0;
            st_event_start(chapter, i);
            return 1;
        }
    }
    return 0;
}

/* State 3, every unpaused frame: the authored events first; says the next due line, one at a time, each once; clears
   it after its time. */
static void st_fight_update(void) {
    s32 chapter = st_chapter();
    const StScene *sc;
    s32 i;

    if (chapter < 0 || (Battle_GetWork()->flags & BATTLE_FLAG_PAUSE)) {
        return;
    }
    if(chapter==6){int k,a;for(k=10;k<=14;k++){if(k==13)continue;a=Port_StoryCastActor(k);
      {static int probe=-1;extern char*getenv(const char*);if(probe<0)probe=getenv("BT3_STORY_WAVE_PROBE")!=0;
       if(probe&&a>=0&&!Port_StorySceneParked(a)&&st_hp(a)>1000)st_set_hp(a,1000); /* fixture only: real hits still defeat the wave */}

      if(a>=0&&!Port_StorySceneParked(a)&&!(sStGoneActors>>a&1u)&&st_hp(a)<=0&&Port_SequencePartner(a)<0){sStGoneActors|=1u<<a;Port_StoryRemoveActor(a);}
    }}

    if(chapter==6&&!sStRun.active&&sStAtkFrames<=0){int v=Port_StoryCastActor(9),t=st_lead_now(chapter);if(v>=0&&t>=0&&v!=t)st_face(v,t);}
    if (sStEventRun < 0 && sStBeamFrames <= 0) {
        sStFightFrames++;
    }
    if(st_checkpoint_restore())return;
    if(chapter==6&&!sStRun.active&&sStEventRun<0&&sStAtkFrames<=0){
      int k,n=st_rival_now(chapter);extern void Port_StoryResumeCombatActor(int,int);
      if(n==Port_StoryCastActor(8)&&st_hp(n)>0)for(k=2;k<=4;k++){
        int a=Port_StoryCastActor(k);
        if(a>=0&&!Port_StorySceneParked(a)&&!(sStGoneActors>>a&1u)&&st_hp(a)>0&&a!=sStTiredActor)
          Port_StoryResumeCombatActor(a,n);
      }
    }
    if(chapter==6&&sStCh7ReleaseFrames>0&&!sStRun.active&&sStEventRun<0&&sStAtkFrames<=0){
      int k,a,n=st_rival_now(chapter);extern void Port_StoryReleaseActor(int);
      for(k=0;k<stCasts[chapter].count;k++){a=Port_StoryCastActor(k);
        if(a<0||Port_StorySceneParked(a)||(sStGoneActors>>a&1u)||st_hp(a)<=0)continue;
        if(n>=0&&n!=a&&Port_Team(n)!=Port_Team(a))gPortTarget[a]=n;
        Port_StoryReleaseActor(a);
      }
      --sStCh7ReleaseFrames;
    }
    /* Owned regression fixture: deliberately start the execution victim high
       above the terrain, then exercise the real native suicide/camera handoff.
       No checkpoint/save mutation; ordinary launchers strip BT3_STORY_* vars. */
    {static int seeded,waveWait;extern char*getenv(const char*);
     if(chapter==6&&getenv("BT3_STORY_CH7_EXECUTION_PROBE")){
      if(!seeded&&sStFightFrames==1){int a=Port_StoryCastActor(7);Vec4 p;seeded=1;
       BtlCharApi_GetPos(st_obj(a),&p);p.y-=2000;BtlCtrl_SetPos(a,&p);st_set_hp(a,1);
      }
      if(!sStRun.active&&sStEventRun<0&&sStAtkFrames<=0){
       if((sStEventsDone&1u)&&!(sStEventsDone&2u))st_set_hp(Port_StoryCastActor(13),1);
       if((sStEventsDone&2u)&&!(sStEventsDone&4u)&&++waveWait>180){int k;for(k=10;k<=14;k++)if(k!=13)st_set_hp(Port_StoryCastActor(k),0);}
      }
     }}
    /* Owned segmented native test: seed the consequences before the throw,
       then run the real event/animation/effect/contact paths. Default off. */
    {static int finalApplied;extern char*getenv(const char*);
     if(!finalApplied&&chapter==11&&getenv("BT3_STORY_FINAL_KO_PROBE")){
      int k,v=Port_StoryCastActor(4);finalApplied=1;
      for(k=0;k<=14;k++){st_event_effects(chapter,&stEvents[chapter].event[k],v);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}
      st_set_hp(Port_StoryCastActor(0),0);sStLastEventEnd=sStFightFrames;
     }}
    {static int applied;extern char*getenv(const char*);
     if(!applied&&chapter==11&&sStFightFrames==1&&getenv("BT3_STORY_GENKIDAMA_PROBE")){
      int k,v=Port_StoryCastActor(4);applied=1;
      for(k=0;k<10;k++){st_event_effects(chapter,&stEvents[chapter].event[k],v);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}
      st_set_hp(Port_StoryCastActor(0),0);st_set_hp(v,BtlCharApi_GetHpMax(st_obj(v))/5);
      sStLastEventEnd=sStFightFrames;
     }}
    {static int applied;extern char*getenv(const char*);
     if(!applied&&chapter==11&&getenv("BT3_STORY_APE_PHASE_PROBE")){
      int k,v=Port_StoryCastActor(4);applied=1;
      for(k=0;k<=10;k++){st_event_effects(chapter,&stEvents[chapter].event[k],v);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}
      Port_StoryHandControl(Port_StoryCastActor(1));st_set_hp(Port_StoryCastActor(0),0);
      sStLastEventEnd=sStFightFrames;
     }
     if(chapter==11&&(sStEventsDone&(1u<<11))&&!(sStEventsDone&(1u<<12))&&sStFightFrames%15==0&&getenv("BT3_STORY_SCENE_LOG")){
      int g=Port_StoryCastActor(1),k=Port_StoryCastActor(2),v=Port_StoryCastActor(4);extern void Port_StoryControlProbe(int);
      Port_Dbg3("story three-way phase",k,Port_Team(k),Port_Team(g),Port_Team(v));Port_StoryControlProbe(k);Port_StoryControlProbe(g);Port_StoryControlProbe(v);
     }}
    {static int probe=-1,applied,lowered;extern char*getenv(const char*);
     if(probe<0)probe=getenv("BT3_STORY_NAPPA_CHAIN_PROBE")!=0;
     if(probe&&chapter==6){int n=Port_StoryCastActor(8),k;
      if(!applied){applied=1;for(k=0;k<3;k++){int rival=st_rival_now(chapter);sStEventAlt=0;st_event_effects(chapter,&stEvents[chapter].event[k],rival);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*89/100);}
      if(sStEventRun<0&&sStAtkFrames<=0){
       if((sStEventsDone&(1u<<3))&&!(lowered&1)&&sStFightFrames-sStLastEventEnd>300){st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*60/100);lowered|=1;}
       if((sStEventsDone&(1u<<5))&&!(lowered&2)&&sStFightFrames-sStLastEventEnd>240){st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*49/100);lowered|=2;}
       if((sStEventsDone&(1u<<6))&&!(lowered&4)){st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*39/100);lowered|=4;}
      }
     }
    }
    /* Explicit owned native regression: replay earlier checkpoint consequences,
       then exercise the real shield/Masenko/arrival with Piccolo and Krillin KO.
       No reads/writes to another game; absent env leaves ordinary play unchanged. */
    {static int probe=-2,applied;extern char*getenv(const char*);
     if(probe==-2)probe=getenv("BT3_STORY_SHIELD_PROBE")!=0;
     if(probe&&chapter==6&&!applied){int k,n=Port_StoryCastActor(8);applied=1;
      for(k=0;k<7;k++){int r=st_rival_now(chapter);sStEventAlt=0;st_event_effects(chapter,&stEvents[chapter].event[k],r);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}
      if(!getenv("BT3_STORY_SHIELD_ALIVE_PROBE")){st_set_hp(Port_StoryCastActor(2),0);st_set_hp(Port_StoryCastActor(3),0);}
      st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*39/100);sStLastEventEnd=sStFightFrames;
      Port_Dbg3("story shield probe staged",chapter,Port_StoryCastActor(2),Port_StoryCastActor(3),n);
     }
    }
    {extern char*getenv(const char*);if(chapter==6&&sStFightFrames%15==0&&getenv("BT3_STORY_WAVE_PROBE")){int k;extern void Port_StoryControlProbe(int);for(k=2;k<=6;k++)Port_StoryControlProbe(Port_StoryCastActor(k));}}
    {extern char*getenv(const char*);if(chapter==6&&sStFightFrames%15==0&&getenv("BT3_STORY_WAVE_PROBE")&&!sStRun.active&&sStEventRun<0&&sStAtkFrames<=0){
      Port_Dbg3("story ch7 free boundary",sStEventsDone,st_lead_now(6),st_rival_now(6),sStCh7ReleaseFrames);
    }}
    if (ST_Interlude(chapter)) { /* a scene-only interlude: over as soon as its opening is */
        if (!sStInterludeEnded) { sStInterludeEnded = 1; BattleResult_Set(1, 1); }
        return;
    }
    if(sStFormActor>=0){int v[9];
      if(Port_StoryResourceIO(sStFormActor,0,v)&&v[0]==sStFormTarget){Port_Dbg3("story form confirmed",chapter,sStFormActor,sStFormTarget,0);
       if(chapter==11&&sStFormActor==Port_StoryCastActor(4)&&sStFormTarget==29&&(sStEventsDone&(1u<<6))){extern void Port_StoryTailOutfit(int);Port_StoryTailOutfit(sStFormActor);if(sStFormActor>=0&&sStFormActor<32)sStTailCut|=1u<<sStFormActor;}
       if(chapter==11&&sStFormTarget==29&&st_hp(sStFormActor)>0){sStTiredActor=sStFormActor;sStTiredFrames=ST_TIRED_FRAMES;Port_Dbg3("story tired after reversion",chapter,sStFormActor,sStFormTarget,0);}
       sStFormActor=-1;}
      else if(++sStFormWait>300){Port_Dbg3("story form failed",chapter,sStFormActor,sStFormTarget,0);sStFormActor=-1;BattleResult_Set(2,1);return;}
      else {if(sStFormWait%15==0&&BtlCtrl_IsInterruptible(sStFormActor))BtlCtrl_Transform(sStFormActor,sStFormTarget);return;}
    }
    {extern char*getenv(const char*);extern void Port_StoryActorProbe(s32,s32);static int al=-1;if(al<0)al=getenv("BT3_STORY_ACTOR_LOG")!=0;
     if(al&&sStFightFrames%30==0){int i,a;for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0)Port_StoryActorProbe(a,(sStFightFrames/30)&0xFFFF);}}}
    {extern char*getenv(const char*);if(chapter==6&&getenv("BT3_STORY_NAPPA_END_PROBE")&&(sStEventsDone&(1u<<8))&&!(sStEventsDone&(1u<<9))&&sStEventRun<0&&sStAtkFrames<=0){int n=Port_StoryCastActor(8);st_set_hp(n,BtlCharApi_GetHpMax(st_obj(n))*20/100);}}
    if(chapter==6&&sStFightFrames%30==0&&sStEventRun<0&&sStAtkFrames<=0){int n=Port_StoryCastActor(8),v[9];
      if(n>=0&&!Port_StorySceneParked(n)&&st_hp(n)>0&&Port_StoryResourceIO(n,0,v)){v[4]+=4000;if(v[4]>v[5])v[4]=v[5];Port_StoryResourceIO(n,1,v);}}
    st_sb_lines(chapter); /* (after the ball has landed too, until the next event) */
    if(chapter==11&&sStEventRun<0&&!sStRun.active&&sStAtkFrames<=0){int veg=Port_StoryCastActor(4);extern void Port_StoryHoldPose(int,int); /* he stays under the giant's weight */
     if(veg>=0&&veg<32&&(sStDownActors>>veg&1u)&&st_hp(veg)>0)Port_StoryHoldPose(veg,ST_MOTION_PRONE);
     {int g=Port_StoryCastActor(1);if(g>=0&&g<32&&(sStDownActors>>g&1u)&&sStFormActor!=g)Port_StoryHoldPose(g,ST_MOTION_PRONE);}}
    if(sStTiredActor>=0&&sStTiredFrames>0){extern void Port_StoryHoldPose(int,int);extern void Port_StoryReleaseActor(int);
     if(st_hp(sStTiredActor)<=0){sStTiredActor=-1;sStTiredFrames=0;}
     else if(sStEventRun>=0||sStRun.active||sStAtkFrames>0){} /* a scene owns him meanwhile: the tired beat waits for free play */
     else if(sStDownActors>>sStTiredActor&1u){sStTiredActor=-1;sStTiredFrames=0;} /* already lying down */
     else{Port_StoryHoldPose(sStTiredActor,ST_MOTION_TIRED);
      if(--sStTiredFrames<=0){BtlCtrl_StopMotion(sStTiredActor);Port_StoryReleaseActor(sStTiredActor);sStTiredActor=-1;}}}
    if (st_event_update(chapter)) {
        return;
    }
    if (sStCueFrames > 0) {
        if (--sStCueFrames == 0) {
            Port_StorySubtitle(-1, 0, 0);
        }
        return;
    }
    sc = &stScenes[chapter];
    for (i = 0; i < sc->cueCount; i++) {
        s32 rp = st_percent(1);
        if (ST_Phased(chapter)) {
            s32 r = st_rival_now(chapter), mx = r >= 0 ? (s32)BtlCharApi_GetHpMax(st_obj(r)) : 0;
            rp = mx > 0 ? (s32)((long long)st_hp(r) * 100 / mx) : 100;
        }
        if(chapter==6&&i>0){int r=st_rival_now(chapter),role;for(role=0;role<stCasts[chapter].count;role++)if(Port_StoryCastActor(role)==r)break;if(r<0||role>=stCasts[chapter].count||stCasts[chapter].actor[role].chara!=80)continue;if(i==3&&!(sStEventsDone&(1u<<8)))continue; /* Gohan only reacts after Goku actually arrives. */}
        if (!(sStCuesDone & (1u << i)) && ST_CueDue(&sc->cue[i], sStFightFrames / 30, rp, st_percent(0))) {
            sStCuesDone |= 1u << i;
            sStCueFrames = st_frames(sc->cue[i].line.text[0]);
            Port_StorySubtitle(chapter, ST_SET_COMBAT, i);
            return;
        }
    }
}

/* Ending scenes own the camera/motions until state6's fade is fully black.
   Releasing them in WinTalk exit exposes one last Duel shot and lets the
   defeated escaping actor fall before the fade completes. */
static void st_ending_finish(void) {
    s32 i;
    for(i=0;i<ST_ACTORS;i++)if(sStRun.cast[i]>=0)BtlCharApi_GetPos(st_obj(sStRun.cast[i]),&sStRun.endPos[i]);
    sStRun.finishing=1;
    Port_StorySubtitle(-1,0,0);
}

static void st_ending_fade_hold(void) {
    s32 i;
    if(!sStRun.active||!sStRun.finishing||st_chapter()<0)return;
    for(i=0;i<ST_ACTORS;i++)if(sStRun.cast[i]>=0&&sStRun.cast[i]<32&&(sStRun.actors>>sStRun.cast[i]&1u))BtlCtrl_SetPos(sStRun.cast[i],&sStRun.endPos[i]);
    st_scene_camera();
}

/* Release after opening scenes, or after the ending fade is already opaque. Scene-only actors go back to parking. */
static void st_scene_end(void) {
    s32 i;
    st_attack_restore_down(); /* also closes the borrowed native-contact slot on surrender/abort */
    st_prefight_stop(st_chapter()>=0);sStPreludeJustFinished=0;
    if(sStRun.active){
        for(i=0;i<32;i++)if(sStRun.actors>>i&1u){BtlCtrl_StopMotion(i);{extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(i);}if(Port_StorySceneActor(i))st_scene_take(i,0);}
    }
    if(st_chapter()>=0||sStRun.chapter>=0)BtlFacade_ClearFixedCamera();
    if (st_chapter() >= 0 || sStRun.chapter >= 0) {
        Port_StorySubtitle(-1, 0, 0);
    }
    if (sStEventRun >= 0) {
        sStEventRun = -1;
        Battle_GetWork()->flags &= ~BATTLE_FLAG_DEMO;
    }
    sStRun.chapter = -1;
    sStRun.active=sStRun.departing=sStRun.finishing=sStRun.bridge=0;
    sStRun.actors=0;
    if(st_chapter()==6)sStCh7ReleaseFrames=3;
    sStCueFrames = 0;
}

/* The moon blast is scenery choreography, not an attack against the captured child.
   Keep the native beam/effects, suppress only its transient collision record. */
s32 Port_StorySkyShot(s32 obj){return st_chapter()==3&&sStMoonFrames>0&&sStMoonShooter>=0&&obj==st_obj(sStMoonShooter);}

s32 Port_StoryOwnAttackCamera(s32 obj){
 if(Port_StorySkyShot(obj))return 1;
 if(Port_StoryTechniqueDonor(sStSbThrower)>=0&&obj==st_obj(sStSbThrower))return !sStAtkContact;
 return st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent>=0&&sStAtkBy>=0&&obj==st_obj(sStAtkBy)&&(sStAtkEvent==7||stEvents[6].event[sStAtkEvent].choreo==ST_CHOREO_EXECUTE);
}

int Port_StoryFixedCameraOwned(void){int ch=st_chapter();if(ch==11&&Port_StoryTechniqueDonor(sStSbThrower)>=0)return !sStAtkContact;if(ch!=6)return 0;
 return sStRun.active||(sStAtkFrames>0&&sStAtkEvent>=0&&(sStAtkEvent==7||stEvents[ch].event[sStAtkEvent].choreo==ST_CHOREO_EXECUTE));
}

int Port_StoryApplyFixedCamera(void){
 if(!Port_StoryFixedCameraOwned()||sStRun.shot<0||!st_finite_pos(&sStRun.exitEye)||!st_finite_pos(&sStRun.exitRot))return 0;
 BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return 1;
}



int Port_StoryUltimatePrelude(int who){
 return sStAtkFrames>0&&((st_chapter()==6&&(sStAtkEvent==7||sStAtkEvent==8)&&who==sStAtkBy)||(st_chapter()==11&&sStAtkBack>=0&&(who==sStAtkBy||who==sStAtkBack)));
}

int Port_StoryBeamPairActor(int actor){return st_chapter()>=0&&sStAtkFrames>0&&sStAtkBack>=0&&(actor==sStAtkBy||actor==sStAtkBack);}
