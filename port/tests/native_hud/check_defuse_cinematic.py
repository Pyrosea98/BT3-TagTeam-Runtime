import subprocess,tempfile
from pathlib import Path
# The actual dance clock with the defuse cinematic (port/include/mod_defuse_cinematic.inc), driven through whole
# timed defusions by a small model of the engine: requested actions start on the next update and call
# Port_DefuseStyle on entry as BtlAct_TransformA does. The fusion-style 0xEC plays 0x1F, holds 0x20 until the reload
# callback, then closes with stagger 0xB3; the stock 0xEC (no fusion clips) plays 0x177 / 0x178 / 0x179. Stagger 0xC2
# plays its clip and ends in idle 0xB.
r=Path.cwd();body=(r/'port/include/mod_fusion_native.inc').read_text()
head=r'''#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef int s32;typedef float f32;typedef unsigned char u8;
#define PORT 1
struct Vec4 {float x,y,z,w;};
#include "mod_defuse_crack.h"
struct BtlActJForm {int index,chara,costume,cost,kind,variant,animChara,unk1C,voiceChara,unk24,unk28,partner,unk30,allowed;};
struct BtlActJChr {int player,pad,side,objId;unsigned char raw[0x12cc-16];BtlActJForm form;unsigned char tail[0x1600-0x1304];};
struct BtlActJMember {int chara,costume;};struct BtlActJGauge {int health;};
struct BtlActJPose {Vec4 pos,rot;unsigned char rest[0x90];};
enum {N=3,LEADER=0,FOE=1,PARTNER=2};
static BtlActJChr actors[N];static BtlActJPose poses[N];
static int action[N],pending[N],anim[N],animFrames[N],requests[N][0x100],voices[N],hold[N],cinematic=1,clipMissing=-1,fusionClips=1,released,releasedIn=-1;
static float white[N];static int whiteCalls[N];
static int cuts,cutWho[16],cutTime[16],cutFlags[16],cutTrack[16];static float cutDist[16],cutYaw[16];
int rd(void *p,int off){return *(int*)((char*)p+off);}void wr(void *p,int off,int v){*(int*)((char*)p+off)=v;}
BtlActJMember *BtlMember_Get(BtlActJChr *c,int i){return (BtlActJMember*)((char*)c+0x9a4+i*0xa4);}
BtlActJMember *BtlMember_GetActive(BtlActJChr *c){return BtlMember_Get(c,rd(c,0x994));}
BtlActJGauge *BtlMember_GetActiveGauge(BtlActJChr *c){return (BtlActJGauge*)((char*)BtlMember_GetActive(c)+0x40);}
int Port_Setting(const char *key,int def){if(!strcmp(key,"fusion_duration_seconds"))return 1;if(!strcmp(key,"fusion_time_by_form"))return 0;if(!strcmp(key,"fusion_defuse_cinematic"))return cinematic;return def;}
void Port_FusionConsentReset(){}int Port_FusionPartnerPad(int,int){return -1;}
int Port_NetActive(){return 0;}int BattleReplay_IsActive(){return 0;}int BtlChar_GetCount(){return N;}BtlActJChr *BtlChar_Get(int i){return i>=0&&i<N?actors+i:0;}
int BtlChar_IsFrozen(BtlActJChr*){return 0;}int ChrCam_IsCutActive(BtlActJChr*){return 0;}int BtlSeq_GetState(){return 3;}
int BtlAct_GetCurrent(BtlActJChr *c){return action[c-actors];}int BtlChar_IsFree(BtlActJChr *c){return action[c-actors]==0xB;}int Port_ModelUsable(int,int,int){return 1;}
void BtlAct_Request(BtlActJChr *c,int a){pending[c-actors]=a;requests[c-actors][a&0xFF]++;}
int Port_ExtraFusionRelease(int who,int member,const void*,const void*){assert(who==LEADER&&member==1);released++;releasedIn=action[LEADER];
 poses[PARTNER].pos=poses[LEADER].pos;poses[PARTNER].pos.x+=10;BtlAct_Request(actors+PARTNER,0xB);return 1;}
BtlActJPose *BtlChar_GetPos(BtlActJChr *c){return poses+(c-actors);}
int BtlAnim_GetId(BtlActJChr *c){return anim[c-actors];}
void BtlChar_SetFlag(BtlActJChr *c,int bit){assert(bit==0x125);hold[c-actors]++;}
float BtlUtil_WrapAngle(float a){while(a>3.14159265f)a-=6.2831853f;while(a<-3.14159265f)a+=6.2831853f;return a;}
int Port_DefuseClipOk(int object,int clip){if(clip==0x1F||clip==0x20)return fusionClips;assert(clip==0xB3||clip==0xB4||clip==0xB1);return !(object==clipMissing&&clip==0xB1);}
void Port_DefuseWhiten(int object,float strength){assert(strength>=0&&strength<=0.9001f);white[object]=strength;whiteCalls[object]++;}
int Port_DefusePreviewLink(int,int,int){return 0;}static int demoActive,heldOn,heldTimes;int DemoCam_IsActive(){return demoActive||heldOn;}void Port_DemoCamHold(int on){if(on){heldOn=1;heldTimes++;}else heldOn=0;}static int commits[N];
int Port_AssistSafePoint(int,float*p){return p[0]>-1000&&p[0]<1000;}float Mathf_Sin(float a){return sinf(a);}float Mathf_Cos(float a){return cosf(a);}
int Port_AssistCommitPosition(int who,const float*p,float yaw){poses[who].pos={p[0],p[1],p[2],1};poses[who].rot.y=yaw;commits[who]++;return 1;}
int Port_Team(int i){return i==FOE;}
int Port_FusionPartnerActor(int who,int member){return who==LEADER&&member==1?PARTNER:-1;}
float BtlCharApi_GetHeight(int){return 20;}
static int nodesMissing,nodeCalls;
int Port_DefuseNodeFrame(int obj,int node,float *pos,float *front,float *right){nodeCalls++;if(nodesMissing)return 0;
 assert(node==48||node==2);pos[0]=poses[obj].pos.x;pos[1]=node==48?-16.5f:-10.1f;pos[2]=poses[obj].pos.z;front[0]=0;front[1]=0;front[2]=-1;right[0]=1;right[1]=0;right[2]=0;return 1;}
/* the fused model's surface crack: 19 front points from the head (-18.9) down to the hips (-10.1), zigzag, facing -z */
static int builds,placeFails;
int Port_DefuseCrackBuild(int obj,PortCrackPt *out,int max){builds++;if(nodesMissing)return 0;assert(max>=19);
 for(int k=0;k<19;k++){PortCrackPt &q=out[k];memset(&q,0,sizeof(q));q.node=17;q.parent=16;q.w=1;q.p[0]=(k%2?.4f:-.4f)*(k&&k<18);q.p[1]=-18.9f+8.8f*k/18;q.p[2]=-1;q.n[2]=-1;}return 19;}
int Port_DefuseCrackPlace(int obj,const PortCrackPt *q,float *pos,float *normal){if(placeFails)return 0;
 for(int i=0;i<3;i++){pos[i]=q->p[i]+(i==0?poses[obj].pos.x:i==2?poses[obj].pos.z:0);normal[i]=q->n[i];}return 1;}
static int viewBack;
int Port_ModHudProjectFine(int view,Vec4 *p,int *x,int *y){if(view>0)return 0;*x=(int)((viewBack?-p->x:p->x)*160+4096);*y=(int)(p->y*160+3584);return 1;}
void BtlCharApi_GetNodePos(int id,int node,Vec4 *out){assert(node==3);*out=poses[id].pos;out->y-=12;}
void BtlChar_PlayVoice(BtlActJChr *c,int kind){assert(kind==1);voices[c-actors]++;}
void ChrCam_SetCut(void *who,Vec4 *a,Vec4*,Vec4 *b,Vec4*,Vec4 *c,Vec4*,s32 track,f32 yaw,f32,f32 pitch,f32,f32 dist,f32,s32,s32,s32,s32 time,s32 flags){
 assert(cuts<16);cutWho[cuts]=(BtlActJChr*)who-actors;cutTime[cuts]=time;cutFlags[cuts]=flags;cutTrack[cuts]=track;cutDist[cuts]=dist;cutYaw[cuts]=yaw;
 assert(a->x==b->x&&b->x==c->x&&fabsf(a->x-5)<.001f&&pitch<0);cuts++;}
'''
test=r'''
static int lightGrow,lightWhite,lightOn,style[N];static int s_max(int a,int b){return a>b?a:b;}static unsigned char members[N][5*0xa4];
static void engine(){ /* one fight update */
 for(int i=0;i<N;i++){
  if(pending[i]){action[i]=pending[i];pending[i]=0;animFrames[i]=0;
   if(action[i]==0xEC){style[i]=Port_DefuseStyle(actors+i,0);anim[i]=style[i]?0x1F:0x177;}
   else anim[i]=action[i]==0xC2?0xB3:action[i]==0xC3?0xB4:0;}
  animFrames[i]++;
  if(action[i]==0xEC){
   if(anim[i]==0x1F&&animFrames[i]>20){anim[i]=0x20;animFrames[i]=0;}
   if(anim[i]==0x177&&animFrames[i]>20){anim[i]=0x178;animFrames[i]=0;}
   if(anim[i]==0x179&&animFrames[i]>25){action[i]=0xB;anim[i]=0;}
   if(anim[i]==0xB4&&style[i])BtlAct_Request(actors+i,0xB); /* the fusion-style closing ends the change action at once */
   assert(Port_DefuseStyle(actors+i,1)==style[i]); /* decided on entry, kept for the action */
  }
  if((action[i]==0xC2||action[i]==0xC3)&&animFrames[i]>40)BtlAct_Request(actors+i,0xB); /* the idle follow-up at the clip's end */
 }
 Port_FusionClockTick();
 lightOn=Port_DefuseLight(actors+LEADER,&lightGrow,&lightWhite);
}
static void setup(){
 memset(actors,0,sizeof(actors));memset(poses,0,sizeof(poses));memset(action,0,sizeof(action));memset(pending,0,sizeof(pending));
 memset(requests,0,sizeof(requests));memset(commits,0,sizeof(commits));memset(voices,0,sizeof(voices));memset(hold,0,sizeof(hold));memset(white,0,sizeof(white));memset(whiteCalls,0,sizeof(whiteCalls));
 cuts=released=0;releasedIn=-1;heldOn=heldTimes=0;
 for(int i=0;i<N;i++){actors[i].player=i;actors[i].objId=i;action[i]=0xB;}
 actors[LEADER].form.allowed=1;Port_FusionClockReset();
 for(int i=0;i<2;i++){void *m=BtlMember_Get(actors,i);wr(m,0,i?53:52);wr(m,8,1);wr(m,0x40,50000);wr(m,0x44,100000);}
 wr(BtlMember_Get(actors+PARTNER,0),0x40,40000);wr(BtlMember_Get(actors+FOE,0),0x40,90000);
 Port_FusionClockCapture(actors,1,0xF1);wr(BtlMember_GetActive(actors),0,54);wr(BtlMember_GetActive(actors),0x40,110000);wr(BtlMember_GetActive(actors),0x44,200000);
 poses[LEADER].rot.y=.5f;
}
/* Runs one timed defusion; returns the updates from the expiry request until the cinematic is over. */
static int run(int reloadAt){
 int u,expiry=-1,loadedAt=-1,grewAt=-1,whiteAt=-1,releaseAt=-1;float peak=0;
 for(u=0;u<4000;u++){
  engine();
  if(expiry<0&&requests[LEADER][0xEC]){expiry=u;assert(cinematic?lightOn:!lightOn);}
  if(expiry>=0&&u-expiry==reloadAt){wr(BtlMember_GetActive(actors),0,52);Port_FusionClockOnReload(actors);}
  if(expiry>=0&&loadedAt<0&&(anim[LEADER]==0x20?Port_DefuseReady(actors+LEADER):anim[LEADER]==0x178)&&u-expiry>reloadAt){if(style[LEADER])Port_DefuseHoldCamera(actors+LEADER);anim[LEADER]=style[LEADER]?0xB4:0x179;animFrames[LEADER]=0;loadedAt=u;} /* loaded: closing */
  if(lightOn&&lightGrow==1000&&grewAt<0)grewAt=u-expiry;
  if(lightOn&&lightWhite==1000&&whiteAt<0)whiteAt=u;
  if(white[LEADER]>peak)peak=white[LEADER];
  if(released&&releaseAt<0){releaseAt=u;assert(!lightOn);for(int i=0;i<N;i++)memcpy(members[i],(char*)(actors+i)+0x9a4,sizeof(members[i]));}
  if(releaseAt>=0&&!sDefuse[LEADER].stage&&action[LEADER]==0xB&&action[PARTNER]==0xB&&!pending[LEADER]&&!pending[PARTNER])break;
 }
 assert(expiry>=0&&releaseAt>0);
 if(cinematic){assert(grewAt==24);assert(peak>.899f);if(cuts)assert(white[LEADER]==0&&white[PARTNER]==0);} /* white, then back to colour (a skipped beat 3 leaves the stock flash to expire in 0.2 s) */
 else assert(!whiteCalls[LEADER]&&!whiteCalls[PARTNER]);
 return u-expiry;
}
static void presentation_only(){
 for(int i=0;i<N;i++)assert(!memcmp(members[i],(char*)(actors+i)+0x9a4,sizeof(members[i]))); /* no health or member writes */
 assert(rd(BtlMember_Get(actors+FOE,0),0x40)==90000&&rd(BtlMember_Get(actors+PARTNER,0),0x40)==40000);
}
static void beat3(int fusionStyle){
 /* the partner (on the leader's +x side, sliding to its own +x) reacts in stock stagger 0xC2 (0xB3) with its own voice;
    the leader, sliding to its -x, in the mirrored one: its fusion-style closing 0xB4, or 0xC3 on the stock path */
 assert(requests[PARTNER][0xC2]==1&&!requests[PARTNER][0xC3]&&!requests[FOE][0xC2]&&voices[PARTNER]==1&&!voices[FOE]);
 assert(requests[LEADER][0xC3]==1&&!requests[LEADER][0xC2]&&voices[LEADER]==(fusionStyle?0:1));
 assert(releasedIn==0xB); /* shown once the change action is over: while it runs only its fighter is drawn */
 /* one screen throughout: without a reveal camera, the two-fighter cut on the leader's camera only, the whole screen
    (0x40), set again the update after the split; with one, its view is held instead and released at the end */
 if(!demoActive){assert(cuts==2&&cutWho[0]==LEADER&&cutWho[1]==LEADER&&!heldTimes);
  for(int i=0;i<2;i++){assert(cutTime[i]==(i<1?45:43)&&cutFlags[i]==0x45&&cutTrack[i]==-1);assert(cutDist[i]>=32&&cutDist[i]<=60);assert(fabsf(cutYaw[i]-BtlUtil_WrapAngle(.5f+3.14159265f))<.001f);}}
 else assert(!cuts&&heldTimes==(fusionStyle?1:0));
 assert(!heldOn);
 /* slid apart by place only, 3 units shrinking by 0.35 an update (14.4 in all), facing unchanged */
 assert(commits[LEADER]==9&&commits[PARTNER]==9&&!commits[FOE]);
 assert(fabsf(poses[LEADER].pos.x+14.4f)<.01f&&fabsf(poses[PARTNER].pos.x-24.4f)<.01f&&fabsf(poses[LEADER].pos.z)<.01f);
 assert(poses[LEADER].rot.y==.5f&&poses[PARTNER].rot.y==0);
 assert(hold[LEADER]>=42&&hold[LEADER]<45&&hold[PARTNER]==hold[LEADER]&&!hold[FOE]);
 assert(whiteCalls[PARTNER]>=13); /* the partner comes out of the white too */
 presentation_only();
}
int main(){
 for(int reload=10;reload<=300;reload*=3){
  setup();int total=run(reload);beat3(1);assert(total<900);
  printf("  fusion style, reload after %d updates: the defusion with its cinematic took %d updates (hold bound 900)\n",reload,total);
 }
 /* a body without the fusion clips keeps the stock strain for beats 1-2 */
 demoActive=1;setup();run(30);beat3(1);demoActive=0; /* the fusion camera held through the reactions */
 /* the held view also goes when the cinematic is cut short */
 demoActive=1;setup();for(int u=0;u<400&&!released;u++){engine();if(action[LEADER]==0xEC&&anim[LEADER]==0x20&&sFusionReceipts[0].state==2&&Port_DefuseReady(actors+LEADER)){
   wr(BtlMember_GetActive(actors),0,52);Port_FusionClockOnReload(actors);Port_DefuseHoldCamera(actors+LEADER);anim[LEADER]=0xB4;animFrames[LEADER]=0;}}
 assert(heldOn);engine();wr(BtlMember_Get(actors+PARTNER,0),0x40,0);engine();assert(!heldOn&&!sDefuse[LEADER].stage);demoActive=0;
 setup();demoActive=1;for(int u=0;u<200&&!heldOn;u++){engine();if(action[LEADER]==0xEC&&anim[LEADER]==0x20&&Port_DefuseReady(actors+LEADER))Port_DefuseHoldCamera(actors+LEADER);}
 assert(heldOn);Port_FusionClockReset();assert(!heldOn);demoActive=0;
 fusionClips=0;setup();{int total=run(30);beat3(0);assert(total<900);printf("  stock strain fallback: %d updates\n",total);}fusionClips=1;
 /* Off: today's behaviour, no light, no white, no stagger, no camera cut */
 cinematic=0;setup();run(30);assert(released==1&&!requests[LEADER][0xC2]&&!requests[PARTNER][0xC2]&&!cuts&&!voices[LEADER]&&!hold[LEADER]&&releasedIn==0xB);cinematic=1;
 /* an empty partner stagger clip skips the push instead of ending it at once */
 clipMissing=PARTNER;setup();run(30);assert(released==1&&!requests[PARTNER][0xC2]&&!cuts&&!sDefuse[LEADER].stage);clipMissing=-1;
 /* the partner knocked out during beat 3: the cinematic ends at once */
 setup();for(int u=0;u<400&&!released;u++){engine();if(requests[LEADER][0xEC]&&action[LEADER]==0xEC&&anim[LEADER]==0x20&&!sFusionReceipts[0].hp){}
  if(action[LEADER]==0xEC&&anim[LEADER]==0x20&&sFusionReceipts[0].state==2&&Port_DefuseReady(actors+LEADER)){wr(BtlMember_GetActive(actors),0,52);Port_FusionClockOnReload(actors);anim[LEADER]=0xB4;animFrames[LEADER]=0;}}
 assert(released);engine();assert(sDefuse[LEADER].stage==2&&action[PARTNER]==0xC2);
 wr(BtlMember_Get(actors+PARTNER,0),0x40,0);engine();assert(!sDefuse[LEADER].stage);
 /* the leader knocked out under the light: the light goes */
 setup();for(int u=0;u<40&&!lightOn;u++)engine();for(int u=0;u<5;u++)engine();assert(lightOn);wr(BtlMember_GetActive(actors),0x40,0);engine();assert(!lightOn&&!sDefuse[LEADER].stage);
 /* a change action that never ends drops the cinematic under the hold bound */
 setup();int u;for(u=0;u<2000;u++){engine();if(lightOn)break;}int on=0;for(;u<2000;u++){engine();if(action[LEADER]==0xEC)anim[LEADER]=0x20;if(!lightOn)break;on++;}assert(on>=740&&on<=760);
 /* the crack: grows from the top of the head to the hips, forks after the main line passes them, dims as the model
    whitens, only where the front faces the camera, nothing without the skeleton or outside the cinematic */
 {setup();builds=0;int buf[6*48],grownAt=-1,maxSeg=0,seen=0;
  for(int u=0;u<200;u++){engine();int n=Port_DefuseCrack(actors+LEADER,0,buf,48);
   if(sDefuse[LEADER].stage!=1){assert(!n);if(seen)break;continue;}seen=1;
   if(n>maxSeg)maxSeg=n;
   for(int k=0;k<n;k++){int *s=buf+6*k;assert(s[4]>=0&&s[4]<=255&&s[5]>0);}
   if(sDefuse[LEADER].frames>=24&&grownAt<0){grownAt=u;int lowest=-100000;for(int k=0;k<n;k++){lowest=s_max(lowest,buf[6*k+1]);lowest=s_max(lowest,buf[6*k+3]);}
    assert(abs(lowest-(int)(-10.1f*160+3584))<40);} /* ends at the hips */
   if(sDefuse[LEADER].frames==3){assert(n>0&&n<6);} /* still near the head */
  }
  assert(grownAt>0&&maxSeg==18&&builds==1); /* the 19 surface points, built once */
  setup();for(int u=0;u<120&&!(sDefuse[LEADER].stage==1&&sDefuse[LEADER].frames>=20);u++)engine();
  assert(Port_DefuseCrack(actors+LEADER,0,buf,48)>0);viewBack=1;assert(Port_DefuseCrack(actors+LEADER,0,buf,48)==0);viewBack=0; /* seen from behind */
  assert(Port_DefuseCrack(actors+LEADER,1,buf,48)==0); /* a view that cannot see it */
  placeFails=1;assert(Port_DefuseCrack(actors+LEADER,0,buf,48)==0);placeFails=0;
  setup();nodesMissing=1;for(int u=0;u<120&&!(sDefuse[LEADER].stage==1&&sDefuse[LEADER].frames>=20);u++)engine();
  assert(Port_DefuseCrack(actors+LEADER,0,buf,48)==0&&Port_DefuseCrack(actors+LEADER,0,buf,48)==0);nodesMissing=0; /* a model that cannot be read: no crack, built once */
  assert(Port_DefuseCrack(actors+FOE,0,buf,48)==0&&Port_DefuseCrack(actors+LEADER,0,buf,2)<=2);
  printf("  crack: grown at update %d, up to %d segments\n",grownAt,maxSeg);}
 /* match reset */
 setup();for(int i=0;i<60&&!lightOn;i++)engine();assert(lightOn);Port_FusionClockReset();assert(!Port_DefuseLight(actors,&lightGrow,&lightWhite));
 puts("PASS actual defuse cinematic: fusion-style presentation decided on entry, surface crack to the crotch (built once from the model, front-facing only, dims) and a 0.9 white, both actors out of the white together, each thrown toward its own side (0xB3 / mirrored 0xB4) with its own voice, stage-checked opposite slide keeping both facings, two-fighter cuts, opponent hold, one screen (held reveal view or whole-screen cut, released at the end), no member/health writes, under the 900-update hold; stock-strain fallback, Off/empty-clip/KO/stuck/reset paths");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+body+test)
 subprocess.run(['g++','-fsanitize=address,undefined','-Iport/include',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
