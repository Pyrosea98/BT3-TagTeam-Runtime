#include "mod_terrain_bridge.h"
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
#include "mod_story_wardrobe.h"
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
#define ST_GOHAN_FLIGHT 25 /* frames of the headbutt tracking shot */
#define ST_MOTION_IDLE 0x181
#define ST_ANIM_HIT 0xB3       /* native in-place heavy hit reaction (action 0xC2, btl_act_a.c BtlAct_HitStaggerHandler) */
#define ST_MOTION_WOUNDED 0x184
#define ST_MOTION_PRONE 0xD5 /* native grounded down-rest loop, never a lose/get-up pose */
#define ST_MOTION_TAKEOFF 0x18
#define ST_MOTION_FLIGHT 0x19
#define ST_SE_STRIKE 0x45 /* native common SE of a level-4 strike landing (btl_char_fx.c BtlChar_SpawnHitFx) */
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
/* Direction layer (STORY-CUTSCENE-PLAYBOOK.md, from the SUPER SPARKING! script study; no reference data is used).
   - A composed dialogue shot is never a still frame: the lens eases onto the accepted framing over the line and
     settles on it exactly. Authored special shots do not set sStCamDrift and stay as they were.
   - A line can name its poses (stActs): the generic slots our own disc's story scripts play on every character
     (INTRO_IN 384 -> INTRO_LOOP 385, WIN_IN 386 -> WIN_LOOP 387, STAND_LOOP 413, SURPRISED 406, GROUND_TIRED 1). */
static int sStKameArrival,sStKameFrames;
static Vec4 sStKameLanding;
/* Standard beat PAIRED HOLD (CLAUDE-CINEMATIC-STANDARD-20261114.md): two clips animated against each other, both actors
   at one origin. The clips come from the story animation library (imported into the EMPTY slots of our own packs);
   without the library the slot is a 64-byte stub and the beat keeps its previous staging. */
extern int Port_StoryClipReady(int actor,int anim);
#define ST_CLIP_HOLD_BY 287   /* reference 363/10018: final paired hold; 10014 is the tail/stomp sequence */
#define ST_CLIP_HOLD_AT 288   /* Raditz counterpart of Goku 287, reference 363/10018 */
static int sStHoldPair,sStHoldIntroFrames;
static int sStChaoPair,sStChaoBy=-1,sStChaoAt=-1,sStChaoPhase,sStChaoFrame,sStChaoLift;
static Vec4 sStChaoOrigin,sStChaoRot;
static void st_chao_pair_frame(void),st_chao_pair_sample(int);
static int st_ch8_clip(int,int,int,int);
static int sStElbowActive,sStElbowDone,sStElbowFrame,sStElbowGrip;
static int sStRescueActive,sStRescueDone,sStRescueFrame;
static void st_rescue_begin(void),st_rescue_pair(void),st_rescue_pose(int),st_rescue_camera(void);
static int st_rescue_tick(void),st_rescue_role(int);
static int sStKienActive,sStKienDone,sStKienFrame,sStShieldPostReady,sStShieldPostAge,sStShieldHitAge;
static int sStPowerActive,sStPowerDone,sStPowerFrame;
static void st_power_tick(void),st_shield_reaction(int),st_shield_observers(void);
static int st_shield_post_owned(int);
static void st_shield_post_pose(int),st_shield_post_camera(void);
static void st_kien_begin(void),st_kien_pose(int),st_kien_pair(void),st_kien_camera(void),st_kien_geometry(void);
static int st_kien_tick(void),st_kien_role(int);
static int sStAssaultActive,sStAssaultDone,sStAssaultFrame,sStAssaultShotFrame;
static void st_assault_begin(void),st_assault_pose(int),st_assault_pair(void),st_assault_camera(void),st_assault_shots(void),st_assault_handoff_camera(void);
static int st_assault_tick(void),st_assault_role(int),st_assault_charge_owns(int);
static Vec4 sStElbowGohanPos;static int sStElbowGohanReady,sStElbowCueFrame=-1,sStElbowBloodFrame=-1;
static Vec4 sStElbowPos[3],sStElbowRot[3],sStElbowFar;
static void st_ch8_elbow_begin(void),st_ch8_elbow_pose(int),st_ch8_elbow_pair(void),st_ch8_elbow_camera(void);
static int sStPlantActive,sStPlantFrame,sStPlantRestore,sStPlantRevealed;
static u32 sStPlantParked;static int sStPlantHp[16];static Vec4 sStPlantPos[7],sStPlantRot[7];
static void st_ch8_plant_begin(void),st_ch8_plant_pose(int),st_ch8_plant_camera(void),st_ch8_plant_end(void);
static u32 sStWardrobePending;static void st_wardrobe_queue(int);static int st_shield_spread(int);
static int sStArmBeat,sStArmBeatDone,sStArmFrame,sStArmCut,sStArmWait,sStArmBloodAge;static Vec4 sStArmBy,sStArmAt,sStArmRot;
extern void Port_StoryCommitSampledRoot(int);
static void st_ch8_arm_pose(int),st_ch8_arm_begin(void);static int st_ch8_arm_tick(void);
static int sStSv=-1,sStSvK,sStSvFrom,sStSvList[64],sStSvN;static Vec4 sStSvPos,sStSvRot; /* TEMP survey */
static void st_ch8_fireworks_pose(int);
static Vec4 sStFireBy,sStFireAt,sStFireRot,sStFireGroup[6],sStFireGroupRot[6];static float sStFireGroupFloor[6];
static Vec4 sStMournEye,sStMournAt;static int sStMournLensReady;
static int st_yamcha_mourns(int);static void st_yamcha_mourning_pose(int);
static int sStCamDrift,sStDriftFrames; /* the drift has its own clock: only a new line restarts it, never a scene-end hold */
enum { ST_ACT_NONE, ST_ACT_ATTITUDE, ST_ACT_NEUTRAL, ST_ACT_SURPRISED, ST_ACT_TIRED, ST_ACT_WIN, ST_ACT_GUARD, ST_ACT_HOLD };
typedef struct StAct {int chapter,set,index,who,act,who2,act2,shake10;} StAct; /* who: cast index, -1 the speaker, -2 nobody */
static int sStActLead[2]={-1,-1},sStActLoop[2],sStActFrames[2];
extern s32 BtlCtrl_IsMotionPlaying(s32);
extern s32 BtlFacade_ShakeCamera(f32 time);
static int st_direction_on(void){static int off=-1;extern char*getenv(const char*);
 if(off<0)off=getenv("BT3_STORY_PLAIN_SCENES")!=0;return !off;}
static u32 sStCuesDone;
static s32 sStFightFrames, sStCueFrames;
/* Authored R3 events (st_event_update below): the offered / running event, its scene and the beam that follows. */
static BtlSeqTalkCtx sStEventCtx;
static s32 sStEventOffer = -1, sStEventRun = -1, sStEventPressed, sStEventFrames, sStEventHit;
static u32 sStEventsDone;
static Vec4 sStGripWristOffset;static int sStGripWristReady;
static int sStBeamBlood;static int sStCh3Landed,sStCh3EndingReady;static Vec4 sStCh3EndingPos[3],sStCh3EndingRot[3];
static s32 sStBeamFrames, sStBeamHp, sStBeamLead, sStBeamRival, sStBeamPartner;
static Vec4 sStEventFrom, sStEventTo, sStBeamPos, sStBeamRot, sStBeamLeadPos, sStBeamLeadRot;
static int sStBeamMovie,sStBeamMovieFrames,sStPierceShot;
static s32 sStBeamLanded, sStSwitchActor = -1, sStSwitchTeam;
/* Scene requests are delivered after BeginFrame clears the native FX/SE queues. */
static int sStGohanFrames,sStGohanNative,sStGohanRecoil;
static Vec4 sStGohanRecoilFrom,sStGohanRecoilDelta;
static int sStRaditzArmorBroken;
/* Reference363/10014 has distinct roots AND rotations for these three interactions. */
static int sStTailActive,sStTailToGohan,sStTailPhase,sStTailFrame,sStTailIntro,sStTailG=-1,sStTailR=-1;
static Vec4 sStTailOrigin,sStTailBaseRot,sStTailPos[2],sStTailRot[2];
static void st_tail_sample(int actor);
static void st_tail_frame(void);
static unsigned sStPresentation[16];
static int sStAuraActive[16],sStBeamChargeActive[16];
static void st_presentation_queue(int actor,unsigned bits){if(actor>=0&&actor<16)sStPresentation[actor]|=bits;}
static s32 st_down(s32 actor);static s32 st_hp(s32 actor);
static s32 st_lead_now(s32 ch);
/* Generic story events: actors an event knocked out (shown down in the ending) or took out for good, and the
   scene-only cast an event shows (bit = battle actor / cast index). */
static u32 sStDownActors, sStGoneActors, sStEventShow;
/* Tien/Piccolo's scripted deaths settle into a captured, motionless down pose. */
static u32 sStStillPendingActors, sStStillActors;
static Vec4 sStStillPos[32],sStStillRot[32];
/* The departure shot of several actors (StCast.departMask): who flies, and from where. */
static int sStEarlyTakeoff; /* CH1/2: actual one-shot completion, then flight loop. */
static u32 sStDepartActors;
static Vec4 sStDepartFrom[ST_ACTORS];
/* Chapter 4's moon shot: frames since its lines ended (0 = idle), the shooter, the Great Ape and his HP at the beam. */
static s32 sStMoonTailAt;
static int sStMoonCameraValid;
static s32 sStMoonFrames, sStMoonShooter = -1, sStMoonTarget = -1, sStMoonHp;
static Vec4 sStMoonFallFloor,sStMoonFallRot;
static Vec4 sStMoonShotRoot,sStMoonShotRot,sStMoonApeRoot;static int sStMoonShotPlaced;
static Vec4 sStMoonRetreatFrom,sStMoonRetreatTo;static int sStMoonFallValid;
static int sStMoonRev,sStMoonRevAt; /* 1 contact, 2 ape fall, 3 child fall, 4 landing, 5 prone hold */
/* Phased chapters: the fight frame the previous event ended on (ST_TRIG_AFTER). */
static s32 sStLastEventEnd;
/* Script section 18 inside a phased battle: the event whose alternative ends the battle (-1 none), and whether the
   running event plays as its phase's alternative. */
static s32 sStAltEnd = -1, sStEventAlt;
/* An authored attack after an event's lines: frames since it began (0 idle), the event, attacker, counter, target, the
   target's HP when it was fired. */
static s32 sStAtkFrames, sStAtkEvent = -1, sStAtkBy = -1, sStAtkBack = -1, sStAtkAt = -1, sStAtkHp;
/* Final native placement ownership for static authored shots. These are captured
   AFTER staging, never from the prior fighting frame or the old stage starts. */
static int sStAttackPlacement;
static void st_yamcha_crater(int actor);
static int sStYamchaWarning;static int sStYamchaCraterReady;static float sStYamchaFloor;static Vec4 sStYamchaCraterCenter;
static int sStYamchaCorpse,sStYamchaCaptured;static float sStYamchaBottom;static Vec4 sStYamchaAnchor,sStYamchaRot,sStYamchaMovieRoot,sStYamchaMovieRot;
int Port_StoryYamchaCorpseActive(void){return Port_StoryChapter()==6&&sStYamchaCorpse;}
int Port_StoryYamchaPosePending(void){return Port_StoryChapter()==6&&sStYamchaCaptured;}


static Vec4 sStAttackPos[2],sStAttackRot[2];
static Vec4 sStCh9ApeFloor,sStCh9ApeFacing;
static float sStCh9CrushDrop;
static Vec4 sStCh9CrawlFrom,sStCh9CrawlTo,sStCh9CrawlRoot,sStCh9CrawlRot;
static s32 sStAtkContact,sStAtkRetry,sStAtkRival,sStAtkLost,sStShieldWait;
static Vec4 sStShieldGohanAnchor,sStTransferAnchor;
static int sStEndingExecutionHit,sStEndingFall;
static void st_ch8_end_tick(void),st_ch8_end_pair(void),st_ch8_end_camera(void),st_ch8_end_reset(void);
static Vec4 sStEndingVegetaAnchor;
static Vec4 sStEndingDrawAnchor[2],sStEndingGokuAnchor;static int sStEndingAnchorReady,sStEndingGokuReady;
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
static s32 sStEventPost,sStIntroFormFrames,sStIntroScared,sStIntroPound; /* CH4 opening: scared child, form, chest pounding */
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
  if(!sStEndingExecutionHit){sStEndingExecutionHit=1;{extern void Port_StoryHurtVoice(int);Port_StoryHurtVoice(at);}BtlFacade_ShakeCamera(.6f);Port_Dbg3("story execution native contact",by,at,kind,sStRun.frames);}return;
 }
 if(ch<0||ch>=ST_COUNT||sStAtkFrames<1||sStAtkEvent<0)return;
 ev=&stEvents[ch].event[sStAtkEvent];
 if(ev->choreo==ST_CHOREO_SPIRITBOMB&&sStSbStage!=3)return; /* the first pass must not complete the attack */
 {extern char*getenv(const char*);if(ch==6&&(sStAtkEvent==7||sStAtkEvent==8)&&getenv("BT3_STORY_WAVE_PROBE"))Port_Dbg3("story authored native contact",by,at,kind,sStAtkFrames);}
 if(by==sStAtkBy&&at==sStAtkAt&&kind==0x100+st_attack_slot(ch,sStAtkEvent)){
  /* Action4's bracing pose deliberately blocks native forced reactions.
     Change to the native heavy-hit motion once, at REAL beam contact. */
  if(!sStAtkContact&&ch==6&&sStAtkEvent==7){extern void Port_StoryHurtVoice(int);st_wardrobe_queue(at);sStShieldHitAge=1;Port_StoryHurtVoice(at);BtlFacade_ShakeCamera(.5f);} /* Juan 2026-11-18: arms stay spread through the beam; clothes damaged by it */
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
int Port_StoryStillPoseActive(int actor){if(st_shield_post_owned(actor))return 0;return st_chapter()==6&&actor>=0&&actor<32&&(sStStillActors>>actor&1u);}
static void st_set_hp(s32,s32);
static void st_face(s32,s32);static void st_front_cam(int,float,float,float,float);static void st_moon_reveal(int);
extern s32 gPortTarget[];
extern void Port_StoryEnterActor(int),Port_StoryExitActor(int,int);
extern void BtlCtrl_ClearHidden(s32 player);
extern void BtlCtrl_SetHidden(s32 player); /* generic scene visibility control */
typedef struct StActorSave {int valid,v[9],parked,reserve;Vec4 pos,rot;} StActorSave;
static struct {int chapter,event,difficulty,dense,ball,frames,last;unsigned events,cues,down,gone,stillPending,still;int switchActor,switchTeam;StActorSave actor[ST_ACTORS];} sStCheckpoint={.chapter=-1};
static int sStRestoreFrames;
static int sStFormActor=-1,sStFormTarget,sStFormWait;
static void st_checkpoint_capture(int chapter,int event){
 int i,a;
 if(sStCheckpoint.chapter==chapter&&sStCheckpoint.event==event&&Port_StoryResumeEvent()==0&&sStCheckpoint.frames>0)return;
 sStCheckpoint.chapter=chapter;sStCheckpoint.event=event;sStCheckpoint.difficulty=Port_StoryDifficulty();sStCheckpoint.dense=Port_Setting("story_density_full",0);
 sStCheckpoint.ball=Port_StoryFoundBallValue();sStCheckpoint.frames=sStFightFrames;sStCheckpoint.last=sStLastEventEnd;
 sStCheckpoint.events=sStEventsDone;sStCheckpoint.cues=sStCuesDone;sStCheckpoint.down=sStDownActors;sStCheckpoint.gone=sStGoneActors;
 sStCheckpoint.stillPending=sStStillPendingActors;sStCheckpoint.still=sStStillActors;
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
 sStStillPendingActors=sStCheckpoint.stillPending;sStStillActors=sStCheckpoint.still;
 {int y=Port_StoryCastActor(1);if(y<0||y>=32||!(sStDownActors>>y&1u))sStYamchaCorpse=sStYamchaCaptured=0;}
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
#define ST_TAIL_UP 0.28f
#define ST_TAIL_SLASH 18
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
 if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_EVENT0&&sStEventPost){int i;for(i=7;i<15;i++)if((i==7||i>=10)&&actor==Port_StoryCastActor(i))return 1; /* next wave is admitted already, but revealed only when dialogue ends */}
 return st_chapter()==6&&sStYamchaCraterReady&&(sStYamchaCorpse||(sStAtkFrames>0&&sStAtkEvent==1))&&actor==Port_StoryCastActor(13); /* Native self-destruct stays alive for its script, but its exploded model is hidden. */
}
int Port_StoryTechniqueDonor(int actor){
 int ch=st_chapter();
 return ch==11&&sStAtkFrames>0&&sStAtkEvent>=0&&actor==sStSbThrower&&
  stEvents[ch].event[sStAtkEvent].choreo==ST_CHOREO_SPIRITBOMB?sStSbThrower:-1;
}
int Port_StoryGenkiCast(int owner){
 return Port_StoryTechniqueDonor(owner)>=0&&sStSbStage==3?sStSbGohan:-1;
}
int Port_StoryGenkiCinematicHit(int by,int at,int slot){
 return slot==4&&Port_StoryGenkiCast(by)>=0&&at==sStAtkAt;
}
int Port_StoryMotionDonor(int actor){
 int donor=Port_StoryTechniqueDonor(actor);
 if(donor==actor)donor=-1; /* the Genkidama uses Krillin's own slot-4 animation pack */
 if(donor<0&&actor==sStAtkAt&&sStSbStage==3)donor=Port_StoryTechniqueDonor(sStSbThrower);
 if(donor==sStSbThrower)donor=-1; /* never replace Vegeta's own rebound reaction with Krillin's clip */
 return donor;
}
int Port_StoryNativeImpactCamera(int obj){
 if(st_chapter()==2&&sStBeamFrames>0&&!sStBeamLanded&&sStBeamPartner>=0&&obj==st_obj(sStBeamPartner))return 1;
 return sStSbStage==3&&Port_StoryTechniqueDonor(sStSbThrower)>=0&&obj==st_obj(sStSbThrower);
}
int Port_StoryCameraActor(void){
 if(st_chapter()==2&&sStBeamFrames>0&&!sStBeamLanded)return sStBeamPartner;
 return sStSbStage==3&&Port_StoryTechniqueDonor(sStSbThrower)>=0?sStSbThrower:-1;
}
int Port_StoryAuthoredVictim(int by,int at,int slot){
 int ch=st_chapter();
 if(ch==2&&sStBeamFrames>0&&!sStBeamLanded&&by==sStBeamPartner&&at==sStBeamRival&&slot==4)return 1;
 return ch>=0&&sStAtkFrames>0&&(!sStAtkFree||(ch==11&&sStSbStage==3&&Port_StoryTechniqueDonor(by)>=0))&&sStAtkBack<0&&sStAtkEvent>=0&&
  by==sStAtkBy&&at==sStAtkAt&&slot==st_attack_slot(ch,sStAtkEvent);
}
/* Native StartThrow has accepted a real pair. Its movie now owns both roots;
   keep waiting for actual damage/contact, but do not pin its victim in place. */
void Port_StoryPairedRushStarted(int by,int at,int slot){
 if(st_chapter()==2&&sStBeamFrames>0&&by==sStBeamPartner&&at==sStBeamRival&&slot==4){sStBeamMovie=1;sStBeamMovieFrames=0;sStPierceShot=0;{extern void Port_StoryAcceptNativeVictim(int,int);Port_StoryAcceptNativeVictim(at,slot);}return;}
 if(Port_StoryAuthoredVictim(by,at,slot))sStAtkFree=1;
}
void Port_StoryYamchaFinalShot(int actor){
 if(Port_StoryChapter()==6&&sStAtkFrames>0&&sStAtkEvent==1&&actor==Port_StoryCastActor(1)&&!sStYamchaCraterReady){
  extern void *BtlChar_Get(s32);
  void*c=BtlChar_Get(actor),*o=(void*)BtlObj_Get(st_obj(actor));extern void BtlObjAnim_SamplePose(void*),BtlChar_PoseToObj(void*,int),BtlObjPose_CalcMatrices(void*);
  if(c&&o){BtlObjAnim_SamplePose(o);BtlChar_PoseToObj(c,0);BtlObjPose_CalcMatrices(o);}
  st_yamcha_crater(actor);
 }
}
void Port_StoryYamchaCinematicEnd(int actor){
 extern void Port_StoryCaptureCinematicPose(int,void*);
 if(Port_StoryChapter()!=6||sStAtkFrames<=0||sStAtkEvent!=1||actor!=Port_StoryCastActor(1))return;
 Port_StoryCaptureCinematicPose(actor,&sStYamchaBottom);
 BtlCharApi_GetPos(st_obj(actor),&sStYamchaMovieRoot);BtlCharApi_GetRot(st_obj(actor),&sStYamchaMovieRot);
 sStYamchaCaptured=1;
}
s32 Port_StorySceneActorShown(s32 actor) {
    int ch=st_chapter();
    if(ch<0||actor<0||actor>=32)return 0;
    if(sStRun.active&&(sStRun.actors>>actor&1u))return 1;
    if((sStDownActors>>actor&1u)&&!Port_StorySceneParked(actor))return 1; /* Yamcha/Tien/Piccoro bodies remain visible. */
    if(Port_StorySceneParked(actor)||(sStGoneActors>>actor&1u))return 0;
    if(sStMoonFrames>0&&(actor==sStMoonShooter||actor==sStMoonTarget||(sStMoonRev>=3&&actor==Port_StoryCastActor(2))))return 1;
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

static Vec4 sStCh2ContactRoot,sStCh2BodyContact;static unsigned sStSurprisedActors;
static int sStCh2TimeQ8,sStCh2DrawQ8,sStCh2Spit;
extern int Port_StorySampleMotionQ8(int,int,int),Port_StoryMouthPose(int,Vec4*,Vec4*);
static float sStCh2VictimFloor;static int sStCh2Strike,sStCh2Contact,sStCh2CarryReady;static Vec4 sStCh2Root,sStCh2Rot,sStCh2VictimRoot,sStCh2VictimRot;
static void st_ch2_pair_frame(void);
extern int Port_StorySampleMotion(int,int,int);
extern void Port_StoryHitFx(int);
extern void Port_StoryBodyImpact(int),Port_StoryMouthBlood(int,int),Port_StoryImpactSound(int,int);
extern int Port_StoryLoopMotion(int,int,int);
extern void Port_StoryResetSceneOffset(int);
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
/* Establish a committed, grounded origin before dependent GetPos/aim queries.
   Only used at scripted attack boundaries; never ordinary KOs. */
static void st_reset_attack_origin(int actor){
 extern void Port_StoryResetSceneOffset(int),Port_StoryCommitSampledRoot(int);
 if(actor<0)return;
 {extern char*getenv(const char*);if(getenv("BT3_STORY_PROBE")&&getenv("BT3_STORY_PLACEMENT_PROBE")){
  Vec4 p;BtlCharApi_GetPos(st_obj(actor),&p);p.x+=st_height(actor)*80.f;p.y-=st_height(actor)*40.f;
  BtlCtrl_SetPos(actor,&p);Port_StoryCommitSampledRoot(actor);
  Port_Dbg3("story placement probe incoming altitude",actor,(int)((BtlCharApi_GetGroundY(st_obj(actor))-p.y)*10),0,0);
 }}
 st_stage_actor(actor);Port_StoryResetSceneOffset(actor);Port_StoryCommitSampledRoot(actor);
 st_scene_ground_actor(actor);Port_StoryCommitSampledRoot(actor);
 {Vec4 p;BtlCharApi_GetPos(st_obj(actor),&p);Port_Dbg3("story attack origin grounded",actor,(int)((p.y-BtlCharApi_GetGroundY(st_obj(actor)))*10),(int)(p.x*10),(int)(p.z*10));}
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
 if(st_chapter()==6&&sStRun.active&&(sStRun.set==ST_SET_CANON||sStRun.set==ST_SET_EVENT0+2)){held=Battle_GetWork()->flags&0x2000u;Battle_GetWork()->flags&=~held;}
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
  for(i=0;st_chapter()>=0&&i<stCasts[st_chapter()].count;i++){
   int other=Port_StoryCastActor(i);if(other<0||!(sStRun.actors>>other&1u))continue;
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
  st_place_near(enemy,lead,(sStRun.chapter==4?1.3f:7.f)*h/st_height(lead),0,0);st_scene_ground_actor(enemy);
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
static void st_ch3_ending_arrange(void){
 int oldChapter=sStRun.chapter,oldCast[ST_ACTORS],i,a;unsigned oldActors=sStRun.actors;
 for(i=0;i<ST_ACTORS;i++){oldCast[i]=sStRun.cast[i];sStRun.cast[i]=i<stCasts[2].count?Port_StoryCastActor(i):-1;}
 sStRun.chapter=2;sStRun.actors=0;
 for(i=0;i<3;i++){a=Port_StoryCastActor(i);if(a>=0)sStRun.actors|=1u<<a;}
 st_scene_formation();
 for(i=0;i<3;i++){a=Port_StoryCastActor(i);if(a>=0){BtlCharApi_GetPos(st_obj(a),&sStCh3EndingPos[i]);BtlCharApi_GetRot(st_obj(a),&sStCh3EndingRot[i]);}}
 sStCh3EndingReady=1;sStRun.chapter=oldChapter;sStRun.actors=oldActors;
 for(i=0;i<ST_ACTORS;i++)sStRun.cast[i]=oldCast[i];
}
static void st_ch3_ending_restore(void){int i,a;for(i=0;i<3;i++){a=Port_StoryCastActor(i);if(a>=0){BtlCtrl_SetPos(a,&sStCh3EndingPos[i]);BtlCtrl_SetRot(a,&sStCh3EndingRot[i]);}}}
/* Lookout guides observe behind the fighters, never in a combat formation slot.
   Search only on scene admission, not each frame or each subtitle. */
static int st_scene_observer_place(s32 actor,s32 anchor){
 int i,j,ch=sStRun.chapter,kind=-1;float h;
 if(ch==3&&sStMoonFallValid&&actor==Port_StoryCastActor(2)){BtlCtrl_SetPos(actor,&sStMoonFallFloor);BtlCtrl_SetRot(actor,&sStMoonFallRot);return 1;}
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
 /* a PAIRED MOVE owns both fighters' clips and spot until it ends: a line must not re-place or re-pose them */
 if(sStHoldPair&&(actor==sStBeamLead||actor==sStBeamRival))return;
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
 if(sStCamDrift){
  /* Start a little wide (and high for a group view), ease out, rest on the composed frame. About a quarter of a
     character height in five seconds: the reference's median dialogue move. Clearance below still applies. */
  float t=sStDriftFrames/(kind==0?180.f:150.f),k,len;
  sStCamDrift=0;if(t<0)t=0;if(t>1)t=1;k=(1-t)*(1-t);dx=eye.x-at.x;dz=eye.z-at.z;len=sqrtf(dx*dx+dz*dz);
  if(k>0&&len>.001f&&st_direction_on()){
   float back=h*(kind==0?.45f:kind==5?.30f:.22f)*k,side=kind==1?h*.08f*k*((sStRun.index&1)?-1.f:1.f):0;
   eye.x+=dx/len*back+dz/len*side;eye.z+=dz/len*back-dx/len*side;
   if(kind==0)eye.y-=h*.35f*k;
  }
 }
 if(sStRun.cameraActor!=actor||sStRun.frames%6==0){
  float floor=BtlCharApi_GetGroundY(st_obj(actor));
  if(!(floor>-1e9f&&floor<1e9f))floor=at.y;
  if(BtlStage_IsReady()){
   StgGroundHit hit;Vec4 probe=eye;probe.y-=h*12+100;hit.y=3.4e38f;
   if(BtlStage_ProbeGround(-1,&probe,&hit)>=0&&hit.y>-1e9f&&hit.y<1e9f)floor=hit.y;
  }
  sStRun.cameraFloor=floor;sStRun.cameraActor=actor;
 }
 {float clearance=h*(kind==9&&sStTailActive&&sStRun.chapter==2?.025f:.4f);
 if(eye.y>sStRun.cameraFloor-clearance)eye.y=sStRun.cameraFloor-clearance;
 {float water;if(BtlStage_IsReady()&&BtlStage_GetWaterLevel(&water)&&water>-1e9f&&water<1e9f&&eye.y>water-clearance)eye.y=water-clearance;}}

 /* Keep the vertical move and depth push-in; reduce only screen-sideways
    travel for CH6/CH7 and both Goku/Kaio interludes. Cache per shot, never across a camera cut. */
 if(sStRun.chapter==5||sStRun.chapter==21||sStRun.chapter==18||sStRun.chapter==20||(sStRun.chapter==6&&sStChaoPair)){
  static int ch=-1,set=-1,line=-1,who=-1,lens=-1;static Vec4 baseEye,baseAt;static float rx,rz;
  float sideways,rawSide,len;
  if(ch!=sStRun.chapter||set!=sStRun.set||line!=sStRun.index||who!=actor||lens!=kind||sStRun.frames==0){
   ch=sStRun.chapter;set=sStRun.set;line=sStRun.index;who=actor;lens=kind;baseEye=eye;baseAt=at;
   dx=at.x-eye.x;dz=at.z-eye.z;len=sqrtf(dx*dx+dz*dz);rx=len>.001f?dz/len:1;rz=len>.001f?-dx/len:0;
  }
  rawSide=(eye.x-baseEye.x)*rx+(eye.z-baseEye.z)*rz;
  eye.x-=rawSide*.85f*rx;eye.z-=rawSide*.85f*rz;
  sideways=(at.x-baseAt.x)*rx+(at.z-baseAt.z)*rz;
  at.x-=sideways*.85f*rx;at.z-=sideways*.85f*rz;
  {extern char*getenv(const char*);if(getenv("BT3_STORY_CAMERA_TRACE")&&sStRun.frames%30==0)Port_Dbg3("story lateral camera before-after",sStRun.chapter,sStRun.index,(int)(rawSide*100),(int)(rawSide*15));}
 }
 dx=at.x-eye.x;dz=at.z-eye.z;
 rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));
 {static int trace=-1;extern char*getenv(const char*);
  if(trace<0)trace=getenv("BT3_STORY_SCENE_LOG")!=0;
  if(trace&&(sStRun.shot!=kind||sStRun.shotActor!=actor||sStRun.shotLine!=sStRun.index))Port_Dbg3("story camera shot",sStRun.chapter,actor,kind,sStRun.index);}
 sStRun.shot=kind;sStRun.shotActor=actor;sStRun.shotLine=sStRun.index;
 BtlFacade_SetFixedCamera(&eye,&rot);sStRun.exitEye=eye;sStRun.exitRot=rot;
}
static int sStWishRevivePhase;
extern void Fade_Start(s32,s32,f32);

/* CH6/CH7 dialogue lenses: frame only the intended actors, from their
   evaluated facial front, rather than fitting the entire combat separation. */
static void st_training_dialogue_lens(int first,int last,int focus,int group){
 Vec4 at={0,0,0,1},eye,front={0,0,-1,0},mouth,rot,p;float h=1,span=0,d,len;int i,n=0;
 for(i=first;i<last;i++){int a=sStRun.cast[i];if(a<0)continue;p=st_scene_face(a);at.x+=p.x;at.y+=p.y;at.z+=p.z;n++;if(st_height(a)>h)h=st_height(a);}
 if(!n)return;at.x/=n;at.y/=n;at.z/=n;
 for(i=first;i<last;i++){int a=sStRun.cast[i];if(a<0)continue;p=st_scene_face(a);d=sqrtf((p.x-at.x)*(p.x-at.x)+(p.z-at.z)*(p.z-at.z));if(d>span)span=d;}
 BtlCharApi_GetRot(st_obj(focus),&rot);front.x=-sinf(rot.y);front.z=-cosf(rot.y);
 Port_StoryMouthPose(focus,&mouth,&front);len=sqrtf(front.x*front.x+front.z*front.z);
 if(len>.01f){front.x/=len;front.z/=len;}else{front.x=-sinf(rot.y);front.z=-cosf(rot.y);}
 at.y+=h*(group?.30f:.12f);d=h*(group?2.8f:n==1?.95f:1.65f)+span*(group?1.35f:1.65f);
 eye=at;eye.x+=front.x*d+front.z*h*.18f;eye.z+=front.z*d-front.x*h*.18f;eye.y-=h*(group?.65f:.08f);
 sStCamDrift=0;st_camera_finish(eye,at,h,focus,group?6:n>1?4:3);
}
/* The original model's open hatch and floor reference in Rocky Area. */
static const Vec4 stCh9PodHatch={2746.f,6.f,-1801.f,1.f};
static const float stCh9PodScale=.18f;
static Vec4 sStCh9PodBase;
enum {
    ST_CH9_CRAWL_FRAMES=72, ST_CH9_HATCH_START=106, ST_CH9_HATCH_CLOSED=126,
    ST_CH9_POD_LAUNCH=138, ST_CH9_POD_END=201
};

static int st_ch9_pod_scene(void){
    return sStRun.active&&sStRun.chapter==11&&sStRun.set==ST_SET_CANON&&sStRun.departing;
}

static int st_ch9_crawl_floor(Vec4 *p){
    Vec4 probe=*p;
    StgGroundHit hit;
    probe.y=-700.f;hit.y=3.4e38f;
    if(BtlStage_ProbeGround(-1,&probe,&hit)<0||!(hit.y>-1e9f&&hit.y<1e9f))return 0;
    p->y=hit.y;
    return 1;
}

static Vec4 st_ch9_pod_hatch_now(void){
    Vec4 p=sStCh9PodBase;
    float age=(sStRun.frames-ST_CH9_POD_LAUNCH)/(float)(ST_CH9_POD_END-ST_CH9_POD_LAUNCH);
    if(age>0.f){
        p.y-=age*(32.f+340.f*age);
        p.x+=age*age*42.f;p.z-=age*age*27.f;
    }
    return p;
}

int Port_StoryCh9PodMatrix(Mtx44 *out,Mtx44 *door){
    Vec4 p;
    const float hinge[3]={2863.1f,-7.65f,-1846.35f};
    const float axis[3]={.464434f,-.232217f,.854621f};
    const float cross[3][3]={{0,axis[2],-axis[1]},{-axis[2],0,axis[0]},{axis[1],-axis[0],0}};
    float t,s,c;
    int i,j;
    if(!st_ch9_pod_scene())return 0;
    p=st_ch9_pod_hatch_now();
    *out=(Mtx44){{{stCh9PodScale,0,0,0},{0,stCh9PodScale,0,0},{0,0,stCh9PodScale,0},
        {p.x-stCh9PodHatch.x*stCh9PodScale,p.y-stCh9PodHatch.y*stCh9PodScale,
         p.z-stCh9PodHatch.z*stCh9PodScale,1}}};
    *door=*out;
    if(sStRun.frames<=ST_CH9_HATCH_START)return 1;
    t=(sStRun.frames-ST_CH9_HATCH_START)/(float)(ST_CH9_HATCH_CLOSED-ST_CH9_HATCH_START);
    if(t>1.f)t=1.f;
    t=.6981317f*t*t*(3.f-2.f*t); /* 40 degrees seats the stock hatch in its rim. */
    s=sinf(t);c=cosf(t);
    for(j=0;j<3;j++){
        door->m[3][j]+=stCh9PodScale*hinge[j];
        for(i=0;i<3;i++){
            door->m[i][j]=stCh9PodScale*((i==j?c:0.f)+s*cross[i][j]+(1.f-c)*axis[i]*axis[j]);
            door->m[3][j]-=door->m[i][j]*hinge[i];
        }
    }
    return 1;
}

static int st_ch9_pod_begin(void){
    int vegeta=sStRun.cast[4],stage=Battle_GetStage(),i;
    if(vegeta<0||!BtlStage_IsReady()||(stage!=1&&stage!=25&&stage!=26))return 0;
    sStCh9CrawlFrom=(Vec4){2022.f,0.f,-887.f,1.f};
    sStCh9CrawlTo=(Vec4){2056.f,0.f,-912.f,1.f};
    for(i=0;i<=4;i++){
        Vec4 p=sStCh9CrawlFrom;
        p.x+=(sStCh9CrawlTo.x-p.x)*i*.25f;
        p.z+=(sStCh9CrawlTo.z-p.z)*i*.25f;
        if(!st_ch9_crawl_floor(&p))return 0;
        if(i==0)sStCh9CrawlFrom.y=p.y;
        if(i==4)sStCh9CrawlTo.y=p.y;
    }
    sStCh9PodBase=sStCh9CrawlTo;sStCh9PodBase.x+=13.f;sStCh9PodBase.z-=8.f;
    if(!st_ch9_crawl_floor(&sStCh9PodBase))return 0;
    sStCh9CrawlRoot=sStCh9CrawlFrom;
    sStCh9CrawlRot=(Vec4){0,atan2f(sStCh9PodBase.x-sStCh9CrawlFrom.x,sStCh9PodBase.z-sStCh9CrawlFrom.z),0,1};
    sStRun.rival=vegeta;sStRun.departing=1;
    Port_StoryResetSceneOffset(vegeta);
    {extern void BtlCtrl_SetAuraOff(s32);extern s32 BtlCtrl_SetChargeFx(s32,s32);
     BtlCtrl_SetAuraOff(vegeta);BtlCtrl_SetChargeFx(vegeta,0);}
    Port_Dbg3("story CH9 pod crawl begins",vegeta,stage,(int)sStCh9CrawlFrom.y,(int)sStCh9CrawlTo.y);
    return 1;
}

static void st_ch9_crawl_pose(void){
    int age=sStRun.frames,phase;
    int vegeta=sStRun.rival;
    Vec4 p=sStCh9CrawlFrom;
    float t;
    extern void Port_StoryCrawlReach(int,int,float,float,float);
    if(age>=ST_CH9_CRAWL_FRAMES){
        int board=age-ST_CH9_CRAWL_FRAMES;
        Vec4 seat=st_ch9_pod_hatch_now();
        seat.x+=18.f;seat.z-=12.f;seat.y-=11.f;
        t=board/35.f;if(t>1.f)t=1.f;t=t*t*(3.f-2.f*t);
        p=sStCh9CrawlTo;
        p.x+=(seat.x-p.x)*t;p.y+=(seat.y-p.y)*t;p.z+=(seat.z-p.z)*t;
        sStCh9CrawlRoot=p;
        BtlCtrl_SetPos(vegeta,&p);BtlCtrl_SetRot(vegeta,&sStCh9CrawlRot);
        Port_StorySampleMotion(vegeta,board<18?0xE2:388,board<18?board:56);
        Port_StoryCommitSampledRoot(vegeta);
        return;
    }
    phase=age%24;
    /* Three pulls. Advance on the effort and settle briefly between pulls. */
    t=phase/17.f;if(t>1)t=1;
    t=(age/24+t*t*(3.f-2.f*t))/3.f;
    p.x+=(sStCh9CrawlTo.x-p.x)*t;p.z+=(sStCh9CrawlTo.z-p.z)*t;
    if(st_ch9_crawl_floor(&p))sStCh9CrawlRoot=p;
    BtlCtrl_SetPos(vegeta,&sStCh9CrawlRoot);BtlCtrl_SetRot(vegeta,&sStCh9CrawlRot);
    Port_StorySampleMotion(vegeta,186,10000); /* native face-down damage pose */
    Port_StoryCommitSampledRoot(vegeta);
    t=sinf(phase*3.14159265f/24.f);
    Port_StoryCrawlReach(st_obj(vegeta),age/24%2,sStCh9CrawlRot.y,sStCh9CrawlRoot.y,t);
}

/* Establishing views, close faces, reverse shoulders, solo charge and Gohan tracking. */
static void st_scene_camera(void){
 if(st_ch9_pod_scene()){
  int vegeta=sStRun.rival;
  float h=st_height(vegeta),yaw=sStCh9CrawlRot.y;
  Vec4 at=st_ch9_pod_hatch_now(),eye;
  if(sStRun.frames<ST_CH9_POD_LAUNCH){
   at.x=(at.x+sStCh9CrawlRoot.x)*.5f;at.z=(at.z+sStCh9CrawlRoot.z)*.5f;
  }
  at.y-=h*.74f;
  eye=sStCh9PodBase;eye.x-=sinf(yaw)*h*2.2f;eye.z-=cosf(yaw)*h*2.2f;
  eye.x-=cosf(yaw)*h*4.05f;eye.z+=sinf(yaw)*h*4.05f;eye.y-=h*1.65f;
  if(sStRun.frames>=ST_CH9_POD_LAUNCH)eye.y+=(at.y-eye.y)*.5f;
  sStCamDrift=0;st_camera_finish(eye,at,h,vegeta,5);
  return;
 }
 if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1){st_ch8_end_camera();return;}
 if(sStRun.departing&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON){if(sStRun.frames<=1){int g=sStRun.cast[6],v=sStRun.cast[9];Vec4 a,b,eye;float h=st_height(g);a=sStDepartFrom[6];b=sStDepartFrom[9];a.x=(a.x+b.x)*.5f;a.y=(a.y+b.y)*.5f-h;a.z=(a.z+b.z)*.5f;eye=a;eye.x+=h*4.f;eye.z+=h*5.f;eye.y-=h*2.f;sStCamDrift=0;st_camera_finish(eye,a,h,g,6);}else BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return;}
 if(sStPowerActive)return; /* The charge owns its close lens; do not overwrite it with the dialogue group lens. */
 if(sStRescueActive){st_rescue_camera();return;}
 if(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost&&sStRun.actor>=0&&sStRun.actor!=sStRun.cast[4]){ /* never frame a speaker through the cloud (Claude 2026-11-18) */
  int a=sStRun.actor;Vec4 at,eye;float h=st_height(a),dx,dz,l;extern Vec4 rsCloud;BtlCharApi_GetNodePos(st_obj(a),46,&at);dx=at.x-rsCloud.x;dz=at.z-rsCloud.z;l=sqrtf(dx*dx+dz*dz);if(l<.01f){dx=1;dz=0;l=1;}
  eye=at;eye.x+=dx/l*h*1.7f-dz/l*h*.5f;eye.z+=dz/l*h*1.7f+dx/l*h*.5f;eye.y-=h*.05f;at.y+=h*.15f;sStCamDrift=0;st_camera_finish(eye,at,h,a,3);return;}
 if(st_shield_post_owned(-1)){st_shield_post_camera();return;}
 if(sStKienActive){st_kien_camera();return;}
 if(sStAssaultActive){st_assault_camera();return;}
 if(st_assault_charge_owns(sStRun.cast[0])){st_assault_handoff_camera();return;}
 if(sStSv>=0){Vec4 at=sStSvPos,eye;float h=st_height(sStSv),y=sStSvRot.y+1.3f;BtlCharApi_GetNodePos(st_obj(sStSv),17,&at);eye=at;eye.x+=sinf(y)*h*1.9f;eye.z+=cosf(y)*h*1.9f;eye.y-=h*.1f;sStCamDrift=0;st_camera_finish(eye,at,h,sStSv,3);return;} /* TEMP survey lens */
 if(sStElbowActive){st_ch8_elbow_camera();return;}
 if(sStElbowDone&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&sStRun.index==3){int p=sStRun.cast[3],n=sStRun.cast[8];Vec4 a,b,eye,at;float hp=st_height(p),dx,dz,l,y;
  BtlCharApi_GetPos(st_obj(p),&a);BtlCharApi_GetPos(st_obj(n),&b);dx=a.x-b.x;dz=a.z-b.z;l=sqrtf(dx*dx+dz*dz);if(l<.01f){dx=0;dz=1;l=1;}{Vec4 fr={0,0,0,1};BtlCharApi_GetRot(st_obj(p),&fr);y=fr.y+((dx*cosf(fr.y)-dz*sinf(fr.y))>0?-.55f:.55f);} /* in front of his own face, swung to the side away from Nappa */
  BtlCharApi_GetNodePos(st_obj(p),46,&at);at.y+=hp*.12f; /* the visible head (node 46), not the object root the fall drifted */
  eye=at;eye.x+=sinf(y)*hp*1.5f;eye.z+=cosf(y)*hp*1.5f;eye.y-=hp*.02f;sStCamDrift=0;st_camera_finish(eye,at,hp,p,3);return;}
 if(sStPlantActive){st_ch8_plant_camera();return;}


 Vec4 at={0,0,0,1},facing={0,0,0,1},eye,p;float h,distance;int i,n=0,other=-1,actor=sStRun.actor;
 /* Goku is still lying in the stomp pose during his warning to Gohan.
    The normal standing face shot intersects foreground geometry at the recovery cut. */
 if(sStRun.chapter==2&&sStRun.set==ST_SET_EVENT0&&sStEventHit&&actor==sStRun.cast[0]){
  Vec4 rot;h=st_height(actor);BtlCharApi_GetNodePos(st_obj(actor),3,&at);BtlCharApi_GetRot(st_obj(actor),&rot);
  eye=at;eye.y-=h*1.3f;eye.x+=(cosf(rot.y)*1.0f+sinf(rot.y)*.35f)*h;eye.z+=(-sinf(rot.y)*1.0f+cosf(rot.y)*.35f)*h;
  sStCamDrift=0;st_camera_finish(eye,at,h,actor,3);return;
 }
 if(sStRun.chapter==2&&sStRun.set==ST_SET_CANON&&actor==sStRun.cast[0]){
  Vec4 rot;h=st_height(actor);BtlCharApi_GetNodePos(st_obj(actor),3,&at);BtlCharApi_GetRot(st_obj(actor),&rot);
  eye=at;eye.y-=h*1.8f;eye.x+=(sinf(rot.y)*.25f+cosf(rot.y)*.2f)*h;eye.z+=(cosf(rot.y)*.25f-sinf(rot.y)*.2f)*h;
  sStCamDrift=0;st_camera_finish(eye,at,h,actor,3);return;
 }
 if(!sStRun.active||sStRun.chapter<0||sStRun.chapter>=ST_COUNT)return;
 /* Shenron's wish is shown on Goku: halo -> white flash -> alive. */
 if(sStRun.chapter==20&&sStRun.set==ST_SET_INTRO){
  int focus=sStRun.elapsed<96?sStRun.cast[0]:actor;
  int role=focus==sStRun.cast[0]?0:1;
  st_training_dialogue_lens(role,role+1,focus,0);return;
 }
 {extern char*getenv(const char*);if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0&&getenv("BT3_STORY_CLIP_PROBE")){st_front_cam(sStRun.cast[9],1.5f,.25f,.65f,.25f);return;}}
 if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+5&&sStRun.index<4){
  int who=sStRun.cast[sStRun.index==0?2:sStRun.index==2?8:9];
  st_front_cam(who,1.35f,.18f,.68f,.12f);return;
 }
 if(st_yamcha_mourns(sStRun.cast[2])){
  if(actor>=0&&actor==sStRun.cast[3]){
   Vec4 rot;h=st_height(actor);at=st_scene_face(actor);BtlCharApi_GetRot(st_obj(actor),&rot);
   eye=at;eye.x+=sinf(rot.y+.35f)*h*1.4f;eye.z+=cosf(rot.y+.35f)*h*1.4f;eye.y-=h*.08f;
   at.y+=h*.12f;sStCamDrift=0;st_camera_finish(eye,at,h,actor,3);return;
  }
  int k=sStRun.cast[2],y=sStRun.cast[1];Vec4 yp,front={0,0,1,0},mouth;float h=st_height(k);
  if(!sStMournLensReady){BtlCharApi_GetNodePos(st_obj(y),3,&yp);at=st_scene_face(k);Port_StoryMouthPose(k,&mouth,&front);{float len=sqrtf(front.x*front.x+front.z*front.z);if(len>.01f){front.x/=len;front.z/=len;}}
   eye=at;eye.x+=(front.x*.95f+front.z*.45f)*h;eye.z+=(front.z*.95f-front.x*.45f)*h;eye.y+=h*.20f;
   at.x=at.x*.85f+yp.x*.15f;at.z=at.z*.85f+yp.z*.15f;at.y+=h*.04f;sStMournEye=eye;sStMournAt=at;sStMournLensReady=1;}
  sStCamDrift=0;st_camera_finish(sStMournEye,sStMournAt,h,k,3);return;
 }
 if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost&&sStRun.set==ST_SET_EVENT0+3){
  int n=sStChaoAt,k=sStChaoBy;Vec4 head,front={0,0,1,0},mouth,other;float h=st_height(n),len;
  if(sStRun.index==1){st_training_dialogue_lens(0,1,sStRun.cast[0],0);return;}
  BtlCharApi_GetNodePos(st_obj(n),0x2E,&head);BtlCharApi_GetNodePos(st_obj(k),0x2E,&other);at=head;
  Port_StoryMouthPose(sStRun.index==2?k:n,&mouth,&front);len=sqrtf(front.x*front.x+front.z*front.z);if(len>.01f){front.x/=len;front.z/=len;}
  if(sStRun.index==2){float yaw=sStChaoRot.y+1.9f;at=other;eye=at;eye.x+=sinf(yaw)*h*1.1f;eye.z+=cosf(yaw)*h*1.1f;eye.y-=h*.08f;}
  else{float yaw=sStChaoRot.y;at.x=(head.x+other.x)*.5f;at.y=(head.y+other.y)*.5f;at.z=(head.z+other.z)*.5f;eye=at;eye.x+=sinf(yaw+2.4f)*h*1.3f;eye.z+=cosf(yaw+2.4f)*h*1.3f;eye.y-=h*.12f;}
  sStCamDrift=0;st_camera_finish(eye,at,h,n,3);return;
 }
 /* Openings only: leave combat and accepted endings/choreography alone. */
 if(sStRun.set==ST_SET_INTRO&&(sStRun.chapter==5||sStRun.chapter==21)){
  if(sStRun.index==0&&sStRun.frames<45){st_training_dialogue_lens(0,5,sStRun.cast[0],1);return;}
  if(sStRun.chapter==5&&sStRun.index==2){ /* retained Tien/Chiaotzu composition */ }
  else{
   int role;for(role=0;role<stCasts[sStRun.chapter].count;role++)if(sStRun.cast[role]==actor)break;
   if(sStRun.chapter==21&&sStRun.index==2&&sStRun.frames<45){st_training_dialogue_lens(5,7,actor,0);return;}
   if(role<stCasts[sStRun.chapter].count){st_training_dialogue_lens(role,role+1,actor,0);return;}
  }
 }
 /* CH5 interludes: readable training pairs and a proper teacher/student reverse,
    not one distant overhead view for every line. */
 if(sStRun.chapter==17||sStRun.chapter==18){
  float mh=1.f,radius=0;int first=0,last=stCasts[sStRun.chapter].count,count=0;Vec4 rot;
  if(sStRun.chapter==18&&sStRun.index>0&&actor>=0){Vec4 mouth,front;h=st_height(actor);at=st_scene_face(actor);BtlCharApi_GetRot(st_obj(actor),&facing);front.x=-sinf(facing.y);front.y=0;front.z=-cosf(facing.y);Port_StoryMouthPose(actor,&mouth,&front);eye=at;eye.x+=(front.x*1.25f+front.z*.45f)*h;eye.z+=(front.z*1.25f-front.x*.45f)*h;eye.y+=front.y*h*1.25f-h*.05f;st_camera_finish(eye,at,h,actor,3);return;}
  if(sStRun.chapter==17&&sStRun.index==1){first=0;last=2;}else if(sStRun.chapter==17&&sStRun.index==2){first=2;last=4;if(sStRun.frames>85){first=4;last=5;}}else if(sStRun.chapter==17&&sStRun.index>=3){first=5;last=7;}
  at.x=at.y=at.z=0;for(i=first;i<last;i++){int a=sStRun.cast[i];if(a<0)continue;BtlCharApi_GetPos(st_obj(a),&p);at.x+=p.x;at.y+=p.y;at.z+=p.z;if(st_height(a)>mh)mh=st_height(a);count++;}
  if(count){at.x/=count;at.y/=count;at.z/=count;}for(i=first;i<last;i++){int a=sStRun.cast[i];float q;if(a<0)continue;BtlCharApi_GetPos(st_obj(a),&p);q=sqrtf((p.x-at.x)*(p.x-at.x)+(p.z-at.z)*(p.z-at.z));if(q>radius)radius=q;}
  at.y-=mh*.5f;BtlCharApi_GetRot(st_obj(sStRun.cast[first]),&facing);distance=mh*(last-first==2?2.25f:3.f)+radius*(last-first==2?.8f:1.2f);eye=at;eye.x+=sinf(facing.y+(first==0&&last==2&&sStRun.chapter==17?1.15f:.9f))*distance;eye.z+=cosf(facing.y+(first==0&&last==2&&sStRun.chapter==17?1.15f:.9f))*distance;eye.y-=mh*.3f;st_camera_finish(eye,at,mh,sStRun.cast[first],6);return;
 }

 if(sStRun.chapter==2&&sStRun.set==ST_SET_EVENT0+1&&sStHoldPair&&!sStBeamMovie&&sStBeamRival>=0){
  Vec4 r;h=st_height(sStBeamRival);at=st_scene_face(sStBeamRival);at.y+=h*.12f;
  BtlCharApi_GetRot(st_obj(sStBeamRival),&r);eye=at;eye.x+=sinf(r.y+.12f)*h*1.5f;eye.z+=cosf(r.y+.12f)*h*1.5f;eye.y-=h*.06f;
  sStCamDrift=0;st_camera_finish(eye,at,h,sStBeamRival,3);return;
 }
 if(sStTailActive&&!sStTailToGohan&&sStRun.chapter==2){
  float yaw=sStTailBaseRot.y+(sStTailPhase==2?.35f:1.1f);h=st_height(sStTailR);
  if(sStTailPhase==1){
    /* Ground-level close-up: the plea reads on Raditz's face; the loose tail
       and Goku's approximate hand contact remain outside the frame. */
    at=st_scene_face(sStTailR);eye=at;
    {Vec4 forward;if(Port_StoryMouthPose(sStTailR,&at,&forward)){
      eye=at;eye.x+=forward.x*h*.45f;eye.y+=forward.y*h*.45f;eye.z+=forward.z*h*.45f;
      at.y-=h*.06f;
     }else{eye=at;eye.x+=sinf(sStTailRot[1].y)*h*.4f;eye.z+=cosf(sStTailRot[1].y)*h*.4f;eye.y=at.y+h*.1f;}}

  }else{
    at=sStTailOrigin;at.y-=h*(sStTailPhase==2?.35f:.65f);eye=at;
    eye.x+=sinf(yaw)*h*(sStTailPhase==2?1.9f:1.65f);eye.z+=cosf(yaw)*h*(sStTailPhase==2?1.9f:1.65f);
    eye.y=sStTailPhase==2?sStTailOrigin.y-h*.12f:eye.y-h*.45f;
  }
  sStCamDrift=0;st_camera_finish(eye,at,h,sStTailR,sStTailPhase==1?9:3);return;
 }

 if(sStRun.finishing||(sStRun.departing&&sStRun.frames>0)){BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return;}
 if(sStRun.chapter==1&&sStRun.bridge&&sStRun.index==0&&sStRun.arriving>=0){
  int a=sStRun.arriving;float t=(sStRun.frames-40)/30.f,yaw;Vec4 rot;
  if(t<0)t=0;if(t>1)t=1;t=t*t*(3-2*t);h=st_height(a);BtlCharApi_GetRot(st_obj(a),&rot);yaw=rot.y+.85f;
  at=sStRun.arrival;at.y-=h*(2.4f-1.6f*t);eye=at;distance=h*(6.8f-3.8f*t);
  eye.x+=sinf(yaw)*distance;eye.z+=cosf(yaw)*distance;eye.y-=h*.2f;
  sStCamDrift=0;st_camera_finish(eye,at,h,a,7);return;
 }
 /* CH2's family moment stays intimate. The visitor has not entered the shot yet. */
 if(sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&sStRun.index<2){
  int g=sStRun.cast[0],k=sStRun.cast[1];Vec4 q;
  h=st_height(g);BtlCharApi_GetPos(st_obj(g),&at);BtlCharApi_GetPos(st_obj(k),&q);BtlCharApi_GetRot(st_obj(g),&facing);
  at.x=(at.x+q.x)*.5f;at.z=(at.z+q.z)*.5f;at.y-=h*.6f;
  eye=at;eye.x+=(sinf(facing.y)*2.f+cosf(facing.y)*1.2f)*h;
  eye.z+=(cosf(facing.y)*2.f-sinf(facing.y)*1.2f)*h;eye.y-=h*.1f;
  st_camera_finish(eye,at,h,g,6);return;
 }
 if(sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&sStKameArrival==1){
  int a=sStRun.cast[2];h=st_height(a);BtlCharApi_GetRot(st_obj(a),&facing);
  at=sStKameLanding;at.y-=h*1.8f;eye=at;
  eye.x+=(sinf(facing.y)*3.2f+cosf(facing.y)*.8f)*h;
  eye.z+=(cosf(facing.y)*3.2f-sinf(facing.y)*.8f)*h;eye.y-=h*.5f;
  st_camera_finish(eye,at,h,a,7);return;
 }
 if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+2&&actor==Port_StoryCastActor(8)&&sStNappaIntroAnchorReady){
  h=st_height(actor);BtlCharApi_GetRot(st_obj(actor),&facing);
  at=sStNappaIntroAnchor;at.y-=h*.7f;eye=at;
  eye.x+=(sinf(facing.y)*1.4f+cosf(facing.y)*.3f)*h;
  eye.z+=(cosf(facing.y)*1.4f-sinf(facing.y)*.3f)*h;eye.y-=h*.25f;
  st_camera_finish(eye,at,h,actor,1);return;
 }
 if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)){
  int a=Port_StoryCastActor(1);if(a>=0){if(sStIntroScared>=22&&sStIntroScared<56)st_moon_reveal(a);else st_front_cam(a,1.25f,.65f,.55f,.35f);return;}
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
 if(sStEventRun>=0&&stEvents[sStRun.chapter].event[sStEventRun].kind==ST_EVENT_GOHAN&&actor==(int)sStEventTo.w&&sStGohanFrames>0&&sStGohanFrames<ST_GOHAN_FLIGHT+24){
  float x=sStEventTo.x-sStEventFrom.x,z=sStEventTo.z-sStEventFrom.z,len=sqrtf(x*x+z*z);
  if(len>.01f){x/=len;z/=len;}else{x=sinf(facing.y);z=cosf(facing.y);}
  at=st_scene_face(actor);eye=at;eye.x+=(-x*2.8f+z*1.2f)*h;eye.z+=(-z*2.8f-x*1.2f)*h;eye.y-=h*.35f;
  at.x+=x*h*.7f;at.z+=z*h*.7f;st_camera_finish(eye,at,h,actor,4);return;
 }
 if(ST_Interlude(sStRun.chapter)||(n>=4&&sStRun.index==0&&sStRun.set<ST_SET_EVENT0&&!((sStRun.chapter==1||sStRun.chapter==2)&&sStRun.set==ST_SET_CANON))){
  float radius=0,maxH=h;int count=0;at.x=at.y=at.z=0;
  for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];float ah;if(a<0||!(sStRun.actors>>a&1u))continue;
   BtlCharApi_GetPos(st_obj(a),&p);at.x+=p.x;at.y+=p.y;at.z+=p.z;count++;ah=st_height(a);if(ah>maxH)maxH=ah;}
  if(count){at.x/=count;at.y/=count;at.z/=count;}
  for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];float d;if(a<0||!(sStRun.actors>>a&1u))continue;
   BtlCharApi_GetPos(st_obj(a),&p);d=sqrtf((p.x-at.x)*(p.x-at.x)+(p.z-at.z)*(p.z-at.z));if(d>radius)radius=d;}
  distance=maxH*3.1f+radius*1.45f;eye=at;
  eye.x+=sinf(facing.y+.45f)*distance;eye.z+=cosf(facing.y+.45f)*distance;eye.y-=maxH*1.9f;
  at.y-=maxH*.6f;sStCamDrift=1;st_camera_finish(eye,at,maxH,actor,0);return;
 }
 /* PAIRED HOLD two-shot, from the reference's first cut of the hold (lens 2.3 heights from the held fighter, 1.7 heights
    up, 64 degrees to his right): the holder's and the held fighter's lines play on the pair, never on one of them alone. */
 if(sStHoldPair&&sStRun.chapter==2&&sStEventRun>=0&&stEvents[2].event[sStEventRun].kind==ST_EVENT_BEAM&&(actor==sStBeamLead||actor==sStBeamRival)&&sStBeamRival>=0){
  float rh=st_height(sStBeamRival),yaw=sStBeamRot.y+1.117f;
  at=sStBeamPos;at.y-=rh*.55f;at.w=1;eye=sStBeamPos;eye.w=1;
  eye.x+=sinf(yaw)*rh*2.3f;eye.z+=cosf(yaw)*rh*2.3f;eye.y-=rh*1.7f;
  sStCamDrift=1;st_camera_finish(eye,at,rh,actor,3);return;
 }
 if(sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&!sStCh2Strike&&
    (actor==sStRun.carrier||actor==sStRun.carried||sStRun.departing)){
  Vec4 rot;float h=st_height(sStRun.carrier),yaw;
  BtlCharApi_GetPos(st_obj(sStRun.carrier),&at);BtlCharApi_GetRot(st_obj(sStRun.carrier),&rot);yaw=rot.y+.5f;
  at.y-=h*.55f;eye=at;eye.x+=sinf(yaw)*h*2.5f;eye.z+=cosf(yaw)*h*2.5f;eye.y-=h*.15f;
  sStCamDrift=0;st_camera_finish(eye,at,h,sStRun.carrier,3);return;
 }
 /* CH1 blast aftermath: Piccolo's "barely moved" plays over Raditz, standing unfazed in the clearing smoke */
 /* second half of the line: Piccolo, shocked (the generic close shot below) */
 if(sStRun.chapter==0&&sStRun.set==ST_SET_EVENT0&&sStRun.index>=1&&sStRun.frames>60&&sStRun.cast[0]>=0)actor=sStRun.cast[0];
 else if(sStRun.chapter==0&&sStRun.set==ST_SET_EVENT0&&sStRun.index>=1&&sStRun.rival>=0&&sStRun.cast[0]>=0){Vec4 f=st_scene_face(sStRun.rival),q;float rh=st_height(sStRun.rival),dx,dz,len;BtlCharApi_GetPos(st_obj(sStRun.cast[0]),&q);dx=q.x-f.x;dz=q.z-f.z;len=sqrtf(dx*dx+dz*dz);if(len<.01f){dx=0;dz=1;len=1;}dx/=len;dz/=len; /* from Piccolo's side, a little off axis and low: Raditz's face, unfazed */eye=f;eye.x+=(dx*1.15f+dz*.35f)*rh;eye.z+=(dz*1.15f-dx*.35f)*rh;eye.y+=rh*.05f;at=f;sStCamDrift=1;st_camera_finish(eye,at,rh,sStRun.rival,1);return;}
 if(sStRun.chapter==2&&sStEventRun>=0&&stEvents[2].event[sStEventRun].kind==ST_EVENT_BEAM&&actor==sStBeamPartner){
  at=st_scene_face(actor);eye=at;eye.x+=(sinf(facing.y)*1.15f+cosf(facing.y)*.45f)*h;eye.z+=(cosf(facing.y)*1.15f-sinf(facing.y)*.45f)*h;
  eye.y-=h*.08f;at.y+=h*.17f;st_camera_finish(eye,at,h,actor,3);return;
 }
 if(sStRun.chapter!=6&&!sStRun.departing&&!st_down(actor)&&other>=0&&(sStRun.index&1)&&!(sStRun.chapter==0&&sStRun.set==ST_SET_EVENT0)&&!(sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&actor==sStRun.cast[0])&&
    (sStEventRun<0||stEvents[sStRun.chapter].event[sStEventRun].kind!=ST_EVENT_GOHAN)){
  Vec4 target;float x,z,len,side=(sStRun.index&2)?-1.f:1.f;
  BtlCharApi_GetPos(st_obj(other),&target);x=target.x-at.x;z=target.z-at.z;len=sqrtf(x*x+z*z);
  if(len>h*2.f){x/=len;z/=len;eye=st_scene_face(actor);eye.x+=(-x*.95f+z*.7f*side)*h;eye.z+=(-z*.95f-x*.7f*side)*h;eye.y-=h*.08f;
   target=st_scene_face(other);sStCamDrift=1;st_camera_finish(eye,target,h,actor,2);return;}
 }
 /* A kneeling loser (CH7 / CH8 lay theirs prone and keep the wide frame): close and low enough to see the face,
    not the top of a bowed head from 2.6 heights away. */
 {int kneel=st_down(actor)&&!sStRun.departing&&sStRun.chapter!=6&&sStRun.chapter!=11;
 distance=h*(sStRun.departing?4.8f:kneel?1.5f:st_down(actor)?2.6f:.72f);
 if(!sStRun.departing&&!st_down(actor)){at=st_scene_face(actor);eye=at;eye.y-=h*.08f;
  eye.x+=sinf(facing.y)*distance+cosf(facing.y)*h*.25f;eye.z+=cosf(facing.y)*distance-sinf(facing.y)*h*.25f;
  sStCamDrift=1;st_camera_finish(eye,at,h,actor,1);return;}
 eye=at;eye.x+=sinf(facing.y)*distance+cosf(facing.y)*h*.4f;eye.z+=cosf(facing.y)*distance-sinf(facing.y)*h*.4f;
 eye.y-=h*(kneel?.3f:.9f);at.y-=h*(kneel?.38f:st_down(actor)?.32f:.78f);sStCamDrift=!sStRun.departing;st_camera_finish(eye,at,h,actor,sStRun.departing?6:st_down(actor)?5:1);}
}

/* The chapter of a story fight on the versus sequence, else -1 (stock mode-1 scripts keep theirs). */
static s32 st_chapter(void) {
    return Battle_GetMode() != 1 && Port_StoryActive() ? Port_StoryChapter() : -1;
}

static s32 st_frames(const char *text) {
    {extern char*getenv(const char*);if(getenv("BT3_STORY_PROBE")&&getenv("BT3_STORY_INTRO_FAST_PROBE")&&sStRun.set==ST_SET_INTRO)return 15;}
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
    const StCast *c;
    s32 i;
    if(actor>=0&&actor<32&&((sStDownActors>>actor&1u)||(!Port_StorySceneActor(actor)&&BtlCharApi_GetHp(st_obj(actor))<=0)))return 1;
    if(!sStRun.active||sStRun.chapter<0)return 0;
    c=&stCasts[sStRun.chapter];
    if(sStRun.departing&&(actor==sStRun.rival||(actor>=0&&actor<32&&(sStDepartActors>>actor&1u))))return 0;
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
    if(sStRun.chapter==2&&sStRun.set==ST_SET_CANON&&actor==sStRun.cast[0]&&st_down(actor)){
        Port_StorySampleMotion(actor,0xD5,10000);st_scene_ground_actor(actor);return;
    }
    if(Port_StoryYamchaCorpseActive()&&actor==Port_StoryCastActor(1))return;
    {extern int Port_StoryStillPoseActive(int);if(Port_StoryStillPoseActive(actor))return;}
    if((sStRun.chapter==6||sStRun.chapter==11)&&st_down(actor)){st_scene_ground_actor(actor);BtlCtrl_PlayMotion(actor,ST_MOTION_PRONE,1,.15f);return;}
    if(sStRun.chapter==3&&(sStRun.set==ST_SET_CANON||sStRun.set==ST_SET_ALT)&&(actor==sStRun.cast[1]||actor==sStRun.cast[2])){
        Port_StorySampleMotion(actor,186,10000);st_scene_ground_actor(actor);return;
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
/* Line acting. Cast indices are those of stCasts. Only standing, owned, not-down actors are posed; a carried actor and
   every authored event choreography keep their own motions. Chapters 6 and 11 (CH7 / CH8) are deliberately absent. */
static const StAct stActs[]={
 /* CH1 Piccolo / Raditz */
 {0,ST_SET_INTRO,1, -1,ST_ACT_ATTITUDE, -2,0, 0},      /* Raditz: his entrance flourish on his first line */
 {0,ST_SET_CANON,1, -1,ST_ACT_SURPRISED, -2,0, 5},     /* "That power..." the reading that makes him leave */
 {0,ST_SET_CANON,2, 1,ST_ACT_HOLD, -2,0, 0},             /* back to his stance before he leaves */
 {0,ST_SET_ALT,0, -1,ST_ACT_WIN, -2,0, 0},             /* Piccolo, the alternative winner */
 /* CH2 Kame House: a calm visit until the name "Saiyan" */
 {1,ST_SET_INTRO,0, 0,ST_ACT_NEUTRAL, 1,ST_ACT_NEUTRAL, 0},
 {1,ST_SET_INTRO,2, -1,ST_ACT_NEUTRAL, -2,0, 0},       /* his arrival has already finished; never replay INTRO_IN */
 {1,ST_SET_INTRO,4, 0,ST_ACT_SURPRISED, 1,ST_ACT_SURPRISED, 5}, /* "I'm Raditz, your brother." */
 {1,ST_SET_INTRO,7, 0,ST_ACT_GUARD, 1,ST_ACT_GUARD, 0}, /* "I'm not hurting anyone": both drop into their stance */
 {1,ST_SET_CANON,0, -2,0, -2,0, 5},                    /* "Dad!" */
 {1,ST_SET_ALT,0, 0,ST_ACT_TIRED, -2,0, 0},            /* "Goku, hold on!" he won, but spent */
 /* CH3 the alliance */
 {2,ST_SET_INTRO,0, -1,ST_ACT_ATTITUDE, -2,0, 0},      /* Raditz */
 {2,ST_SET_INTRO,1, -1,ST_ACT_ATTITUDE, -2,0, 0},      /* Goku steps up */
 {2,ST_SET_CANON,3, 1,ST_ACT_SURPRISED, -2,0, 5},      /* "Two Saiyans will arrive in a year." Piccolo hears it */
 {2,ST_SET_CANON,4, 1,ST_ACT_NEUTRAL, -2,0, 0},
 {2,ST_SET_ALT,0, 0,ST_ACT_TIRED, 1,ST_ACT_NEUTRAL, 0},
 /* CH4 the wilderness: a frightened child, not a rival in his battle intro */
 {3,ST_SET_INTRO,0, 1,ST_ACT_NEUTRAL, 0,ST_ACT_NEUTRAL, 0},
 /* CH5 a year of training */
 {4,ST_SET_INTRO,1, -1,ST_ACT_TIRED, -2,0, 0},
 {0,ST_SET_EVENT0,1, 1,ST_ACT_NEUTRAL, 0,ST_ACT_SURPRISED, 0}, /* Piccolo: baffled that it did nothing */       /* CH1 blast aftermath: Raditz standing as if nothing happened */         /* "I was still too slow." */
};
static void st_act_clear(void){sStActLead[0]=sStActLead[1]=-1;sStSurprisedActors=0;}
static void st_act_play(int actor,int act){int slot;
 if(actor<0||actor>=ST_ACTORS||!(sStRun.actors>>actor&1u)||st_down(actor)||actor==sStRun.carried||(sStRun.set==ST_SET_CANON&&actor==sStRun.carrier))return;
 for(slot=0;slot<2;slot++)if(sStActLead[slot]==actor)sStActLead[slot]=-1;
 sStSurprisedActors&=~(1u<<actor);
 switch(act){
 case ST_ACT_NEUTRAL:BtlCtrl_PlayMotion(actor,413,1,.2f);break;
 case ST_ACT_SURPRISED:sStSurprisedActors|=1u<<actor;BtlCtrl_PlayMotion(actor,406,1,.12f);break;
 case ST_ACT_TIRED:BtlCtrl_PlayMotion(actor,1,1,.2f);break;
 case ST_ACT_GUARD:BtlCtrl_PlayMotion(actor,0,1,.2f);break;
 case ST_ACT_HOLD:BtlCtrl_PlayMotion(actor,385,1,.2f);break;
 case ST_ACT_ATTITUDE:case ST_ACT_WIN:
  /* Lead-in once, then its loop when the one-shot has really ended (st_act_frame); never restarted per frame. */
  slot=sStActLead[0]<0?0:1;sStActLead[slot]=actor;sStActLoop[slot]=act==ST_ACT_WIN?387:385;sStActFrames[slot]=0;
  BtlCtrl_PlayMotion(actor,act==ST_ACT_WIN?386:384,0,.15f);break;
 default:break;}
}
static void st_act_line(void){unsigned i;
 if(!st_direction_on()||sStRun.departing||sStRun.finishing||sStRun.chapter<0)return;
 for(i=0;i<sizeof(stActs)/sizeof(stActs[0]);i++){const StAct*a=&stActs[i];
  if(a->chapter!=sStRun.chapter||a->set!=sStRun.set||a->index!=sStRun.index)continue;
  if(a->who>-2&&a->act)st_act_play(a->who<0?sStRun.actor:sStRun.cast[a->who],a->act);
  if(a->who2>-2&&a->act2)st_act_play(a->who2<0?sStRun.actor:sStRun.cast[a->who2],a->act2);
  if(a->shake10>0)BtlFacade_ShakeCamera(a->shake10*.1f);
 }
}
static void st_act_frame(void){int slot;
 for(slot=0;slot<2;slot++){int a=sStActLead[slot];if(a<0)continue;
  if(!sStRun.active||!(sStRun.actors>>a&1u)||sStRun.departing){sStActLead[slot]=-1;continue;}
  /* A few frames for the request to be taken, then the native end flag; bounded so a missing flag cannot stall. */
  if(++sStActFrames[slot]>=4&&(!BtlCtrl_IsMotionPlaying(a)||sStActFrames[slot]>=150)){
   BtlCtrl_PlayMotion(a,sStActLoop[slot],1,.1f);sStActLead[slot]=-1;}
 }
}
/* PRIVATE reference viewer (study only): compiled in only while st_ref_view_private.h is on the include path, which
   it never is in a shipped tree. BT3_STORY_REF_VIEW=<n> replays reference scene n with the two leads when the probed
   chapter's opening starts: the same placement, camera, pose, wait and shake calls the native script commands make. */
#if defined(__has_include)
#if __has_include("st_ref_view_private.h")
#include "st_ref_view_private.h"
#define ST_REF_VIEW 1
static const StRefStep*sRv;static int sRvWait,sRvPoseSide=-1,sRvPoseFrames,sRvKeys,sRvKey,sRvKeyFrame,sRvFrames;
static float sRvCam[6],sRvFrom[6],sRvKeyData[16][7];
extern void BtlCtrl_SetRot(s32,Vec4*);extern void BtlFacade_SetCtrl101(s32),BtlFacade_ClearCtrl101(s32),BtlFacade_SetCtrl102(s32),BtlFacade_SetCtrlFE(s32),BtlFacade_SetCtrlFF(s32);
static int st_rv_actor(int side){return side?sStRun.rival:sStRun.cast[0];}
static void st_rv_apply(void){Vec4 p={sRvCam[0],sRvCam[1],sRvCam[2],1},r={sRvCam[3]*.0174532925f,sRvCam[4]*.0174532925f,sRvCam[5]*.0174532925f,1};BtlFacade_SetFixedCamera(&p,&r);}
static void st_rv_start(void){extern char*getenv(const char*);extern int atoi(const char*);char*e=getenv("BT3_STORY_REF_VIEW");int n;
 sRv=0;if(!e)return;n=atoi(e);if(n<0||n>=ST_REF_SCENES)return;sRv=stRefScenes[n];sRvWait=0;sRvPoseSide=-1;sRvKeys=sRvKey=0;sRvFrames=0;Port_Dbg3("story ref view start",n,0,0,0);}
static int st_rv_tick(void){int guard=0;
 if(!sRv)return 0;sRvFrames++;
 if(sRvKey<sRvKeys){float*k=sRvKeyData[sRvKey];int total=(int)(k[0]*30.f),i;
  if(total<=0||++sRvKeyFrame>=total){for(i=0;i<6;i++)sRvCam[i]=sRvFrom[i]=k[i+1];sRvKey++;sRvKeyFrame=0;}
  else for(i=0;i<6;i++)sRvCam[i]=sRvFrom[i]+(k[i+1]-sRvFrom[i])*sRvKeyFrame/total;
  if(sRvKey>=sRvKeys)sRvKeys=sRvKey=0;}
 st_rv_apply();
 if(sRvWait>0){sRvWait--;return 1;}
 if(sRvPoseSide>=0){if(++sRvPoseFrames<3||(BtlCtrl_IsMotionPlaying(st_rv_actor(sRvPoseSide))&&sRvPoseFrames<400))return 1;sRvPoseSide=-1;}
 while(guard++<64){const StRefStep*s=sRv++;int i,a;
  switch(s->op){
  case RV_END:Port_Dbg3("story ref view end",sRvFrames,0,0,0);sRv=0;return 0;
  case RV_PLACE:{Vec4 p={s->a[1],s->a[2],s->a[3],1},r={s->a[4]*.0174532925f,s->a[5]*.0174532925f,s->a[6]*.0174532925f,1};a=st_rv_actor((int)s->a[0]);BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&r);break;}
  case RV_CAM:for(i=0;i<6;i++)sRvCam[i]=sRvFrom[i]=s->a[i];sRvKeys=sRvKey=0;st_rv_apply();break;
  case RV_MOVE:if(sRvKeys==0){sRvKey=0;sRvKeyFrame=0;for(i=0;i<6;i++)sRvFrom[i]=sRvCam[i];}if(sRvKeys<16){for(i=0;i<7;i++)sRvKeyData[sRvKeys][i]=s->a[i];sRvKeys++;}break;
  case RV_POSE:a=st_rv_actor((int)s->a[0]);BtlCtrl_PlayMotion(a,(int)s->a[1],((int)s->a[2])&1,0);Port_Dbg3("story ref view pose",a,(int)s->a[1],(int)s->a[2],sRvFrames);
   if(((int)s->a[2]&3)==2){sRvPoseSide=(int)s->a[0];sRvPoseFrames=0;return 1;}break;
  case RV_WAIT:sRvWait=(int)(s->a[0]*30.f);return 1;
  case RV_SHAKE:BtlFacade_ShakeCamera(s->a[0]);break;
  case RV_FX:a=st_rv_actor((int)s->a[1]);switch((int)s->a[0]){case 1:BtlFacade_SetCtrl101(a);break;case 2:BtlFacade_SetCtrl102(a);break;case 3:BtlFacade_SetCtrlFE(a);break;case 4:BtlFacade_SetCtrlFF(a);break;case 5:BtlFacade_ClearCtrl101(a);break;}break;
  }}
 return 1;}
#endif
#endif
/* Starts a scene of `count` lines of `set` from line `first`; the talk context runs it. */
static void st_scene_begin(BtlSeqTalkCtx *ctx, s32 chapter, s32 set, s32 first, s32 count, s32 winner, s32 leaves) {
    const StCast *cast = &stCasts[chapter];
    s32 i;

    {extern void Port_StoryInterludeShow(int);if(ST_Interlude(chapter))Port_StoryInterludeShow(1);}
    if(set==ST_SET_CANON||set==ST_SET_ALT){extern void Port_StoryMusicStart(int);
     if(chapter==4)Port_StoryMusicStart(PortSongs_StoryBgm(stChapters[chapter].id,stChapters[chapter].saga,0));
     else if(chapter==5||chapter==21)Port_StoryMusicStart(-1);
    }
    if(chapter==20&&set==ST_SET_INTRO)sStWishRevivePhase=0;
    if(chapter!=6||set!=ST_SET_EVENT0+3||sStEventPost)sStChaoPair=0;
    if(chapter==6&&set==ST_SET_CANON)st_ch8_end_reset();
    sStRun.chapter = chapter;
    sStRun.set = set;
    sStRun.index = first;
    sStRun.count = first + count;
    sStRun.winner = winner;
    sStRun.leaves = leaves;
    sStRun.active=1;sStRun.departing=sStRun.finishing=sStRun.bridge=0;sStRun.frames=0;sStRun.elapsed=0;sStRun.cameraActor=-1;
    sStKameArrival=sStKameFrames=0;
    sStRun.actors=0;sStRun.rival=1;sStRun.shot=sStRun.shotActor=sStRun.shotLine=-1;sStEndingAnchorReady=sStEndingGokuReady=0;sStNappaIntroAnchorReady=0;st_act_clear();sStDriftFrames=0;
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
    if(chapter==3&&set==ST_SET_INTRO){int a=Port_StoryCastActor(1);if(a>=0)sStSurprisedActors|=1u<<a;}
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
    if(chapter==2&&set==ST_SET_CANON&&sStCh3EndingReady)st_ch3_ending_restore();else st_scene_formation();
    if(chapter==11&&set==ST_SET_INTRO){st_rock_perch(Port_StoryCastActor(0));st_rock_perch(Port_StoryCastActor(4));st_face(Port_StoryCastActor(0),Port_StoryCastActor(4));st_face(Port_StoryCastActor(4),Port_StoryCastActor(0));}
    if (sStRun.carried >= 0) {
        if ((set == ST_SET_INTRO && !cast->noIntroCompanion) || set == ST_SET_ALT) { /* Gohan beside his father: hiding behind him, or safe */
            st_scene_take(sStRun.carried, 1);
            st_place_near(sStRun.carried, sStRun.cast[0], set == ST_SET_INTRO ? -0.42f : 0.1f, set == ST_SET_INTRO ? -0.14f : 0.6f, 0.0f); /* intro: tucked behind his father, away from the lens (which sits front-right) */
            st_scene_ground_actor(sStRun.carried);
            BtlCtrl_PlayMotion(sStRun.carried, chapter==1&&set==ST_SET_INTRO?241:0, 1, 0.15f); /* 241: the guard loop, arms up in front of the face: a child cowering */
        } else if (set == ST_SET_CANON && sStRun.carrier >= 0) { /* taken: limp at Raditz's side */
            st_scene_take(sStRun.carried, 1);
            BtlCtrl_PlayMotion(sStRun.carried, ST_MOTION_LIMP, 0, 0.f);
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
      if(g>=0){st_scene_ground_actor(g);BtlCharApi_GetPos(st_obj(g),&sStEndingGokuAnchor);sStEndingGokuReady=1;}

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
    if(chapter==1&&set==ST_SET_CANON){int r=sStRun.carrier,g=sStRun.cast[0];
      sStCh2Strike=1;sStCh2TimeQ8=sStCh2DrawQ8=256;sStCh2Spit=0;sStCh2Contact=sStCh2CarryReady=0;
      st_scene_ground_actor(r);BtlCharApi_GetPos(st_obj(r),&sStCh2Root);st_face(r,g);BtlCharApi_GetRot(st_obj(r),&sStCh2Rot);
      st_place_near(g,r,.65f,0,0);st_scene_ground_actor(g);BtlCharApi_GetPos(st_obj(g),&sStCh2VictimRoot);sStCh2VictimFloor=sStCh2VictimRoot.y;st_face(g,r);BtlCharApi_GetRot(st_obj(g),&sStCh2VictimRot);
      gPortTarget[r]=g;gPortTarget[g]=r;BtlCtrl_PlayMotion(r,0x47,0,0.f);BtlCtrl_PlayMotion(g,0,0,0.f);
    }else sStCh2Strike=sStCh2Contact=0;
    if(chapter==1&&set==ST_SET_INTRO){
      int a=sStRun.cast[2];Vec4 p;
      BtlCtrl_PlayMotion(sStRun.cast[0],413,1,.15f);BtlCtrl_PlayMotion(sStRun.cast[1],413,1,.15f);
      BtlCharApi_GetPos(st_obj(a),&sStKameLanding);
      p=sStKameLanding;p.y-=st_height(a)*12.f;BtlCtrl_SetPos(a,&p);
      BtlCtrl_PlayMotion(a,0x32,1,.1f); /* native fast descent loop, staged above the family shot */
    }
#ifdef ST_REF_VIEW
    if(set==ST_SET_INTRO)st_rv_start();
#endif
    ctx->step = ST_STEP_BEAT;
}

/* Every frame of a canonical scene: the carried actor follows the carrier (the native carry pair's own technique:
   the held fighter's position is set from the holder's each frame), at his side and at hip height. */
/* These authored CH5 poses are sampled outside the movement solver.
   Place the evaluated soles on each actor's own cached floor, not just its root. */
static void st_ch5_ground_pose(int actor,float floor){
 /* Ankle nodes11/15 are ~1.07 units above the sole in Goku's native bind.
    Keep boot thickness; include pelvis/knees/torso for seated/get-up poses. */
 static const int node[]={11,15,10,14,3,16,17,46};
 static const float pad[]={.055f,.055f,.055f,.055f,.12f,.12f,.12f,.10f};
 Vec4 q,p;float low=-1e20f,dy,h=st_height(actor);int j;extern void Port_StoryCommitSampledRoot(int);
 BtlCharApi_GetPos(st_obj(actor),&p);
 for(j=0;j<8;j++){float clearance=pad[j];
  /* The supine back is thinner than the conservative seated/rising torso
     clearance. Blend back to that clearance only as he starts getting up. */
  if(sStRun.chapter==18&&actor==sStRun.cast[0]&&sStRun.elapsed<100&&j>=4){
   float rise=sStRun.elapsed<=60?0.f:(sStRun.elapsed-60)/40.f;
   clearance=.045f+(pad[j]-.045f)*rise;
  }
  if(sStElbowActive&&actor==sStRun.cast[3]&&sStElbowFrame>=231&&j>=4){float rise=sStElbowFrame<265?0:(sStElbowFrame-265)/20.f;if(rise>1)rise=1;clearance=.045f+(pad[j]-.045f)*rise;}
  BtlCharApi_GetNodePos(st_obj(actor),node[j],&q);if(st_finite_pos(&q)&&q.y+clearance*h>low)low=q.y+clearance*h;
 }
 dy=floor-low;if(fabsf(dy)>.001f&&fabsf(dy)<h*2.f){p.y+=dy;BtlCtrl_SetPos(actor,&p);Port_StoryCommitSampledRoot(actor);}
}

/* Nappa ISO3216/284: Juan visually identified the Saibaman planting action. */
static const int stPlantCast[7]={8,7,10,11,12,13,14};
static void st_ch8_plant_begin(void){int i,n=Port_StoryCastActor(8),seed=Port_StoryCastActor(7);Vec4 anchor,rot;float h=st_height(seed);sStPlantActive=sStPlantRestore=1;sStPlantFrame=sStPlantRevealed=0;sStPlantParked=0;
 BtlCharApi_GetPos(st_obj(seed),&anchor);BtlCharApi_GetRot(st_obj(seed),&rot);
 if(Port_StorySceneActor(n)){sStPlantParked|=1u<<n;sStPlantHp[n]=Port_StoryReserveHealth(n);Port_StoryEnterActor(n);}st_scene_take(n,1);st_place_near(n,seed,-1.7f,0,0);Port_StoryCommitSampledRoot(n);st_scene_ground_actor(n);Port_StoryCommitSampledRoot(n);st_face(n,seed);BtlCharApi_GetPos(st_obj(n),&sStPlantPos[0]);BtlCharApi_GetRot(st_obj(n),&sStPlantRot[0]);
 for(i=1;i<7;i++){int a=Port_StoryCastActor(stPlantCast[i]);Vec4 q=anchor;float right=((i-1)%3-1)*h*1.25f,forward=((i-1)/3)*h*1.6f;if(a<0)continue;
  if(Port_StorySceneActor(a)){sStPlantParked|=1u<<a;sStPlantHp[a]=Port_StoryReserveHealth(a);}else sStPlantHp[a]=st_hp(a);
  q.x+=cosf(rot.y)*right+sinf(rot.y)*forward;q.z+=-sinf(rot.y)*right+cosf(rot.y)*forward;sStPlantPos[i]=q;sStPlantRot[i]=rot;
  /* They are genuinely parked until the seeds have been planted. */
  Port_StoryExitActor(a,sStPlantHp[a]);st_scene_take(a,0);
 }
 Port_Dbg3("story Nappa planting begins",n,seed,6,0);
}
static void st_ch8_plant_reveal(void){int i;if(sStPlantRevealed)return;sStPlantRevealed=1;
 for(i=1;i<7;i++){int a=Port_StoryCastActor(stPlantCast[i]);if(a<0)continue;Port_StoryEnterActor(a);st_scene_take(a,1);BtlCtrl_ClearHidden(a);BtlCtrl_SetPos(a,&sStPlantPos[i]);BtlCtrl_SetRot(a,&sStPlantRot[i]);Port_StoryCommitSampledRoot(a);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&sStPlantPos[i]);}
 Port_Dbg3("story planted six reveal",sStPlantFrame,6,0,0);
}

static void st_ch8_plant_pose(int actor){int i;if(!sStPlantActive)return;for(i=0;i<7;i++)if(actor==Port_StoryCastActor(stPlantCast[i])){void*obj;if(i>0&&!sStPlantRevealed)return;BtlCtrl_SetPos(actor,&sStPlantPos[i]);BtlCtrl_SetRot(actor,&sStPlantRot[i]);
 if(i==0)st_ch8_clip(actor,3216,284,(sStPlantFrame<129?sStPlantFrame:128)*256);else Port_StorySampleMotion(actor,0,0);
 st_ch5_ground_pose(actor,sStPlantPos[i].y);obj=BtlObj_Get(st_obj(actor));if(obj)BtlObj_SetPartVisible(obj,0,i==0||sStPlantFrame>=180);return;}}
static void st_ch8_plant_camera(void){Vec4 at,eye;int n=Port_StoryCastActor(8),seed=Port_StoryCastActor(7),i;float h=st_height(n),yaw=sStPlantRot[0].y;
 if(sStPlantFrame>=180)st_ch8_plant_reveal();
 if(sStPlantFrame<180){BtlCharApi_GetNodePos(st_obj(n),17,&at);eye=at;eye.x+=sinf(yaw+1.f)*h*2.1f;eye.z+=cosf(yaw+1.f)*h*2.1f;eye.y-=h*.15f;st_camera_finish(eye,at,h,n,3);}
 else{at=sStPlantPos[2];at.y-=st_height(seed)*.5f;eye=at;eye.x+=sinf(sStPlantRot[1].y)*h*2.6f;eye.z+=cosf(sStPlantRot[1].y)*h*2.6f;eye.x+=cosf(sStPlantRot[1].y)*h*1.4f;eye.z-=sinf(sStPlantRot[1].y)*h*1.4f;eye.y-=h*1.4f;st_camera_finish(eye,at,h,seed,6);}
 if(sStPlantFrame==180)Port_StorySubtitle(-1,0,0);for(i=0;i<7;i++)st_ch8_plant_pose(Port_StoryCastActor(stPlantCast[i]));
}
static void st_ch8_plant_end(void){int a,i,active=0;if(!sStPlantRestore)return;sStPlantActive=0;
 /* START/skip before the reveal must still admit the first real opponent. */
 {int first=Port_StoryCastActor(7);if(first>=0&&!(sStPlantParked>>first&1u)){Port_StoryEnterActor(first);BtlCtrl_ClearHidden(first);if(!sStPlantRevealed){BtlCtrl_SetPos(first,&sStPlantPos[1]);BtlCtrl_SetRot(first,&sStPlantRot[1]);Port_StoryCommitSampledRoot(first);st_scene_ground_actor(first);Port_StoryCommitSampledRoot(first);}}}

 for(i=1;i<7;i++)BtlCtrl_ClearHidden(Port_StoryCastActor(stPlantCast[i]));
 for(a=0;a<16;a++)if(sStPlantParked>>a&1u){Port_StoryExitActor(a,sStPlantHp[a]);st_scene_take(a,0);}
 for(i=1;i<7;i++){a=Port_StoryCastActor(stPlantCast[i]);if(a>=0&&!Port_StorySceneParked(a))active++;}
 Port_Dbg3("story planted wave restored active",active,Port_StorySceneParked(Port_StoryCastActor(8)),0,0);sStPlantRestore=0;sStPlantParked=0;
}

static void st_scene_follow(void) {
    /* Standard beat HIDE BEHIND, held every frame from the protector's CURRENT facing (the facing read while the scene is
       being staged can still be the fight's): straight behind him, so any shot from his front has him in the way. */
    if(sStRun.active&&sStRun.set==ST_SET_INTRO&&sStRun.carried>=0&&sStRun.carried<32&&(sStRun.actors>>sStRun.carried&1u)&&!stCasts[sStRun.chapter].noIntroCompanion){
        st_place_near(sStRun.carried,sStRun.cast[0],-0.38f,0.0f,0.0f);st_scene_ground_actor(sStRun.carried);
        /* once the visitor is there, the child peeks at him from behind his father */
        if(sStRun.chapter==1&&sStRun.index>=2&&sStRun.cast[2]>=0)st_face(sStRun.carried,sStRun.cast[2]);
    }
    if(sStRun.active&&sStRun.chapter==6&&!(sStRun.set==ST_SET_CANON&&sStRun.index==1)){
      int v=Port_StoryCastActor(9),lead=st_lead_now(6);
      if(v>=0&&lead>=0&&v!=lead&&(sStRun.actors>>v&1u))st_face(v,lead);
    }
    if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost)st_chao_pair_frame();
    {extern char*getenv(const char*);if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0&&getenv("BT3_STORY_CLIP_PROBE")){
      int a=sStRun.cast[9],f=sStRun.elapsed;st_ch8_clip(a,2824,f<159?352:353,(f<159?f:f-159)*256);if(f==420)Port_Dbg3("story clip probe complete",2824,353,0,0);
    }}
    if(st_yamcha_mourns(sStRun.cast[2]))st_yamcha_mourning_pose(sStRun.cast[2]);
    if(sStRun.active&&sStRun.chapter==20){
     int f=sStRun.elapsed,a=sStRun.cast[0];void*o=a>=0?BtlObj_Get(st_obj(a)):0;
     if(f>=60&&sStWishRevivePhase==0){Fade_Start(1,0,2.f/30.f);sStWishRevivePhase=1;}
     if(f>=62&&sStWishRevivePhase==1)sStWishRevivePhase=2;
     if(f>=64&&sStWishRevivePhase==2){Fade_Start(1,1,8.f/30.f);sStWishRevivePhase=3;}
     if(o)BtlObj_SetPartVisible(o,4,sStWishRevivePhase<2);
    }
    if(sStRun.chapter==17||sStRun.chapter==18){
     static Vec4 roots[16];int i,lead=sStRun.cast[0];extern int Port_StoryLoopMotion(int,int,int);
     if(sStRun.elapsed<=1){
      if(sStRun.chapter==17){st_place_near(sStRun.cast[1],lead,.60f,0,0);st_place_near(sStRun.cast[2],lead,-.4f,2.6f,0);st_place_near(sStRun.cast[3],sStRun.cast[2],.48f,0,0);st_place_near(sStRun.cast[4],lead,-2.5f,-4.f,0);{StgObjDef*door=BtlStage_GetObjDef(1);if(door){Vec4 p=door->pos;p.y=0;p.z-=60;p.x-=12;BtlCtrl_SetPos(sStRun.cast[5],&p);p.x+=24;BtlCtrl_SetPos(sStRun.cast[6],&p);}}}
      else st_place_near(sStRun.cast[1],lead,2.7f,.2f,0);
      for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i];if(a<0)continue;st_scene_ground_actor(a);BtlCharApi_GetPos(st_obj(a),roots+i);}
     }
     if(sStRun.elapsed>=0)for(i=0;i<stCasts[sStRun.chapter].count;i++){int a=sStRun.cast[i],n=(sStRun.elapsed+(i>=2?18:0))%72;if(a<0)continue;BtlCtrl_SetPos(a,roots+i);
      if(sStRun.chapter==18){
       if(i)Port_StoryLoopMotion(a,ST_MOTION_IDLE,sStRun.elapsed);
       else{int f=sStRun.elapsed;st_face(a,sStRun.cast[1]);
        if(f<60)Port_StorySampleMotion(a,0xE2,0); /* hold the same rise start: no prone-to-supine snap */
        else if(f<250)Port_StorySampleMotionQ8(a,0xE2,(f-60)*64); /* seated rise; E4 contains a roll */
        else {extern int Port_StorySampleOwnMotionQ8(int,int,int);extern void Port_StoryBlendSampledPose(void*,int,int);int q=(f-250)/4%40;void*o=BtlObj_Get(st_obj(a));
         if(f==250){Port_StorySampleMotionQ8(a,0xE2,190*64);Port_StoryBlendSampledPose(o,1,4096);}
         if(q>20)q=40-q;Port_StorySampleOwnMotionQ8(a,362,(10+q)*256);
         if(f<274)Port_StoryBlendSampledPose(o,0,(274-f)*4096/24);
        }
        {void*o=BtlObj_Get(st_obj(a));if(o)BtlObj_SetPartVisible(o,4,1);} /* native halo mesh111 */
       }
      }
      else if(i>=4){if(i==4)Port_StorySampleMotion(a,0xE2,14);else{Port_StoryLoopMotion(a,i==5?0:ST_MOTION_IDLE,sStRun.elapsed);st_face(a,lead);}}
      else{int opponent=sStRun.cast[i==0?1:i==1?0:i==2?3:2],t=(sStRun.elapsed+(i>=2?12:0))%144,turn=t/72,beat=(t%72)/24,q=t%24;
       int attack=(i==0||i==3)^turn;st_face(a,opponent);
       if(attack)Port_StorySampleMotion(a,55+beat,q);
       else Port_StorySampleMotion(a,0x193+beat,q); /* Action41: actual melee Sonic Sway, not ki deflect */
      }
      st_ch5_ground_pose(a,roots[i].y);
     }
    }
    if (sStRun.set == ST_SET_CANON && sStRun.carried >= 0 && sStRun.carrier >= 0 && sStRun.carried < 32 &&
        (sStRun.actors >> sStRun.carried & 1u)) {
        if(sStRun.chapter==1)st_ch2_pair_frame();
        else st_place_near(sStRun.carried, sStRun.carrier, 0.12f, 0.32f, 0.45f);
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
    if(sStRun.chapter==1)st_face(sStRun.arriving,sStRun.cast[0]);
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
    if(sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0){ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    {extern char*getenv(const char*);extern int atoi(const char*);extern void Port_StoryCommitSampledRoot(int);const char*e=getenv("BT3_STORY_MOTION_SURVEY");if(e&&sStSv<0&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&sStRun.index==2){
      int a=Port_StoryCastActor(atoi(e)),q;Vec4 pp,rr;const char*fr=getenv("BT3_STORY_SURVEY_FROM");sStSvFrom=fr?atoi(fr):40;{const char*l=getenv("BT3_STORY_SURVEY_LIST");sStSvN=0;while(l&&*l&&sStSvN<64){sStSvList[sStSvN++]=atoi(l);while(*l&&*l!=',')l++;if(*l)l++;}}{int tq=Port_StoryCastActor(0);Vec4 far;if(tq>=0&&tq!=a){BtlCharApi_GetPos(st_obj(tq),&far);far.x+=400;BtlCtrl_SetPos(tq,&far);}}
      for(q=0;q<16;q++){int o=Port_StoryCastActor(q);if(o>=0&&o!=a)BtlCtrl_SetHidden(o);}
      Port_StoryEnterActor(a);st_scene_take(a,1);if(BtlStage_GetStartPlace(0,&pp,&rr,0)>=0){BtlCtrl_SetPos(a,&pp);BtlCtrl_SetRot(a,&rr);}
      Port_StoryCommitSampledRoot(a);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&sStSvPos);BtlCharApi_GetRot(st_obj(a),&sStSvRot);
      sStSv=a;sStSvK=0;Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;Port_Dbg3("story survey begin actor/from",a,sStSvFrom,0,0);return;}}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost&&sStRun.index==2&&!sStRescueDone){st_rescue_begin();Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+9&&!sStEventPost&&sStRun.index==0&&!sStPowerDone){sStPowerActive=1;sStPowerFrame=0;Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;return;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+7&&!sStEventPost&&sStRun.index==0&&!sStKienDone&&!sStEventAlt){st_kien_begin();Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+4&&sStRun.index==0&&!sStAssaultDone&&!sStEventAlt){st_assault_begin();Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&sStRun.index==0&&!sStElbowGohanReady){int g=sStRun.cast[4],p=sStRun.cast[3];Port_StoryEnterActor(g);st_scene_take(g,1);{Vec4 sp,sr;if(BtlStage_GetStartPlace(0,&sp,&sr,0)>=0){BtlCtrl_SetPos(g,&sp);BtlCtrl_SetRot(g,&sr);Port_StoryCommitSampledRoot(g);}} /* off reserve parking first (Claude 2026-11-18) */{Vec4 sp,sr;if(BtlStage_GetStartPlace(0,&sp,&sr,0)>=0){sp.x+=60.f;sp.z+=40.f;BtlCtrl_SetPos(g,&sp);BtlCtrl_SetRot(g,&sr);}}Port_StorySampleMotion(g,0,0);Port_StoryCommitSampledRoot(g);{Vec4 q;BtlCharApi_GetPos(st_obj(g),&q);if(!(q.y>-1e4f&&q.y<1e4f)){Vec4 sp,sr;if(BtlStage_GetStartPlace(0,&sp,&sr,0)>=0){sp.x+=40.f;BtlCtrl_SetPos(g,&sp);Port_StoryCommitSampledRoot(g);}}}st_scene_ground_actor(g);Port_StoryCommitSampledRoot(g);BtlCharApi_GetPos(st_obj(g),&sStElbowGohanPos);sStElbowGohanReady=1;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&sStRun.index==2&&!sStElbowDone){st_ch8_elbow_begin();Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+2&&sStRun.index==3&&!sStArmBeatDone){st_ch8_arm_begin();Port_StorySubtitle(-1,0,0);ctx->step=ST_STEP_HOLD;sStRun.frames=0;return;}
    if (sStRun.index >= sStRun.count && ST_Interlude(sStRun.chapter)) { /* an interlude: no departure, nothing after */
        sStRun.frames = 0;
        ctx->step = ST_STEP_HOLD;
        return;
    }
    if (sStRun.index >= sStRun.count && !sStRun.leaves) {ctx->step=ST_STEP_HOLD;sStRun.frames=0;Ramp_Start(&ctx->timer,1.f/30.f,0,1);return;}
    if (sStRun.index >= sStRun.count) { /* the departure caption stays on the last shot */
        sStRun.actor=sStRun.rival;sStRun.departing=1;sStEarlyTakeoff=0;
        BtlCharApi_GetPos(st_obj(sStRun.rival),&sStRun.departure);
        sStDepartActors=0;
        if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON){
            /* Compose the two departing actors before sampling the new lens;
               the finishing fight may have left Goku across the doubled map. */
            int g=sStRun.cast[6],v=sStRun.cast[9];Vec4 p,r;float h=st_height(g);BtlCharApi_GetPos(st_obj(v),&p);BtlCharApi_GetRot(st_obj(v),&r);
            BtlCtrl_SetPos(v,&p);st_scene_ground_actor(v);Port_StoryCommitSampledRoot(v);
            p.x+=cosf(r.y)*h*2.5f;p.z-=sinf(r.y)*h*2.5f;
            BtlCtrl_SetPos(g,&p);st_scene_ground_actor(g);Port_StoryCommitSampledRoot(g);
        }
        if(sStRun.set==ST_SET_CANON&&stCasts[sStRun.chapter].departMask){ /* several leave together (chapter 7: Goku and Vegeta) */
            s32 i,first=-1;
            for(i=0;i<stCasts[sStRun.chapter].count&&i<ST_ACTORS;i++){s32 a=sStRun.cast[i];if(a<0||a>=32||!(stCasts[sStRun.chapter].departMask>>i&1u))continue;
                if(first<0)first=a;sStDepartActors|=1u<<a;BtlCharApi_GetPos(st_obj(a),&sStDepartFrom[i]);BtlCtrl_PlayMotion(a,0x2E,0,0.10f);st_presentation_queue(a,1u);}
            if(first>=0)sStRun.actor=first;
        }else{
        /* Native start -> flight loop, followed by a visible upward escape. */
        if(!(sStRun.chapter==1&&sStRun.set==ST_SET_CANON)){BtlCtrl_PlayMotion(sStRun.rival,0x2E,0,0.10f);st_presentation_queue(sStRun.rival,1u);}
        }
        Port_StorySubtitle(sStRun.chapter, sStRun.set == ST_SET_CANON ? ST_SET_LEAVES : ST_SET_LEAVES_ALT, 0);
        Ramp_Start(&ctx->timer, 2.0f, 0.0f, 1.0f);
        sStRun.frames = 0;
        st_scene_camera(); /* save the fixed exit framing before he moves */
        ctx->step = ST_STEP_HOLD;
        return;
    }
    /* A separate, bounded action beat BEFORE Raditz speaks. Never spend his
       subtitle time on a second entrance, and never reveal him in the family shot. */
    if(sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&sStRun.index==2&&!sStKameArrival){
      int a=sStRun.cast[2];Vec4 p=sStKameLanding;
      sStKameArrival=1;sStKameFrames=0;sStRun.actor=a;
      p.y-=st_height(a)*3.5f;BtlCtrl_SetPos(a,&p);BtlCtrl_PlayMotion(a,0x32,1,.1f);
      Port_StorySubtitle(-1,0,0);st_presentation_queue(a,1u);
      sStRun.frames=0;ctx->step=ST_STEP_HOLD;st_scene_camera();return;
    }
    /* Displayed CH8: the time-skip narrator cue changes Nappa into damaged outfit2. */
    if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+5&&sStRun.index==4){
      extern void Port_StoryNappaOutfit(int);Port_StoryNappaOutfit(Port_StoryCastActor(8));
    }
    line = ST_SceneLine(sStRun.chapter, sStRun.set, sStRun.index);
    sStRun.actor = st_line_actor(line);
    st_scene_admit_speaker(sStRun.actor);sStRun.cameraActor=-1;
    if(sStChaoPair&&sStRun.chapter==6&&!sStEventPost&&sStRun.index==1)BtlCtrl_PlayMotion(sStRun.actor,406,0,.12f);
    st_scene_follow();
    sStDriftFrames=0;st_scene_camera();
    st_act_line();
    {int set=sStRun.set,index=sStRun.index;
     if(sStRun.chapter==6&&line&&line->text[0]&&strstr(line->text[0],"Yamcha... Tien...")&&st_hp(Port_StoryCastActor(1))>0){set=ST_SET_YAMCHA_ALIVE;index=0;}
     Port_StorySubtitle(sStRun.chapter,set,index);}
    if(sStRun.chapter==6&&sStRun.set==ST_SET_INTRO&&sStRun.index==7)st_ch8_plant_begin();
    Ramp_Start(&ctx->timer, (f32)st_frames(line->text[0]) / 30.0f, 0.0f, 1.0f);
    sStRun.frames = 0;
    ctx->step = ST_STEP_HOLD;
}

/* update of a story scene: 1 while it runs, 0 when its last line (and its caption / bridge) has been held. */
static s32 st_scene_hold(BtlSeqTalkCtx *ctx) {
    if(st_shield_post_owned(-1))sStShieldPostAge++;
    if(sStRescueActive){if(st_rescue_tick()){sStRun.arriving=-1;ctx->step=ST_STEP_BEAT;}sStRun.frames++;return 1;}

    s32 timeUp;

    if (ctx->step != ST_STEP_HOLD) {
        return 1;
    }
    if(sStPowerActive){st_power_tick();if(!sStPowerActive)ctx->step=ST_STEP_BEAT;return 1;}
    if(sStKienActive){if(st_kien_tick()){sStRun.index=sStRun.count;ctx->step=ST_STEP_BEAT;}sStRun.frames++;return 1;}
    if(sStAssaultActive){if(st_assault_tick()){sStRun.index=1;ctx->step=ST_STEP_BEAT;}sStRun.frames++;return 1;}
    if(sStElbowActive){st_ch8_elbow_pair();st_ch8_elbow_camera();if(sStElbowFrame==120)Port_StorySubtitle(6,ST_SET_EVENT0+6,2);if(sStElbowFrame==170){Port_StoryBodyImpact(sStRun.cast[3]);Port_StoryImpactSound(sStRun.cast[8],ST_SE_STRIKE);BtlFacade_ShakeCamera(.9f);Port_Dbg3("story elbow impact",sStRun.cast[8],sStRun.cast[3],170,0);{Vec4 e,c;float x,y,z;BtlCharApi_GetNodePos(st_obj(sStRun.cast[8]),20,&e);BtlCharApi_GetNodePos(st_obj(sStRun.cast[3]),46,&c);x=e.x-c.x;y=e.y-c.y;z=e.z-c.z;Port_Dbg3("story elbow contact gap xyz",(int)(x*100),(int)(y*100),(int)(z*100),(int)(sqrtf(x*x+y*y+z*z)*100));}}if(sStElbowFrame==180)BtlFacade_ShakeCamera(0);sStElbowFrame++;sStRun.frames++;if(sStElbowFrame>=322){sStElbowActive=0;sStElbowDone=1;sStRun.index=3;ctx->step=ST_STEP_BEAT;}return 1;}
    if(sStPlantActive&&sStRun.chapter==6){st_ch8_plant_camera();sStPlantFrame++;sStRun.frames++;if(sStPlantFrame>=270){sStPlantActive=0;sStRun.index++;ctx->step=ST_STEP_BEAT;}return 1;}
    if(sStArmBeat&&sStRun.chapter==6){if(st_ch8_arm_tick()){sStArmBeat=0;sStArmBeatDone=1;ctx->step=ST_STEP_BEAT;}return 1;}
    if(sStSv>=0&&sStRun.chapter==6){Vec4 at=sStSvPos,eye;float h=st_height(sStSv),y=sStSvRot.y+1.3f;BtlCharApi_GetNodePos(st_obj(sStSv),17,&at);eye=at;eye.x+=sinf(y)*h*2.4f;eye.z+=cosf(y)*h*2.4f;eye.y-=h*.1f;sStCamDrift=0;st_camera_finish(eye,at,h,sStSv,3);
      if(sStSvK==0)Fade_Start(1,0,.03f);if(sStSvK==3)Fade_Start(1,1,.03f); /* white marker: k=0 */
      if(sStSvK%12==0)Port_Dbg3("story survey motion/k/fight",sStSvFrom+sStSvK/12,sStSvK,sStFightFrames,0);sStSvK++;return 1;}
    if(sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0){
      int r=sStRun.carrier,g=sStRun.cast[0],q=sStCh2TimeQ8,f=q/256;float phase=q/256.f;Vec4 at,eye;
      sStCh2DrawQ8=q;sStCh2Strike=f+1;sStCh2TimeQ8+=(f>=26&&f<44)?64:256;
      BtlCtrl_SetPos(r,&sStCh2Root);BtlCtrl_SetRot(r,&sStCh2Rot);BtlCtrl_SetPos(g,&sStCh2VictimRoot);BtlCtrl_SetRot(g,&sStCh2VictimRot);
      if(sStCh2Contact&&f>34){float t=(phase-34)/36.f,h=st_height(r);if(t>1)t=1;t=t*t*(3-2*t);
        /* The native stagger translates through its attacker if both roots stay
           at contact. Withdraw on the approach side, then settle to the floor. */
        sStCh2VictimRoot=sStCh2ContactRoot;
        sStCh2VictimRoot.x+=sinf(sStCh2Rot.y)*h*.85f*t;
        sStCh2VictimRoot.z+=cosf(sStCh2Rot.y)*h*.85f*t;
        sStCh2VictimRoot.y+=(sStCh2VictimFloor-sStCh2ContactRoot.y)*t;
        BtlCtrl_SetPos(g,&sStCh2VictimRoot);}
      Port_StorySampleMotionQ8(r,f<=14?0x47:0x49,f<=14?q:f<34&&f>=22?8*256:f<22?q-14*256:q-26*256);
      Port_StorySampleMotionQ8(g,f<22?0:0xAE,f<22?0:q-22*256);
      if(f==22){Vec4 hand,body;extern void Port_StoryBloodImpact(int,float);
        /* Native Heavy Finish49 has hit mask0xC: left knee/leg nodesA/B. */
        BtlCharApi_GetNodePos(st_obj(r),0xA,&hand);BtlCharApi_GetNodePos(st_obj(g),0x10,&body);
        sStCh2VictimRoot.x+=hand.x-body.x;sStCh2VictimRoot.y+=hand.y-body.y;sStCh2VictimRoot.z+=hand.z-body.z;BtlCtrl_SetPos(g,&sStCh2VictimRoot);Port_StorySampleMotion(g,0xAE,0);
        sStCh2BodyContact=hand;sStCh2ContactRoot=sStCh2VictimRoot;Port_StoryBodyImpact(g);st_presentation_queue(g,8u);sStCh2Contact=1;
      }
      if(f>=22&&sStCh2Contact){
        Vec4 body;float t=(phase-34)/36.f,h=st_height(r);if(t<0)t=0;if(t>1)t=1;t=t*t*(3-2*t);
        BtlCharApi_GetNodePos(st_obj(g),0x10,&body);
        /* Lock the visible abdomen, not just the root: AE contains local
           forward travel, which otherwise carries Goku through Raditz. */
        sStCh2VictimRoot.x+=sStCh2BodyContact.x+sinf(sStCh2Rot.y)*h*.85f*t-body.x;
        sStCh2VictimRoot.z+=sStCh2BodyContact.z+cosf(sStCh2Rot.y)*h*.85f*t-body.z;
        BtlCtrl_SetPos(g,&sStCh2VictimRoot);Port_StorySampleMotionQ8(g,0xAE,q-22*256);

        {Vec4 head,torso;float dx=sinf(sStCh2Rot.y),dz=cosf(sStCh2Rot.y),gap,minGap;
         BtlCharApi_GetNodePos(st_obj(g),0x2E,&head);BtlCharApi_GetNodePos(st_obj(r),0x10,&torso);
         gap=(head.x-torso.x)*dx+(head.z-torso.z)*dz;
         minGap=st_height(r)*.22f+st_height(g)*.23f;
         if(gap<minGap){sStCh2VictimRoot.x+=dx*(minGap-gap);sStCh2VictimRoot.z+=dz*(minGap-gap);
          BtlCtrl_SetPos(g,&sStCh2VictimRoot);Port_StorySampleMotionQ8(g,0xAE,q-22*256);}
        }
        const int nodes[]={0xB,0xF,0x2E,0x10,0x21,0x13};Vec4 p;float bottom=-1e20f;int j;
        for(j=0;j<6;j++){BtlCharApi_GetNodePos(st_obj(g),nodes[j],&p);if(p.y>bottom)bottom=p.y;}
        if(bottom>sStCh2VictimFloor-st_height(g)*.02f){
          sStCh2VictimRoot.y-=bottom-sStCh2VictimFloor+st_height(g)*.02f;
          BtlCtrl_SetPos(g,&sStCh2VictimRoot);Port_StorySampleMotionQ8(g,0xAE,q-22*256);
        }
      }
      if(f>=28&&!sStCh2Spit&&sStCh2Contact){Port_StoryMouthBlood(g,256);sStCh2Spit=1;}
      at=sStCh2Root;at.x=(at.x+sStCh2VictimRoot.x)*.5f;at.z=(at.z+sStCh2VictimRoot.z)*.5f;at.y-=st_height(r)*.6f;eye=at;
      eye.x+=(cosf(sStCh2Rot.y)*1.9f-sinf(sStCh2Rot.y)*.6f)*st_height(r);eye.z+=(-sinf(sStCh2Rot.y)*1.9f-cosf(sStCh2Rot.y)*.6f)*st_height(r);eye.y-=st_height(r)*.55f;
      if(f>=26&&f<44){Vec4 forward;float h=st_height(g);if(Port_StoryMouthPose(g,&at,&forward)){
        at.y-=h*.045f;eye=at;eye.x+=forward.x*h*.18f+cosf(sStCh2Rot.y)*h*.32f;
        eye.z+=forward.z*h*.18f-sinf(sStCh2Rot.y)*h*.32f;eye.y-=h*.06f;}}
      sStCamDrift=0;st_camera_finish(eye,at,st_height(r),r,8);
      if(f>=90){sStCh2Strike=0;BtlCtrl_PlayMotion(g,ST_MOTION_PRONE,1,0.f);st_scene_ground_actor(g);ctx->step=ST_STEP_BEAT;}
      return 1;
    }
#ifdef ST_REF_VIEW
    if(st_rv_tick())return 1;
#endif
    if(sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&sStKameArrival==1){
      int a=sStRun.cast[2],f=++sStKameFrames;float t=f/36.f;Vec4 p=sStKameLanding;
      if(t>1)t=1;p.y-=st_height(a)*3.5f*(1-t)*(1-t);BtlCtrl_SetPos(a,&p);
      if(f==36){BtlCtrl_PlayMotion(a,0x33,0,.08f);st_presentation_queue(a,2u);}
      st_scene_camera();
      if(f>=48){BtlCtrl_SetPos(a,&sStKameLanding);BtlCtrl_PlayMotion(a,413,1,.15f);
        sStKameArrival=2;ctx->step=ST_STEP_BEAT;}
      return 1;
    }
    if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStFormActor==Port_StoryCastActor(6)&&sStFormTarget==0){
      int g=sStFormActor,v[9];
      if(Port_StoryResourceIO(g,0,v)&&v[0]==0&&BtlCtrl_IsInterruptible(g)){
        sStFormActor=-1;sStRun.actors|=1u<<g;st_scene_ground_actor(g);st_pose(g);
        BtlCharApi_GetPos(st_obj(g),&sStEndingGokuAnchor);sStEndingGokuReady=1;
      }else if(++sStFormWait<300){
        if(sStFormWait%15==0&&BtlCtrl_IsInterruptible(g))BtlCtrl_Transform(g,0);
        return 1;
      }else{sStFormActor=-1;sStRun.actors|=1u<<g;st_scene_ground_actor(g);st_pose(g);Port_Dbg3("story ending base-form timeout",6,g,0,0);}
    }

    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index<=1&&sStEndingAnchorReady){
      /* Reapply our last authored positions BEFORE reading them again. The
         previous native frame may have changed its output root; adopting that
         output would turn a hold into a one-frame-late follower of the drift. */
      BtlCtrl_SetPos(Port_StoryCastActor(9),&sStEndingDrawAnchor[0]);
      BtlCtrl_SetPos(Port_StoryCastActor(8),&sStEndingDrawAnchor[1]);
    }
    sStRun.frames++;sStRun.elapsed++;sStDriftFrames++;st_act_frame();
    {extern char*getenv(const char*);if(sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0&&getenv("BT3_STORY_CLIP_PROBE")){st_scene_follow();st_scene_camera();return 1;}}
    if(st_ch9_pod_scene()){
        st_ch9_crawl_pose();
        if(sStRun.frames==ST_CH9_CRAWL_FRAMES)Port_Dbg3("story CH9 pod boarding",11,sStRun.rival,sStRun.frames,0);
        if(sStRun.frames==ST_CH9_HATCH_CLOSED)Port_Dbg3("story CH9 pod hatch closed",11,sStRun.rival,sStRun.frames,0);
        if(sStRun.frames==ST_CH9_POD_LAUNCH)Port_Dbg3("story CH9 pod launch",11,sStRun.rival,sStRun.frames,0);
    }else if(sStRun.departing&&sStDepartActors){
        s32 i;f32 t=(sStRun.frames-8)/30.0f;if(t<0)t=0;if(t>1)t=1;
        for(i=0;i<stCasts[sStRun.chapter].count&&i<ST_ACTORS;i++){s32 a=sStRun.cast[i];Vec4 p;f32 h;if(a<0||a>=32||!(sStDepartActors>>a&1u))continue;
            p=sStDepartFrom[i];h=st_height(a);if(sStRun.frames==9)BtlCtrl_PlayMotion(a,0x2F,1,0.10f);
            p.y-=h*(2.0f*t+18.0f*t*t);p.x+=h*4.0f*t*(i&1?1.0f:-1.0f)*0.0f;BtlCtrl_SetPos(a,&p);}
    }else if(sStRun.departing){
        Vec4 p=sStRun.departure;f32 h=st_height(sStRun.rival);
        f32 t;
        if(sStRun.chapter<=1){extern s32 BtlCtrl_IsMotionPlaying(s32);
            if(!sStEarlyTakeoff&&sStRun.frames>=4&&(!BtlCtrl_IsMotionPlaying(sStRun.rival)||sStRun.frames>=45)){
                sStEarlyTakeoff=sStRun.frames;if(!(sStRun.chapter==1&&sStRun.set==ST_SET_CANON))BtlCtrl_PlayMotion(sStRun.rival,0x2F,1,0.10f);st_presentation_queue(sStRun.rival,1u);
            }
            t=sStEarlyTakeoff?(sStRun.frames-sStEarlyTakeoff)/30.f:0.f;
        }else{t=(sStRun.frames-8)/30.f;if(sStRun.frames==9)BtlCtrl_PlayMotion(sStRun.rival,0x2F,1,0.10f);}
        if(t<0)t=0;if(t>1)t=1;
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
        p.y-=h*6.0f*(sStRun.chapter==1?1.0f-t*t:(1.0f-t)*(1.0f-t));BtlCtrl_SetPos(sStRun.arriving,&p);
        if(sStRun.index==0&&f==40)BtlCtrl_PlayMotion(sStRun.arriving,sStRun.chapter==1?0x33:ST_MOTION_IDLE,sStRun.chapter!=1,.05f);
        if(sStRun.chapter==1&&sStRun.index==0&&f==60)BtlCtrl_PlayMotion(sStRun.arriving,ST_MOTION_IDLE,1,.15f);
    }
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1)st_ch8_end_tick();
    st_scene_follow();
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index<=1){
      BtlCharApi_GetPos(st_obj(Port_StoryCastActor(9)),&sStEndingDrawAnchor[0]);
      BtlCharApi_GetPos(st_obj(Port_StoryCastActor(8)),&sStEndingDrawAnchor[1]);sStEndingAnchorReady=1;
    }
    if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!sStIntroFormFrames&&!sStIntroPound&&!(sStEventsDone&1u)){
      int a=Port_StoryCastActor(1);if(a>=0){if(sStRun.frames<=1||!(sStSurprisedActors>>a&1u))BtlCtrl_PlayMotion(a,241,1,.12f);sStSurprisedActors|=1u<<a;}
    }
    st_scene_camera();
    /* Finish this short performance before leaving; bounded fallback handles a missing end flag. */
    if(sStRun.departing&&sStRun.chapter<=1&&(!sStEarlyTakeoff||sStRun.frames-sStEarlyTakeoff<30))return 1;
    timeUp = Ramp_Step(&ctx->timer);
    if(sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&timeUp){int v=Port_StoryCastActor(9);
      /* Subtitle timing cannot skip the firing cue or cut off the actual impact/fall. */
      if((sStRun.frames<100||!sStEndingExecutionHit||sStEndingFall<24||!BtlCtrl_IsInterruptible(v))&&sStRun.frames<360)return 1;
      if(!sStEndingExecutionHit)Port_Dbg3("story execution missed",6,v,Port_StoryCastActor(8),sStRun.frames);
      st_scene_ground_actor(Port_StoryCastActor(8));
      sStRun.actors|=1u<<v;BtlCtrl_StopMotion(v);st_scene_ground_actor(v);st_face(v,Port_StoryCastActor(6));st_pose(v);
    }
    if (!timeUp) {
        return 1;
    }
    if(sStRun.chapter==11&&sStRun.set==ST_SET_CANON&&!sStRun.departing&&
       sStRun.index+1==sStRun.count&&st_ch9_pod_begin()){
        sStRun.index=sStRun.count;sStRun.frames=0;
        Port_StorySubtitle(-1,0,0);
        Ramp_Start(&ctx->timer,ST_CH9_POD_END/30.f,0,1);
        st_ch9_crawl_pose();st_scene_camera();
        return 1;
    }
    if (sStRun.index >= sStRun.count) {
        if(sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)){
          int a=Port_StoryCastActor(1),v[9];
          sStRun.actor=a;st_scene_camera(); /* stay on Gohan throughout the opening transformation */
          if(sStIntroScared>=22&&sStIntroScared<56)st_moon_reveal(a);else if(sStIntroScared<70)st_front_cam(a,1.1f,.62f,.7f,.35f);else if(sStIntroPound)st_front_cam(a,1.35f,.35f,.62f,.3f);
          if(sStIntroScared<70){if(!sStIntroScared++){int m=241; /* the child shields his head: scared */st_stage_actor(a);sStSurprisedActors|=1u<<a;BtlCtrl_PlayMotion(a,m,1,.12f);}return 1;}
                    if(sStIntroPound){if(++sStIntroPound>8&&(!BtlCtrl_IsMotionPlaying(a)||sStIntroPound>240)){if(sStIntroPound<1000){sStIntroPound=1000;BtlCtrl_PlayMotion(a,389,0,.12f);return 1;}sStIntroPound=0;sStEventsDone|=1u;Port_StoryEventDone(0);}else return 1;}
          else if(!sStIntroFormFrames){sStIntroFormFrames=1;st_stage_actor(a);BtlCtrl_StopMotion(a);sStSurprisedActors&=~(1u<<a);
            sStRun.actors&=~(1u<<a);Port_StoryGrantForm(a,180);BtlCtrl_Transform(a,180);
            Port_StorySubtitle(3,ST_SET_EVENT0,0);}
          if(!(sStEventsDone&1u)&&!sStIntroPound){
          if(Port_StoryResourceIO(a,0,v)&&v[0]==180&&BtlCtrl_IsInterruptible(a)){int clip=386; /* the Great Ape pounds its chest */
            sStIntroFormFrames=0;sStIntroPound=1;sStRun.actors|=1u<<a;st_stage_actor(a);BtlCtrl_PlayMotion(a,clip,0,.12f);return 1;}
          else if(++sStIntroFormFrames<300){if(sStIntroFormFrames%15==0&&BtlCtrl_IsInterruptible(a))BtlCtrl_Transform(a,180);return 1;}
          else {Port_Dbg3("story intro form failed",3,a,180,0);BattleResult_Set(2,1);sStIntroFormFrames=0;}}
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
        ScrXfade_Start(1, (sStRun.chapter==17||sStRun.chapter==18)?.18f:1.0f);
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
    sStEventsDone=Port_StoryEvents();sStSurprisedActors=0;
    sStMoonTailAt=0;sStMoonShooter=sStMoonTarget=-1;sStMoonRev=sStMoonRevAt=0;sStMoonCameraValid=0;sStMoonShotPlaced=0;
    sStCh3Landed=sStCh3EndingReady=sStGripWristReady=0;
    sStTailActive=sStTailToGohan=sStTailIntro=0;sStRaditzArmorBroken=0;sStArmBeat=sStArmBeatDone=sStArmFrame=sStArmCut=sStArmWait=0;sStHoldPair=sStHoldIntroFrames=sStGohanNative=sStGohanRecoil=sStBeamMovie=sStBeamMovieFrames=0;
    sStPowerActive=sStPowerDone=sStPowerFrame=sStShieldHitAge=0;sStRescueActive=sStRescueDone=sStRescueFrame=0;sStShieldPostReady=sStShieldPostAge=0;sStKienActive=sStKienDone=sStKienFrame=0;sStAssaultActive=sStAssaultDone=0;sStElbowGohanReady=0;sStElbowActive=sStElbowDone=0;sStWardrobePending=0;sStRun.active=0;sStYamchaCorpse=sStYamchaCaptured=0;sStDownActors=sStGoneActors=sStEventShow=sStDepartActors=0;sStStillPendingActors=sStStillActors=0;{extern void Port_StoryClearStillBodyPoses(void);Port_StoryClearStillBodyPoses();}sStTailCut=0;sStMoonFallValid=0;sStTiredActor=-1;sStTiredFrames=0;
    sStAtkFrames=sStBeamFrames=sStMoonFrames=sStEventPost=sStIntroFormFrames=sStIntroScared=sStIntroPound=0;
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
    sStBeamFrames = sStBeamLanded = sStBeamBlood = 0;
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
    sStEventPressed = sStBeamFrames = sStBeamLanded = sStBeamBlood = 0;sStCh3Landed=sStCh3EndingReady=sStGripWristReady=0;
    sStTailActive=sStTailToGohan=sStTailIntro=0;sStRaditzArmorBroken=0;sStArmBeat=sStArmBeatDone=sStArmFrame=sStArmCut=sStArmWait=0;sStHoldPair=sStHoldIntroFrames=sStGohanNative=sStGohanRecoil=sStBeamMovie=sStBeamMovieFrames=0;
    sStSwitchActor = -1;
    sStEventsDone = Port_StoryEvents(); /* preserve events actually completed during the opening */
    sStYamchaCorpse=sStYamchaCaptured=0;sStDownActors = sStGoneActors = sStEventShow = 0;sStStillPendingActors=sStStillActors=0;{extern void Port_StoryClearStillBodyPoses(void);Port_StoryClearStillBodyPoses();}sStTailCut = 0;sStMoonFallValid=0;sStTiredActor = -1;sStTiredFrames = 0;
    sStMoonFrames = sStMoonTailAt = 0;
    sStMoonShooter = sStMoonTarget = -1;
    sStLastEventEnd = 0;
    sStAltEnd = -1;
    sStEventAlt = 0;
    sStAtkFrames = 0;sStEventPost=sStIntroFormFrames=sStIntroScared=sStIntroPound=0;
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

/* Story wardrobe continuity: this gate concerns appearance, never hit admission or HP. */
int Port_StoryClothesProtected(int actor){int ch=st_chapter(),i;if(ch<0||actor<0)return 0;for(i=0;i<stCasts[ch].count;i++)if(Port_StoryCastActor(i)==actor)return ST_ScriptedWardrobe(ch,i);return 0;}
static void st_wardrobe_queue(int actor){if(actor>=0&&actor<16)sStWardrobePending|=1u<<actor;}
static void st_wardrobe_tick(void){int a;extern int Port_StoryScriptDamage(int);for(a=0;a<16;a++)if((sStWardrobePending>>a&1u)&&Port_StoryScriptDamage(a))sStWardrobePending&=~(1u<<a);}

static int st_wardrobe_ready(int actor){extern int Port_StoryWardrobeBusy(int);return !(sStWardrobePending>>actor&1u)&&!Port_StoryWardrobeBusy(actor);}
static void st_transform_ready(int actor,int target){if(st_wardrobe_ready(actor))BtlCtrl_Transform(actor,target);}
static void st_raditz_headbutt_outfit(int rival){
 extern void Port_StoryRaditzOutfit(int);
 if(st_chapter()!=2||sStRaditzArmorBroken)return;
 sStRaditzArmorBroken=1;Port_Dbg3("story Raditz armour after headbutt",2,rival,st_hp(rival),0);Port_StoryRaditzOutfit(rival);
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

/* Original coordinates expressed relative to Raditz, then rotated onto this map.
   No normalization by the 2x map scale: these offsets belong to the actor rigs. */
/* The native rush establishes the blast position before its final corpse shot.
   Queue the scar there on contact, not in the later event teardown. */
static void st_yamcha_crater(int actor){
 Vec4 root,probe;StgGroundHit hit;TdEvent scar;float h;u32 held;int zone;
 if(sStYamchaCraterReady||actor<0||st_chapter()!=6)return;
 h=st_height(actor);BtlCharApi_GetNodePos(st_obj(actor),3,&root);sStYamchaCraterCenter=root;probe=root;probe.y-=h*12.f+100.f;hit.y=3.4e38f;
 held=st_scene_geometry_open();zone=BtlStage_ProbeGround(-1,&probe,&hit);st_scene_geometry_close(held);
 memset(&scar,0,sizeof(scar));scar.owner=actor;scar.fx=1;scar.ax=scar.bx=root.x;scar.az=scar.bz=root.z;
 scar.ay=zone>=0&&hit.y>-1e9f&&hit.y<1e9f?hit.y:root.y;scar.r=h*1.8f;scar.depth=h*.3f;
 Port_TerrainImpact(&scar);sStYamchaFloor=scar.ay+scar.depth;hit.y=3.4e38f;if(BtlStage_ProbeGround(-1,&probe,&hit)>=0&&hit.y>-1e9f&&hit.y<1e9f)sStYamchaFloor=hit.y;sStMournLensReady=0;sStYamchaCraterReady=1;Port_Dbg3("story Yamcha crater at native final-shot entry",actor,sStAtkFrames,(int)root.x,(int)root.z);
}
static int st_yamcha_mourns(int actor){return st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_EVENT0+1&&sStEventPost&&sStYamchaCorpse&&actor==sStRun.cast[2];}
static void st_yamcha_mourning_pose(int actor){
 int y=sStRun.cast[1];Vec4 body,p,r;float h=st_height(actor);BtlCharApi_GetNodePos(st_obj(y),3,&body);BtlCharApi_GetRot(st_obj(y),&r);p=body;p.x+=cosf(r.y)*h*.75f;p.z-=sinf(r.y)*h*.75f;BtlCtrl_SetPos(actor,&p);st_scene_ground_actor(actor);r.x=r.z=0;r.y=atan2f(body.x-p.x,body.z-p.z);BtlCtrl_SetRot(actor,&r);Port_StorySampleMotion(actor,388,32);st_ch5_ground_pose(actor,sStYamchaFloor);
}
static int st_ch8_clip(int actor,int bank,int motion,int frameQ8){
 extern int Port_StorySampleChapterClip(int,int,int,int);
 if(st_chapter()!=6||actor<0)return 0;
 Port_StorySampleMotion(actor,0,0);
 return Port_StorySampleChapterClip(st_obj(actor),bank,motion,frameQ8);
}
static void st_ch8_elbow_begin(void){int n=sStRun.cast[8],p=sStRun.cast[3],k=sStRun.cast[2],g=sStRun.cast[4],i,a;Vec4 at,r;float h=st_height(n);
 for(i=0;i<3;i++){a=i==0?n:i==1?p:k;Port_StoryEnterActor(a);st_scene_take(a,1);}if(BtlStage_GetStartPlace(1,&at,&r,0)<0){BtlCharApi_GetPos(st_obj(n),&at);BtlCharApi_GetRot(st_obj(n),&r);}BtlCtrl_SetPos(n,&at);BtlCtrl_SetRot(n,&r);Port_StoryCommitSampledRoot(n);st_scene_ground_actor(n);Port_StoryCommitSampledRoot(n);BtlCharApi_GetPos(st_obj(n),&sStElbowPos[0]);BtlCharApi_GetRot(st_obj(n),&sStElbowRot[0]);
 st_place_near(p,n,-.68f,-.28f,0);Port_StoryCommitSampledRoot(p);st_scene_ground_actor(p);Port_StoryCommitSampledRoot(p);st_face(p,n);Port_StoryCommitSampledRoot(p);BtlCharApi_GetPos(st_obj(p),&sStElbowPos[1]);BtlCharApi_GetRot(st_obj(p),&sStElbowRot[1]);
 st_place_near(k,n,2.4f,.2f,0);Port_StoryCommitSampledRoot(k);st_scene_ground_actor(k);Port_StoryCommitSampledRoot(k);st_face(k,n);Port_StoryCommitSampledRoot(k);BtlCharApi_GetPos(st_obj(k),&sStElbowPos[2]);BtlCharApi_GetRot(st_obj(k),&sStElbowRot[2]);
 if(g>=0){Vec4 cero={0,0,0,0};extern void Port_StoryOffsetVisual(int,const Vec4*);Port_StoryEnterActor(g);st_scene_take(g,1);
  /* Gohan en el lado de Krillin, adentro del encuadre y sobre el piso. Antes iba a 2.5/-2.3 detras de Nappa:
     esa zona queda fuera del terreno del beat y se lo veia bajo tierra. */
  st_place_near(g,k,-1.25f,1.1f,0);Port_StoryCommitSampledRoot(g);st_scene_ground_actor(g);Port_StoryCommitSampledRoot(g);
  Port_StoryOffsetVisual(st_obj(g),&cero);BtlCharApi_GetPos(st_obj(g),&sStElbowGohanPos);sStElbowGohanReady=1;}
 sStElbowFar=sStElbowPos[1];sStElbowFar.x-=sinf(sStElbowRot[0].y)*h*2;sStElbowFar.z-=cosf(sStElbowRot[0].y)*h*2;
 sStElbowActive=1;sStElbowFrame=sStElbowGrip=0;sStElbowCueFrame=sStElbowBloodFrame=-1;
 {extern void Port_StoryElbowPose(int,float,float,int),Port_StoryElbowTarget(const Vec4*),Port_StoryTailGrip(int,int,int);Vec4 head,e,hand,tail,best=sStElbowPos[1];float score=1e20f;int u,v;
  /* Solve this pair once, with native fixed-length joints. Never stretch either model. */
  sStElbowFrame=100;
  for(u=0;u<6;u++)for(v=0;v<6;v++){float loss=0,x,y,z;st_place_near(p,n,-.46f-u*.045f,-.08f-v*.055f,0);Port_StoryCommitSampledRoot(p);st_scene_ground_actor(p);Port_StoryCommitSampledRoot(p);BtlCharApi_GetPos(st_obj(p),&sStElbowPos[1]);st_ch8_elbow_pose(p);Port_StoryTailGrip(st_obj(p),st_obj(n),1);
   BtlCharApi_GetNodePos(st_obj(p),21,&hand);BtlCharApi_GetNodePos(st_obj(n),6,&tail);x=hand.x-tail.x;y=hand.y-tail.y;z=hand.z-tail.z;loss+=x*x+y*y+z*z;
   BtlCharApi_GetNodePos(st_obj(p),35,&hand);BtlCharApi_GetNodePos(st_obj(n),5,&tail);x=hand.x-tail.x;y=hand.y-tail.y;z=hand.z-tail.z;loss+=x*x+y*y+z*z;
   BtlCharApi_GetNodePos(st_obj(p),46,&head);Port_StoryElbowTarget(&head);Port_StorySampleMotion(n,0,0);Port_StoryElbowPose(st_obj(n),sStElbowRot[0].y,1,1);BtlCharApi_GetNodePos(st_obj(n),20,&e);x=e.x-head.x;y=e.y-head.y;z=e.z-head.z;loss+=2*(x*x+y*y+z*z);if(loss<score){score=loss;best=sStElbowPos[1];}Port_StorySampleMotion(n,0,0);
  }
  sStElbowPos[1]=best;st_ch8_elbow_pose(n);st_ch8_elbow_pose(p);
  BtlCharApi_GetNodePos(st_obj(p),46,&head);Port_StoryElbowTarget(&head);Port_StorySampleMotion(n,0,0);Port_StoryElbowPose(st_obj(n),sStElbowRot[0].y,0,1);sStElbowFrame=0;
  
  Port_Dbg3("story elbow pair solve",(int)(score*100),(int)(best.x*100),(int)(best.y*100),(int)(best.z*100));
 }

 Port_Dbg3("story paired elbow begins",n,p,k,0);
}
static void st_ch8_elbow_pose(int actor){int n=sStRun.cast[8],p=sStRun.cast[3],k=sStRun.cast[2],f=sStElbowFrame;float h=st_height(n),yaw=sStElbowRot[0].y,t;Vec4 q;extern int Port_StoryRetargetClip(int,int,int,int,int);extern void Port_StoryOffsetVisual(int,const Vec4*),Port_StoryElbowPose(int,float,float,int);
 if(actor==n){BtlCtrl_SetPos(n,&sStElbowPos[0]);BtlCtrl_SetRot(n,&sStElbowRot[0]);Port_StorySampleMotion(n,0,0);t=f<152?0:f<170?(f-152)/18.f:f<178?1:f<202?1-(f-178)/24.f:0;Port_StoryElbowPose(st_obj(n),yaw,t,0);st_ch5_ground_pose(n,sStElbowPos[0].y);}
 if(actor==p){q=sStElbowPos[1];if(f<100){t=f<60?0:(f-60)/40.f;t=t*t*(3-2*t);q.x=sStElbowFar.x+(q.x-sStElbowFar.x)*t;q.z=sStElbowFar.z+(q.z-sStElbowFar.z)*t;}else if(f>=174){t=(f-174)/34.f;if(t>1)t=1;q.x-=sinf(yaw)*h*.7f*t;q.z-=cosf(yaw)*h*.7f*t;}
 BtlCtrl_SetPos(p,&q);BtlCtrl_SetRot(p,&sStElbowRot[1]);if(f<100)Port_StorySampleMotion(p,ST_MOTION_FLIGHT,0);else if(f<=170){extern void Port_StoryTailBasePose(int,int);Port_StorySampleMotion(p,0,0);Port_StoryTailBasePose(st_obj(p),st_obj(sStRun.cast[6]));{extern void Port_StoryLeanTo(int,int);Port_StoryLeanTo(st_obj(p),st_obj(n));}}else if(f<174)Port_StorySampleMotion(p,217,0);else if(f<231)Port_StorySampleMotion(p,217,f-174);else if(f<265)Port_StorySampleMotion(p,217,56);else Port_StorySampleMotion(p,227,f-265<30?f-265:29);
 if(f>170&&f<186){extern void Port_StoryElbowRecoilBlend(int,float,int);Port_StoryElbowRecoilBlend(st_obj(p),f<174?0:(f-174)/12.f,0);}
 {Vec4 pelvis,d;BtlCharApi_GetNodePos(st_obj(p),3,&pelvis);d=(Vec4){q.x-pelvis.x,0,q.z-pelvis.z,0};Port_StoryOffsetVisual(st_obj(p),&d);}st_ch5_ground_pose(p,q.y);}
 if(actor==k){q=sStElbowPos[2];if(f<28){t=f/28.f;q.x-=sinf(yaw)*h*1.5f*t;q.z-=cosf(yaw)*h*1.5f*t;q.y-=h*.3f;}else if(f<100){t=(f-28)/35.f;if(t>1)t=1;q.x-=sinf(yaw)*h*1.5f;q.z-=cosf(yaw)*h*1.5f;q.y-=h*(.3f+1.8f*t);}else if(f<130){t=(f-100)/30.f;q.y-=h*2.1f*(1-t);}
 BtlCtrl_SetPos(k,&q);BtlCtrl_SetRot(k,&sStElbowRot[2]);Port_StorySampleMotion(k,f<28?ST_MOTION_FLIGHT:f<36?0x2E:f<63?0x2F:f<75?0x30:f<100?0x26:f<130?0x32:0,f<28?f:f<36?f-28:f<63?(f-36)%12:f<75?f-63:0);if(f>=130)st_ch5_ground_pose(k,q.y);}
}
static void st_ch8_elbow_pair(void){int n=sStRun.cast[8],p=sStRun.cast[3],g=sStRun.cast[4],k=sStRun.cast[2],f=sStElbowFrame;extern void Port_StoryTailGrip(int,int,int);st_ch8_elbow_pose(n);st_ch8_elbow_pose(p);st_ch8_elbow_pose(k);
 /* A head capsule against BOTH arm segments, on the final evaluated geometry.
    Horizontal correction keeps feet grounded; hands are solved afterward. */
 if(f>=100&&f<205){int pass,seg;float min=st_height(p)*.18f;extern void Port_StoryOffsetVisual(int,const Vec4*);
  for(pass=0;pass<3;pass++)for(seg=19;seg<=20;seg++){Vec4 head,a,b,d;float vx,vy,vz,len,t,dx,dy,dz,xz,want;BtlCharApi_GetNodePos(st_obj(p),46,&head);BtlCharApi_GetNodePos(st_obj(n),seg,&a);BtlCharApi_GetNodePos(st_obj(n),seg+1,&b);vx=b.x-a.x;vy=b.y-a.y;vz=b.z-a.z;len=vx*vx+vy*vy+vz*vz;t=len>.001f?((head.x-a.x)*vx+(head.y-a.y)*vy+(head.z-a.z)*vz)/len:0;if(t<0)t=0;if(t>1)t=1;dx=head.x-a.x-vx*t;dy=head.y-a.y-vy*t;dz=head.z-a.z-vz*t;xz=sqrtf(dx*dx+dz*dz);want=min*min-dy*dy;
   if(want>0){want=sqrtf(want);if(xz<want){if(xz<.001f){dx=-sinf(sStElbowRot[0].y);dz=-cosf(sStElbowRot[0].y);xz=1;}d=(Vec4){dx/xz*(want-xz+.01f),0,dz/xz*(want-xz+.01f),0};Port_StoryOffsetVisual(st_obj(p),&d);}}
  }
 }
 /* Gohan mira desde el lado de Krillin: se vuelve a apoyar cada tanto para que no se hunda si el terreno
    cambia de altura con la camara del beat. */
 if(g>=0&&sStElbowGohanReady&&(f%24)==0){st_ch5_ground_pose(g,sStElbowGohanPos.y);}
 /* Teletransporte ANTES del dash (Juan): el golpe de aire y el sonido salen cuando Piccolo aparece en su
    punto de partida, y recien despues arranca el dash hacia Nappa. El aura lo acompana desde ese cuadro. */
 if(f==0&&sStElbowCueFrame!=f){sStElbowCueFrame=f;extern void BtlCtrl_SetAuraOn(s32);BtlCtrl_SetAuraOn(p);Port_StoryImpactSound(p,ST_SE_DASH);Port_StoryBodyImpact(p);BtlFacade_ShakeCamera(.25f);Port_Dbg3("story elbow teleport",p,0,0,0);}
 /* Llegada del dash: solo se apaga el aura. */
 if(f==100&&sStElbowCueFrame!=f){sStElbowCueFrame=f;extern void BtlCtrl_SetAuraOff(s32);BtlCtrl_SetAuraOff(p);}
 if(f>=100&&f<=170){Port_StoryTailGrip(st_obj(p),st_obj(n),(!sStElbowGrip||f>=152));sStElbowGrip=1;}
 if(f==170){extern void Port_StoryElbowRecoilBlend(int,float,int);Port_StoryElbowRecoilBlend(st_obj(p),0,1);}

}
static void st_ch8_elbow_camera(void){int n=sStRun.cast[8],p=sStRun.cast[3],k=sStRun.cast[2],f=sStElbowFrame,kind;float h=st_height(n),yaw=sStElbowRot[0].y;Vec4 at,eye,a,b;
 if(f<60){BtlCharApi_GetNodePos(st_obj(k),17,&at);BtlCharApi_GetNodePos(st_obj(n),46,&eye);eye.x+=cosf(yaw)*h*.8f;eye.z-=sinf(yaw)*h*.8f;eye.y-=h*.2f;kind=3;}
 else{BtlCharApi_GetNodePos(st_obj(n),f<152?6:20,&a);BtlCharApi_GetNodePos(st_obj(p),46,&b);at=b;if(f<178){at.x=(a.x+b.x)*.5f;at.y=(a.y+b.y)*.5f;at.z=(a.z+b.z)*.5f;}eye=at;eye.x+=cosf(yaw)*h*(f<178?1.15f:1.5f)+sinf(yaw)*h*.45f;eye.z-=sinf(yaw)*h*(f<178?1.15f:1.5f)-cosf(yaw)*h*.45f;eye.y-=h*.1f;kind=f<152?8:f<178?5:3;}
 sStCamDrift=0;st_camera_finish(eye,at,h,p,kind);
}

#include "mod_story_ch8_assault.inc"
#include "mod_story_ch8_shield.inc"
#include "mod_story_ch8_end.inc"

static void st_ch8_arm_begin(void){
 int n=sStRun.cast[8],t=sStRun.cast[0];extern void Port_StoryDetachedPrepare(void);
 Port_StoryEnterActor(n);Port_StoryEnterActor(t);st_scene_take(n,1);st_scene_take(t,1);{Vec4 p,r;if(BtlStage_GetStartPlace(0,&p,&r,0)>=0){BtlCtrl_SetPos(t,&p);BtlCtrl_SetRot(t,&r);Port_StoryCommitSampledRoot(t);}else st_stage_actor(t);}st_scene_ground_actor(t);Port_StoryCommitSampledRoot(t);st_place_near(n,t,1.15f,0,0);st_scene_ground_actor(n);st_scene_flat_pair(t,n,1.15f);st_face(n,t);st_face(t,n);Port_StoryCommitSampledRoot(t);Port_StoryCommitSampledRoot(n);
 BtlCharApi_GetPos(st_obj(n),&sStArmBy);BtlCharApi_GetPos(st_obj(t),&sStArmAt);BtlCharApi_GetRot(st_obj(n),&sStArmRot);
 sStArmBeat=1;sStArmFrame=sStArmWait=0;
 /* Align the verified punch at frame13 using its active hand21, before playback. */
 {Vec4 hand,arm;Port_StorySampleMotion(n,55,13);Port_StorySampleMotion(t,241,6);BtlCharApi_GetNodePos(st_obj(n),21,&hand);BtlCharApi_GetNodePos(st_obj(t),34,&arm);sStArmBy.x+=arm.x-hand.x;sStArmBy.z+=arm.z-hand.z;}
 Port_Dbg3("story arm anchors",(int)(sStArmBy.y*100),(int)(sStArmAt.y*100),(int)(st_height(t)*100),0);
}
static void st_ch8_arm_pose(int actor){
 int n=sStRun.cast[8],t=sStRun.cast[0],f=sStArmFrame;
 if(!sStArmBeat||st_chapter()!=6)return;
 /* Nappa strikes from above Tien so their bodies never cross (Y negative is up). */
 if(actor==n){BtlCtrl_SetPos(n,&sStArmBy);BtlCtrl_SetRot(n,&sStArmRot);Port_StorySampleMotion(n,55,f<13?f:f<19?13:f<23?f-5:17);st_ch5_ground_pose(n,sStArmBy.y-st_height(t)*.4f);}
 else if(actor==t){BtlCtrl_SetPos(t,&sStArmAt);st_face(t,n);Port_StorySampleMotion(t,f<18?241:0xAE,f<18?6:f-18<20?f-18:19);st_ch5_ground_pose(t,sStArmAt.y);}
}
static int st_ch8_arm_tick(void){
 int n=sStRun.cast[8],t=sStRun.cast[0],f=sStArmFrame;Vec4 at,eye;float h=st_height(t),yaw=sStArmRot.y;
 extern int Port_StoryDetachedReady(void),Port_StoryDetachedSpawn(int,int,float);extern void Port_StoryTienArmOutfit(int),Port_Fx_WorldSpit(const Vec4*,const Vec4*,int);
 st_ch8_arm_pose(n);st_ch8_arm_pose(t);
 if(!Port_StoryDetachedReady()&&sStArmFrame==0){if(++sStArmWait<120)return 0;Port_Dbg3("story forearm resource unavailable",n,t,0,0);return 1;}
 if(f==18){
  Vec4 arm,hand,dir,probe;
  StgGroundHit hit;
  float floor=sStArmAt.y,len;
  int spawned;
  BtlCharApi_GetNodePos(st_obj(n),21,&hand);
  BtlCharApi_GetNodePos(st_obj(t),34,&arm);
  probe=arm;probe.y-=h*4.f;hit.y=3.4e38f;
  if(BtlStage_IsReady()&&BtlStage_ProbeGround(-1,&probe,&hit)>=0&&
     fabsf(hit.y-floor)<h*2.f)floor=hit.y;
  dir=(Vec4){arm.x-hand.x,h*.18f,arm.z-hand.z,floor};
  len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
  if(len>.001f){dir.x/=len;dir.y/=len;dir.z/=len;}
  arm.w=1.f;
  spawned=Port_StoryDetachedSpawn(st_obj(t),34,floor);
  if(st_finite_pos(&arm)){
   extern void Port_Fx_WorldMouthSpit(const Vec4*,const Vec4*,int);
   Port_Fx_WorldSpit(&arm,&dir,(int)(h*320));
   Port_Fx_WorldMouthSpit(&arm,&dir,(int)(h*256));
  }
  Port_StoryBodyImpact(t);st_presentation_queue(t,16u);
  sStArmCut=1;sStArmBloodAge=0;Port_StoryTienArmOutfit(t);BtlFacade_ShakeCamera(.55f);
  Port_Dbg3("story Tien arm contact",n,t,f,spawned);
 }
 BtlCharApi_GetNodePos(st_obj(t),34,&at);eye=at;eye.x+=sinf(yaw+1.2f)*h*1.7f;eye.z+=cosf(yaw+1.2f)*h*1.7f;eye.y-=h*.15f;if(eye.y>sStArmAt.y-h*.6f)eye.y=sStArmAt.y-h*.6f;if(at.y>sStArmAt.y-h*.4f)at.y=sStArmAt.y-h*.4f;sStCamDrift=0;st_camera_finish(eye,at,h,t,3);
 if(f%12==0)Port_Dbg3("story arm camera Y",f,(int)(eye.y*100),(int)(at.y*100),(int)(sStRun.cameraFloor*100));
 sStArmFrame++;if(f>=84){BtlFacade_ShakeCamera(0);return 1;}return 0;
}
static void st_ch8_fireworks_pose(int actor){
 if(st_chapter()!=6||sStAtkFrames<=0||sStAtkEvent!=0)return;
 if(actor==sStAtkBy){extern void Port_StoryCapturePointArm(int),Port_StoryApplyPointArm(int,float),Port_StoryIdlePointBase(int);BtlCtrl_SetPos(actor,&sStFireBy);BtlCtrl_SetRot(actor,&sStFireRot);st_ch8_clip(actor,2824,353,120*256);Port_StoryCapturePointArm(st_obj(actor));BtlCtrl_SetPos(actor,&sStFireBy);Port_StoryIdlePointBase(st_obj(actor));st_ch5_ground_pose(actor,sStFireBy.y);Port_StoryApplyPointArm(st_obj(actor),sStFireRot.y);}
 else if(actor==sStAtkAt&&sStAtkFrames<62){BtlCtrl_SetPos(actor,&sStFireAt);Port_StorySampleMotion(actor,388,10000);}
}
static void st_chao_pair_sample(int a){
 int child=a==sStChaoBy,motion,tick;Vec4 p=sStChaoOrigin;
 extern int Port_StorySampleOwnMotionQ8(int,int,int);extern void Port_StoryAttachBack(int,int,float);
 if(!sStChaoPair||!sStRun.active||sStRun.chapter!=6||sStEventPost||(a!=sStChaoBy&&a!=sStChaoAt))return;
 motion=child?(sStChaoPhase?285:284):(sStChaoPhase?286:285);
 tick=sStChaoPhase?sStChaoFrame%136:sStChaoFrame<244?sStChaoFrame:243;
 if(child&&sStChaoPhase){float yaw=sStChaoRot.y+.2207846f;p.x-=3.f*sinf(yaw);p.z-=3.f*cosf(yaw);}
 {float t=sStChaoLift/60.f;if(t>1)t=1;t=t*t*(3-2*t);p.y-=st_height(sStChaoAt)*1.5f*t;}
 BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&sStChaoRot);
 Port_StorySampleOwnMotionQ8(a,motion,tick*256);
 if(!child){float t=sStChaoLift/60.f;if(t>1)t=1;t=t*t*(3-2*t);st_ch5_ground_pose(a,sStChaoOrigin.y-st_height(a)*1.5f*t);if(sStChaoLift<60&&sStChaoLift%15==0){Vec4 l,r;BtlCharApi_GetNodePos(st_obj(a),11,&l);BtlCharApi_GetNodePos(st_obj(a),15,&r);Port_Dbg3("story Nappa lift soles target",sStChaoLift,(int)(l.y*100),(int)(r.y*100),(int)((sStChaoOrigin.y-st_height(a)*1.5f*t)*100));}}

 /* Keep actual torso frames together; native placement roots are not the
    animated armour surface. Preserve child size and all local limb motion. */
 if(child)Port_StoryAttachBack(st_obj(a),st_obj(sStChaoAt),st_height(sStChaoAt)*.19f);
}
static void st_chao_pair_frame(void){
 int phase=sStRun.index>=2;if(phase!=sStChaoPhase){sStChaoPhase=phase;sStChaoFrame=0;}
 st_chao_pair_sample(sStChaoAt);st_chao_pair_sample(sStChaoBy);sStChaoFrame++;if(sStChaoLift<60)sStChaoLift++;
}
static Vec4 st_tail_reference(float x,float y,float z){
 float d=sStTailBaseRot.y-132.91f*.0174532925f;Vec4 p=sStTailOrigin;
 p.x+=x*cosf(d)+z*sinf(d);p.z+=-x*sinf(d)+z*cosf(d);p.y+=y;return p;
}
static void st_tail_sample(int actor){
 int n=actor==sStTailG?0:1,motion;
 if(!sStTailActive||st_chapter()!=2||(actor!=sStTailG&&actor!=sStTailR)||(sStTailToGohan&&sStEventHit&&actor==sStTailR))return;
 motion=n==0?(sStTailPhase==0?284:sStTailPhase==1?285:286):(sStTailPhase==0?285:sStTailPhase==1?286:287);
 BtlCtrl_SetPos(actor,&sStTailPos[n]);BtlCtrl_SetRot(actor,&sStTailRot[n]);
 Port_StorySampleMotion(actor,motion,sStTailFrame);
}
static void st_tail_frame(void){
 int phase=sStTailToGohan?2:sStRun.index>=4?2:sStRun.index>=1?1:0;
 if(phase!=sStTailPhase){sStTailPhase=phase;sStTailFrame=0;
  sStTailPos[0]=st_tail_reference(phase==2?0:-6.25f,-.4f,phase==2?0:6.68f);
  sStTailPos[1]=st_tail_reference(phase==1?-1.62f:0,0,phase==1?-.52f:0);
  sStTailRot[0]=sStTailRot[1]=sStTailBaseRot;
  sStTailRot[0].x=phase==2?3.14159265f:0;
  sStTailRot[0].y+=(phase==2?-175.24f:121.21f)*.0174532925f-132.91f*.0174532925f;
  if(phase==1){sStTailRot[1].x=70.44f*.0174532925f;sStTailRot[1].y+=(137.33f-132.91f)*.0174532925f;}
 }
 st_tail_sample(sStTailG);st_tail_sample(sStTailR);
 /* Prepare the visual swap during the last Raditz-only plea shot, so
    Goku is intact in the tail-grab shot and already torn at the ground cut. */
 if(sStTailPhase==1&&sStRun.index>=3){extern void Port_StoryStompOutfit(int);Port_StoryStompOutfit(sStTailG);Port_StoryStompOutfit(sStRun.cast[1]);}
 sStTailFrame++;
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
    if(pressed&&(was||ready)&&!(st_chapter()==11&&sStEventOffer==10&&!ready))sStEventPressed=1; /* retain a visible prompt press across one-frame admission races */
    if(!ready)return 0;
    Port_StoryPrompt(1);
    return 1;
}

static s32 st_event_ready(s32 chapter) {
    const StCast *c = &stCasts[chapter];
    s32 i, a;
    /* CH9 Genkidama requires its living thrower, including the frame of R3.
       Phased readiness normally skips down allies; do not skip Krillin here. */
    if(chapter==11&&sStEventOffer==10){a=Port_StoryCastActor(2);
        if(a<0||a>=ST_ACTORS||st_hp(a)<=0||Port_StorySceneParked(a)||(sStGoneActors>>a&1u))return 0;
    }
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
 if(ch==0&&k==0)return 1; /* CH1 blast: the charge line, then "barely moved" after the real contact */
 if(ch==6){switch(k){case 0:return 1;case 1:return 1;case 3:return 3;case 4:return 3;case 7:return 1;}}
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
    if(chapter==11&&k==0){st_wardrobe_queue(Port_StoryCastActor(0));st_wardrobe_tick();}
    Port_Dbg3("story event starts",chapter,k,ev->transformTo,ev->attackCast);
    sStEventFrames = sStEventHit = sStGohanFrames = sStGohanNative = sStGohanRecoil = 0;sStEventPost=0;sStX4Wait=sStX4Frames=sStX4Said=0;sStSbLine=sStSbLineFrames=0;
    sStEventsDone |= 1u << k;
    Port_StoryEventDone(k);
    sStCueFrames = 0;
    Port_StorySubtitle(-1, 0, 0);
    Battle_GetWork()->flags |= BATTLE_FLAG_DEMO; /* frozen as in the opening talk */
    Hud_SlideOut(0.5f);
    sStEventShow = ev->showMask;if(chapter==6&&k==1)sStYamchaWarning=0;
    if (sStEventAlt) {
        st_scene_begin(&sStEventCtx, chapter, ST_SET_ALTEV0 + k, 0, ev->altCount, -1, 0);
    } else {
        st_scene_begin(&sStEventCtx, chapter, ST_SET_EVENT0 + k, 0, st_event_before(chapter,k), -1, 0);
    }
    lead = st_lead_now(chapter);
    rival = sStRun.rival;
    sStChaoPair=0;
    if(chapter==6&&k==2){extern void Port_StoryDetachedPrepare(void);sStArmBeat=sStArmBeatDone=sStArmFrame=sStArmWait=0;Port_StoryDetachedPrepare();}
    if(chapter==6&&k==3&&Port_StoryClipReady(Port_StoryCastActor(5),284)&&Port_StoryClipReady(Port_StoryCastActor(8),285)){
     sStChaoBy=Port_StoryCastActor(5);sStChaoAt=Port_StoryCastActor(8);
     Port_StoryEnterActor(sStChaoBy);Port_StoryEnterActor(sStChaoAt);st_scene_take(sStChaoBy,1);st_scene_take(sStChaoAt,1);
     st_place_near(sStChaoAt,Port_StoryCastActor(0),2.4f,0,0);st_stage_actor(sStChaoAt);st_scene_ground_actor(sStChaoAt);
     BtlCharApi_GetPos(st_obj(sStChaoAt),&sStChaoOrigin);BtlCharApi_GetRot(st_obj(sStChaoAt),&sStChaoRot);
     sStChaoLift=0;sStChaoPair=1;sStChaoPhase=-1;sStChaoFrame=0;st_chao_pair_frame();
    }
    if(chapter==2&&k==2&&st_direction_on()&&Port_StoryClipReady(lead,284)&&Port_StoryClipReady(rival,285)){
      sStTailG=lead;sStTailR=rival;st_scene_ground_actor(rival);st_face(rival,lead);
      BtlCharApi_GetPos(st_obj(rival),&sStTailOrigin);BtlCharApi_GetRot(st_obj(rival),&sStTailBaseRot);
      sStTailActive=1;sStTailPhase=-1;sStTailFrame=0;sStTailIntro=0;st_tail_frame();
    }
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
            sStGohanNative=st_direction_on()&&Port_StoryClipReady(kid,288)&&Port_StoryClipReady(rival,352)&&Port_StoryClipReady(rival,353);
            BtlCtrl_PlayMotion(kid,sStGohanNative?52:ST_MOTION_FLIGHT,sStGohanNative?0:1,0.1f);
            /* Aura/charge are refreshed by Port_StoryFxFrame after native per-frame clears. */
            /* Rush begins on Gohan's own line, while he is actually on camera. */
        }
        if(chapter==2&&sStTailToGohan&&kid>=0){
          st_place_near(kid,rival,6.f,1.5f,0);st_scene_ground_actor(kid);
          BtlCharApi_GetPos(st_obj(kid),&sStEventFrom);st_face(kid,rival);
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
            sStHoldPair=st_direction_on()&&Port_StoryClipReady(lead,ST_CLIP_HOLD_BY)&&Port_StoryClipReady(rival,ST_CLIP_HOLD_AT);
            sStHoldIntroFrames=sStHoldPair?1:0;
            if(sStHoldPair){ /* as the reference: both on the ground, the holder at the SAME origin and facing; the clips do the rest */
                st_scene_ground_actor(rival);BtlCharApi_GetPos(st_obj(rival),&sStBeamPos);
                /* Exact reference uses a small separate root behind Raditz. Rotate
                   that local separation into this stage's authored facing. */
                {float delta=sStBeamRot.y-132.91f*.0174532925f;
                 sStBeamLeadPos=sStBeamPos;
                 sStBeamLeadPos.x+=-1.82f*cosf(delta)+1.86f*sinf(delta);
                 sStBeamLeadPos.z+=1.82f*sinf(delta)+1.86f*cosf(delta);
                 sStBeamLeadPos.y-=1.4f;}
                BtlCtrl_SetPos(lead,&sStBeamLeadPos);BtlCtrl_SetRot(lead,&sStBeamRot);
                BtlCtrl_PlayMotion(lead, ST_CLIP_HOLD_BY, 0, 0.1f); /* held for the whole scene, as the reference's held pose */
                BtlCtrl_PlayMotion(rival, ST_CLIP_HOLD_AT, 0, 0.1f); /* held for the whole scene, as the reference's held pose */
                BtlFacade_ShakeCamera(1.f);
            }else{
            st_place_near(lead, rival, -0.32f, 0.0f, 0.0f);
            BtlCtrl_PlayMotion(rival, ST_MOTION_SURPRISED, 1, 0.15f);
            BtlCtrl_PlayMotion(lead, ST_MOTION_IDLE, 1, 0.15f);
            }
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

static int st_gohan_recoil_owned(int actor){
 return st_chapter()==2&&sStRun.active&&sStEventRun==0&&sStGohanNative&&sStEventHit&&sStGohanRecoil&&actor==sStRun.rival;
}
static void st_gohan_recoil_place(int actor){
 Vec4 p;float t=(sStGohanFrames-ST_GOHAN_FLIGHT)/25.f;if(!st_gohan_recoil_owned(actor))return;
 if(t<0)t=0;if(t>1)t=1;t=t*t*(3.f-2.f*t);p=sStGohanRecoilFrom;
 p.x+=sStGohanRecoilDelta.x*t;p.z+=sStGohanRecoilDelta.z*t;BtlCtrl_SetPos(actor,&p);
}

/* Every frame of an event scene (after the talk context's own work). */
static void st_event_frame(s32 chapter) {
    const StEvent *ev = &stEvents[chapter].event[sStEventRun];
    s32 kid = (s32)sStEventTo.w, rival = sStRun.rival;
    sStEventFrames++;
    if(sStTailActive&&chapter==2&&sStEventRun==2){st_tail_frame();st_scene_camera();return;}
    if (ev->kind == ST_EVENT_GOHAN && kid >= 0) {
        if(sStTailToGohan&&sStTailActive)st_tail_frame();
        if(sStRun.index<1){
            if(sStGohanNative&&!BtlCtrl_IsMotionPlaying(kid))BtlCtrl_PlayMotion(kid,53,1,.08f);
            return;
        }
        if(++sStGohanFrames==1){BtlCtrl_PlayMotion(kid,sStGohanNative?288:ST_MOTION_FLIGHT,sStGohanNative?0:1,0.1f);st_presentation_queue(kid,1u);}
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
        /* The quiet torn-armour reload rebinds the model. Re-sample the authored
           reaction so loading cannot replace it with fight idle. */
        if(sStGohanNative&&sStEventHit){int f=sStGohanFrames-ST_GOHAN_FLIGHT;st_gohan_recoil_place(rival);
          if(f>=0&&f<25)Port_StorySampleMotion(rival,352,f);
          else if(f<143)Port_StorySampleMotion(rival,353,f-25);
          if(f==3)Port_StoryMouthBlood(rival,256);
        }
        if(sStEventHit&&sStGohanFrames==ST_GOHAN_FLIGHT+24){
          /* Cut is on Raditz's reaction: settle the child beside the pair,
             instead of replacing an airborne headbutt with standing idle. */
          st_place_near(kid,rival,1.8f,1.4f,0);st_scene_ground_actor(kid);
          BtlCtrl_PlayMotion(kid,33,0,.1f);
        }
        if(sStEventHit&&sStGohanFrames==ST_GOHAN_FLIGHT+44)BtlCtrl_PlayMotion(kid,ST_MOTION_GROUND_TIRED,1,.12f);
        if(sStEventHit&&sStGohanFrames<ST_GOHAN_FLIGHT+24){
          int f=sStGohanFrames-ST_GOHAN_FLIGHT;Vec4 rot,p,root,head,chest;float h=st_height(rival),t=f/24.f;
          BtlCharApi_GetRot(st_obj(rival),&rot);
          /* Native352 moves Raditz's evaluated torso away from the contact
             root. Follow that torso during the rebound, not stale EventTo. */
          BtlCharApi_GetPos(st_obj(kid),&root);head=st_scene_face(kid);chest=st_scene_face(rival);chest.y+=h*.22f;
          p.x=chest.x-(head.x-root.x);p.y=chest.y-(head.y-root.y);p.z=chest.z-(head.z-root.z);p.w=1;
          p.x+=(sinf(rot.y)*.65f+cosf(rot.y)*.45f)*h*t;
          p.z+=(cosf(rot.y)*.65f-sinf(rot.y)*.45f)*h*t;
          BtlCtrl_SetPos(kid,&p);Port_StorySampleMotion(kid,288,24);
          {int speaker=sStRun.actor;sStRun.actor=kid;st_scene_camera();sStRun.actor=speaker;}
        }
        if(sStEventHit&&sStGohanFrames>=ST_GOHAN_FLIGHT+24&&sStGohanFrames<=ST_GOHAN_FLIGHT+143){
            Vec4 at=st_scene_face(rival),eye=at,rot;float h=st_height(rival);
            BtlCharApi_GetRot(st_obj(rival),&rot);
            {Vec4 d;if(Port_StoryMouthPose(rival,&at,&d)){eye=at;eye.x+=d.x*h*.40f+cosf(rot.y)*h*.12f;eye.y+=d.y*h*.40f;eye.z+=d.z*h*.40f-sinf(rot.y)*h*.12f;at.y-=h*.05f;}
             else{eye.x+=sinf(rot.y)*h*.4f;eye.z+=cosf(rot.y)*h*.4f;}}

            st_camera_finish(eye,at,h,rival,3);
        }
        if (sStEventHit && sStGohanFrames == ST_GOHAN_FLIGHT + (sStGohanNative?25:ST_HIT_FRAMES)) {
            BtlCtrl_PlayMotion(rival, sStGohanNative?353:ST_ANIM_STAGGER, 0, 0.1f);
        } else if (sStEventHit && sStGohanFrames == ST_GOHAN_FLIGHT + (sStGohanNative?25:ST_HIT_FRAMES) + (sStGohanNative?118:ST_STAGGER_FRAMES)) {
            BtlCtrl_PlayMotion(rival, ST_ANIM_TIRED, 1, 0.2f);
        }
        if (sStGohanFrames == ST_GOHAN_FLIGHT && !sStEventHit) { /* the headbutt lands */
            sStEventHit = 1;
            if(chapter==2&&sStGohanNative){float dx=sStEventTo.x-sStEventFrom.x,dz=sStEventTo.z-sStEventFrom.z,len=sqrtf(dx*dx+dz*dz),h=st_height(rival);Vec4 rot;
             BtlCharApi_GetPos(st_obj(rival),&sStGohanRecoilFrom);BtlCharApi_GetRot(st_obj(rival),&rot);
             if(len>.001f){dx/=len;dz/=len;}else{dx=-sinf(rot.y);dz=-cosf(rot.y);}
             sStGohanRecoilDelta.x=dx*h*1.5f;sStGohanRecoilDelta.y=0;sStGohanRecoilDelta.z=dz*h*1.5f;sStGohanRecoilDelta.w=0;sStGohanRecoil=1;
            }
            st_raditz_headbutt_outfit(rival);Port_StoryBodyImpact(rival);
            st_set_hp(rival, st_hp(rival) > ST_GOHAN_DAMAGE ? st_hp(rival) - ST_GOHAN_DAMAGE : 1);
            BtlCtrl_PlayMotion(rival, sStGohanNative?352:ST_ANIM_HIT, 0, 0.05f);
            /* Hold the actual headbutt end pose through the contact beat. */
            if(st_direction_on())BtlFacade_ShakeCamera(.5f);
            st_presentation_queue(rival,2u|4u);
        }
    } else if (ev->kind == ST_EVENT_BEAM) {
        if (sStBeamLead >= 0 && rival >= 0) {
            BtlCtrl_SetPos(rival, &sStBeamPos);
            BtlCtrl_SetRot(rival, &sStBeamRot);
            if(sStHoldPair){extern void Port_StoryHoldPose(int,int);BtlCtrl_SetPos(sStBeamLead,&sStBeamLeadPos);BtlCtrl_SetRot(sStBeamLead,&sStBeamRot);
                /* a clip that ended hands the fighter back to his fight action (Raditz flew back up): keep both in the pair */
                Port_StoryHoldPose(sStBeamLead,ST_CLIP_HOLD_BY);Port_StoryHoldPose(rival,ST_CLIP_HOLD_AT);}
            else st_place_near(sStBeamLead, rival, -0.32f, 0.0f, 0.0f);
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
            if(sStEventFrames==30&&st_direction_on())BtlFacade_ShakeCamera(1.f);
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
    /* Scene skip still applies its wardrobe consequence. */
    if(chapter==6&&ev==&stEvents[6].event[2]&&!sStArmCut){extern void Port_StoryTienArmOutfit(int);sStArmCut=1;Port_StoryTienArmOutfit(Port_StoryCastActor(0));}
    if(chapter==6&&ev==&stEvents[6].event[5]){extern void Port_StoryNappaOutfit(int);Port_StoryNappaOutfit(Port_StoryCastActor(8));}

    if(chapter==6&&ev==&stEvents[6].event[4])st_wardrobe_queue(Port_StoryCastActor(8));
    if(chapter==11&&ev==&stEvents[11].event[0])st_wardrobe_queue(Port_StoryCastActor(0));
    if(chapter==11&&ev==&stEvents[11].event[1])st_wardrobe_queue(Port_StoryCastActor(4));
    if(chapter==11&&ev==&stEvents[11].event[6]){int v=Port_StoryCastActor(4);if(v>=0)sStTailCut|=1u<<v;}
    if(chapter==11&&ev==&stEvents[11].event[13]){int g=Port_StoryCastActor(1);if(g>=0)sStTailCut|=1u<<g;}
    st_wardrobe_tick();
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
      if(g>=0)sStDownActors|=1u<<g;if(v>=0){sStDownActors|=1u<<v;BtlCtrl_PlayMotion(v,ST_MOTION_PRONE,1,0.f);}
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
            if(chapter==6&&(i==0||i==3))sStStillPendingActors|=1u<<a;
            if(chapter==6&&ev==&stEvents[6].event[1]&&i==1&&sStAtkEvent==1&&sStAtkContact&&sStYamchaCaptured&&BtlCtrl_IsInterruptible(a)&&Port_SequencePartner(a)<0){
              float bottom=sStYamchaBottom;Vec4 root=sStYamchaMovieRoot,probe;StgGroundHit hit;TdEvent scar;float h=st_height(a);u32 held;int zone;
              sStYamchaCorpse=1;sStYamchaAnchor=root;sStYamchaRot=sStYamchaMovieRot;{Vec4 center;BtlCharApi_GetNodePos(st_obj(a),3,&center);if(sStYamchaCraterReady){sStYamchaAnchor.x+=sStYamchaCraterCenter.x-center.x;sStYamchaAnchor.z+=sStYamchaCraterCenter.z-center.z;}}
              memset(&scar,0,sizeof(scar));scar.owner=a;scar.fx=1;scar.ax=scar.bx=root.x;scar.az=scar.bz=root.z;
              held=st_scene_geometry_open();probe=sStYamchaCraterCenter;probe.y-=h*12.f+100.f;hit.y=3.4e38f;zone=BtlStage_ProbeGround(-1,&probe,&hit);
              scar.ay=zone>=0&&hit.y>-1e9f&&hit.y<1e9f?hit.y:root.y;scar.r=h*1.8f;scar.depth=h*.3f;
              if(!sStYamchaCraterReady){Port_TerrainImpact(&scar);sStYamchaCraterReady=1;}
              /* Query the actual remeshed collision floor, including an existing
                 explosion pit, and put the captured body's lowest joint on it. */
              hit.y=3.4e38f;zone=BtlStage_ProbeGround(-1,&probe,&hit);st_scene_geometry_close(held);
              sStYamchaFloor=zone>=0&&hit.y>-1e9f&&hit.y<1e9f?hit.y:scar.ay;sStYamchaAnchor.y=sStYamchaFloor-(bottom-root.y);
              BtlCtrl_SetPos(a,&sStYamchaAnchor);
            }else Port_StoryBeamVictim(a, rival >= 0 && rival != a ? rival : Port_StoryCastActor(0), st_hp(a));
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
    if(chapter==6&&k==1){sStYamchaCaptured=0;sStYamchaCraterReady=0;}
    const StEvent *ev = &stEvents[chapter].event[k];
    sStAttackPlacement=0;
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
    if(chapter==0||(chapter==11&&(k==6||k==13))){
      st_reset_attack_origin(sStAtkAt);st_reset_attack_origin(sStAtkBy);
    }
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
    if(chapter==0||chapter==6){st_scene_ground_actor(sStAtkBy);st_scene_ground_actor(sStAtkAt);if(chapter==0){Port_StoryCommitSampledRoot(sStAtkBy);Port_StoryCommitSampledRoot(sStAtkAt);}}
    if(sStAtkBack>=0){Vec4 q;BtlCharApi_GetPos(st_obj(sStAtkBack),&q);q.y-=st_height(sStAtkBack)*5;BtlCtrl_SetPos(sStAtkBack,&q);st_face(sStAtkBack,sStAtkBy);st_face(sStAtkBy,sStAtkBack);}
    if(sStAtkBy<0||sStAtkAt<0){sStAtkFrames=0;st_attack_restore_down();Port_Dbg3("story authored attack missing participant",chapter,k,0,0);BattleResult_Set(2,1);return;}

    if(chapter==6&&k==7){
      st_stage_actor(sStAtkAt);Port_StoryResetSceneOffset(sStAtkAt);if(sStKienDone){BtlCtrl_SetPos(sStAtkAt,&ksPos[4]);BtlCtrl_SetRot(sStAtkAt,&ksRot[4]);Port_StoryCommitSampledRoot(sStAtkAt);}st_scene_ground_actor(sStAtkAt);Port_StoryCommitSampledRoot(sStAtkAt);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStShieldGohanAnchor);
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
    if(chapter==6&&k==0){
      Port_StoryEnterActor(sStAtkBy);st_place_near(sStAtkBy,sStAtkAt,-2.5f,.55f,0);st_scene_ground_actor(sStAtkBy);st_face(sStAtkBy,sStAtkAt);st_face(sStAtkAt,sStAtkBy);
      BtlCharApi_GetPos(st_obj(sStAtkBy),&sStFireBy);BtlCharApi_GetPos(st_obj(sStAtkAt),&sStFireAt);BtlCharApi_GetRot(st_obj(sStAtkBy),&sStFireRot);
      {int i;float h=st_height(sStAtkBy),dx=sStFireAt.x-sStFireBy.x,dz=sStFireAt.z-sStFireBy.z,len=sqrtf(dx*dx+dz*dz);if(len>.01f){dx/=len;dz/=len;for(i=0;i<6;i++){int a=Port_StoryCastActor(i);Vec4 p=sStFireAt;st_scene_take(a,1);p.x+=(dx*2.4f+dz*(i*.9f+.4f))*h;p.z+=(dz*2.4f-dx*(i*.9f+.4f))*h;BtlCtrl_SetPos(a,&p);st_scene_ground_actor(a);BtlCharApi_GetPos(st_obj(a),&sStFireGroup[i]);sStFireGroupFloor[i]=sStFireGroup[i].y;st_face(a,sStAtkAt);BtlCharApi_GetRot(st_obj(a),&sStFireGroupRot[i]);sStEventShow|=1u<<i;}}}
      Battle_GetWork()->flags|=BATTLE_FLAG_DEMO;return;
    }
    if(ev->choreo==ST_CHOREO_EXECUTE){
      /* A player KO already left the phase enemy down. The authored execution
         still needs a hittable native victim, strictly inside this owned shot. */
      if(st_hp(sStAtkAt)<=0)Port_StoryReviveActor(sStAtkAt,1);
      Port_StoryEnterActor(sStAtkBy);st_place_near(sStAtkBy,sStAtkAt,-1.5f,.5f,0);st_scene_ground_actor(sStAtkBy);st_scene_ground_actor(sStAtkAt);
      if(chapter==6)st_scene_flat_pair(sStAtkBy,sStAtkAt,1.3f);
      {extern void Port_StoryHoldPose(int,int);Port_StoryHoldPose(sStAtkAt,chapter==6&&sStAtkAt==sStRun.cast[3]&&ev==&stEvents[6].event[7]?284:ST_MOTION_IDLE);} /* Juan 2026-11-18: Piccolo spreads his arms (own 284) */
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
        s32 helper = sStAtkBy, i; /* Krillin owns the throw and uses his own slot-4 Genkidama resources. */
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
        BtlCtrl_PlayMotion(sStAtkBy,ST_MOTION_FLIGHT,1,.1f);
        BtlCtrl_PlayMotion(sStAtkAt,ST_MOTION_FLIGHT,1,.1f);
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
        if(chapter==6&&k==3&&sStChaoPair){
          sStChaoPair=0;sStAtkFired=1;BtlCtrl_StopMotion(sStAtkAt);
          {extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(sStAtkAt);}
          sStAtkContact=0;sStAtkFrames=1;
          if(!Port_StoryUseTechnique(sStAtkBy,ev->attackTech)){sStAtkFrames=0;Port_Dbg3("story technique admission failed",chapter,k,0,0);BattleResult_Set(2,1);}
          return;
        }
        BtlCtrl_PlayMotion(sStAtkBy, ST_MOTION_FLIGHT, 1, 0.1f);
        st_presentation_queue(sStAtkBy, 1u);
        return; /* st_choreo_update */
    }

    if (ev->attackTech >= 0) {
        /* A stationary shot's held victim must keep this staged world frame
           through native saved/path/ground writers, not merely until dispatch.
           Moving rushes, paired clashes, grips and the Spirit Bomb own their
           paths separately and must never inherit a static shooter anchor. */
        if(ev->counterCast<=0&&(ev->choreo==0||ev->choreo==ST_CHOREO_EXECUTE)){
          BtlCharApi_GetPos(st_obj(sStAtkAt),&sStAttackPos[0]);BtlCharApi_GetRot(st_obj(sStAtkAt),&sStAttackRot[0]);
          BtlCharApi_GetPos(st_obj(sStAtkBy),&sStAttackPos[1]);BtlCharApi_GetRot(st_obj(sStAtkBy),&sStAttackRot[1]);
          sStAttackPlacement=1|((chapter==0||ev->choreo==ST_CHOREO_EXECUTE)?2:0);
        }
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
enum {
    ST_APE_FALL_FRAMES = 40, ST_CH9_KIENZAN_CUT = 38,
    ST_CH9_CRUSH_BRACE = 33, ST_CH9_CRUSH_SETTLE = 54, ST_CH9_APE_IMPACT_HOLD = 23
};
static void st_tail_base(int,Vec4*);
static void st_vnode(int,int,Vec4*);

static float st_ch9_tail_yaw(int ape,const Vec4 *tail){
    Vec4 hip,rot;
    st_vnode(ape,3,&hip);
    if(st_finite_pos(&hip)&&hypotf(tail->x-hip.x,tail->z-hip.z)>.01f)
        return atan2f(tail->x-hip.x,tail->z-hip.z);
    BtlCharApi_GetRot(st_obj(ape),&rot);
    return rot.y+3.14159265f;
}

static void st_ch9_tail_blood(int ape){
    Vec4 stump,dir;
    float yaw,h=st_height(ape);
    extern void Port_Fx_SetTint(int,int,int);
    extern void Port_Fx_WorldWoundSpit(const Vec4*,const Vec4*,int);
    st_tail_base(ape,&stump);
    if(!st_finite_pos(&stump))return;
    yaw=st_ch9_tail_yaw(ape,&stump);
    dir=(Vec4){sinf(yaw),-.4f,cosf(yaw),BtlCharApi_GetGroundY(st_obj(ape))};
    Port_Fx_SetTint(190,12,12);
    Port_Fx_WorldWoundSpit(&stump,&dir,(int)(h*170.f));
    dir.x=sinf(yaw+.7f);dir.z=cosf(yaw+.7f);dir.y=-.72f;
    Port_Fx_WorldWoundSpit(&stump,&dir,(int)(h*115.f));
}

static void st_ch9_sword_cut(int f){
    Vec4 tail,p,rot={0,0,0,1},at,eye,hand;
    int by=sStAtkBy,ape=sStAtkAt;
    float h=st_height(by),yaw,t;
    st_tail_base(ape,&tail);
    yaw=st_ch9_tail_yaw(ape,&tail);
    p=tail;
    p.x+=(cosf(yaw)*.85f+sinf(yaw)*.62f)*h;
    p.z+=(cosf(yaw)*.62f-sinf(yaw)*.85f)*h;
    p.y+=h*.32f;
    rot.y=atan2f(tail.x-p.x,tail.z-p.z);
    BtlCtrl_SetPos(by,&p);BtlCtrl_SetRot(by,&rot);Port_StoryCommitSampledRoot(by);
    if(f==2)BtlCtrl_PlayMotion(by,ST_ANIM_SWORD,0,.1f);
    if(f>=ST_TAIL_SLASH-6){
        BtlCharApi_GetNodePos(st_obj(by),0x23,&hand);
        t=(f-ST_TAIL_SLASH+6)/6.f;
        if(st_finite_pos(&hand)&&st_dist(&hand,&tail)<2.2f*h)
            st_move_toward(by,&(Vec4){p.x+(tail.x-hand.x)*t,p.y+(tail.y-hand.y)*t,p.z+(tail.z-hand.z)*t,1},1.f);
    }
    at=tail;at.y+=h*.1f;
    eye=at;eye.x+=(cosf(yaw)*1.2f+sinf(yaw)*2.9f)*h;
    eye.z+=(cosf(yaw)*2.9f-sinf(yaw)*1.2f)*h;
    eye.y-=h*.65f;
    sStCamDrift=0;st_camera_finish(eye,at,h,by,3);
}

static void st_ch9_kienzan_cut(int f,int draw){
    int by=sStAtkBy,ape=sStAtkAt,krillin=Port_StoryCastActor(2);
    Vec4 tail,rot={0,0,0,1},hand,p,at,eye;
    float h=st_height(by),yaw,t;
    extern int Port_StoryKienzanArm(int,int,float),Port_StoryNativeDiscPlace(int,const Vec4*,float,int);
    extern int Port_StoryDiscPalette(int);
    st_tail_base(ape,&tail);
    if(!draw){
        yaw=st_ch9_tail_yaw(ape,&tail);
        p=tail;p.x+=(cosf(yaw)*1.25f+sinf(yaw)*1.8f)*h;
        p.z+=(cosf(yaw)*1.8f-sinf(yaw)*1.25f)*h;
        p.y+=h*.78f;
        rot.y=atan2f(tail.x-p.x,tail.z-p.z);
        BtlCtrl_SetPos(by,&p);BtlCtrl_SetRot(by,&rot);Port_StoryCommitSampledRoot(by);
        sStAttackPlacement=2;
        sStAttackPos[1]=p;sStAttackRot[1]=rot;
        at=p;at.x+=(tail.x-at.x)*.24f;at.z+=(tail.z-at.z)*.24f;at.y-=h*.78f;
        eye=at;
        eye.x+=(cosf(yaw)*2.65f+sinf(yaw)*.85f)*h;
        eye.z+=(cosf(yaw)*.85f-sinf(yaw)*2.65f)*h;
        eye.y-=h*.13f;sStCamDrift=0;st_camera_finish(eye,at,h,by,5);
        return;
    }
    if(krillin<0)return;
    t=f/10.f;if(t>1.f)t=1.f;
    if(!Port_StoryKienzanArm(st_obj(by),st_obj(krillin),t*t*(3.f-2.f*t)))return;
    BtlCharApi_GetNodePos(st_obj(by),21,&hand);hand.y-=h*.16f;
    t=(f-(ST_CH9_KIENZAN_CUT-12))/12.f;if(t<0)t=0;if(t>1)t=1;
    p=as_lerp(hand,tail,t);
    Port_StoryNativeDiscPlace(st_obj(by),&p,.35f,Port_StoryDiscPalette(krillin));
}

int Port_StoryCh9QuietRevert(int obj){
    return st_chapter()==11&&sStAtkFrames>=ST_CH9_KIENZAN_CUT+ST_APE_FALL_FRAMES&&
        sStAtkEvent==13&&sStAtkAt>=0&&obj==st_obj(sStAtkAt);
}

/* CH4's native forward collapse, with its original sampling and easing. */
static void st_sample_ape_fall(int actor,int age,const Vec4 *floor,const Vec4 *facing,Vec4 *pos,Vec4 *rot){
    float pitch=age/28.f,drop=age/(float)ST_APE_FALL_FRAMES;
    if(pitch>1.f)pitch=1.f;
    if(drop>1.f)drop=1.f;
    pitch=pitch*pitch*(3.f-2.f*pitch);
    *pos=*floor;*rot=*facing;
    rot->x=-1.2f*(1.f-pitch);
    pos->y-=st_height(actor)*.4f*(1.f-drop*drop);
    Port_StorySampleMotionQ8(actor,199,(age>ST_APE_FALL_FRAMES?ST_APE_FALL_FRAMES:age)*128);
    BtlCtrl_SetPos(actor,pos);BtlCtrl_SetRot(actor,rot);
}

static void st_ch9_tail_fall(int age,int crush){
    int ape=sStAtkAt,cutter=sStAtkBy;
    float h=st_height(ape),yaw,dolly=0.f,distance;
    Vec4 at,eye;
    if(age==0){
        sStCh9CrushDrop=0.f;
        st_scene_ground_actor(ape);
        Port_StoryCommitSampledRoot(ape);
        BtlCharApi_GetPos(st_obj(ape),&sStCh9ApeFloor);
        BtlCharApi_GetRot(st_obj(ape),&sStCh9ApeFacing);
        sStCh9ApeFacing.x=sStCh9ApeFacing.z=0;
        sStAttackPlacement=1;
        if(crush){
            Vec4 head,mark;
            float facing=sStCh9ApeFacing.y,vh=st_height(cutter);
            /* Evaluate the landing pose before the shot starts, then restore the
               current frame below. The head can land well short of the actor's facing offset. */
            st_sample_ape_fall(ape,ST_APE_FALL_FRAMES,&sStCh9ApeFloor,&sStCh9ApeFacing,
                &sStAttackPos[0],&sStAttackRot[0]);
            Port_StoryCommitSampledRoot(ape);
            head=sStCh9ApeFloor;st_vnode(ape,0x2E,&head);
            st_place_near(cutter,ape,.54f,.14f,0);
            Port_StoryCommitSampledRoot(cutter);
            if(st_finite_pos(&head)&&st_dist(&head,&sStCh9ApeFloor)<h*2.f){
                mark=head;
                mark.x+=cosf(facing)*vh*.26f;
                mark.z-=sinf(facing)*vh*.26f;
                mark.y=sStCh9ApeFloor.y;
                BtlCtrl_SetPos(cutter,&mark);
                Port_StoryCommitSampledRoot(cutter);
            }
            st_scene_ground_actor(cutter);
            Port_StoryCommitSampledRoot(cutter);
            st_face(cutter,ape);
            Port_StoryCommitSampledRoot(cutter);
            BtlCharApi_GetPos(st_obj(cutter),&sStAttackPos[1]);
            BtlCharApi_GetRot(st_obj(cutter),&sStAttackRot[1]);
            if(st_finite_pos(&head)&&st_dist(&head,&sStCh9ApeFloor)<h*2.f){
                float dy=sStAttackPos[1].y-vh*.55f-(head.y+h*.09f);
                /* Keep the falling hand above the floor while the head covers Vegeta. */
                if(dy>0)sStCh9CrushDrop=dy<h*.045f?dy:h*.045f;
            }
            sStAttackPlacement|=2;
        }
    }
    st_sample_ape_fall(ape,age,&sStCh9ApeFloor,&sStCh9ApeFacing,&sStAttackPos[0],&sStAttackRot[0]);
    if(crush&&age>26){
        float drop=(age-26)/(float)(ST_APE_FALL_FRAMES-26);
        if(drop>1)drop=1;
        sStAttackPos[0].y+=sStCh9CrushDrop*drop*drop*(3.f-2.f*drop);
        BtlCtrl_SetPos(ape,&sStAttackPos[0]);
    }
    if(age<5){
        BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);
        return;
    }
    at=sStCh9ApeFloor;
    yaw=sStCh9ApeFacing.y;
    if(crush){
        dolly=(age-23)/20.f;if(dolly<0)dolly=0;if(dolly>1)dolly=1;
        dolly=dolly*dolly*(3.f-2.f*dolly);
        at=as_lerp(at,sStAttackPos[1],.5f+.5f*dolly);
    }
    at.y-=crush?h*.32f*(1.f-dolly)+st_height(cutter)*.5f*dolly:h*.47f;
    eye=at;
    distance=h*(crush?1.48f-.99f*dolly:1.9f);
    yaw+=1.38f-.98f*dolly;
    eye.x+=sinf(yaw)*distance;
    eye.z+=cosf(yaw)*distance;
    eye.y-=h*(.18f-.045f*dolly);
    sStCamDrift=0;
    st_camera_finish(eye,at,crush?st_height(cutter):h,crush?cutter:ape,5);
}

static void st_ch9_crush_pose(int age){
    int actor=sStAtkBy;
    int recoil=age<ST_APE_FALL_FRAMES;
    int start=recoil?ST_CH9_CRUSH_BRACE:ST_APE_FALL_FRAMES;
    int end=recoil?ST_APE_FALL_FRAMES:ST_CH9_CRUSH_SETTLE;
    float t=(age-start)/(float)(end-start);
    Vec4 pos=sStAttackPos[1],rot=sStAttackRot[1];
    void *obj=BtlObj_Get(st_obj(actor));
    extern void Port_StoryBlendSampledPose(void*,int,int);
    if(age==start)Port_StoryBlendSampledPose(obj,1,4096);
    if(t>1)t=1;
    if(!recoil){
        float slide=st_height(actor)*.28f*t*(2.f-t);
        pos.x-=sinf(rot.y)*slide;
        pos.z-=cosf(rot.y)*slide;
    }
    BtlCtrl_SetPos(actor,&pos);BtlCtrl_SetRot(actor,&rot);
    Port_StorySampleMotion(actor,recoil?ST_ANIM_HIT:ST_MOTION_PRONE,recoil?(age-start)*2:0);
    Port_StoryBlendSampledPose(obj,0,(int)(4096*(1.f-t)));
    Port_StoryCommitSampledRoot(actor);
    st_ch5_ground_pose(actor,pos.y);
    if(age==ST_APE_FALL_FRAMES){
        Vec4 head=sStAttackPos[0],torso=pos;
        float dx,dz,dy;
        st_vnode(sStAtkAt,0x2E,&head);
        BtlCharApi_GetNodePos(st_obj(actor),17,&torso);
        dx=head.x-torso.x;dz=head.z-torso.z;
        dy=head.y+st_height(sStAtkAt)*.09f-torso.y;
        if(st_finite_pos(&head)&&st_finite_pos(&torso))
            Port_Dbg3("story CH9 head contact gap x10",(int)(sqrtf(dx*dx+dz*dz)*10.f),
                (int)(dy*10.f),(int)(st_height(actor)*10.f),(int)(sStCh9CrushDrop*10.f));
    }
}

static s32 st_choreo_update(s32 chapter, const StEvent *ev, s32 f) {
    if(chapter==6&&sStAtkEvent==0){
      extern void Port_StoryExplosion(int,float);Vec4 at=sStFireAt,eye;float h=st_height(sStAtkAt),yaw=sStFireRot.y;
      st_ch8_fireworks_pose(sStAtkBy);if(f<58)st_ch8_fireworks_pose(sStAtkAt);
      if(f<35||f>=86){Vec4 head,hand,front={0,0,1,0},mouth;float vh=st_height(sStAtkBy);BtlCharApi_GetNodePos(st_obj(sStAtkBy),0x2E,&head);BtlCharApi_GetNodePos(st_obj(sStAtkBy),0x15,&hand);Port_StoryMouthPose(sStAtkBy,&mouth,&front);at=head;at.x=(head.x+hand.x)*.5f;at.y=(head.y+hand.y)*.5f;at.z=(head.z+hand.z)*.5f;eye=at;eye.x+=sinf(yaw+1.1f)*vh*.75f;eye.z+=cosf(yaw+1.1f)*vh*.75f;eye.y-=vh*.06f;sStCamDrift=0;st_camera_finish(eye,at,vh,sStAtkBy,3);}
      else{float vh=st_height(sStAtkBy);at.x+=cosf(yaw)*vh*.85f;at.z-=sinf(yaw)*vh*.85f;at.y-=h*.6f;eye=sStFireBy;eye.x-=sinf(yaw)*vh*1.6f;eye.z-=cosf(yaw)*vh*1.6f;eye.x+=cosf(yaw)*vh*1.4f;eye.z-=sinf(yaw)*vh*1.4f;eye.y-=vh*.85f;if(f>=58&&f<78){float q=(78-f)/20.f;eye.x+=sinf(f*2.7f)*vh*.045f*q;eye.y+=cosf(f*3.1f)*vh*.035f*q;}sStCamDrift=0;st_camera_finish(eye,at,vh,sStAtkBy,3);}
      if(f==58){Port_StoryExplosion(st_obj(sStAtkAt),.8f);{extern void Port_Fx_WorldGreenBurst(const Vec4*,const Vec4*,int);Vec4 p=sStFireAt,d={0,-.5f,1,0};p.y-=h*.4f;d.w=sStFireAt.y;Port_Fx_WorldGreenBurst(&p,&d,(int)(h*512));}st_presentation_queue(sStAtkAt,2u);BtlFacade_ShakeCamera(.8f);Port_Dbg3("story Fireworks authored explosion",sStAtkBy,sStAtkAt,f,0);}
      if(f==60)Port_StoryExplosion(st_obj(sStAtkAt),.45f);
      if(f==62){st_set_hp(sStAtkAt,0);sStGoneActors|=1u<<sStAtkAt;Port_StoryRemoveActor(sStAtkAt);}
      if(f>=108){BtlFacade_ShakeCamera(0);st_attack_done(chapter,ev);}
      return 1;
    }

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
        if(f<=ST_TAIL_SLASH)st_ch9_sword_cut(f);
        if (f == ST_TAIL_SLASH) {
            st_ch9_tail_blood(sStAtkAt);
            st_presentation_queue(sStAtkBy,2u);
            st_presentation_queue(sStAtkAt,2u|4u);
            st_event_effects(chapter,ev,sStAtkRival);
            Port_Dbg3("story CH9 Yajirobe tail contact",sStAtkBy,sStAtkAt,f,0);
        }
        if(f>=ST_TAIL_SLASH&&f<=ST_TAIL_SLASH+ST_APE_FALL_FRAMES)
            st_ch9_tail_fall(f-ST_TAIL_SLASH,0);
        if (f == ST_TAIL_SLASH+ST_APE_FALL_FRAMES+1) {
            s32 who = ev->transformCast >= 0 && ev->transformCast < stCasts[chapter].count ? Port_StoryCastActor(ev->transformCast) : -1;
            sStAttackPlacement=0;sStAtkFree=1;
            if (who >= 0 && ev->transformTo > 0) {
                BtlCtrl_StopMotion(who);
                Port_StoryGrantForm(who, ev->transformTo);
                sStFormActor=who;sStFormTarget=ev->transformTo;sStFormWait=0;st_transform_ready(who, ev->transformTo);
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
        if (f >= ST_TAIL_SLASH+ST_APE_FALL_FRAMES+8) {
            st_scene_take(sStAtkBy, 0); /* back to the sidelines */
            sStAtkFrames = 0;
        }
        return 1;
    }
    if (ev->choreo == ST_CHOREO_SPIRITBOMB) {
        /* Krillin's own ultimate already contains Gohan's return and Vegeta's
           paired impact. Dispatch it once; no authored dodge/rebound or steering. */
        s32 helper = sStAtkBy;
        if (sStSbStage == 0 && f >= ST_SB_FIRE) {
            sStSbStage = 3;
            sStAtkContact = 0;
            sStAtkFree = 1;
            sStSbFlip = 0;
            BtlCtrl_StopMotion(sStAtkAt);
            BtlFacade_ClearFixedCamera();
            gPortTarget[helper] = sStAtkAt;
            if (!Port_StoryUseTechnique(helper, ev->attackTech)) {
                sStAtkFrames = 0;
                Port_Dbg3("story technique admission failed", chapter, sStAtkEvent, 0, 0);
                BattleResult_Set(2, 1);
                return 1;
            }
            if (!sStSbLine) { sStSbLine = 1; sStSbLineFrames = 0; }
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
 if(chapter==6&&k==7){before=0;st_wardrobe_tick();if(!st_wardrobe_ready(Port_StoryCastActor(3))&&sStAtkFrames<ST_ATTACK_FRAMES)return;}
 if(chapter==11&&k==1&&sStX4Said){before=ev->lineCount;sStX4Frames=0;Port_StorySubtitle(-1,0,0);} /* already said in the struggle */
 if(chapter==6&&k==7){int j;for(j=0;j<5;j++){int a=sStRun.cast[ksCast[j]];Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&ksPos[j]);BtlCharApi_GetRot(st_obj(a),&ksRot[j]);}}
 sStAtkFrames=0;st_attack_restore_down();
 for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0&&!Port_StorySceneParked(a))BtlCtrl_StopMotion(a);}
 if(ev->choreo==ST_CHOREO_EXECUTE&&!(chapter==6&&k==0))Port_StoryExitActor(sStAtkBy,st_hp(sStAtkBy));
 st_event_effects(chapter,ev,sStAtkRival);
 for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0&&!Port_StorySceneParked(a))st_stage_actor(a);}
 if(chapter==6&&k==1&&sStYamchaWarning)before=2;
 if(chapter==6&&k==0)sStEventShow=0x23fu; /* cast0..5 plus Vegeta9; Goku6 has not arrived */
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

    if(chapter==6&&sStAtkEvent==3){
     int n=sStAtkAt,k=sStAtkBy;Vec4 nh,kh,at,eye;float h=st_height(n),yaw=sStChaoRot.y+1.9f;
     BtlCharApi_GetNodePos(st_obj(n),0x2E,&nh);BtlCharApi_GetNodePos(st_obj(k),0x2E,&kh);
     at=sStAtkContact?nh:kh;eye=at;eye.x+=sinf(yaw)*h*1.1f;eye.z+=cosf(yaw)*h*1.1f;eye.y-=h*.08f;
     sStCamDrift=0;st_camera_finish(eye,at,h,n,3);
    }
    if(chapter==6&&sStAtkEvent==7){int g=Port_StoryCastActor(4);extern void Port_StoryHoldPose(int,int);
     BtlCtrl_SetPos(g,&sStShieldGohanAnchor);Port_StoryHoldPose(g,ST_MOTION_IDLE);st_face(g,sStAtkBy);
     if(!sStAtkContact){st_place_near(sStAtkAt,g,1.8f,0,0);st_scene_ground_actor(sStAtkAt);st_face(sStAtkAt,sStAtkBy);}
     if(sStShieldHitAge>0)sStShieldHitAge++;if(sStShieldHitAge==18)BtlFacade_ShakeCamera(0);if(sStAtkAt==Port_StoryCastActor(3)){st_shield_reaction(sStAtkAt);st_wardrobe_tick();} /* sampled directly: the action path remaps 284 */ /* Juan 2026-11-18: arms spread to take the blast (own 284, slowed by Port_StoryAnimRate) */
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
        enum { cut=ST_CH9_KIENZAN_CUT, impact=cut+ST_APE_FALL_FRAMES, revert=impact+ST_CH9_APE_IMPACT_HOLD };
        if(f<cut)st_ch9_kienzan_cut(f,0);
        if (f == cut) {
            extern void Port_StoryNativeDiscEnd(void);
            Port_StoryNativeDiscEnd();
            st_ch9_tail_blood(sStAtkAt);
            Port_StoryImpactSound(sStAtkBy,0xF);
            sStTailCut|=1u<<sStAtkAt;
            st_presentation_queue(sStAtkAt,2u);
            Port_Dbg3("story ape tail cut", chapter, sStAtkBy, sStAtkAt, f);
        }
        if(f>=cut&&f<revert)st_ch9_tail_fall(f-cut,1);
        if (f == impact) {
            st_presentation_queue(sStAtkBy, 2u | 4u);
            Port_Dbg3("story ape falls on vegeta", chapter, sStAtkBy, sStAtkAt, f);
        }
        if(f==cut+ST_CH9_CRUSH_SETTLE){
            if(sStAtkBy>=0&&sStAtkBy<32)sStDownActors|=1u<<sStAtkBy;
            Port_Dbg3("story CH9 Vegeta crush settled",chapter,sStAtkBy,f,0);
        }
        if(f==revert-2)Fade_Start(1,0,2.f/30.f);
        if (f == revert && ev->transformTo > 0 && ev->transformCast >= 0) {
            s32 who = Port_StoryCastActor(ev->transformCast);
            sStAttackPlacement=0;
            if (who >= 0) { BtlCtrl_StopMotion(who);Port_StoryGrantForm(who, ev->transformTo); sStFormActor = who; sStFormTarget = ev->transformTo; sStFormWait = 0; st_transform_ready(who, ev->transformTo); }
            else Fade_Start(1,1,5.f/30.f);
        }
        if (f >= revert+8) st_attack_done(chapter, ev);
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
        /* CH1 blast: the CPU Raditz keeps fighting after the real hit, so "both interruptible" may never coincide;
           after a REAL contact the aftermath line starts within 2.5 seconds anyway. */
        {static int contactAt;if(!sStAtkContact||f<=1)contactAt=0;else if(!contactAt){contactAt=f;Port_Dbg3("story attack contact",chapter,sStAtkEvent,sStAtkBy,sStAtkAt);}
         if(chapter==0&&sStAtkContact&&contactAt&&f-contactAt>75)landed=1;
         /* Raditz shrugs it off: the aftermath line waits until Piccolo's blast is over (Piccolo free again, then 25
            frames for the smoke), whether or not the explosion reported a hit; bounded at 10 s. */
         {static int beamOver;if(f<=1)beamOver=0;if(chapter==0){if(!beamOver&&f>30&&BtlCtrl_IsInterruptible(sStAtkBy))beamOver=f;
          if((beamOver&&f-beamOver>=25)||f>300)landed=1;}}}
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
    if(chapter==6&&sStEventPost&&sStEventRun==7&&!sStEventAlt){int p=Port_StoryCastActor(3);extern void Port_StoryCaptureStillBodyPose(void*,int);st_shield_post_pose(p);Port_StoryCaptureStillBodyPose(BtlObj_Get(st_obj(p)),p);BtlCharApi_GetPos(st_obj(p),&sStStillPos[p]);BtlCharApi_GetRot(st_obj(p),&sStStillRot[p]);sStStillActors|=1u<<p;sStEventRun=-1;sStEventPost=0;st_scene_end();st_event_start(6,8);st_event_finish(6);Port_StorySubtitle(6,ST_SET_EVENT0+8,1);Port_Dbg3("story shield joins immediate Masenko",6,7,8,0);return;}
    if(sStEventPost){
        sStEventRun=-1;sStEventPost=0;st_scene_end();
        Battle_GetWork()->flags&=~BATTLE_FLAG_DEMO;Hud_SlideIn(.5f);if(ev->canon)BattleResult_Set(1,1);return;
    }
    if(chapter==2&&sStEventRun==2&&sStTailActive){
      /* One continuous cutscene: keep Raditz above Goku into the child's
         charge/headbutt, rather than a second R3 and another free fight. */
      st_scene_end();sStTailActive=1;sStTailToGohan=1;sStTailIntro=0;
      st_event_start(chapter,0);st_tail_frame();return;
    }
    if(sStTailActive){int a;for(a=0;a<2;a++){Vec4 rot=sStTailBaseRot;int actor=a?sStTailR:sStTailG;rot.x=rot.z=0;BtlCtrl_SetRot(actor,&rot);st_scene_ground_actor(actor);}sStTailActive=sStTailToGohan=sStTailIntro=0;}
    sStEventRun = -1;
    if (ev->kind == ST_EVENT_GOHAN && !sStEventHit) { /* skipped before the hit: the hit still counts */
        sStEventHit = 1;
        st_raditz_headbutt_outfit(rival);
        st_set_hp(rival, st_hp(rival) > ST_GOHAN_DAMAGE ? st_hp(rival) - ST_GOHAN_DAMAGE : 1);
    }
    /* Transfer pair ownership BEFORE dialogue teardown. Otherwise teardown
       queues idle on both actors, undoing the grip at Piccolo's fire cue. */
    if(ev->kind==ST_EVENT_BEAM&&sStHoldPair&&sStBeamLead>=0&&sStBeamRival>=0){sStBeamFrames=1;sStBeamLanded=sStBeamBlood=0;}
    st_scene_end(); /* the kid goes back to parking */
    Battle_GetWork()->flags &= ~BATTLE_FLAG_DEMO;
    if(ev->kind!=ST_EVENT_MOON)Hud_SlideIn(0.5f); /* moon/tail/reversion stays cinematic through the ending */
    if (ev->transformTo > 0 && ev->transformCast >= 0 && ev->transformCast < stCasts[chapter].count &&
        !(ev->choreo == ST_CHOREO_TAILCUT && ev->attackCast > 0 && !sStEventAlt) && /* (a tail cut: when the stroke lands) */
        !(chapter == 11 && ev == &stEvents[11].event[13] && !sStEventAlt)) { /* (Great Ape Gohan: after he falls on Vegeta) */
        s32 who = Port_StoryCastActor(ev->transformCast); /* the native form change, granted for this fight */
        if (who >= 0) {
            Port_StoryGrantForm(who, ev->transformTo);
            sStFormActor=who;sStFormTarget=ev->transformTo;sStFormWait=0;st_transform_ready(who, ev->transformTo);
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
        sStBeamLanded = sStBeamBlood = 0;
        if (ev->switchCast >= 0 && ev->switchCast < stCasts[chapter].count) { /* he changes sides for this attack */
            s32 who = Port_StoryCastActor(ev->switchCast);
            sStSwitchTeam = Port_Team(sStBeamRival);
            sStSwitchActor = who;
        }
        if (sStBeamPartner >= 0) {
            /* Keep the paired grip through the projectile flight, not just dialogue. */
            if(sStHoldPair){
                extern void Port_StoryHoldPose(int,int);
                Port_StoryHoldPose(sStBeamLead,ST_CLIP_HOLD_BY);
                Port_StoryHoldPose(sStBeamRival,ST_CLIP_HOLD_AT);
                {Vec4 l,r,c;float dx,dz,co=cosf(sStBeamRot.y),si=sinf(sStBeamRot.y);
                 BtlCharApi_GetNodePos(st_obj(sStBeamLead),0x15,&l);BtlCharApi_GetNodePos(st_obj(sStBeamLead),0x23,&r);BtlCharApi_GetNodePos(st_obj(sStBeamRival),17,&c);
                 dx=(l.x+r.x)*.5f-c.x;dz=(l.z+r.z)*.5f-c.z;
                 sStGripWristOffset.x=co*dx-si*dz;sStGripWristOffset.y=(l.y+r.y)*.5f-c.y;sStGripWristOffset.z=si*dx+co*dz;sStGripWristOffset.w=0;sStGripWristReady=1;
                 Port_Dbg3("CH3 accepted grip wrist offset x/y/z Q1000",(int)(sStGripWristOffset.x*1000),(int)(sStGripWristOffset.y*1000),(int)(sStGripWristOffset.z*1000),0);}
            }else{
                BtlCtrl_PlayMotion(sStBeamLead,ST_MOTION_IDLE,1,0.0f);
                BtlCtrl_PlayMotion(sStBeamRival,ST_MOTION_SURPRISED,1,0.0f);
            }
            Port_StoryUseTechnique(sStBeamPartner, 4);
        }
    }
}

/* The beam is on its way: Goku keeps his grip; when it lands (or at the latest after ST_BEAM_FRAMES) both fall. */
static void st_beam_update(void) {
    s32 hp = st_hp(sStBeamRival);
    sStBeamFrames++;
    {extern int Port_StoryNativeMovieActive(int);
     if(!sStBeamMovie&&!sStBeamLanded&&sStBeamPartner>=0&&Port_StoryNativeMovieActive(sStBeamPartner)){
       sStBeamMovie=1;sStBeamMovieFrames=0;sStPierceShot=0;
     }}
    if(sStBeamMovie){extern int Port_StorySbcContact(int,int);extern void Port_StoryBloodImpact(int,float),Port_StoryPiercingBlood(int,int);
      int beat=!(sStBeamBlood&1)?1:!(sStBeamBlood&2)?2:0;
      if(beat&&Port_StorySbcContact(sStBeamRival,beat==1?168:297)){
       sStBeamBlood|=beat;sStPierceShot=1;
       Port_StoryBloodImpact(sStBeamLead,1.15f);Port_StoryBloodImpact(sStBeamRival,1.15f);
       Port_StoryPiercingBlood(sStBeamLead,sStBeamPartner);Port_StoryPiercingBlood(sStBeamRival,sStBeamPartner);
       Port_Dbg3("story SBC mouth and wound contact",sStBeamLead,sStBeamRival,sStBeamMovieFrames,beat);
      }

    }
    if(sStPierceShot>0&&sStPierceShot<30){sStPierceShot++;if(sStPierceShot==8){extern void Port_StoryBloodImpact(int,float);Port_StoryBloodImpact(sStBeamLead,.9f);Port_StoryBloodImpact(sStBeamRival,.9f);}}
    if(sStBeamMovie){
        /* Native movie owns the victim and helper until its sequence finishes.
           Do not replace a 476-frame authored movie with our 150-frame fallback. */
        extern int Port_StoryNativeMovieActive(int);
        int paired=Port_StoryNativeMovieActive(sStBeamPartner)||Port_SequencePartner(sStBeamPartner)>=0||Port_SequencePartner(sStBeamRival)>=0;
        if(paired&&++sStBeamMovieFrames<900)return;
        sStBeamMovie=0;
        if(sStBeamBlood){int i;int actors[2];actors[0]=sStBeamLead;actors[1]=sStBeamRival;
          sStHoldPair=0;sStCh3Landed=1;sStBeamFrames=0;
          for(i=0;i<2;i++){int a=actors[i];st_set_hp(a,0);sStDownActors|=1u<<a;st_scene_ground_actor(a);Port_StorySampleMotion(a,i?186:0xD5,10000);}
          st_ch3_ending_arrange();
          BattleResult_Set(1,1);return; /* one canonical arrangement, before the KO view and epilogue */
        }
    }
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
    /* Blood is contact-driven: a timeout may finish the story but must not
       pretend that the beam pierced either actor. Each victim is emitted once. */
    if(!sStBeamBlood&&(hp<sStBeamHp||st_hp(sStBeamLead)<=0)){
        sStBeamBlood=1;
        extern void Port_StoryBloodImpact(int actor,float scale);
        Port_StoryBloodImpact(sStBeamLead,1.15f);
        Port_StoryBloodImpact(sStBeamRival,1.15f);
    }
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

/* CH4 moon shot (Juan 2026-11-16): the sky moon of this stage, measured in an owned camera sweep: yaw ~2.4 deg, elevation
   ~9 deg (Y is negative up). The lens rides behind the beam head toward it, a white flash when it is gone, then the tail
   cut in slow motion close on the tail. */
static const float sStMoonDir[3]={-.14289f,-.207912f,.967654f}; /* Juan: one degree farther left; yaw -8.4, elevation12. */
/* A node of the actor's VISIBLE model (a transformed actor draws another object). */
static void st_vnode(int a,int node,Vec4*out){extern int Port_StoryVisualObjectId(int,int);BtlCharApi_GetNodePos(Port_StoryVisualObjectId(a,st_obj(a)),node,out);}
static void st_tail_base(int actor,Vec4*out){
 extern int Port_StoryTailBase(int,int,Vec4*),Port_StoryVisualObjectId(int,int);
 if(!Port_StoryTailBase(Port_StoryVisualObjectId(actor,st_obj(actor)),4,out))st_vnode(actor,4,out);
}
static Vec4 sStMoonCameraEye,sStMoonCameraRot;
static void st_look(Vec4 eye,Vec4 at){Vec4 rot={0,0,0,1};float dx=at.x-eye.x,dz=at.z-eye.z;rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));if(st_chapter()==3||(sStRun.active&&sStRun.chapter==3)){int a=sStMoonFrames>0?sStMoonShooter:Port_StoryCastActor(1);if(a>=0){sStCamDrift=0;st_camera_finish(eye,at,st_height(a),a,3);eye=sStRun.exitEye;rot=sStRun.exitRot;}if(sStMoonFrames>0||(sStRun.active&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u))){sStMoonCameraEye=eye;sStMoonCameraRot=rot;sStMoonCameraValid=1;}}BtlFacade_SetFixedCamera(&eye,&rot);}
/* A front three-quarter shot of one actor: distance, eye and look heights in body heights. */
static void st_front_cam(int a,float d,float eh,float ah,float side){Vec4 p,eye,at,fc={0,0,0,1};float h=st_height(a),y;
 BtlCharApi_GetPos(st_obj(a),&p);BtlCharApi_GetRot(st_obj(a),&fc);y=fc.y+side;eye=p;at=p;eye.x+=sinf(y)*h*d;eye.z+=cosf(y)*h*d;eye.y-=h*eh;at.y-=h*ah;st_look(eye,at);}
static void st_moon_reveal(int a){Vec4 eye,at;float h=st_height(a);BtlCharApi_GetPos(st_obj(a),&eye);eye.y-=h*.9f+200.f;at=eye; /* clear the foreground tree: reveal the whole moon */
 at.x+=sStMoonDir[0]*5000.f;at.y+=sStMoonDir[1]*5000.f;at.z+=sStMoonDir[2]*5000.f;st_look(eye,at);}
static Vec4 sStMoonHead;static int sStMoonHeadAt,sStMoonFlashAt,sStMoonChaseEnd;
extern void Fade_Start(s32 idx,s32 dir,f32 seconds);
void Port_StoryBeamHead(s32 obj,const float*p){if(st_chapter()==3&&sStMoonFrames>0&&!sStMoonFlashAt&&sStMoonShooter>=0&&obj==st_obj(sStMoonShooter)){sStMoonHead.x=p[0];sStMoonHead.y=p[1];sStMoonHead.z=p[2];sStMoonHead.w=1;sStMoonHeadAt=sStMoonFrames;}}
/* Slow motion of the tail cut: both actors run their animation at this rate (btl_char_mgr.c, after the per-frame reset). */
float Port_StoryAnimRate(int side){if(st_chapter()==3&&sStMoonFrames>0&&sStMoonTailAt&&sStMoonFrames<sStMoonTailAt+92&&(side==sStMoonShooter||side==sStMoonTarget))return .35f;return 0;}
static void st_moon_update(void) {
    s32 f=++sStMoonFrames;
    if(f%30==0){extern int Port_StoryModelScale(int);Port_Dbg3("CH4 scale audit: phase/scaleQ10000/heightQ100/frame",sStMoonRev,Port_StoryModelScale(st_obj(sStMoonTarget)),(int)(st_height(sStMoonTarget)*100),f);}
    if(f<=2){sStMoonCameraValid=0;sStMoonShotPlaced=0;sStMoonFallValid=0;sStMoonRev=0;sStMoonHeadAt=sStMoonFlashAt=sStMoonChaseEnd=0;}
    if(f==2){
        Vec4 rot={0,0,0,1};
        st_reset_attack_origin(sStMoonTarget);st_reset_attack_origin(sStMoonShooter);
        BtlCharApi_GetPos(st_obj(sStMoonTarget),&sStMoonApeRoot);
        BtlCtrl_PlayMotion(sStMoonTarget,ST_MOTION_SURPRISED,1,.15f);
        st_stage_actor(sStMoonShooter);BtlCharApi_GetRot(st_obj(sStMoonShooter),&rot);rot.y=atan2f(sStMoonDir[0],sStMoonDir[2]);BtlCtrl_SetRot(sStMoonShooter,&rot);
        {Vec4 up;BtlCharApi_GetPos(st_obj(sStMoonShooter),&up);up.y-=st_height(sStMoonShooter)*1.6f;BtlCtrl_SetPos(sStMoonShooter,&up);} /* airborne, clear of the ground */
        Port_StoryCommitSampledRoot(sStMoonShooter);BtlCharApi_GetPos(st_obj(sStMoonShooter),&sStMoonShotRoot);sStMoonShotRot=rot;sStMoonShotPlaced=1;
        BtlCtrl_StopMotion(sStMoonShooter);
        if(!Port_StoryUseTechnique(sStMoonShooter,2)){sStMoonFrames=0;sStMoonShotPlaced=0;Port_Dbg3("story moon dispatch failed",sStMoonShooter,2,0,0);BattleResult_Set(2,1);return;}
    }
    if(f>=2&&!sStMoonFlashAt&&!sStMoonChaseEnd){ /* low beside him with the moon in frame, then riding behind the beam head toward it */
        Vec4 o,eye,at;float h=st_height(sStMoonShooter),fx=sStMoonDir[0],fz=sStMoonDir[2],fl=sqrtf(fx*fx+fz*fz),dist=0;
        BtlCharApi_GetPos(st_obj(sStMoonShooter),&o);fx/=fl;fz/=fl;
        if(sStMoonHeadAt&&f-sStMoonHeadAt<3){float dx=sStMoonHead.x-o.x,dy=sStMoonHead.y-o.y,dz=sStMoonHead.z-o.z;dist=sqrtf(dx*dx+dy*dy+dz*dz);}
        if(dist>h*2.f){float bk=h*(5.f+dist/h*.12f);eye=sStMoonHead;eye.x-=sStMoonDir[0]*bk;eye.z-=sStMoonDir[2]*bk;eye.y-=sStMoonDir[1]*bk+h*.7f;} /* close behind the beam axis: little parallax, the head meets the moon on screen */
        else{eye=o;eye.x+=fz*h*2.4f-fx*h*.4f;eye.z+=-fx*h*2.4f-fz*h*.4f;eye.y-=h*.35f;}
        at=dist>h*2.f?sStMoonHead:o;at.x+=sStMoonDir[0]*h*60.f;at.y+=sStMoonDir[1]*h*60.f;at.z+=sStMoonDir[2]*h*60.f;
        if(dist<h*8.f){Vec4 head;st_vnode(sStMoonShooter,0x2E,&head);at=head;at.y+=h*.13f;eye=at;
          eye.x+=(fx*1.3f+fz*.55f)*h;eye.z+=(fz*1.3f-fx*.65f)*h;eye.x+=fz*h*.1f;eye.y-=h*.08f;}
        st_look(eye,at);
        if(f%10==0)Port_Dbg3("story moon beam head dist/h x10",(int)(dist/h*10),f,sStMoonHeadAt,(int)((sStMoonHead.y-o.y)/h*10));
        if(sStMoonHeadAt&&(dist>h*34.f||f>=ST_MOON_SKY_FRAMES+40))sStMoonChaseEnd=f;
        if(!sStMoonHeadAt&&f>=ST_MOON_SKY_FRAMES+120){Port_Dbg3("story moon projectile missing",sStMoonShooter,f,0,0);sStMoonFrames=0;sStMoonShotPlaced=0;BattleResult_Set(2,1);return;} /* the lens stops; the beam flies on into the moon */
    }
    if(sStMoonChaseEnd&&!sStMoonFlashAt&&f>=sStMoonChaseEnd+18){sStMoonFlashAt=f;Fade_Start(1,0,.1f);st_presentation_queue(sStMoonShooter,2u);
    }
    if(sStMoonFlashAt&&f==sStMoonFlashAt+8)Fade_Start(1,1,.7f);
    if(sStMoonFlashAt&&!sStMoonTailAt&&f>=sStMoonFlashAt+8&&BtlCtrl_IsInterruptible(sStMoonShooter)){
        sStMoonTailAt=f;
        BtlCtrl_StopMotion(sStMoonShooter);st_stage_actor(sStMoonTarget);
        BtlCtrl_PlayMotion(sStMoonShooter,ST_ANIM_SWORD,0,.1f);
        {Vec4 t,eye,rot={0,0,0,1},fc={0,0,0,1};float h=st_height(sStMoonTarget),bx,bz,dx,dz; /* slow-motion close-up on the tail */
         BtlCharApi_GetPos(st_obj(sStMoonTarget),&t);BtlCharApi_GetRot(st_obj(sStMoonTarget),&fc);bx=-sinf(fc.y);bz=-cosf(fc.y);
         t.x+=bx*h*.3f;t.z+=bz*h*.3f;t.y-=h*.38f;eye=t;eye.x+=bx*h*.75f-bz*h*.6f;eye.z+=bz*h*.75f+bx*h*.6f;eye.y-=h*.12f;
         dx=t.x-eye.x;dz=t.z-eye.z;rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-t.y,sqrtf(dx*dx+dz*dz));st_look(eye,t);}
    }
    /* CH4 reversion (Juan 2026-11-16): the cut lands (hit, tail hidden), then the Great Ape's OWN de-transform clips on
       our disc, UNTRANSFORM_IN 399 once and UNTRANSFORM_LOOP 400 held, the form change back to the child, and the child's
       UNTRANSFORM_OUT 401 before he lies down. No power-up transform look. */
    if(sStMoonTailAt&&f<sStMoonTailAt+92&&!sStMoonRev){Vec4 t,P,eye,fc={0,0,0,1};float h=st_height(sStMoonTarget),hp=st_height(sStMoonShooter),dx,dz,d; /* slow motion: over his shoulder onto the tail */
        BtlCharApi_GetPos(st_obj(sStMoonTarget),&t);BtlCharApi_GetRot(st_obj(sStMoonTarget),&fc);BtlCharApi_GetPos(st_obj(sStMoonShooter),&P);
        st_tail_base(sStMoonTarget,&t);dx=P.x-t.x;dz=P.z-t.z;d=sqrtf(dx*dx+dz*dz);if(d<.01f)d=.01f;dx/=d;dz/=d;
        {Vec4 hip;st_vnode(sStMoonTarget,3,&hip);dx=t.x-hip.x;dz=t.z-hip.z;d=sqrtf(dx*dx+dz*dz);if(d<.01f)d=1;dx/=d;dz/=d;eye=t;eye.x+=(dx*2.8f+dz*1.2f)*hp;eye.z+=(dz*2.8f-dx*1.2f)*hp;eye.y-=hp*2.1f;st_look(eye,t);}} /* side-on, low: blade and tail */
    if(sStMoonTailAt&&f==sStMoonTailAt+2){ /* nodes are valid once the staged ape has been posed */
        {Vec4 tn,pp,fc={0,0,0,1},rr={0,0,0,1};float hp=st_height(sStMoonShooter); /* beside the tail itself (node 7), at its height, facing it */
         st_tail_base(sStMoonTarget,&tn);BtlCharApi_GetRot(st_obj(sStMoonTarget),&fc);pp=tn; /* tail root (node 6), a little out along the tail */
         pp.x+=cosf(fc.y)*hp*.9f;pp.z-=sinf(fc.y)*hp*.9f;pp.y+=hp*.35f;BtlCtrl_SetPos(sStMoonShooter,&pp);
         rr.y=atan2f(tn.x-pp.x,tn.z-pp.z);BtlCtrl_SetRot(sStMoonShooter,&rr);
         Port_Dbg3("story tail node / shooter x10",(int)(tn.x*10),(int)(tn.y*10),(int)(tn.z*10),(int)(hp*10));}
    }
    if(sStMoonTailAt&&(f==sStMoonTailAt+34||f==sStMoonTailAt+40||f==sStMoonTailAt+46)){ /* the stump keeps spurting */
        extern void Port_Fx_WorldWoundSpit(const Vec4*,const Vec4*,int);extern float BtlCharApi_GetGroundY(s32);Vec4 t,d={0,-.5f,0,0},fc={0,0,0,1};float h=st_height(sStMoonTarget);
        st_tail_base(sStMoonTarget,&t);BtlCharApi_GetRot(st_obj(sStMoonTarget),&fc);d.x=-sinf(fc.y+(f&1?.6f:-.6f));d.z=-cosf(fc.y+(f&1?.6f:-.6f));d.w=BtlCharApi_GetGroundY(st_obj(sStMoonTarget));
        Port_Fx_WorldWoundSpit(&t,&d,(int)(h*120.f));}
    if(sStMoonTailAt&&f==sStMoonTailAt+20){
        Port_StoryImpactSound(sStMoonShooter,ST_SE_STRIKE); /* land the audible contact at Piccolo, not the distant giant root */
        Port_Dbg3("story CH4 tail contact sound",sStMoonShooter,ST_SE_STRIKE,f,0);
        st_presentation_queue(sStMoonTarget,4u);sStTailCut|=1u<<sStMoonTarget;{int child=Port_StoryCastActor(2);if(child>=0)sStTailCut|=1u<<child;}
        {extern void Port_Fx_WorldWoundSpit(const Vec4*,const Vec4*,int);extern float BtlCharApi_GetGroundY(s32);Vec4 t,d={0,0,0,0},fc={0,0,0,1};float h=st_height(sStMoonTarget);
         BtlCharApi_GetPos(st_obj(sStMoonTarget),&t);BtlCharApi_GetRot(st_obj(sStMoonTarget),&fc);d.x=-sinf(fc.y);d.z=-cosf(fc.y);d.y=-.35f;
         st_tail_base(sStMoonTarget,&t);d.w=BtlCharApi_GetGroundY(st_obj(sStMoonTarget));
         Port_Fx_WorldWoundSpit(&t,&d,(int)(h*150.f));d.y=-.8f;Port_Fx_WorldWoundSpit(&t,&d,(int)(h*110.f));d.x=-d.x;d.z=-d.z;d.y=-.35f;Port_Fx_WorldWoundSpit(&t,&d,(int)(h*90.f));} /* the cut tail bleeds */
        {Vec4 rot;float h=st_height(sStMoonTarget);BtlCharApi_GetPos(st_obj(sStMoonShooter),&sStMoonRetreatFrom);BtlCharApi_GetRot(st_obj(sStMoonTarget),&rot);sStMoonRetreatTo=sStMoonRetreatFrom;sStMoonRetreatTo.x-=sinf(rot.y)*h*.85f;sStMoonRetreatTo.z-=cosf(rot.y)*h*.85f;sStMoonRetreatTo.y=BtlCharApi_GetGroundY(st_obj(sStMoonShooter));}
        BtlCtrl_PlayMotion(sStMoonTarget,ST_ANIM_HIT,0,.05f);sStMoonRev=1;sStMoonRevAt=f;
    }

    if(sStMoonRev==1){int child=Port_StoryCastActor(2);extern void Port_StoryCh4Outfit(int);extern int Port_StoryCh4OutfitReady(int);
      Port_StoryCh4Outfit(child);
      if(f>=sStMoonRevAt+18&&Port_StoryCh4OutfitReady(child)){
        BtlCtrl_StopMotion(sStMoonTarget);BtlCharApi_GetPos(st_obj(sStMoonTarget),&sStMoonFallFloor);
        BtlCharApi_GetRot(st_obj(sStMoonTarget),&sStMoonFallRot);sStMoonFallRot.x=sStMoonFallRot.z=0;
        sStMoonRev=2;sStMoonRevAt=f;
      }
    }
    if(sStMoonRev==2){int n=f-sStMoonRevAt;float h=st_height(sStMoonTarget);Vec4 at,eye,fc=sStMoonFallRot;
      /* Native prone fall sampled at half speed, with a continuous forward collapse. */
      {Vec4 p,r;st_sample_ape_fall(sStMoonTarget,n,&sStMoonFallFloor,&sStMoonFallRot,&p,&r);}
      st_vnode(sStMoonTarget,0x10,&at);eye=at;eye.x+=cosf(fc.y)*h*.95f;eye.z-=sinf(fc.y)*h*.95f;eye.y-=h*.65f;st_look(eye,at);
      if(n==37)Fade_Start(1,0,2.f/30.f); /* brief native white flash into the form swap */
      if(n>=40){int child=Port_StoryCastActor(2);void*o=(void*)BtlObj_Get(st_obj(sStMoonTarget));
        Fade_Start(1,1,5.f/30.f);
        st_vnode(sStMoonTarget,0x10,&at);if(o)BtlObj_SetPartVisible(o,0,0);
        sStGoneActors|=1u<<sStMoonTarget;sStRun.actors&=~(1u<<sStMoonTarget);Port_StoryRemoveActor(sStMoonTarget);
        st_scene_take(child,1);st_stage_actor(child);sStDownActors|=1u<<child;
        sStMoonFallFloor.x=at.x;sStMoonFallFloor.z=at.z;sStMoonFallFloor.w=1;
        {Vec4 probe=at;StgGroundHit hit;int zone;probe.y-=h*12.f+100.f;hit.y=3.4e38f;
         zone=BtlStage_ProbeGround(-1,&probe,&hit);if(zone>=0&&hit.y>-1e9f&&hit.y<1e9f)sStMoonFallFloor.y=hit.y;
         else sStMoonFallFloor.y=sStMoonApeRoot.y;}
        Port_Dbg3("story CH4 child landing floor",child,(int)(sStMoonFallFloor.x*10),(int)(sStMoonFallFloor.y*10),(int)(sStMoonFallFloor.z*10));
        sStMoonFallValid=1;BtlCtrl_SetRot(child,&sStMoonFallRot);
        sStMoonRev=3;sStMoonRevAt=f;
      }
    }
    if(sStMoonRev==3){int child=Port_StoryCastActor(2),n=f-sStMoonRevAt;float t=n/48.f,h=st_height(child);Vec4 p=sStMoonFallFloor,eye,at;
      if(t>1)t=1;p.y-=h*1.5f*(1-t*t);BtlCtrl_SetPos(child,&p);BtlCtrl_SetRot(child,&sStMoonFallRot);
      Port_StorySampleMotionQ8(child,199,n*128);
      at=p;at.y-=h*.25f;eye=at;eye.x+=cosf(sStMoonFallRot.y)*h*2.2f;eye.z-=sinf(sStMoonFallRot.y)*h*2.2f;eye.y-=h*.7f;st_look(eye,at);
      if(n>=48){BtlCtrl_SetPos(child,&sStMoonFallFloor);Port_StorySampleMotion(child,205,0);st_presentation_queue(child,2u|4u);sStMoonRev=4;sStMoonRevAt=f;}
    }
    if(sStMoonRev==4){int child=Port_StoryCastActor(2),n=f-sStMoonRevAt;Port_StorySampleMotionQ8(child,205,n*192);BtlCtrl_SetPos(child,&sStMoonFallFloor);if(n>=36){sStMoonRev=5;sStMoonRevAt=f;Port_StorySampleMotion(child,186,10000);}}
    if(sStMoonRev==5&&f>=sStMoonRevAt+30){sStMoonFrames=0;BattleResult_Set(1,1);return;}
}

s32 Port_StorySkyAim(s32 obj,float*out){
    if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&obj==st_obj(Port_StoryCastActor(9))){
     Vec4 a,b;float d,x,y,z;int by=Port_StoryCastActor(9),at=Port_StoryCastActor(8);
     BtlCharApi_GetPos(st_obj(by),&a);BtlCharApi_GetPos(st_obj(at),&b);a.y-=st_height(by)*.6f;b.y-=st_height(at)*.6f;
     x=b.x-a.x;y=b.y-a.y;z=b.z-a.z;d=sqrtf(x*x+y*y+z*z);if(d>.01f){out[0]=x/d;out[1]=y/d;out[2]=z/d;out[3]=0;return 1;}
    }

    if(sStMoonFrames>0&&!sStMoonTailAt&&sStMoonShooter>=0&&obj==st_obj(sStMoonShooter)&&st_chapter()==3){out[0]=sStMoonDir[0];out[1]=sStMoonDir[1];out[2]=sStMoonDir[2];out[3]=0;return 1;}
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

/* Native ultimate owns the complete flight, rebound and paired victim track. */
int Port_StorySpiritBallVelocity(int objId, float *pos, float *vel, float *dir) {
    (void)objId; (void)pos; (void)vel; (void)dir;
    return 0;
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
 /* CH9 plays the ultimate as a scene: authored consequences follow once.
    Native contact/camera/animation still run, but combat scaling cannot KO it. */
 if(flags!=0x42B&&sStAtkFrames>0&&Port_StoryGenkiCast(sStAtkBy)>=0&&
    (actor==sStAtkBy||actor==sStAtkAt||actor==sStSbGohan))return 1;

 {static int inv=-1;extern char*getenv(const char*);if(inv<0)inv=getenv("BT3_STORY_INVULN_PROBE")!=0; /* test fixture only: heroes take no damage */
  if(inv&&st_chapter()>=0&&actor>=0&&actor<ST_ACTORS&&flags!=0x42B&&Port_Team(actor)==0&&!(sStAtkFrames>0&&actor==sStAtkAt))return 1;}
 /* CH1 blast: Raditz is untouched by it (no health loss, so no torn outfit either) */
 if(st_chapter()==0&&sStAtkFrames>0&&actor==sStAtkAt&&flags!=0x42B)return 1;
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
#define ST_TAIL_NODE_LO 4 /* decoded model section3: tail chain4->5->6->7; node4 is its proximal attachment */
#define ST_TAIL_NODE_HI 7
int Port_StoryNodeHidden(void*obj,int node){
 static int lo=-2,hi=-1,all=0;int a;
 if(lo==-2){extern char*getenv(const char*);const char*e=getenv("BT3_STORY_TAIL_NODES");
  lo=ST_TAIL_NODE_LO;hi=ST_TAIL_NODE_HI;all=getenv("BT3_STORY_TAIL_ALL")!=0;
  if(e){int v=0,which=0;lo=hi=0;for(;*e;e++){if(*e>='0'&&*e<='9')v=v*10+(*e-'0');else if(*e=='-'&&!which){lo=v;v=0;which=1;}}if(which)hi=v;else lo=hi=-1;}}
 if(node<lo||node>hi||st_chapter()<0||!obj)return 0;
 if(ST_GohanTailAbsent(st_chapter())){for(a=0;a<stCasts[st_chapter()].count;a++){int actor=Port_StoryCastActor(a);if(actor>=0&&BtlCharApi_GetChara(st_obj(actor))==13){extern int Port_StoryVisualObjectId(int,int);if((void*)BtlObj_Get(st_obj(actor))==obj||(void*)BtlObj_Get(Port_StoryVisualObjectId(actor,st_obj(actor)))==obj)return 1;}}}
 if(!sStTailCut&&!all)return 0;
 for(a=0;a<ST_ACTORS;a++)if((all||(sStTailCut>>a&1u))&&Port_FighterObjId(a)>=0){extern int Port_StoryVisualObjectId(int,int);int id=Port_FighterObjId(a);if((void*)BtlObj_Get(id)==obj||(void*)BtlObj_Get(Port_StoryVisualObjectId(a,id))==obj)return 1;}
 return 0;
}

s32 Port_StoryRetired(s32 actor){return st_chapter()>=0&&actor>=0&&actor<32&&(sStGoneActors>>actor&1u);}

s32 Port_StoryHeld(s32 actor) {
 if(st_chapter()==2&&sStCh3Landed&&(actor==sStBeamLead||actor==sStBeamRival))return 1;
    if(st_chapter()==2&&sStBeamMovie&&actor==sStBeamRival)return 0;
    if(Port_StoryStillPoseActive(actor))return 1;
    if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==Port_StoryCastActor(9))return 1;
    if(actor==sStSbThrower&&Port_StorySpiritBallPending(actor))return 1;
    if(Port_StoryYamchaCorpseActive()&&actor==Port_StoryCastActor(1))return 1;
    if(st_chapter()==3&&actor>=0&&actor<32&&(sStDownActors>>actor&1u))return 1;
    if(st_chapter()==11&&actor>=0&&actor<32&&(sStDownActors>>actor&1u)&&actor==Port_StoryCastActor(4))return 1; /* Vegeta under the fallen giant: he can't get up */
    if(sStRun.active&&actor>=0&&actor<32&&(sStRun.actors>>actor&1u))return 1;
    if(sStAtkFrames>0&&actor>=0&&actor!=sStAtkBy&&actor!=sStAtkAt&&actor!=sStAtkBack&&!(sStSbStage==3&&actor==sStSbGohan))return 1;
    if(sStAtkFrames>0&&sStAtkEvent>=0&&st_chapter()>=0&&stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_CLASH&&(actor==sStAtkBy||actor==sStAtkAt))return 1;
    if (sStMoonFrames > 0 && actor >= 0 && actor == sStMoonTarget && st_chapter() >= 0) {
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
/* Standard beat GROUNDED BODY (STORY-CUTSCENE-PLAYBOOK.md section 12): a defeated actor an ending scene owns. Native
   placement keeps the display offset a launch or rush left on the pose, which draws the body in the air above a
   correct root; BtlCtrl_SetPos clears that offset for these actors (CH7 already did, for its own chapter). */
/* Any actor a running story scene places: its placement is where it is DRAWN. Native placement keeps the display
   offset a flight or launch left on the pose, which draws the fighter up in the sky above a correct root (CH1/CH3). */
int Port_StorySceneOwns(int actor){return sStRun.active&&sStRun.chapter>=0&&actor>=0&&actor<32&&(sStRun.actors>>actor&1u);}
int Port_StoryGroundedBody(int actor){
 if(Port_StoryYamchaCorpseActive()&&actor==Port_StoryCastActor(1))return 1;
 if(Port_StoryStillPoseActive(actor))return 1;
 return st_chapter()>=0&&actor>=0&&actor<32&&st_down(actor);
}
static int st_static_attack_placement(int actor){
 if(st_chapter()<0||sStAtkFrames<=0||sStAtkContact||sStAtkFree||!sStAttackPlacement)return -1;
 if(actor==sStAtkAt&&(sStAttackPlacement&1))return 0;
 if(actor==sStAtkBy&&(sStAttackPlacement&2))return 1;
 return -1;
}
/* The paired CH3 hold is a staged two-body shot, like the static authored
   attacks below. Both actors own their origin until this dialogue releases it. */
static int st_hold_pair_placement(int actor){
 if((sStBeamMovie&&actor==sStBeamRival)||!sStHoldPair||st_chapter()!=2||actor<0||(actor!=sStBeamLead&&actor!=sStBeamRival))return 0;
 /* st_event_finish clears eventRun before the projectile starts. */
 if(sStBeamFrames>0&&!sStBeamLanded)return 1;
 return !sStEventPost&&sStRun.active&&sStRun.chapter==2&&sStEventRun>=0&&
  sStEventRun<stEvents[2].count&&stEvents[2].event[sStEventRun].kind==ST_EVENT_BEAM;
}
static int st_shield_spread(int actor){return st_chapter()==6&&actor==sStRun.cast[3]&&!sStShieldPostReady&&((sStAtkEvent==7&&sStAtkFrames>0)||(sStEventRun==7&&sStKienDone&&!sStEventPost));} /* Juan 2026-11-18: arms spread from the warning through the beam */
int Port_StoryScenePlacementOwned(int actor){
 if(st_ch9_pod_scene()&&actor==sStRun.rival)return 1;
 if(st_chapter()==3&&sStMoonFrames>0&&sStMoonShotPlaced&&
    ((actor==sStMoonShooter&&!sStMoonTailAt)||(actor==sStMoonTarget&&sStMoonRev<2)))return 1;
 if(sStSv>=0&&actor==sStSv)return 1; /* TEMP survey */
 if(st_shield_spread(actor))return 1;
 if((sStRescueActive||(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost))&&st_rescue_role(actor)>=0)return 1;
 if(st_shield_post_owned(actor))return 1;
 if(sStKienActive&&st_kien_role(actor)>=0)return 1;
 if(sStAssaultActive&&st_assault_role(actor)>=0)return 1;
 if(st_assault_charge_owns(actor))return 1;
 if(sStElbowGohanReady&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&actor==sStRun.cast[4])return 1;
 if(sStElbowActive&&(actor==sStRun.cast[8]||actor==sStRun.cast[3]||actor==sStRun.cast[2]))return 1;
 if(sStPlantActive&&st_chapter()==6){int i;for(i=0;i<7;i++)if(actor==Port_StoryCastActor(stPlantCast[i]))return 1;}
 if(sStArmBeat&&st_chapter()==6&&(actor==sStRun.cast[0]||actor==sStRun.cast[8]))return 1;
 if(st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==0&&(actor==sStAtkBy||actor==sStAtkAt))return 1;
 if(st_yamcha_mourns(actor))return 1;
 if(st_gohan_recoil_owned(actor))return 1;
 if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost&&(actor==sStChaoBy||actor==sStChaoAt))return 1;
 if(sStTailActive&&(!sStTailToGohan||!sStEventHit||actor==sStTailG)&&(actor==sStTailG||actor==sStTailR)&&st_chapter()==2)return 1;
 if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0&&(actor==sStRun.carrier||actor==sStRun.cast[0]))return 1;
 if(st_hold_pair_placement(actor))return 1;
 if(Port_StoryYamchaCorpseActive()&&actor==Port_StoryCastActor(1))return 1;
 if(Port_StoryStillPoseActive(actor))return 1;
 if(st_static_attack_placement(actor)>=0)return 1;
 return st_chapter()==6&&sStRun.active&&actor>=0&&actor<32&&(sStRun.actors>>actor&1u);
}
/* A scripted KO can freeze before gravity settles it. Own the floor as well
   as the pose; refresh at 2 Hz so persistent corpses do not repeat expensive
   crater collision walks twice per simulation frame. Native movies still own
   airborne victims until their authored interaction has finished. */
static int st_down_floor_placement(int actor){
 static int chapter=-1,last[32],valid[32];static Vec4 anchor[32];Vec4 p;int ch=st_chapter(),i;
 extern int Port_StoryNativeMovieActive(int);
 /* Authored takeoff and fall own their roots until their real contact/end. */
 if(sStRun.active&&sStRun.departing&&(actor==sStRun.rival||(actor>=0&&actor<32&&(sStDepartActors>>actor&1u))))return 0;
 if(ch==3&&sStMoonFrames>0&&((actor==sStMoonTarget&&sStMoonRev>=1)||
    (actor==Port_StoryCastActor(2)&&sStMoonFallValid&&(sStMoonRev==3||sStMoonRev==4))))return 0;
 if(ch<0||actor<0||actor>=32||!BtlStage_IsReady()||!st_down(actor)||Port_StorySceneParked(actor)||(Port_StoryRetired(actor)&&!(sStDownActors>>actor&1u)))return 0;
 /* Zero HP also covers native, revivable KOs. Only pin a body owned by Story. */
 if (!(sStDownActors & (1u << actor)) && !Port_StorySceneOwns(actor) &&
     !Port_StoryStillPoseActive(actor) &&
     !(Port_StoryYamchaCorpseActive() && actor == Port_StoryCastActor(1))) return 0;
 if((sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0&&(actor==sStRun.carrier||actor==sStRun.cast[0]))||Port_StoryNativeMovieActive(actor)||st_hold_pair_placement(actor)||
    (sStAtkFrames>0&&(actor==sStAtkBy||actor==sStAtkAt||actor==sStAtkBack))||
    (sStBeamFrames>0&&!sStBeamLanded&&(actor==sStBeamLead||actor==sStBeamRival)))return 0;
 if(chapter!=ch){chapter=ch;for(i=0;i<32;i++)valid[i]=0;}
 BtlCharApi_GetPos(st_obj(actor),&p);
 if(!valid[actor]||sStFightFrames<last[actor]||sStFightFrames-last[actor]>=15||
    p.x-anchor[actor].x>1.f||anchor[actor].x-p.x>1.f||p.z-anchor[actor].z>1.f||anchor[actor].z-p.z>1.f){
  st_scene_ground_actor(actor);BtlCharApi_GetPos(st_obj(actor),&anchor[actor]);p=anchor[actor];last[actor]=sStFightFrames;valid[actor]=1;
 }
 p.y=anchor[actor].y;BtlCtrl_SetPos(actor,&p);
 {extern void Port_StoryCommitGroundedBody(int);Port_StoryCommitGroundedBody(actor);}
 if(Port_StoryStillPoseActive(actor))sStStillPos[actor]=p;
 return 1;
}
void Port_StoryRestoreEndingPlacement(int actor){
 if(st_ch9_pod_scene()&&actor==sStRun.rival){st_ch9_crawl_pose();return;}
 if(st_chapter()==3&&sStMoonFrames>0&&sStMoonShotPlaced){
  if(actor==sStMoonShooter&&!sStMoonTailAt){Port_StoryResetSceneOffset(actor);BtlCtrl_SetPos(actor,&sStMoonShotRoot);BtlCtrl_SetRot(actor,&sStMoonShotRot);return;}
  if(actor==sStMoonTarget&&sStMoonRev<2){Port_StoryResetSceneOffset(actor);BtlCtrl_SetPos(actor,&sStMoonApeRoot);return;}
 }
 if(st_shield_spread(actor)){st_shield_reaction(actor);return;}
 if(sStSv>=0&&actor==sStSv){extern void Port_StoryCommitSampledRoot(int);BtlCtrl_SetPos(actor,&sStSvPos);BtlCtrl_SetRot(actor,&sStSvRot);{extern char*getenv(const char*);extern int atoi(const char*);const char*bank=getenv("BT3_STORY_SURVEY_BANK"),*spanEnv=getenv("BT3_STORY_SURVEY_SPAN");int span=spanEnv?atoi(spanEnv):12,motion,frame;if(span<12||span>600)span=12;motion=sStSvN?sStSvList[(sStSvK/span)%sStSvN]:sStSvFrom+sStSvK/span;frame=spanEnv?sStSvK%span:6+((sStSvK%12)/4)*8;if(bank)st_ch8_clip(actor,atoi(bank),motion,frame*256);else if(getenv("BT3_STORY_SURVEY_OWN")){extern int Port_StorySampleOwnMotionQ8(int,int,int);Port_StorySampleOwnMotionQ8(actor,motion,frame*256);}else Port_StorySampleMotion(actor,motion,frame);}Port_StoryCommitSampledRoot(actor);return;}
 if((sStRescueActive||(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost))&&st_rescue_role(actor)>=0){st_rescue_pose(actor);return;}
 if(st_shield_post_owned(actor)){st_shield_post_pose(actor);return;}
 if(sStKienActive&&st_kien_role(actor)>=0){st_kien_pose(actor);return;}
 if(sStAssaultActive&&st_assault_role(actor)>=0){st_assault_pose(actor);return;}
 if(st_assault_charge_owns(actor)){BtlCtrl_SetPos(actor,&asRoot[AS_T]);BtlCtrl_SetRot(actor,&asRot[AS_T]);Port_StorySampleMotion(actor,52,41);st_ch5_ground_pose(actor,asRoot[AS_T].y);return;}
 if(sStElbowGohanReady&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&actor==sStRun.cast[4]){BtlCtrl_SetPos(actor,&sStElbowGohanPos);Port_StorySampleMotion(actor,0,0);Port_StoryCommitSampledRoot(actor);st_ch5_ground_pose(actor,sStElbowGohanPos.y);return;}
 if(sStElbowActive&&(actor==sStRun.cast[8]||actor==sStRun.cast[3]||actor==sStRun.cast[2])){st_ch8_elbow_pose(actor);return;}
 if(sStPlantActive&&st_chapter()==6){int i;for(i=0;i<7;i++)if(actor==Port_StoryCastActor(stPlantCast[i])){st_ch8_plant_pose(actor);return;}}
 if(sStArmBeat&&st_chapter()==6&&(actor==sStRun.cast[0]||actor==sStRun.cast[8])){st_ch8_arm_pose(actor);return;}
 if(st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==0&&(actor==sStAtkBy||actor==sStAtkAt)){st_ch8_fireworks_pose(actor);return;}
 if(st_chapter()==2&&sStCh3Landed&&(actor==sStBeamLead||actor==sStBeamRival)){
  if(sStCh3EndingReady){int i=actor==sStBeamLead?0:2;BtlCtrl_SetPos(actor,&sStCh3EndingPos[i]);BtlCtrl_SetRot(actor,&sStCh3EndingRot[i]);}
  Port_StorySampleMotion(actor,actor==sStBeamLead?0xD5:186,10000);st_down_floor_placement(actor);return;
 }

 if(st_chapter()==3&&sStMoonFallValid&&actor==Port_StoryCastActor(2)){
   if(sStMoonFrames>0&&sStMoonRev==3){float t=(sStMoonFrames-sStMoonRevAt)/48.f;Vec4 p=sStMoonFallFloor;if(t>1)t=1;p.y-=st_height(actor)*1.5f*(1-t*t);BtlCtrl_SetPos(actor,&p);return;}
   if(sStMoonFrames>0&&sStMoonRev==4){BtlCtrl_SetPos(actor,&sStMoonFallFloor);return;}
   Port_StorySampleMotion(actor,186,10000);BtlCtrl_SetPos(actor,&sStMoonFallFloor);st_down_floor_placement(actor);return;
 }

 if(st_chapter()==3&&sStMoonTailAt&&actor==sStMoonShooter){int f=sStMoonFrames-sStMoonTailAt;
  if(f>20&&sStMoonRev){float k=(f-20)/18.f;Vec4 p;if(k>1)k=1;k=k*k*(3-2*k);p=sStMoonRetreatFrom;p.x+=(sStMoonRetreatTo.x-p.x)*k;p.y+=(sStMoonRetreatTo.y-p.y)*k;p.z+=(sStMoonRetreatTo.z-p.z)*k;BtlCtrl_SetPos(actor,&p);return;}

  if(f>=12&&f<=20){Vec4 p,hand,t;float h=st_height(actor),k=(f-12)/8.f,dx,dy,dz,d;
   if(k>1)k=1;k=k*k*(3-2*k);st_tail_base(sStMoonTarget,&t);st_vnode(actor,0x23,&hand);BtlCharApi_GetPos(st_obj(actor),&p);
   dx=t.x-hand.x;dy=t.y-hand.y;dz=t.z-hand.z;d=sqrtf(dx*dx+dy*dy+dz*dz);
   if(d>h*2.f){float cap=h*2.f/d;dx*=cap;dy*=cap;dz*=cap;}
   p.x+=dx*k;p.y+=dy*k;p.z+=dz*k;if(st_finite_pos(&p))BtlCtrl_SetPos(actor,&p);return;
  }
 }

 if(st_gohan_recoil_owned(actor)){st_gohan_recoil_place(actor);return;}
 if(st_chapter()==2&&sStRun.active&&sStRun.set==ST_SET_CANON&&actor==sStRun.cast[4]){
  int g=sStRun.cast[0];Vec4 chest,rot,p;float h=st_height(g);
  BtlCharApi_GetNodePos(st_obj(g),3,&chest);BtlCharApi_GetRot(st_obj(g),&rot);
  p=chest;p.x+=cosf(rot.y)*h*.65f;p.z-=sinf(rot.y)*h*.65f;p.y=BtlCharApi_GetGroundY(st_obj(g));p.w=1;
  BtlCtrl_SetPos(actor,&p);rot.x=rot.z=0;rot.y=atan2f(chest.x-p.x,chest.z-p.z);BtlCtrl_SetRot(actor,&rot);
  Port_StorySampleMotion(actor,388,32);st_scene_ground_actor(actor);Port_StoryCommitSampledRoot(actor);st_ch5_ground_pose(actor,p.y);return;
 }

 if(st_chapter()==2&&sStRun.active&&sStRun.set==ST_SET_CANON&&actor==sStRun.cast[0]&&st_down(actor)){
  /* Only the final CH3 Goku: hold the native supine rest, exposing the
     retained wounded auxiliary's chest. No impact/camera/body timing change. */
  Port_StorySampleMotion(actor,0xD5,10000);st_down_floor_placement(actor);return;
 }

 if(st_yamcha_mourns(actor)){st_yamcha_mourning_pose(actor);return;}
 if(st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==3&&actor==sStAtkBy){extern void Port_StoryAttachBack(int,int,float);Port_StoryAttachBack(st_obj(actor),st_obj(sStAtkAt),st_height(sStAtkAt)*.19f);return;}
 if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost&&(actor==sStChaoBy||actor==sStChaoAt)){st_chao_pair_sample(actor);return;}
 if(sStTailActive&&(!sStTailToGohan||!sStEventHit||actor==sStTailG)&&(actor==sStTailG||actor==sStTailR)&&st_chapter()==2){st_tail_sample(actor);return;}
 if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0&&(actor==sStRun.carrier||actor==sStRun.cast[0])){
  int q=sStCh2DrawQ8,f=q/256,r=actor==sStRun.carrier;
  BtlCtrl_SetPos(actor,r?&sStCh2Root:&sStCh2VictimRoot);BtlCtrl_SetRot(actor,r?&sStCh2Rot:&sStCh2VictimRot);
  if(r)Port_StorySampleMotionQ8(actor,f<=14?0x47:0x49,f<=14?q:f<34&&f>=22?8*256:f<22?q-14*256:q-26*256);
  else Port_StorySampleMotionQ8(actor,f<22?0:0xAE,f<22?0:q-22*256);return;
 }
 if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike==0&&(actor==sStRun.carrier||actor==sStRun.carried)){st_ch2_pair_frame();return;}
 if(st_hold_pair_placement(actor)){
  if(sStBeamMovie&&actor==sStBeamLead){extern int Port_StorySbcHelperVisible(void),Port_StorySbcHasContact(void);
   if(!Port_StorySbcHelperVisible()||!Port_StorySbcHasContact()){
    Vec4 own,left,right,chest,rot;
    BtlCharApi_GetPos(st_obj(actor),&own);BtlCharApi_GetNodePos(st_obj(actor),0x15,&left);BtlCharApi_GetNodePos(st_obj(actor),0x23,&right);
    BtlCharApi_GetNodePos(st_obj(sStBeamRival),17,&chest);BtlCharApi_GetRot(st_obj(sStBeamRival),&rot); /* native sternum17; retain the accepted grip's local wrist offset. */
    if(sStGripWristReady){float co=cosf(rot.y),si=sinf(rot.y);chest.x+=co*sStGripWristOffset.x+si*sStGripWristOffset.z;chest.y+=sStGripWristOffset.y;chest.z+=-si*sStGripWristOffset.x+co*sStGripWristOffset.z;}
    own.x+=chest.x-(left.x+right.x)*.5f;own.y+=chest.y-(left.y+right.y)*.5f;own.z+=chest.z-(left.z+right.z)*.5f;
    BtlCtrl_SetPos(actor,&own);BtlCtrl_SetRot(actor,&rot);return;
   }
  }
  BtlCtrl_SetPos(actor,actor==sStBeamLead?&sStBeamLeadPos:&sStBeamPos);BtlCtrl_SetRot(actor,&sStBeamRot);return;
 }
 if(Port_StoryYamchaCorpseActive()&&actor==Port_StoryCastActor(1)){BtlCtrl_SetPos(actor,&sStYamchaAnchor);BtlCtrl_SetRot(actor,&sStYamchaRot);return;}
 if(Port_StoryStillPoseActive(actor)){BtlCtrl_SetPos(actor,&sStStillPos[actor]);st_down_floor_placement(actor);BtlCtrl_SetRot(actor,&sStStillRot[actor]);return;}
 if(st_down_floor_placement(actor))return;
 int slot=st_static_attack_placement(actor);
 if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&!sStRun.departing&&sStEndingGokuReady&&
    actor==Port_StoryCastActor(6)&&sStFormActor!=actor&&(sStRun.actors>>actor&1u)){
  BtlCtrl_SetPos(actor,&sStEndingGokuAnchor);return;
 }

 /* Called after native placement writers and again after ground resolution,
    immediately before final pose/object matrices. Earlier per-frame pins can
    be overwritten; this is the last writer for an owned scripted root. */
 if(slot>=0){if(st_chapter()==0)Port_StoryResetSceneOffset(actor);BtlCtrl_SetPos(actor,&sStAttackPos[slot]);BtlCtrl_SetRot(actor,&sStAttackRot[slot]);return;}
 slot=-1;
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
extern void Port_StoryFaceFrame(int actor,int mode);
/* Facial-only overlay on the existing jaw joint. Every body clip is unchanged.
   The native HOLD_OPEN uses this same local X axis (21 degrees). */
int Port_StoryJawQ12(int actor){
 if(sStRun.active&&actor>=0&&actor<16&&(sStSurprisedActors>>actor&1u)&&BtlCharApi_GetChara(st_obj(actor))==13){
  if(sStRun.chapter==3&&actor==sStRun.actor)return 0; /* speaking child uses native TALK jaw */
  return 2500;
 }

 if(st_chapter()!=2)return 0;
 if(sStBeamFrames>0&&(actor==sStBeamLead||actor==sStBeamRival))return 0; /* native textured mouth, never rotate an unsupported jaw */
 if(!sStRun.active)return 0;
 if(sStEventRun==0&&actor==sStRun.rival&&sStEventHit){int f=sStGohanFrames-ST_GOHAN_FLIGHT;
  if(f<0||f>143)return 0;
  if(f<4)return 600+f*600;
  if(f<25)return 2400;
  return 1600;
 }

 return 0;
}
void Port_StoryFxFrame(int actor){
 int charge=0,beamCharge=0,handCharge,kid=-1;unsigned pending;void*c;
 if(actor<0||actor>=16)return;
 c=BtlChar_Get(actor);if(!c)return;
 {int mode=-1;
  if(sStRun.active&&st_chapter()>=0&&(sStRun.actors>>actor&1u)){
   mode=actor==sStRun.actor&&sStHoldIntroFrames==0?3:-1;
   if(sStEventRun>=0&&stEvents[sStRun.chapter].event[sStEventRun].kind==ST_EVENT_GOHAN&&
      actor==sStRun.rival&&sStEventHit&&sStGohanFrames<=ST_GOHAN_FLIGHT+143)mode=22; /* owned impact eyes and mouth, released after the reaction */
  }
  if(st_chapter()==2&&sStHoldPair&&!sStBeamBlood&&actor==sStBeamRival)mode=7; /* native strained mouth geometry: this model has no held-mouth texture set */
  if(st_chapter()==2&&(sStBeamLanded||sStBeamBlood)&&sStBeamFrames>0&&(actor==sStBeamLead||actor==sStBeamRival))mode=25;
  if(sStRun.active&&(sStSurprisedActors>>actor&1u))mode=BtlCharApi_GetChara(st_obj(actor))==13?23:sStRun.chapter==0&&actor==sStRun.cast[0]?21:20;
  if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&
     sStCh2Strike>=23&&actor==sStRun.cast[0])mode=(sStCh2Strike>=29&&sStCh2Strike<=37)?28:29;
  /* Moon trance is distinct from ordinary fear; shared by CH4 and CH9. */
  if(BtlCharApi_GetChara(st_obj(actor))==13&&actor==Port_StoryCastActor(1)){
   if(sStRun.active&&sStRun.chapter==3&&sStRun.set==ST_SET_INTRO&&sStIntroScared>=56&&!sStIntroPound)mode=26;
   if(sStRun.active&&sStRun.chapter==11&&sStEventRun>=0){const StEvent*e=&stEvents[11].event[sStEventRun];if(e->transformTo==180)mode=26;}
  }
  if(st_chapter()==11&&actor==Port_StoryCastActor(1)&&sStFormActor==actor&&sStFormTarget==180&&BtlCharApi_GetChara(st_obj(actor))==13)mode=26;
  if(sStRun.active&&sStRun.chapter==18&&actor==sStRun.cast[0])mode=sStRun.elapsed>=60&&sStRun.elapsed<180?10:9; /* strained under tenfold gravity */
  /* Native face geometry only; no forced pupil/jaw overrides. Speaking
     keeps native lip movement; listeners hold their reaction until the cut. */
  if(sStRun.active&&sStRun.set==ST_SET_INTRO&&(sStRun.chapter==5||sStRun.chapter==21)){
   int role;for(role=0;role<stCasts[sStRun.chapter].count;role++)if(sStRun.cast[role]==actor)break;
   if(actor!=sStRun.actor){
    if(sStRun.chapter==21&&role<5)mode=7; /* tense, held teeth while threatened */
    else if(sStRun.chapter==5&&role<5)mode=5; /* focused listening */
   }else if(sStRun.chapter==21&&sStRun.index==0&&sStRun.frames<32)mode=8; /* first cautious reaction, then normal speech */
  }
  if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost){
   if(actor==sStChaoAt)mode=7;
   else if(sStRun.index==1&&actor==sStRun.cast[0]&&sStRun.frames<40)mode=8;
  }
  if(st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==0){if(actor==sStAtkBy)mode=5;else if(actor==sStAtkAt&&sStAtkFrames<62)mode=8;}
  if(sStElbowActive){if(actor==sStRun.cast[8])mode=7;else if(actor==sStRun.cast[3])mode=sStElbowFrame<170?7:sStElbowFrame<188?21:sStElbowFrame<260?10:9;}
  if((sStRescueActive||(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost))&&st_rescue_role(actor)>=0)mode=actor==sStRun.cast[4]?23:7;
  if(sStRescueActive&&actor==sStRun.cast[8]&&sStRescueFrame>=72)mode=8;
  if(sStRescueDone&&sStRun.active&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8&&actor==sStRun.cast[8])mode=8;
  if(st_shield_post_owned(actor))mode=actor==sStRun.cast[3]?9:23;
  if(st_shield_spread(actor)&&sStShieldHitAge>0)mode=10;
  if(sStKienActive)mode=7;
  if(sStAssaultActive){if(actor==sStRun.cast[4])mode=23;else if(actor==sStRun.cast[0])mode=sStAssaultFrame>=264?9:7;else mode=7;}
  if(st_assault_charge_owns(actor))mode=9;
  if(st_yamcha_mourns(actor))mode=7;
  if(sStArmBeat&&sStRun.chapter==6){if(actor==sStRun.cast[0])mode=sStArmFrame<18?7:10;else if(actor==sStRun.cast[8])mode=7;}
  if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+6&&actor==sStRun.cast[4])mode=8;
  if(sStRun.active&&sStRun.chapter==3&&actor==Port_StoryCastActor(1)&&actor==sStRun.actor&&BtlCharApi_GetChara(st_obj(actor))==13)mode=mode==26?30:27;
  if(sStRun.active&&sStRun.chapter==6&&actor==sStRun.actor&&(mode==8||mode==10||mode==20||(mode>=21&&mode<=24)))mode=27; /* distinct from moon-trance face26: native TALK with surprised eyes */
  if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==sStRun.cast[8]&&sStRun.frames>=64)mode=28;
  Port_StoryFaceFrame(actor,mode);
 }
 if((sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&actor==Port_StoryCastActor(8))||(sStAtkFrames>0&&!sStAtkFree&&actor==sStAtkAt)||((sStBeamFrames>0||(st_chapter()==2&&sStCh3Landed))&&(actor==sStBeamLead||actor==sStBeamRival))){int f;for(f=0x42;f<=0x46;f++)BtlChar_ClearFlag(c,f);BtlChar_ClearFlag(c,0x128);}
 if(st_chapter()>=0&&sStEventRun>=0){
  const StEvent*ev=&stEvents[st_chapter()].event[sStEventRun];
  if(ev->kind==ST_EVENT_GOHAN){kid=(int)sStEventTo.w;charge=actor==kid&&!sStEventHit&&(sStGohanFrames>0||sStGohanNative);}
  else if(ev->kind==ST_EVENT_BEAM)beamCharge=charge=actor==sStBeamPartner;
  else if(ev->kind==ST_EVENT_MOON)beamCharge=charge=actor==sStMoonShooter;
  else if(ev->kind==ST_EVENT_SCENE&&!sStEventAlt){int ch=st_chapter();int who=ev->chargeCast>0?Port_StoryCastActor(ev->chargeCast-1):
   ev->attackCast>0&&ev->attackTech>=0?Port_StoryCastActor(ev->attackCast-1):-1;(void)ch;beamCharge=charge=who>=0&&actor==who;
   if(ev->counterCast>0&&actor==Port_StoryCastActor(ev->counterCast-1)&&!sStEventPost)beamCharge=charge=1;
   if(ch==0){beamCharge=0;if(sStEventPost)charge=0;}} /* CH1 blast: aura only (no dimming hand glow), and none after the shot */
 }
 if(sStAtkFrames>0&&!sStAtkFired&&sStAtkEvent>=0&&actor==sStAtkBy&&st_chapter()>=0&&
    stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_CLING)beamCharge=charge=1; /* Chiaotzu gathers it all, holding on */
 if(sStAtkFrames>0&&sStSbStage==0&&actor==sStSbThrower&&sStAtkEvent>=0&&st_chapter()>=0&&
    stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_SPIRITBOMB)beamCharge=charge=1;
 if(sStAtkFrames>0&&sStAtkEvent>=0&&st_chapter()>=0&&stEvents[st_chapter()].event[sStAtkEvent].choreo==ST_CHOREO_TRANSFER){
  if(actor==sStAtkAt&&sStAtkFrames>ST_XFER_REACH&&sStAtkFrames<=ST_XFER_GIVE)beamCharge=1;      /* Goku's hands glow */
  if(actor==sStAtkBy&&sStAtkFrames>=(ST_XFER_REACH+ST_XFER_GIVE)/2)beamCharge=charge=1;        /* Krillin takes it in */
 }
 /* Keep the actual grip readable; the native self-destruct owns its charge
    after the farewell, rather than covering the entire dialogue with an orb. */
 if(sStChaoPair&&sStRun.active&&sStRun.chapter==6&&!sStEventPost)charge=beamCharge=0;
 {extern char*getenv(const char*);if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0)charge=beamCharge=0;}
 if(st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==0&&actor==sStAtkBy){charge=1;beamCharge=0;}
 if(sStKienActive||(sStRun.active&&sStRun.chapter==6&&sStEventPost&&(sStRun.set==ST_SET_EVENT0+7||sStRun.set==ST_SET_EVENT0+8))){charge=beamCharge=0;}
 if(sStKienActive){charge=actor==sStRun.cast[2]&&sStKienFrame<112;beamCharge=0;}
 if(sStPowerActive){charge=actor==sStRun.cast[6];beamCharge=0;}
 if(sStAssaultActive){charge=actor==sStRun.cast[0]&&sStAssaultFrame>=264;beamCharge=0;}
 if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+4&&sStEventPost)charge=beamCharge=0;
 if((sStAssaultActive&&sStAssaultFrame>=264&&actor==sStRun.cast[0])||st_assault_charge_owns(actor)){charge=1;beamCharge=0;}
 /* The rescue uses only Nappa's native hand charge. The full aura obscures the cloud. */
 handCharge=st_rescue_hand_charge(actor);
 if(handCharge)BtlCharSnd_PlayCommonFar(c,BtlParam_GetChargeLoopSound(c));
 if(beamCharge){BtlChar_SetFxBit(c,0x12);}
 handCharge|=beamCharge;
 if(handCharge!=sStBeamChargeActive[actor]){Port_StoryChargeSpark(actor,handCharge);sStBeamChargeActive[actor]=handCharge;}
 if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_INTRO&&sStKameArrival==1&&sStKameFrames<36&&actor==sStRun.cast[2]){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);}
 if(sStRun.active&&sStRun.arriving==actor&&sStRun.frames<=40&&(sStRun.bridge||(sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8&&sStRun.index==2))){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);charge=1;}
 if(sStRun.active&&sStRun.departing&&!st_ch9_pod_scene()&&actor==sStRun.rival&&
    (sStRun.chapter<=1?sStEarlyTakeoff>0:sStRun.frames>=9)){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);}
 if(sStElbowActive&&actor==sStRun.cast[3]){if(sStElbowFrame<100){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);BtlChar_SetFxBit(c,0x13);BtlChar_SetFxBit(c,7);}else if(sStElbowFrame==100)BtlChar_SetFxBit(c,0x14);} /* dash aura (Claude 2026-11-18) */
 if(sStElbowActive&&actor==sStRun.cast[2]){if(sStElbowFrame<63){BtlChar_SetFxBit(c,4);BtlChar_SetFxBit(c,0x3A);BtlChar_SetFxBit(c,0x13);BtlChar_SetFxBit(c,7);}else if(sStElbowFrame==63)BtlChar_SetFxBit(c,0x14);}
 if(charge){BtlChar_SetFxBit(c,0x13);BtlChar_SetFxBit(c,7);BtlCharSnd_PlayCommonFar(c,BtlParam_GetChargeLoopSound(c));}
 else if(sStAuraActive[actor])BtlChar_SetFxBit(c,0x14);
 sStAuraActive[actor]=charge;
 pending=sStPresentation[actor];sStPresentation[actor]=0;
 if(pending&1u)BtlCharSnd_PlayCommonFar(c,ST_SE_DASH);
 if(pending&2u)BtlCharSnd_PlayCommonFar(c,ST_SE_STRIKE);
 if(pending&8u){BtlCharSnd_PlayCommonFar(c,0x22); /* native level3 body impact, not a swing/evade */
  Port_Dbg3("story CH2 knee landed sound",actor,0x22,sStCh2DrawQ8,sStCh2Contact);}
 if(pending&16u){
  BtlCharSnd_PlayCommonFar(c,0x22);
  Port_Dbg3("story Tien arm landed sound",actor,0x22,sStArmFrame,0);
 }

 if(pending&4u)Port_StoryHitFx(actor);
}
/* Gameplay helpers must not join or outlive authored Story interactions. */
int Port_StoryAssistBlocked(void){
 return st_chapter()>=0&&(sStRun.active||sStEventRun>=0||sStAtkFrames>0||
  sStBeamFrames>0||sStMoonFrames>0||sStIntroFormFrames>0);
}
int Port_StoryEndPending(void){
 s32 ch=st_chapter(),i,a,alive=0;
 if(Port_StoryActive()&&sStBeamFrames>0)return 1;
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
    if(st_chapter()==6&&ev==&stEvents[6].event[9]&&sStFightFrames-sStLastEventEnd<10*30)return 0;
    if(st_chapter()==6&&ev==&stEvents[6].event[10]&&sStFightFrames-sStLastEventEnd<8*30)return 0;
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
    if(chapter==6&&sStStillPendingActors){int actor,tien=Port_StoryCastActor(0);extern int Port_StoryCaptureStillPose(int,int);
      for(actor=0;actor<32;actor++)if((sStStillPendingActors>>actor&1u)&&!(sStStillActors>>actor&1u) ){
        st_scene_ground_actor(actor);
        /* Tien gets the requested face-down death pose. Piccolo already has the
           accepted near-perfect down pose, so freeze it in place without
           replacing it with another animation. */
        if(!Port_StoryCaptureStillPose(actor,actor==tien))continue;
        BtlCharApi_GetPos(st_obj(actor),&sStStillPos[actor]);BtlCharApi_GetRot(st_obj(actor),&sStStillRot[actor]);
        sStStillActors|=1u<<actor;Port_Dbg3("story corpse pose frozen",actor,actor==Port_StoryCastActor(0),actor==Port_StoryCastActor(3),chapter);
      }
    }
    if (sStEventRun >= 0) {
        /* Original MoveChar(...,-w): show the paired resistance before cutting
           to speakers. Previously Piccolo's shot hid most of these 108 frames. */
        if(chapter==2&&sStHoldPair&&sStHoldIntroFrames>0&&
           stEvents[chapter].event[sStEventRun].kind==ST_EVENT_BEAM){
            ++sStHoldIntroFrames;
            st_event_frame(chapter);
            sStRun.actor=sStBeamLead;st_scene_camera();Port_StorySubtitle(-1,0,0);
            if((sStHoldIntroFrames>2&&!BtlCtrl_IsMotionPlaying(sStBeamLead)&&!BtlCtrl_IsMotionPlaying(sStBeamRival))||sStHoldIntroFrames>240){
                sStHoldIntroFrames=0;sStEventCtx.step=ST_STEP_BEAT;
            }
            return 1;
        }
        if(chapter==2&&sStTailActive&&sStTailIntro){
          st_event_frame(chapter);Port_StorySubtitle(-1,0,0);
          if(++sStTailIntro>246){sStTailIntro=0;sStEventCtx.step=ST_STEP_BEAT;}
          return 1;
        }
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
        if(chapter==11&&i==10){int thrower=Port_StoryCastActor(2);
            if(thrower<0||st_hp(thrower)<=0||Port_StorySceneParked(thrower)||(sStGoneActors>>thrower&1u)){
                sStEventOffer=i;sStEventPressed=0;return 0; /* revive in free combat */
            }
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
    } else if(chapter==2){
        /* Append the tail scene without renumbering existing Gohan0 / SBC1 checkpoints. */
        i=!(sStEventsDone&4u)?2:!(sStEventsDone&1u)?0:1;
        if((sStEventsDone>>i&1u)||!st_event_due(&evs->event[i],rival,hp)){sStEventPressed=0;return 0;}
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

/* Shared recovery after scripted scenes, including actors outside the shot.
   Only free, living combatants qualify; the native helper further limits resets
   to a stranded Story pose or a stationary neutral CPU for 90 frames. */
static int st_recoverable_combatant(int a){
 return a>=0&&a<ST_ACTORS&&!Port_StorySceneActor(a)&&!Port_StorySceneParked(a)&&
  !((sStGoneActors|sStDownActors)>>a&1u)&&st_hp(a)>0&&a!=sStTiredActor&&!Port_StoryHeld(a);
}
static void st_resume_free_combat(int chapter){
 int k,j,a,n;extern void Port_StoryResumeCombatActor(int,int);
 if(chapter<0||sStRun.active||sStEventRun>=0||sStAtkFrames>0||sStBeamFrames>0||
    sStMoonFrames>0||sStIntroFormFrames>0||sStFormActor>=0||
    (Battle_GetWork()->flags&(BATTLE_FLAG_PAUSE|BATTLE_FLAG_DEMO)))return;
 for(k=0;k<stCasts[chapter].count;k++){
  a=Port_StoryCastActor(k);if(!st_recoverable_combatant(a))continue;
  n=gPortTarget[a];
  if(!st_recoverable_combatant(n)||Port_Team(n)==Port_Team(a)){
   n=-1;
   for(j=0;j<stCasts[chapter].count;j++){
    int foe=Port_StoryCastActor(j);
    if(st_recoverable_combatant(foe)&&Port_Team(foe)!=Port_Team(a)){n=foe;break;}
   }
   if(n>=0)gPortTarget[a]=n;
  }
  Port_StoryResumeCombatActor(a,n);
 }
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
    /* Frozen defeated actors skip native motion/ground stages entirely. */
    {int k,a;for(k=0;k<stCasts[chapter].count;k++){a=Port_StoryCastActor(k);if(a>=0&&a<16&&((sStDownActors>>a&1u)||(!Port_StorySceneActor(a)&&st_hp(a)<=0)))st_down_floor_placement(a);}}
    if(chapter==6){int k,a;for(k=10;k<=14;k++){if(k==13)continue;a=Port_StoryCastActor(k);
      {static int probe=-1;extern char*getenv(const char*);if(probe<0)probe=getenv("BT3_STORY_WAVE_PROBE")!=0;
       if(probe&&a>=0&&!Port_StorySceneParked(a)&&st_hp(a)>1000)st_set_hp(a,1000); /* fixture only: real hits still defeat the wave */}

      if(a>=0&&!Port_StorySceneParked(a)&&!(sStGoneActors>>a&1u)&&st_hp(a)<=0&&Port_SequencePartner(a)<0){sStGoneActors|=1u<<a;Port_StoryRemoveActor(a);}
    }}

    if(chapter==6&&!sStRun.active&&sStAtkFrames<=0){int v=Port_StoryCastActor(9),t=st_lead_now(chapter);if(v>=0&&t>=0&&v!=t)st_face(v,t);}
    if (sStEventRun < 0 && sStBeamFrames <= 0) {
        sStFightFrames++;
    }
    /* Isolated chapter runner only: select one real R3 scene after the real
       chapter intro. Requires both explicit test variables; normal launchers
       clear STORY_* and never take this path. No saves/checkpoints are edited. */
    {static int sceneProbeDone;extern char*getenv(const char*);extern int atoi(const char*);
     char *event=getenv("BT3_STORY_SCENE_EVENT");
     if(!sceneProbeDone&&getenv("BT3_STORY_PROBE")&&event&&sStFightFrames>=120&&
        !sStRun.active&&sStEventRun<0&&sStBeamFrames<=0){
       int k=atoi(event);
       sceneProbeDone=1;
       /* Private ending-camera fixture: normal launchers clear both flags. */
       if(k==-4&&(chapter==5||chapter==21)){int j;for(j=0;j<stCasts[chapter].count;j++)if(stCasts[chapter].actor[j].team==1){int a=Port_StoryCastActor(j);if(a>=0)st_set_hp(a,0);}}
       /* Juan explicitly authorized this private ending-state review,2026-11-18.
          Both probe flags required; runner uses separate saves, never installed. */
       if(k==-5&&chapter==6){int j,n=Port_StoryCastActor(8);for(j=0;j<stEvents[6].count;j++){st_event_effects(6,&stEvents[6].event[j],n);sStEventsDone|=1u<<j;Port_StoryEventDone(j);}for(j=0;j<stCasts[6].count;j++)if(stCasts[6].actor[j].team==1)st_set_hp(Port_StoryCastActor(j),0);Port_Dbg3("story private ending state prepared",6,0,0,0);return;}
       if(chapter==6&&(k==-6||k==-7)){
         /* Exercise the ordinary early-KO branch after the correct wave handover. */
         int prior,preceding=k==-6?3:1,target=Port_StoryCastActor(k==-6?8:13);
         for(prior=0;prior<preceding;prior++){
           st_event_effects(6,&stEvents[6].event[prior],Port_StoryCastActor(prior==0?7:prior==1?13:8));
           sStEventsDone|=1u<<prior;Port_StoryEventDone(prior);
         }
         if(target>=0)st_set_hp(target,0);
         return;
       }
       if(k==-3&&chapter==1){int j;for(j=0;j<stCasts[chapter].count;j++)if(stCasts[chapter].actor[j].team==0){int a=Port_StoryCastActor(j);if(a>=0)st_set_hp(a,0);}}
       if(k==-2&&chapter==6&&getenv("BT3_STORY_GROUND_PROBE")){
         int roles[2]={0,3},j,lead=Port_StoryCastActor(2);Vec4 at,eye;
         Port_StoryEnterActor(lead);Port_StoryReviveActor(lead,BtlCharApi_GetHpMax(st_obj(lead)));st_stage_actor(lead);Port_StoryHandControl(lead);
         for(j=0;j<2;j++){int a=Port_StoryCastActor(roles[j]);Vec4 p;
           Port_StoryEnterActor(a);
           st_place_near(a,lead,2.f,j?1.5f:-1.5f,0);st_scene_ground_actor(a);
           BtlCharApi_GetPos(st_obj(a),&p);p.y-=st_height(a)*5.f;
           st_set_hp(a,0);sStDownActors|=1u<<a;sStGoneActors|=1u<<a;sStStillPendingActors|=1u<<a;
           BtlCtrl_PlayMotion(a,j?0xD5:0xD6,0,0.f);BtlCtrl_SetPos(a,&p);
         }
         BtlCharApi_GetPos(st_obj(lead),&at);at.y-=3.f;eye=at;eye.x+=70.f;eye.y-=70.f;eye.z+=100.f;
         sStCamDrift=0;st_camera_finish(eye,at,st_height(lead),lead,0);
         Port_Dbg3("story isolated airborne corpse probe",chapter,Port_StoryCastActor(0),Port_StoryCastActor(3),lead);
       }
       if(k>=0&&k<stEvents[chapter].count){
         Port_Dbg3("story isolated scene probe",chapter,k,0,0);
         if(chapter==11&&k==11&&getenv("BT3_STORY_MOON_FACE_PROBE")){int role,lead=Port_StoryCastActor(0);for(role=1;role<=3;role++){int a=Port_StoryCastActor(role);if(a<0)continue;Port_StoryEnterActor(a);st_set_hp(a,BtlCharApi_GetHpMax(st_obj(a)));st_stage_actor(a);st_place_near(a,lead,2.f,(role-2)*1.5f,0);}sStEventsDone=(1u<<11)-1;}
         if(chapter==6&&k==1){
          /* Reproduce the preceding wave transition; the actor was otherwise
             still parked when the isolated Yamcha rush asked for its target. */
          sStEventsDone|=1u;st_event_effects(chapter,&stEvents[6].event[0],Port_StoryCastActor(7));
         }
         if(chapter==6&&k==2){sStEventsDone|=3u;st_event_effects(chapter,&stEvents[6].event[0],Port_StoryCastActor(7));st_event_effects(chapter,&stEvents[6].event[1],Port_StoryCastActor(13));}
         if(chapter==6&&k==4){int prior;for(prior=0;prior<4;prior++){sStEventsDone|=1u<<prior;st_event_effects(chapter,&stEvents[6].event[prior],Port_StoryCastActor(prior==0?7:prior==1?13:8));}}
         if(chapter==6&&k==7){int prior;for(prior=0;prior<6;prior++){sStEventsDone|=1u<<prior;st_event_effects(chapter,&stEvents[6].event[prior],Port_StoryCastActor(prior==0?7:prior==1?13:8));}}
         if(chapter==6&&(k==6||k==9||k==10)){
          /* Restore the wave and wardrobe state for the elbow, and admit Goku for the final techniques. */
          int prior;for(prior=0;prior<k;prior++){sStEventsDone|=1u<<prior;st_event_effects(chapter,&stEvents[6].event[prior],Port_StoryCastActor(prior==0?7:prior==1?13:8));}
         }
         st_event_start(chapter,k);return;
       }
     }
    }
    /* Private complete-scene review only: after tail->Gohan has returned to
       combat, advance to the final R3. Ordinary play never enters this branch. */
    {extern char*getenv(const char*);static int wholeWait;
     if(chapter==2&&getenv("BT3_STORY_PROBE")&&getenv("BT3_STORY_CH3_WHOLE")&&
        !sStRun.active&&sStEventRun<0&&sStBeamFrames<=0&&sStEventHit&&sStRaditzArmorBroken&&
        !(sStEventsDone&(1u<<1))){if(++wholeWait==45){st_event_start(chapter,1);return;}}
    }
    if(st_checkpoint_restore())return;
    /* Reassert event-owned appearance on resume, with serialized quiet loads. */
    if(chapter==6&&(sStEventsDone&(1u<<4)))st_wardrobe_queue(Port_StoryCastActor(8));
    if(chapter==11){
        if(sStEventsDone&1u)st_wardrobe_queue(Port_StoryCastActor(0));
        if(sStEventsDone&2u)st_wardrobe_queue(Port_StoryCastActor(4));
        /* Event admission marks progress before dialogue and the actual strike. */
        if((sStEventsDone&(1u<<6))&&sStEventRun!=6&&!(sStAtkFrames>0&&sStAtkEvent==6)){
            int v=Port_StoryCastActor(4);if(v>=0)sStTailCut|=1u<<v;
        }
        if((sStEventsDone&(1u<<13))&&sStEventRun!=13&&!(sStAtkFrames>0&&sStAtkEvent==13)){
            int g=Port_StoryCastActor(1);if(g>=0)sStTailCut|=1u<<g;
        }
    }
    st_wardrobe_tick();
    /* Explicit checkpoint retries after the time skip restore the visual state too. */
    if(chapter==6&&(sStEventsDone&(1u<<5))){extern void Port_StoryNappaOutfit(int);Port_StoryNappaOutfit(Port_StoryCastActor(8));}
    st_resume_free_combat(chapter);
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
      const char *mode=getenv("BT3_STORY_APE_PHASE_PROBE");
      int k,v=Port_StoryCastActor(4),sword=!strcmp(mode,"sword");applied=1;
      for(k=0;k<=(sword?5:10);k++){st_event_effects(chapter,&stEvents[chapter].event[k],v);sStEventsDone|=1u<<k;Port_StoryEventDone(k);}
      Port_StoryHandControl(Port_StoryCastActor(1));st_set_hp(Port_StoryCastActor(0),0);
      if(sword&&v>=0){
       Port_StoryGrantForm(v,30);sStFormActor=v;sStFormTarget=30;sStFormWait=0;st_transform_ready(v,30);
       st_set_hp(v,BtlCharApi_GetHpMax(st_obj(v))*2/5);
      }
      sStLastEventEnd=sStFightFrames;
     }
     if(chapter==11&&applied&&(sStEventsDone&(1u<<11))&&!(sStEventsDone&(1u<<12))&&
        sStFormActor<0&&sStFightFrames-sStLastEventEnd>=8*30){
      const char *mode=getenv("BT3_STORY_APE_PHASE_PROBE");
      int ape=Port_StoryCastActor(1);
      if(mode&&!strcmp(mode,"finish")&&ape>=0){
        int low=BtlCharApi_GetHpMax(st_obj(ape))/4;
        if(st_hp(ape)>low)st_set_hp(ape,low);
      }
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
       if(chapter==11&&sStAtkFrames>0&&sStAtkEvent==13&&sStFormTarget==13){
         st_scene_ground_actor(sStFormActor);
         Port_StorySampleMotion(sStFormActor,186,10000);
         Fade_Start(1,1,5.f/30.f);
       }
       if(chapter==11&&sStFormActor==Port_StoryCastActor(4)&&sStFormTarget==29&&(sStEventsDone&(1u<<6))){extern void Port_StoryTailOutfit(int);Port_StoryTailOutfit(sStFormActor);if(sStFormActor>=0&&sStFormActor<32)sStTailCut|=1u<<sStFormActor;}
       if(chapter==11&&sStFormTarget==29&&st_hp(sStFormActor)>0){sStTiredActor=sStFormActor;sStTiredFrames=ST_TIRED_FRAMES;Port_Dbg3("story tired after reversion",chapter,sStFormActor,sStFormTarget,0);}
       sStFormActor=-1;}
      else if(++sStFormWait>300){
       if(chapter==11&&sStAtkFrames>0&&sStAtkEvent==13)Fade_Start(1,1,5.f/30.f);
       Port_Dbg3("story form failed",chapter,sStFormActor,sStFormTarget,0);sStFormActor=-1;BattleResult_Set(2,1);return;
      }
      else {if(sStFormWait%15==0&&BtlCtrl_IsInterruptible(sStFormActor))st_transform_ready(sStFormActor,sStFormTarget);return;}
    }
    {extern char*getenv(const char*);extern void Port_StoryActorProbe(s32,s32);static int al=-1;if(al<0)al=getenv("BT3_STORY_ACTOR_LOG")!=0;
     if(al&&sStFightFrames%30==0){int i,a;for(i=0;i<stCasts[chapter].count;i++){a=Port_StoryCastActor(i);if(a>=0)Port_StoryActorProbe(a,(sStFightFrames/30)&0xFFFF);}}}
    /* Private verification fixture: start within the native revive radius after
       Krillin settles on his own. Health, action and the revival remain native. */
    {extern char *getenv(const char *);extern s32 BtlCharApi_GetAction(s32);static int positioned;
     if(chapter==1&&!positioned&&getenv("BT3_CH2_REVIVE_PROBE")){
       int hero=Port_StoryCastActor(0),krillin=Port_StoryCastActor(1);
       if(hero>=0&&krillin>=0&&st_hp(hero)>0&&st_hp(krillin)<=0&&
          BtlCharApi_GetAction(st_obj(krillin))==0xD8){
         Vec4 p;BtlCharApi_GetPos(st_obj(krillin),&p);p.x+=8.0f;
         BtlCtrl_SetPos(hero,&p);positioned=1;
         Port_Dbg3("story CH2 revive probe positioned helper",hero,krillin,sStFightFrames,0);
       }
     }}
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
 {extern void Port_StoryDiscEnd(void);Port_StoryDiscEnd();}sStRescueActive=0;sStKienActive=0;
 sStAssaultActive=0;sStElbowActive=0;BtlFacade_ShakeCamera(0);{extern void Port_StoryNimbusSmoke(const Vec4*,float,int);Port_StoryNimbusSmoke(0,0,0);}BtlCtrl_SetAuraOff(sStRun.cast[2]);
 sStArmBeat=0;
 sStTailActive=sStTailToGohan=sStTailIntro=0;
    if(sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_INTRO){
      int a=sStRun.cast[2];if(a>=0){BtlCtrl_SetPos(a,&sStKameLanding);BtlCtrl_StopMotion(a);}
    }
    sStKameArrival=sStKameFrames=0;
    s32 i;
    st_attack_restore_down(); /* also closes the borrowed native-contact slot on surrender/abort */
    st_prefight_stop(st_chapter()>=0);sStPreludeJustFinished=0;
    if(sStRun.active){
        if(sStRun.chapter==6&&sStRun.set>=ST_SET_EVENT0&&
           sStRun.set<ST_SET_EVENT0+stEvents[6].count){
            int observer=Port_StoryCastActor(9);
            /* The execution and the Kienzan can both admit the observer. */
            if(observer>=0&&!Port_StorySceneActor(observer)){
                extern void Port_StoryReleaseActor(int);
                BtlCtrl_StopMotion(observer);Port_StoryReleaseActor(observer);
                Port_StoryExitActor(observer,st_hp(observer));
                st_scene_take(observer,0);
                Port_Dbg3("story Vegeta observer restored",observer,Port_StorySceneParked(observer),sStRun.set-ST_SET_EVENT0,0);
            }
        }
        for(i=0;i<32;i++)if(sStRun.actors>>i&1u){
            if(st_chapter()==2&&sStHoldPair&&sStBeamFrames>0&&!sStBeamLanded&&
               (i==sStBeamLead||i==sStBeamRival))continue;
            /* CH9's fallen Vegeta is alive, but must keep his down motion across
               event/ending teardown. StopMotion would queue a recovery/get-up. */
            if(st_chapter()==11&&i==Port_StoryCastActor(4)&&(sStDownActors>>i&1u)){
                BtlCtrl_PlayMotion(i,ST_MOTION_PRONE,1,0.f);continue;
            }
            BtlCtrl_StopMotion(i);{extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(i);}if(Port_StorySceneActor(i))st_scene_take(i,0);}
    }
    if(st_chapter()>=0||sStRun.chapter>=0)BtlFacade_ClearFixedCamera();
    if (st_chapter() >= 0 || sStRun.chapter >= 0) {
        Port_StorySubtitle(-1, 0, 0);
    }
    /* st_event_finish clears EventRun BEFORE teardown. The scene set retains
       the actual event identity, including post-attack dialogue and skips. */
    if(sStRun.active&&sStRun.chapter>=0&&sStRun.set>=ST_SET_EVENT0&&sStRun.set<ST_SET_EVENT0+stEvents[sStRun.chapter].count){
      int ch=sStRun.chapter,e=sStRun.set-ST_SET_EVENT0;const StEvent*ev=&stEvents[ch].event[e];
      if(ev->killMask||ev->removeMask){int k;extern void Port_StoryAiAudit(int,int,int);
       for(k=0;k<stCasts[ch].count;k++){int a=Port_StoryCastActor(k);
        if(a>=0&&!Port_StorySceneActor(a)&&!Port_StorySceneParked(a)&&!((sStDownActors|sStGoneActors)>>a&1u)&&st_hp(a)>0)
         Port_StoryAiAudit(a,ch,e);
       }
      }
    }
    if (sStEventRun >= 0) {
        sStEventRun = -1;
        Battle_GetWork()->flags &= ~BATTLE_FLAG_DEMO;
    }
    st_ch8_plant_end();
    sStRun.chapter = -1;
    sStRun.active=sStRun.departing=sStRun.finishing=sStRun.bridge=0;
    sStRun.actors=0;st_act_clear();sStCamDrift=0;
    if(st_chapter()==6)sStCh7ReleaseFrames=3;
    sStCueFrames = 0;
}

/* The moon blast is scenery choreography, not an attack against the captured child.
   Keep the native beam/effects, suppress only its transient collision record. */
s32 Port_StorySkyShot(s32 obj){return st_chapter()==3&&sStMoonFrames>0&&sStMoonShooter>=0&&obj==st_obj(sStMoonShooter);}

/* The moon shot queues its native technique on frame two. Give that one
   request priority over the outgoing combat action's decision. */
int Port_StoryMoonLaunchPending(s32 obj) {
    return sStMoonFrames == 2 && Port_StorySkyShot(obj);
}

s32 Port_StoryOwnAttackCamera(s32 obj){
 if(st_chapter()==6&&sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1&&obj==st_obj(sStRun.cast[9]))return 1;
 if(Port_StorySkyShot(obj))return 1;
 if(Port_StoryTechniqueDonor(sStSbThrower)>=0&&obj==st_obj(sStSbThrower))return 0; /* native ultimate camera */
 return st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent>=0&&sStAtkBy>=0&&obj==st_obj(sStAtkBy)&&(sStAtkEvent==3||sStAtkEvent==7||(sStAtkEvent==1&&sStYamchaCraterReady)||stEvents[6].event[sStAtkEvent].choreo==ST_CHOREO_EXECUTE);
}

int Port_StoryFixedCameraOwned(void){int ch=st_chapter();if((ch==3||(sStRun.active&&sStRun.chapter==3))&&sStMoonCameraValid&&(sStMoonFrames>0||(sStRun.active&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u))))return 1;if(ch==11&&Port_StoryTechniqueDonor(sStSbThrower)>=0)return 0;if(ch==1&&sStRun.active&&sStRun.set==ST_SET_CANON)return 1;if(ch!=6)return 0;
 return sStRun.active||(sStAtkFrames>0&&sStAtkEvent>=0&&(sStAtkEvent==3||sStAtkEvent==7||(sStAtkEvent==1&&sStYamchaCraterReady)||stEvents[ch].event[sStAtkEvent].choreo==ST_CHOREO_EXECUTE));
}

int Port_StoryApplyFixedCamera(void){
 if((st_chapter()==3||(sStRun.active&&sStRun.chapter==3))&&sStMoonCameraValid&&(sStMoonFrames>0||(sStRun.active&&sStRun.set==ST_SET_INTRO&&!(sStEventsDone&1u)))){BtlFacade_SetFixedCamera(&sStMoonCameraEye,&sStMoonCameraRot);return 1;}
 if(!Port_StoryFixedCameraOwned()||sStRun.shot<0||!st_finite_pos(&sStRun.exitEye)||!st_finite_pos(&sStRun.exitRot))return 0;
 BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return 1;
}



int Port_StoryUltimatePrelude(int who){
 if(st_chapter()==2&&sStBeamFrames>0&&!sStBeamLanded&&who==sStBeamPartner)return 1;
 return sStAtkFrames>0&&((st_chapter()==6&&(sStAtkEvent==7||sStAtkEvent==8)&&who==sStAtkBy)||(st_chapter()==11&&sStAtkBack>=0&&(who==sStAtkBy||who==sStAtkBack)));
}

int Port_StoryBeamPairActor(int actor){return st_chapter()>=0&&sStAtkFrames>0&&sStAtkBack>=0&&(actor==sStAtkBy||actor==sStAtkBack);}

int Port_StorySbcMovieOwner(void){return st_chapter()==2&&sStBeamMovie&&sStBeamFrames>0?sStBeamPartner:-1;}

int Port_StorySbcObject(int actor,int id){return st_chapter()==2&&sStBeamFrames>0&&!sStBeamLanded&&actor==sStBeamPartner&&id==279;}

static void st_ch2_pair_frame(void){
 int r=sStRun.carrier,k=sStRun.carried;Vec4 hand,neck,p,rot;
 if(!sStRun.active||sStRun.chapter!=1||sStRun.set!=ST_SET_CANON||sStCh2Strike||r<0||k<0)return;
 {extern void Port_StoryCarryArm(int,int);
  if(!sStCh2CarryReady){if(!Port_StorySampleMotion(r,0x95,10))return;Port_StoryCarryArm(r,1);sStCh2CarryReady=1;}
  if(sStRun.departing){
   if(sStEarlyTakeoff>0){if(!Port_StoryLoopMotion(r,0x2F,sStRun.frames-sStEarlyTakeoff))return;}
   else if(!Port_StorySampleMotion(r,0x2E,sStRun.frames))return;
  }else if(!Port_StorySampleMotion(r,0,0))return;
  Port_StoryCarryArm(r,0);
 }
 BtlCharApi_GetRot(st_obj(r),&rot);BtlCtrl_SetRot(k,&rot);gPortTarget[k]=r;
 if(!Port_StorySampleMotion(k,ST_MOTION_LIMP,9))return;
 BtlCharApi_GetNodePos(st_obj(r),0x23,&hand);BtlCharApi_GetNodePos(st_obj(k),0x2E,&neck);BtlCharApi_GetPos(st_obj(k),&p);
 p.x+=hand.x-neck.x;p.y+=hand.y-neck.y+st_height(k)*.12f;p.z+=hand.z-neck.z;
 BtlCtrl_SetPos(k,&p);Port_StorySampleMotion(k,ST_MOTION_LIMP,9);
 {static int tick;if(++tick%120==0){Port_Dbg3("story CH2 grip hand",r,(int)(hand.x*100),(int)(hand.y*100),(int)(hand.z*100));Port_Dbg3("story CH2 child root",k,(int)(p.x*100),(int)(p.y*100),(int)(p.z*100));}}
}

int Port_StoryFxRateQ8(void){return sStRun.active&&sStRun.chapter==1&&sStRun.set==ST_SET_CANON&&sStCh2Strike>0&&sStCh2DrawQ8>=26*256&&sStCh2DrawQ8<44*256?64:256;}

int Port_StorySbcHasContact(void){return st_chapter()==2&&sStBeamBlood!=0;}

int Port_StoryFocusBlur(void){return st_chapter()==3&&sStRun.active&&sStRun.set==ST_SET_INTRO&&sStIntroFormFrames>0&&!sStIntroPound&&!(sStEventsDone&1u);}

static int st_ape_tail_cap_actor(void){
 {extern char*getenv(const char*);if(getenv("BT3_STORY_NO_TAIL_CAP"))return -1;}
 if(st_chapter()==11){
    int a=Port_StoryCastActor(1);
    return a>=0&&(sStTailCut>>a&1u)&&BtlCharApi_GetChara(st_obj(a))==180?a:-1;
 }
 if(st_chapter()!=3||sStMoonFrames<=0||sStMoonTarget<0||!(sStTailCut>>sStMoonTarget&1u))return -1;
 /* Retain the attached stump across the renderer's outgoing ape interval.
    Child animation, camera and all choreography timings remain unchanged. */
 if(sStMoonRev>=3&&(sStMoonRev!=3||sStMoonFrames-sStMoonRevAt>8))return -1;
 return sStMoonTarget;
}

int Port_StoryTailCap(float*out){Vec4 c,hip;float x,y,z,d;
 int a=st_ape_tail_cap_actor();
 if(a<0)return 0;
 {extern int Port_StoryTailCapVertex(int,int,Vec4*);if(!Port_StoryTailCapVertex(st_obj(a),12,&c))return 0;}st_vnode(a,3,&hip);x=c.x-hip.x;y=c.y-hip.y;z=c.z-hip.z;d=sqrtf(x*x+y*y+z*z);if(d<.01f)return 0;
 out[0]=c.x+x/d*.2f;out[1]=c.y+y/d*.2f;out[2]=c.z+z/d*.2f;out[3]=st_height(a)*.035f;out[4]=x/d;out[5]=y/d;out[6]=z/d;return 1;
}

int Port_StoryCh4RevertedObject(void*obj){int a;if(st_chapter()!=3||!sStMoonFallValid)return 0;a=Port_StoryCastActor(1);return a>=0&&(sStGoneActors>>a&1u)&&obj==(void*)BtlObj_Get(st_obj(a));}

void*Port_StoryStumpTexture(int which){extern void*Port_StoryCapTexture(int);int a=which==0?(st_chapter()==3?sStMoonTarget:st_ape_tail_cap_actor()):Port_StoryCastActor(1);if(which==1&&st_chapter()!=2)return 0;return a>=0?Port_StoryCapTexture(st_obj(a)):0;}
int Port_StoryStumpInfo(int which,float*out){Vec4 c,p;int a;float x,y,z,d;
 if(which==0)return Port_StoryTailCap(out);if(st_chapter()!=2||!Port_StoryStumpTexture(1))return 0;a=Port_StoryCastActor(1);
 st_vnode(a,34,&c);st_vnode(a,33,&p);x=c.x-p.x;y=c.y-p.y;z=c.z-p.z;c.x=p.x+x*.47f;c.y=p.y+y*.47f;c.z=p.z+z*.47f; /* the mesh ends halfway along the upper arm, before the elbow joint */d=sqrtf(x*x+y*y+z*z);if(d<.001f)return 0;
 {extern int Port_StoryArmCapVertex(int,int,Vec4*);Port_StoryArmCapVertex(st_obj(a),10,&c);}
 out[0]=c.x;out[1]=c.y;out[2]=c.z;out[3]=st_height(a)*.045f;out[4]=x/d;out[5]=y/d;out[6]=z/d;return 1;
}

int Port_StoryPiercingCamera(void*eyeOut,void*rotOut){Vec4 g,r,eye,at,rot={0,0,0,1},face;float h,dx,dz;extern int Port_StoryVisualObjectId(int,int);
 if(st_chapter()!=2||!sStBeamMovie||sStPierceShot<=0||sStPierceShot>=30)return 0;
 BtlCharApi_GetNodePos(Port_StoryVisualObjectId(sStBeamLead,st_obj(sStBeamLead)),3,&g);BtlCharApi_GetNodePos(st_obj(sStBeamRival),3,&r);BtlCharApi_GetRot(st_obj(sStBeamRival),&face);h=st_height(sStBeamRival);
 at=g;at.x=(g.x+r.x)*.5f;at.y=(g.y+r.y)*.5f;at.z=(g.z+r.z)*.5f;eye=at;eye.x+=sinf(face.y+.7f)*h*1.8f;eye.z+=cosf(face.y+.7f)*h*1.8f;eye.y-=h*.3f;
 dx=at.x-eye.x;dz=at.z-eye.z;rot.y=atan2f(dx,dz);rot.x=atan2f(eye.y-at.y,sqrtf(dx*dx+dz*dz));rot.z=-.12f;*(Vec4*)eyeOut=eye;*(Vec4*)rotOut=rot;return 1;
}

int Port_StoryArmCapPoint(int index,float*out){extern int Port_StoryArmCapVertex(int,int,Vec4*);int a=Port_StoryCastActor(1);Vec4 p;if(st_chapter()!=2||a<0||!Port_StoryStumpTexture(1)||!Port_StoryArmCapVertex(st_obj(a),index,&p))return 0;out[0]=p.x;out[1]=p.y;out[2]=p.z;return 1;}

int Port_StoryTailCapPoint(int index,float*out){extern int Port_StoryTailCapVertex(int,int,Vec4*);Vec4 p;int a=st_ape_tail_cap_actor();if(a<0||!Port_StoryTailCapVertex(st_obj(a),index,&p))return 0;out[0]=p.x;out[1]=p.y;out[2]=p.z;return 1;}

#ifdef PORT
int Port_StoryNativeHalo(void*o){return sStRun.active&&(sStRun.chapter==18||(sStRun.chapter==20&&sStWishRevivePhase<2))&&sStRun.cast[0]>=0&&BtlObj_Get(st_obj(sStRun.cast[0]))==o;}
#endif

/* All native placement/skeleton writers have completed for every actor. */
void Port_StoryFinalizePairs(void){
 if(st_chapter()!=6)return;
 if(sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0&&sStEventPost){int i,v=Port_StoryCastActor(9);extern void Port_StoryIdlePointBase(int);st_scene_take(v,1);BtlCtrl_SetPos(v,&sStFireBy);BtlCtrl_SetRot(v,&sStFireRot);Port_StoryIdlePointBase(st_obj(v));st_ch5_ground_pose(v,sStFireBy.y);for(i=0;i<6;i++){int a=Port_StoryCastActor(i);st_scene_take(a,1);BtlCtrl_SetPos(a,&sStFireGroup[i]);BtlCtrl_SetRot(a,&sStFireGroupRot[i]);Port_StorySampleMotion(a,0,0);st_ch5_ground_pose(a,sStFireGroupFloor[i]);}st_scene_take(Port_StoryCastActor(6),0);}
 if(sStYamchaCraterReady&&!sStYamchaCorpse&&sStAtkFrames>0&&sStAtkEvent==1){int y=Port_StoryCastActor(1);Vec4 at=sStYamchaCraterCenter,eye,r;float h=st_height(y);BtlCharApi_GetRot(st_obj(y),&r);at.y=sStYamchaFloor-h*.12f;eye=at;eye.x+=sinf(r.y+.9f)*h*1.9f;eye.z+=cosf(r.y+.9f)*h*1.9f;eye.y-=h*1.0f;sStCamDrift=0;st_camera_finish(eye,at,h,y,5);}
 if(sStAtkFrames>0&&sStAtkEvent==3&&sStAtkBy>=0&&sStAtkAt>=0){extern void Port_StoryAttachBack(int,int,float);Port_StoryAttachBack(st_obj(sStAtkBy),st_obj(sStAtkAt),st_height(sStAtkAt)*.19f);{Vec4 at,eye;float h=st_height(sStAtkAt),dx,dz,len;Vec4 back,torso;BtlCharApi_GetNodePos(st_obj(sStAtkBy),17,&back);BtlCharApi_GetNodePos(st_obj(sStAtkAt),17,&torso);dx=back.x-torso.x;dz=back.z-torso.z;len=sqrtf(dx*dx+dz*dz);if(len<.001f){dx=sinf(sStChaoRot.y);dz=cosf(sStChaoRot.y);len=1;}dx/=len;dz/=len;BtlCharApi_GetNodePos(st_obj(sStAtkBy),0x2E,&at);eye=at;eye.x+=(dx*.55f+dz*1.05f)*h;eye.z+=(dz*.55f-dx*1.05f)*h;eye.y-=h*.12f;sStCamDrift=0;st_camera_finish(eye,at,h,sStAtkAt,3);}}
 if(sStYamchaCorpse){extern void Port_StoryMoveCapturedBody(int,const Vec4*);int y=Port_StoryCastActor(1);Vec4 p,d;BtlCharApi_GetNodePos(st_obj(y),3,&p);d=sStYamchaCraterCenter;d.x-=p.x;d.z-=p.z;d.y=sStYamchaFloor-st_height(y)*.12f-p.y;if(d.y>-200&&d.y<200)Port_StoryMoveCapturedBody(st_obj(y),&d);}
 if(st_yamcha_mourns(Port_StoryCastActor(2))){st_scene_camera();}
 if(sStAtkFrames>0&&sStAtkEvent==0)st_ch8_fireworks_pose(sStAtkBy);
}

/* Last geometry writer, after native BtlObj_UpdateAll and before visibility. */
int Port_StoryTienArmBeatActive(void)
{
    return sStRun.active && sStRun.chapter == 6 && sStArmBeat;
}

int Port_StoryTienArmSceneActive(void){return sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+2&&sStArmCut;}
void Port_StoryFinalizeGeometry(void){
 if(st_chapter()==11&&sStAtkFrames>0&&sStAtkEvent==13){
  int age=sStAtkFrames-ST_CH9_KIENZAN_CUT;
  if(age<0)st_ch9_kienzan_cut(sStAtkFrames,1);
  if(age>=ST_CH9_CRUSH_BRACE&&age<ST_APE_FALL_FRAMES+ST_CH9_APE_IMPACT_HOLD)st_ch9_crush_pose(age);
  return;
 }
 if(st_chapter()!=6)return;
 if(sStSv>=0){int j;Port_StoryRestoreEndingPlacement(sStSv);st_scene_camera();for(j=0;j<stCasts[6].count;j++){int a=sStRun.cast[j];void*o=a>=0?BtlObj_Get(st_obj(a)):0;if(a>=0&&a!=sStSv&&o)BtlObj_SetPartVisible(o,0,0);}return;}
 if(sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1){st_ch8_end_pair();st_ch8_end_camera();}
 if(st_shield_spread(sStRun.cast[3])){st_shield_reaction(sStRun.cast[3]);}
 /* Match the grounded gameplay anchor in the last model pass as well. */
 if(sStAtkFrames>0&&sStAtkEvent==7){
  int gohan=Port_StoryCastActor(4);
  if(gohan>=0&&gohan!=sStAtkAt){
   BtlCtrl_SetPos(gohan,&sStShieldGohanAnchor);
   st_face(gohan,sStAtkBy);
   Port_StoryCommitSampledRoot(gohan);
   Port_StorySampleMotion(gohan,ST_MOTION_IDLE,0);
   st_ch5_ground_pose(gohan,sStShieldGohanAnchor.y);
  }
 }
 st_shield_observers();
 if(st_shield_post_owned(-1)){st_shield_post_pose(sStRun.cast[3]);st_shield_post_pose(sStRun.cast[4]);}
 if(sStRescueActive||(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8)){
   st_rescue_pair();
   st_rescue_exhaust();
 }
 if(sStKienActive)st_kien_geometry();
 if(sStAssaultActive){st_assault_pair();st_assault_shots();}
 if(sStElbowActive){st_ch8_elbow_pair();if((sStElbowFrame==171||sStElbowFrame==179)&&sStElbowBloodFrame!=sStElbowFrame){sStElbowBloodFrame=sStElbowFrame;Port_StoryMouthBlood(sStRun.cast[3],sStElbowFrame==171?384:192);Port_StoryFaceFrame(sStRun.cast[3],21);}}

 /* Evaluate the visible damaged stump after native model/pose writers. World
    particles follow the wound, never the centre of the screen. */
 if(Port_StoryTienArmSceneActive()){
  int age=sStArmBloodAge++,t=sStRun.cast[0];
  if(age>=9&&age%9==0){
   extern int Port_StoryVisualObjectId(int,int);extern void Port_Fx_WorldSpit(const Vec4*,const Vec4*,int);
   int obj=Port_StoryVisualObjectId(t,st_obj(t));Vec4 p,upper,d;float h=st_height(t),len;
   BtlCharApi_GetNodePos(obj,34,&p);BtlCharApi_GetNodePos(obj,33,&upper);
   d=(Vec4){p.x-upper.x,h*.35f,p.z-upper.z,sStArmAt.y};
   len=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);
   if(st_finite_pos(&p)&&len>.001f){
    d.x/=len;d.y/=len;d.z/=len;p.w=1.f;
    Port_Fx_WorldSpit(&p,&d,(int)(h*192));
    if(age==9)Port_Dbg3("story Tien wound bleeding",t,obj,(int)(p.y*100),age);
   }
  }
 }
 if(sStAtkFrames>0&&sStAtkEvent==0){int i;for(i=0;i<6;i++){int a=Port_StoryCastActor(i);BtlCtrl_SetPos(a,&sStFireGroup[i]);BtlCtrl_SetRot(a,&sStFireGroupRot[i]);Port_StorySampleMotion(a,0,0);st_ch5_ground_pose(a,sStFireGroupFloor[i]);}}
 if(sStChaoPair||(sStAtkFrames>0&&sStAtkEvent==3)){extern void Port_StoryAttachBack(int,int,float);int by=sStChaoPair?sStChaoBy:sStAtkBy,at=sStChaoPair?sStChaoAt:sStAtkAt;if(by>=0&&at>=0)Port_StoryAttachBack(st_obj(by),st_obj(at),st_height(at)*.19f);}
 if(sStYamchaCraterReady&&!sStYamchaCorpse&&sStAtkFrames>0&&sStAtkEvent==1){extern void Port_StoryOffsetVisual(int,const Vec4*);int y=Port_StoryCastActor(1);Vec4 p,d;BtlCharApi_GetNodePos(st_obj(y),3,&p);d=sStYamchaCraterCenter;d.x-=p.x;d.z-=p.z;d.y=sStYamchaFloor-st_height(y)*.12f-p.y;if(d.y>-200&&d.y<200)Port_StoryOffsetVisual(st_obj(y),&d);}
}

int Port_StoryExecutionAura(int obj){return st_chapter()==6&&sStAtkFrames>0&&sStAtkEvent==0&&obj==st_obj(sStAtkBy);}

void Port_StoryYamchaWarning(int actor){if(st_chapter()!=6||sStAtkFrames<=0||sStAtkEvent!=1||actor!=Port_StoryCastActor(1)||sStYamchaWarning)return;sStYamchaWarning=1;Port_StorySubtitle(6,ST_SET_EVENT0+1,1);Port_Dbg3("story Yamcha warning during native ultimate",actor,sStAtkFrames,0,0);}

/* Authored extra-fighter attacks must use their own skill-name script. */
int Port_StoryTechniqueActor(void){if(st_chapter()!=6)return -1;if(sStAtkFrames>0&&sStAtkBy>=0)return sStAtkBy;if(sStRun.active&&sStRun.set==ST_SET_CANON&&sStRun.index==1)return Port_StoryCastActor(9);return -1;}

int Port_StorySbcProfileActor(int actor){return st_chapter()==2&&sStBeamFrames>0&&actor==sStBeamPartner;}
static int st_manual_ultimate_profile(int ch, int role)
{
    const StActor *cast = &stCasts[ch].actor[role];
    if (cast->chara == 22 && cast->costume == 0 && (ch == 2 || ch == 6)) return 880;
    if (cast->chara == 25 && cast->costume == 1 &&
        (ch == 1 || ch == 5 || ch == 6 || ch == 11 || ch == 21)) return 1000;
    return 0;
}

int Port_StoryManualUltimateProfile(int actor)
{
    int ch = st_chapter(), role, profile;
    extern const void *Port_StoryGameplaySuper(int), *Port_StoryUltimateEffectPack(int);
    extern const void *Port_StoryGameplayClipBytes(int, int);

    if (ch < 0 || actor < 0) return 0;
    for (role = 0; role < stCasts[ch].count; ++role) {
        if (Port_StoryCastActor(role) != actor) continue;
        profile = st_manual_ultimate_profile(ch, role);
        if (profile && Port_StoryGameplaySuper(profile) && Port_StoryUltimateEffectPack(profile) &&
            Port_StoryGameplayClipBytes(profile == 880 ? (ch == 2 ? 897 : 896) : 1016, 329)) return profile;
        break;
    }
    return 0;
}

static int st_manual_ultimate_free(void)
{
    return !sStRun.active && sStEventRun < 0 && sStAtkFrames <= 0 &&
        sStBeamFrames <= 0 && sStMoonFrames <= 0;
}

int Port_StoryManualUltimateActive(int actor)
{
    extern int Port_StoryUltimateFxReady(int);
    return st_manual_ultimate_free() && Port_StoryManualUltimateProfile(actor) &&
        Port_StoryUltimateFxReady(actor);
}

static int st_manual_ultimate_object(void *obj)
{
    int ch = st_chapter(), role, actor;
    if (!obj || ch < 0 || !st_manual_ultimate_free()) return 0;
    for (role = 0; role < stCasts[ch].count; ++role) {
        if (!st_manual_ultimate_profile(ch, role)) continue;
        actor = Port_StoryCastActor(role);
        if (actor >= 0 && obj == BtlObj_Get(st_obj(actor))) {
            return Port_StoryManualUltimateActive(actor) ? Port_StoryManualUltimateProfile(actor) : 0;
        }
    }
    return 0;
}

int Port_StoryGameplayBankForObject(void *obj, int motion)
{
    int profile, actor, ch = st_chapter();
    /* Keep the accepted CH3 manual bank for all of its existing slots.
       897 resolves its historical overlay when present, or the base bank. */
    if (ch == 2 && obj && motion >= 261 && motion < 363 &&
        st_manual_ultimate_free()) {
        actor = Port_StoryCastActor(1);
        if (actor >= 0 && obj == BtlObj_Get(st_obj(actor))) return 897;
    }
    if (motion < 329 || motion >= 363) return 0;
    profile = st_manual_ultimate_object(obj);
    return profile == 880 ? 896 : profile == 1000 ? 1016 : 0;
}

/* Keep the native description, button masks and every row except the ultimate title. */
void *Port_StoryUltimateText(void *obj, void *original, int caption)
{
    enum { TEXT_CAP = 8192 };
    static u16 buffers[2][TEXT_CAP];
    const u16 *text = original;
    const char *name;
    int profile = st_manual_ultimate_object(obj);
    int n = 0, i, row = 0, tag, rowTag = -1, nameStart = 0, nameEnd = 0, len = 0;
    u16 *out;
    extern int Port_Setting(const char *, int);
    extern const void *Port_StoryUltimateFallbackText(int);

    if (!profile) return original;
    if (!text) text = Port_StoryUltimateFallbackText(profile);
    if (!text) return original;
    /* The native script ends at its @ line. Some model sections have no NUL
       before the next section, so stop at the same marker as the native reader. */
    for (i = 1; i < TEXT_CAP;) {
        int end = i;
        while (end < TEXT_CAP && text[end] && text[end] != '\n') ++end;
        if (end == TEXT_CAP) return original;
        if (end == i && !text[end]) break;
        tag = i;
        if (text[tag] == '!' && tag + 2 < end) tag += 2;
        if (text[tag] == '&' && tag + 4 < end) tag += 4;
        if (tag < end && text[tag] == '@') {
            n = end;
            break;
        }
        if (tag + 4 < end && text[tag] == '*' && ++row == 5) {
            rowTag = tag;
            nameStart = tag + 4;
            nameEnd = end;
            if (text[nameEnd - 1] == '\r') --nameEnd;
        }
        if (!text[end]) break;
        i = end + 1;
    }
    if (rowTag < 0 || n <= nameEnd) return original;
    if (Port_Setting("mod_language", 1) == 1) name = profile == 880 ? "Makankosappo" : "Kienzan";
    else name = profile == 880 ? "Special Beam Cannon" : "Destructo Disc";
    while (name[len]) ++len;
    if (nameStart + len + n - nameEnd >= TEXT_CAP) return original;
    out = buffers[caption != 0];
    memcpy(out, text, nameStart * sizeof(*out));
    out[rowTag + 2] = profile == 880 ? '5' : '4';
    for (i = 0; i < len; ++i) out[nameStart + i] = (u16)name[i];
    memcpy(out + nameStart + len, text + nameEnd, (n - nameEnd) * sizeof(*out));
    out[nameStart + len + n - nameEnd] = 0;
    return out;
}
