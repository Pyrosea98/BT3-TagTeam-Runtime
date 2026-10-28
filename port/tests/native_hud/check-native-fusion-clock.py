import subprocess,tempfile
from pathlib import Path
r=Path.cwd();body=(r/'port/include/mod_fusion_native.inc').read_text()
head=r'''#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <initializer_list>
#include <math.h>
typedef int s32;typedef float f32;typedef unsigned char u8;
struct Vec4 {float x,y,z,w;};
#include "mod_defuse_crack.h"
#define PORT 1
struct BtlActJForm {int index,chara,costume,cost,kind,variant,animChara,unk1C,voiceChara,unk24,unk28,partner,unk30,allowed;};
struct BtlActJChr {int player,pad,side,objId;unsigned char raw[0x12cc-16];BtlActJForm form;unsigned char tail[0x1600-0x1304];};
struct BtlActJMember {int chara,costume;};struct BtlActJGauge {int health;};
static BtlActJChr actors[4];static int action,request,releaseCount,frozen,cut,replay,net,enabled=1,byForm,seconds=1,partnerPad=-1,controlMode=0,cooldownSec=10;
int rd(void *p,int off){return *(int*)((char*)p+off);}void wr(void *p,int off,int v){*(int*)((char*)p+off)=v;}
BtlActJMember *BtlMember_Get(BtlActJChr *c,int i){return (BtlActJMember*)((char*)c+0x9a4+i*0xa4);}
BtlActJMember *BtlMember_GetActive(BtlActJChr *c){return BtlMember_Get(c,rd(c,0x994));}
BtlActJGauge *BtlMember_GetActiveGauge(BtlActJChr *c){return (BtlActJGauge*)((char*)BtlMember_GetActive(c)+0x40);}
int Port_Setting(const char *key,int def){if(!strcmp(key,"fusion_dance_cooldown_seconds"))return cooldownSec;if(!strcmp(key,"coop_fusion_controls"))return controlMode;if(!strcmp(key,"coop_fusion_swap_seconds"))return 5;if(!strcmp(key,"fusion_duration_seconds"))return seconds;if(!strcmp(key,"fusion_duration_enabled"))return enabled;if(!strcmp(key,"fusion_time_by_form"))return byForm;return def;}
void Port_FusionConsentReset(){}
int Port_FusionPartnerPad(int,int){return partnerPad;}
int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlChar_GetCount(){return 4;}BtlActJChr *BtlChar_Get(int i){return actors+i;}
int BtlChar_IsFrozen(BtlActJChr*){return frozen;}int ChrCam_IsCutActive(BtlActJChr*){return cut;}int BtlSeq_GetState(){return 3;}
int BtlAct_GetCurrent(BtlActJChr*){return action;}int BtlChar_IsFree(BtlActJChr*){return 1;}int Port_ModelUsable(int,int,int){return 1;}
void BtlAct_Request(BtlActJChr*,int a){request=a;}int Port_ExtraFusionRelease(int,int,const void*,const void*){releaseCount++;return 1;}
/* the defuse cinematic (check_defuse_cinematic.py covers it); with no stagger clip its beat 3 never starts here */
struct BtlActJPose {Vec4 pos,rot;unsigned char rest[0x90];};static BtlActJPose poses[4];BtlActJPose *BtlChar_GetPos(BtlActJChr *c){return poses+(c-actors);}
int BtlAnim_GetId(BtlActJChr*){return 0;}void BtlChar_SetFlag(BtlActJChr*,int){}float BtlUtil_WrapAngle(float a){return a;}int Port_FightVsyncs(){return 2;}
int Port_DefuseClipOk(int,int){return 0;}void Port_DefuseWhiten(int,float){}int DemoCam_IsActive(){return 0;}void Port_DemoCamHold(int){}int Port_AssistSafePoint(int,float*){return 0;}int Port_DefuseNodeFrame(int,int,float*,float*,float*){return 0;}int Port_DefuseCrackBuild(int,PortCrackPt*,int){return 0;}int Port_DefuseCrackPlace(int,const PortCrackPt*,float*,float*){return 0;}int Port_ModHudProjectFine(int,Vec4*,int*,int*){return 0;}int Port_AssistCommitPosition(int,const float*,float){return 0;}float Mathf_Sin(float){return 0;}float Mathf_Cos(float){return 1;}int Port_DefusePreviewLink(int,int,int){return 0;}int Port_Team(int){return 0;}int Port_FusionPartnerActor(int,int){return 1;}float BtlCharApi_GetHeight(int){return 20;}void BtlCharApi_GetNodePos(int,int,Vec4*){}void BtlChar_PlayVoice(BtlActJChr*,int){}
void ChrCam_SetCut(void*,Vec4*,Vec4*,Vec4*,Vec4*,Vec4*,Vec4*,s32,f32,f32,f32,f32,f32,f32,s32,s32,s32,s32,s32){}
'''
test=r'''
void setup(){memset(actors,0,sizeof(actors));actors[0].form.allowed=1;request=releaseCount=frozen=cut=action=replay=net=0;Port_FusionClockReset();
 for(int i=0;i<2;i++){void *m=BtlMember_Get(actors,i);wr(m,0,i?53:52);wr(m,4,i?1:2);wr(m,8,1);wr(m,0x40,i?60000:50000);wr(m,0x44,100000);wr(m,0x4c,80000);wr(m,0x50,100000);wr(m,0x54,200000);wr(m,0x58,300000);}}
void committed(){Port_FusionClockCapture(actors,1,0xF1);void *m=BtlMember_GetActive(actors);wr(m,0,54);wr(m,0x40,110000);wr(m,0x44,200000);wr(BtlMember_Get(actors,1),8,0);}
int main(){setup();assert(!Port_FusionClockActive(0));Port_FusionClockCapture(actors,1,0xF2);assert(!Port_FusionClockActive(0));
 replay=1;committed();assert(!Port_FusionClockActive(0));setup();committed();assert(Port_FusionClockSeconds(0)==1);assert(Port_FusionClockActive(0));
 frozen=1;for(int i=0;i<90;i++)Port_FusionClockTick();assert(!request&&Port_FusionClockSeconds(0)==1);frozen=0;cut=1;Port_FusionClockTick();assert(!request);cut=0;
 for(int i=0;i<29;i++)Port_FusionClockTick();assert(!request);action=0xD0;Port_FusionClockTick();assert(!request);action=0;Port_FusionClockTick();assert(request==0xEC&&actors[0].form.chara==52&&actors[0].form.costume==2);
 /* Lost request: no callback, another action/transform takes over. Keep receipt,
    wait for native idle, then retry using current rather than stale gauges. */
 request=0;action=0xEC;Port_FusionClockTick();assert(!request);
 action=0xD0;Port_FusionClockTick();assert(!request);
 wr(BtlMember_GetActive(actors),0,230);Port_FusionClockOnReload(actors);assert(sFusionReceipts[0].state==2);
 wr(BtlMember_GetActive(actors),0x40,100000);action=0;Port_FusionClockTick();
 assert(request==0xEC&&actors[0].form.chara==52&&sFusionReceipts[0].hp==100000);
 wr(BtlMember_GetActive(actors),0x40,110000);Port_FusionClockTick();assert(sFusionReceipts[0].hp==110000);
 wr(BtlMember_GetActive(actors),0,52);Port_FusionClockOnReload(actors);assert(Port_FusionClockActive(0)&&releaseCount==0);
 action=0xEC;Port_FusionClockTick();assert(releaseCount==0);action=0xB;cut=1;Port_FusionClockTick();assert(!releaseCount);cut=0;Port_FusionClockTick();assert(!Port_FusionClockActive(0)&&releaseCount==1);
 assert(Port_FusionClockProgress(0)==0);
 void *m=BtlMember_GetActive(actors),*p=BtlMember_Get(actors,1);assert(rd(m,0)==52&&rd(m,4)==2&&rd(p,0)==53&&rd(p,4)==1&&rd(p,8)==1);
 assert(rd(m,0x40)==55000&&rd(p,0x40)==55000&&rd(m,0x44)==100000&&rd(m,0x4c)==40000&&rd(p,0x4c)==40000&&rd(m,0x74)==0);
 Port_FusionClockOnReload(actors);assert(releaseCount==1);
 /* Realistic KO/revival: retain remaining time and consumed partner, never
    expire while dead, then safely reload both originals after revival. */
 for(int who=0;who<4;who++)for(int cid:{48,49,50,110,53,249,172,54,224,225,226,229,230}){
  setup();byForm=0;seconds=1;actors[who]=actors[0];actors[who].player=who;actors[who].form.allowed=0;
  Port_FusionClockCapture(actors+who,1,0xF1);wr(BtlMember_GetActive(actors+who),0,cid);
  for(int n=0;n<9;n++)Port_FusionClockTick();int remaining=sFusionReceipts[who].remaining;
  wr(BtlMember_GetActive(actors+who),0x40,0);action=216;
  for(int n=0;n<90;n++)Port_FusionClockTick();
  assert(Port_FusionClockActive(who)&&sFusionReceipts[who].remaining==remaining&&!request&&!releaseCount);
  wr(BtlMember_GetActive(actors+who),0x40,20000);action=0xB;
  for(int n=0;n<21;n++)Port_FusionClockTick();assert(request==0xEC);
  wr(BtlMember_GetActive(actors+who),0,52);Port_FusionClockOnReload(actors+who);Port_FusionClockTick();
  assert(!Port_FusionClockActive(who)&&releaseCount==1&&rd(BtlMember_Get(actors+who,1),8)==1);
 }
 setup();committed();for(int n=0;n<30;n++)Port_FusionClockTick();assert(sFusionReceipts[0].state==2);
 wr(BtlMember_GetActive(actors),0x40,0);Port_FusionClockTick();assert(sFusionReceipts[0].state==1&&sFusionReceipts[0].remaining==0&&!Port_DefusionPresenting(0));
 wr(BtlMember_GetActive(actors),0x40,20000);request=0;action=0xB;Port_FusionClockTick();assert(request==0xEC);

 byForm=1;for(auto family: {0,1,2,3,4}){int ids[][5]={{214,216,218,219,-1},{215,220,221,222,223},{224,225,226,229,230},{51,52,166,149,59},{110,53,249,172,54}};for(int f=1;f<5&&ids[family][f]>=0;f++)assert(pf_rate(ids[family][f])>pf_rate(ids[family][f-1]));}
 for(int cid: {224,225,226,229,230,234,235,238,239,241}){byForm=1;setup();seconds=100;committed();wr(BtlMember_GetActive(actors),0,cid);int before=sFusionReceipts[0].remaining;Port_FusionClockTick();int rate=before-sFusionReceipts[0].remaining;assert(rate==pf_rate(cid));assert(Port_FusionClockProgress(0)>0);}
 seconds=1;byForm=0;
 for(int who=0;who<4;who++)for(int cid:{48,49,50,110,53,249,172,54,224,225,226,229,230}){
  setup();actors[who]=actors[0];actors[who].player=who;actors[who].form.allowed=0;
  Port_FusionClockCapture(actors+who,1,0xF1);assert(Port_FusionClockActive(who));wr(BtlMember_GetActive(actors+who),0,cid);
  for(int n=0;n<30;n++)Port_FusionClockTick();assert(request==0xEC&&actors[who].form.chara==52);
 }
 seconds=1;setup();byForm=1;committed();for(int i=0;i<18;i++)Port_FusionClockTick();assert(request==0xEC);
 setup();partnerPad=3;actors[0].pad=1;int move,attack;
 Port_FusionControlsCapture(actors,1);assert(Port_FusionControlPads(0,&move,&attack)&&move==1&&attack==1);
 for(int i=0;i<150;i++)Port_FusionControlsTick();assert(Port_FusionControlPads(0,&move,&attack)&&move==3&&attack==3);
 for(controlMode=1;controlMode<=5;controlMode++){assert(Port_FusionControlPads(0,&move,&attack));if(controlMode==1)assert(move==1&&attack==3);if(controlMode==2||controlMode==3)assert(move==3&&attack==1);if(controlMode==4)assert(move==1&&attack==1);if(controlMode==5)assert(move==3&&attack==3);}
 wr(BtlMember_GetActive(actors),0x40,0);int ticks=sFusionControls[0].ticks;for(int n=0;n<90;n++)Port_FusionControlsTick();assert(sFusionControls[0].active&&sFusionControls[0].ticks==ticks);
 wr(BtlMember_GetActive(actors),0x40,20000);Port_FusionControlsTick();assert(sFusionControls[0].active&&sFusionControls[0].ticks==ticks+1);
 replay=1;assert(!Port_FusionControlPads(0,&move,&attack));replay=0;net=1;assert(!Port_FusionControlPads(0,&move,&attack));net=0;
 Port_FusionClockReset();assert(!Port_FusionControlPads(0,&move,&attack));partnerPad=-1;Port_FusionControlsCapture(actors,1);assert(!Port_FusionControlPads(0,&move,&attack));
 setup();cooldownSec=10;pf_cooldown_start(0,1);assert(Port_DanceCooldown(0)==300&&Port_DanceCooldown(1)==300);
 frozen=1;for(int n=0;n<90;n++)Port_FusionClockTick();assert(Port_DanceCooldown(0)==300);frozen=0;cut=1;Port_FusionClockTick();assert(Port_DanceCooldown(0)==300);cut=0;
 for(int n=0;n<299;n++)Port_FusionClockTick();assert(Port_DanceCooldown(0)==1&&Port_DanceCooldown(1)==1);Port_FusionClockTick();assert(!Port_DanceCooldown(0)&&!Port_DanceCooldown(1));
 cooldownSec=0;pf_cooldown_start(0,1);assert(!Port_DanceCooldown(0));cooldownSec=500;pf_cooldown_start(0,1);assert(Port_DanceCooldown(0)==9000);net=1;assert(!Port_DanceCooldown(0));net=0;replay=1;assert(!Port_DanceCooldown(0));replay=0;
 Port_FusionClockReset();assert(!Port_DanceCooldown(0)&&!Port_DanceCooldown(1));assert(!Port_FusionClockActive(0));puts("PASS actual native dance clock: capture scope, pause/cinematic hold, safe expiry/reload, source costumes/stats/partner, form drain, KO and reset");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+body+test);subprocess.run(['c++','-Iport/include',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
