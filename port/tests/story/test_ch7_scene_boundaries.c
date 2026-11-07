#define main saga_existing_main
#include "test_saga_chapters.c"
#undef main
int main(void){
 int v,n,y,sb,ki,pi;BtlSeqTalkCtx ctx={0};
 prepare_ch7_contacts();sReady=1;v=actor(9);n=actor(8);sPos[v]=(Vec4){-170,0,0,1};sPos[n]=(Vec4){-190,-64,0,1};sProbeLedge=1;assert(st_scene_flat_pair(v,n,1.3f));assert(fabsf(sPos[v].y-sPos[n].y)<1&&sPos[n].x>=-180);sProbeLedge=0;
 prepare_ch7_contacts();sReady=1;v=actor(9);sb=actor(7);sPos[sb].y=-2000;sHp[sb]=1;
 st_attack_begin(6,0,sb);assert(sPos[sb].y==0&&sPos[v].y==0&&sStAtkAt==sb);
 assert(fabsf(sPos[v].x-sPos[sb].x)+fabsf(sPos[v].z-sPos[sb].z)<40);
 prepare_ch7_contacts();sReady=1;y=actor(1);sb=actor(13);Port_StoryEnterActor(sb);sHp[sb]=1000;
 st_attack_begin(6,1,sb);assert(sStAtkAt==y&&sStAtkBy==sb);
 for(int f=0;f<ST_CLING_HOLD;f++)st_attack_update(6);
 assert(sStAtkFired&&Port_StorySceneActorShown(y)&&sHp[y]>0);
 prepare_ch7_contacts();sReady=1;ki=actor(2);pi=actor(3);n=actor(8);
 for(int k=0;k<3;k++)st_event_effects(6,&stEvents[6].event[k],actor(7));
 sStRun.active=1;sStRun.chapter=6;sStRun.actors=1u<<actor(0);sStEventRun=-1;sStAtkFrames=0;
 st_scene_end();assert(sStCh7ReleaseFrames==3);int kr=sReleaseCount[ki],pr=sReleaseCount[pi];
 sStEventsDone=(1u<<stEvents[6].count)-1;sStFightFrames=1;sStLastEventEnd=1;
 for(int f=0;f<3;f++)st_fight_update();assert(sReleaseCount[ki]>=kr+3&&sReleaseCount[pi]>=pr+3&&!sStCh7ReleaseFrames);
 assert(gPortTarget[ki]==n&&gPortTarget[pi]==n);
 prepare_ch7_contacts();sReady=1;sPos[actor(0)].x=333;sPos[actor(0)].z=777;sHp[actor(0)]=0;
 sStDownActors|=1u<<actor(0);sStGoneActors|=1u<<actor(0);st_attack_begin(6,8,actor(8));
 assert(sPos[actor(0)].x==333&&sPos[actor(0)].z==777);
 {Vec4 p=sPos[actor(4)],t=sPos[actor(0)];assert(hypotf(p.x-t.x,p.z-t.z)>15);}
 prepare_ch7_contacts();sReady=1;Port_StoryEnterActor(actor(9));
 st_scene_begin(&ctx,6,ST_SET_EVENT0+2,0,4,-1,0);
 for(int i=0;i<stCasts[6].count;i++){int a=actor(i);if(a>=0&&(sStRun.actors>>a&1u)&&sHp[a]>0)assert(sPos[a].y==0);}
 for(int i=0;i<4;i++){sStRun.actor=ST_LineCast(6,&stEvents[6].event[2].line[i]);sStRun.actor=actor(sStRun.actor);st_scene_camera();assert(isfinite(sStRun.exitEye.y)&&sStRun.exitEye.y<0);}
 st_scene_end();
 prepare_ch7_contacts();sReady=1;v=actor(9);n=actor(8);Port_StoryEnterActor(v);
 st_scene_begin(&ctx,6,ST_SET_CANON,0,4,0,0);assert(sMotionId[n]==ST_MOTION_PRONE&&sPos[n].y==0);
 assert((sStRun.actors>>n&1u)&&(sStRun.actors>>v&1u));
 assert(Port_StoryHeld(n)&&Port_StorySceneActorShown(n)&&Port_StoryScenePlacementOwned(n));
 sPos[n].y=-3000;Port_StoryRestoreEndingPlacement(n);assert(sPos[n].y==0);
 /* Previous-frame native displacement must never become the next anchor. */
 sPos[n].y=-64;sPos[v].y=-12;sStRun.frames=0;ctx.step=ST_STEP_HOLD;Ramp_Start(&ctx.timer,1000,0,1);
 st_scene_hold(&ctx);assert(sPos[n].y==0&&sPos[v].y==0);
 assert(sStEndingDrawAnchor[0].y==0&&sStEndingDrawAnchor[1].y==0);
 sStRun.index=1;sStRun.frames=0;ctx.step=ST_STEP_HOLD;Ramp_Start(&ctx.timer,1000,0,1);
 for(int f=0;f<30;f++)st_scene_hold(&ctx);assert(sMotionId[n]==ST_MOTION_PRONE&&sPos[n].y==0);
 for(int f=30;f<73;f++)st_scene_hold(&ctx);
 sPos[v].y=-1000;st_scene_hold(&ctx);assert(sPos[v].y==sStEndingVegetaAnchor.y);
 sStEndingExecutionHit=1;for(int f=0;f<25;f++)st_scene_hold(&ctx);assert(sHp[n]==0&&sPos[n].y==0&&sMotionId[n]==ST_MOTION_PRONE);
 puts("PASS CH7 airborne execution, paired Yamcha visibility, all-cast CPU release/target, preserved corpse/Masenko clearance, grounded dialogue shots and native ending anchors");
 return 0;
}
