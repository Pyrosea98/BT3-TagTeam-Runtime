from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/tests/story/test_saga_chapters.c').read_text();s=s[:s.index('int main(')]
s=s.replace('static int sReady; s32 BtlStage_IsReady(void) { return sReady; }','static int sReady; s32 BtlStage_IsReady(void) { return 1; }')
s=s.replace('h->y = 0; return 0;', 'h->y = sTestFloor; return 0;')
s=s.replace('h->y = sProbeLedge&&p->x < -180.f ? -64.f : 0.f; return 0;', 'h->y = sTestFloor; return 0;')
s=s.replace('s32 BtlStage_ProbeGround(', 'static float sTestFloor;\ns32 BtlStage_ProbeGround(',1)
s=s.replace('void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { assert(isfinite(p->x) && isfinite(a->y)); }','static Vec4 sTestEye,sTestAt;void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { sTestEye=*p;sTestAt=*a;assert(isfinite(p->x) && isfinite(a->y)); }')
s=s.replace('*p = sPos[a]; *q = (Vec4){0, 0, 0, 1}; return 0;','*p = (Vec4){0,sTestFloor,0,1}; *q = (Vec4){0, 0, 0, 1}; return 0;')
s+=r'''
static void fresh_shot(int ch){MsView v={0};Port_ScenarioClear();Port_StoryChoose(ch);assert(Port_ScenarioDraft(&v));for(int i=0;i<N;i++)sTeam[i]=i<sScN?sScTeams[i]:0;fight_start();}
int main(void){BtlSeqTalkCtx ctx={0};int i,lead,enemy,hidden;Vec4 before[N],eye;
 fresh_shot(2);st_scene_begin(&ctx,2,ST_SET_INTRO,0,1,-1,0);assert(sStRun.active&&!Port_StoryFixedCameraOwned()&&!Port_StoryApplyFixedCamera());st_scene_end();
 /* Hand-authored moving moon/beam shots own their camera separately. Never
    replay a stale prior dialogue shot over those already-working sequences. */
 fresh_shot(2);sStRun.active=0;sStBeamFrames=1;assert(!Port_StoryFixedCameraOwned()&&!Port_StoryApplyFixedCamera());sStBeamFrames=0;
 fresh_shot(3);sStRun.active=0;sStMoonFrames=1;assert(!Port_StoryFixedCameraOwned()&&!Port_StoryApplyFixedCamera());sStMoonFrames=0;

 fresh_shot(5);lead=actor(0);enemy=actor(5);hidden=actor(10);
 for(i=0;i<N;i++)sPos[i]=(Vec4){i*1000.f,500.f,i*-700.f,1};
 st_scene_begin(&ctx,5,ST_SET_INTRO,0,stChapters[5].introCount,-1,0);
 assert(Port_StorySceneParked(hidden)&&!(sStRun.actors>>hidden&1u));
 for(i=0;i<N;i++)if((sStRun.actors>>i&1u)&&i!=actor(11)&&i!=actor(12)){Vec4 to=sPos[sTeam[i]?lead:enemy];float dx=to.x-sPos[i].x,dz=to.z-sPos[i].z,d=sqrtf(dx*dx+dz*dz);
  assert(sPos[i].y==0&&fabsf(sPos[i].x)<150&&fabsf(sPos[i].z)<150&&d>1);
  assert((sinf(sRot[i].y)*dx+cosf(sRot[i].y)*dz)/d>.99f);}
 st_scene_beat(&ctx);assert(sStRun.shot==0&&sTestEye.y<0);
 memcpy(before,sPos,sizeof(before));sStRun.index=1;ctx.step=ST_STEP_BEAT;st_scene_beat(&ctx);assert(sStRun.shot==2);
 assert(!memcmp(before,sPos,sizeof(before)));sStRun.index=2;ctx.step=ST_STEP_BEAT;st_scene_beat(&ctx);assert(sStRun.shot==1);
 sPos[hidden]=(Vec4){900000,900000,900000,1};st_scene_admit_speaker(hidden);assert(!Port_StorySceneParked(hidden)&&sPos[hidden].y==0&&fabsf(sPos[hidden].x)<150);
 st_scene_take(hidden,0);sStRun.actor=hidden;st_scene_camera();assert(sStRun.shotActor!=hidden);
 st_scene_end();assert(Port_StorySceneParked(hidden));st_scene_camera(); /* closed context cannot sample a stale/parked actor */
 /* Wide shots share terrain clearance, including a high terrain surface. */
 sTestFloor=-80;fresh_shot(5);st_scene_begin(&ctx,5,ST_SET_INTRO,0,1,-1,0);st_scene_beat(&ctx);assert(sStRun.shot==0&&sTestEye.y<=-84);st_scene_end();sTestFloor=0;
 /* The actual authored event chooses a solo charge and a travelling headbutt camera. */
 fresh_shot(2);st_event_start(2,1);sStRun.actor=sStBeamPartner;st_scene_camera();assert(sStRun.shot==3&&sStRun.shotActor==sStBeamPartner);
 st_scene_end();fresh_shot(2);st_event_start(2,0);sStGohanFrames=1;sStRun.actor=(int)sStEventTo.w;st_scene_camera();assert(sStRun.shot==4);eye=sTestEye;
 sPos[sStRun.actor].x+=8;st_scene_camera();assert(fabsf(sTestEye.x-eye.x-8)<.001f);assert(sTestEye.y<0);
 st_scene_end();
 /* The execution prelude is a specific interaction: Vegeta and Saibaman face one another. */
 fresh_shot(6);st_event_start(6,0);{const StEvent*e=&stEvents[6].event[0];int by=actor(e->attackCast-1),at=actor(e->attackTarget-1);float dx=sPos[at].x-sPos[by].x,dz=sPos[at].z-sPos[by].z;
 assert(dx*sinf(sRot[by].y)+dz*cosf(sRot[by].y)>0);assert(dx*sinf(sRot[at].y)+dz*cosf(sRot[at].y)<0);}
 /* Guides admitted by the first CH6 R3 are observers, clear of Yamcha and every fighter. */
 st_scene_end();sTestFloor=0;fresh_shot(5);st_event_start(5,0);
 for(int role=11;role<=12;role++){
  int guide=actor(role);st_scene_admit_speaker(guide);assert(sStRun.actors>>guide&1u);
  for(int k=0;k<stCasts[5].count;k++){int other=actor(k);if(other<0||other==guide||!(sStRun.actors>>other&1u))continue;
   float dx=sPos[guide].x-sPos[other].x,dz=sPos[guide].z-sPos[other].z;assert(dx*dx+dz*dz>=13.f*13.f);}
 }
 st_scene_end();
 /* The shield shot stays behind Gohan and contains the Nappa -> Piccolo -> Gohan line. */
 fresh_shot(6);Port_StoryEnterActor(actor(3));Port_StoryEnterActor(actor(4));Port_StoryEnterActor(actor(8));
 sPos[actor(3)].y=-1000;sPos[actor(4)].y=-800;
 st_attack_begin(6,7,actor(8));{
  assert(fabsf(sPos[actor(3)].y-sTestFloor)<.001f&&fabsf(sPos[actor(4)].y-sTestFloor)<.001f);
  Vec4 g=sPos[actor(4)],p=sPos[actor(3)],n=sPos[actor(8)];float dx=n.x-g.x,dz=n.z-g.z,d=sqrtf(dx*dx+dz*dz);
  assert(d>=6*st_height(actor(4))&&d<=10*st_height(actor(4)));
  assert((sTestEye.x-g.x)*dx+(sTestEye.z-g.z)*dz<0);
  assert((p.x-g.x)*dx+(p.z-g.z)*dz>0&&st_dist(&p,&g)>st_height(actor(4)));
  assert(Port_StoryOwnAttackCamera(st_obj(actor(8))));
 }
 sStAtkFrames=0;st_scene_end();
 puts("PASS real scene director:10-fighter formation/facing, no per-line teleports, parked speaker admission/cleanup, safe wide lens, face/shoulder/solo-charge/Gohan-follow shots and execution eyelines");return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'camera';p.with_suffix('.c').write_text(s);subprocess.run(['cc','-std=gnu99','-w','-DPORT','-fsanitize=address,undefined','-Iinclude','-Iport/include','-Iport/src',str(p.with_suffix('.c')),'-lm','-o',str(p)],check=True);subprocess.run([str(p)],check=True)
