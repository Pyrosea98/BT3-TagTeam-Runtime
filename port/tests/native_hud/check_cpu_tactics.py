from pathlib import Path
import subprocess,tempfile
r=Path.cwd();body=(r/'port/include/mod_cpu_tactics_native.inc').read_text()
head=r"""#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#define PORT 1
using s32=int;
struct BtlActJChr {int player;unsigned char rest[0x1600-4];};struct BtlActJMember{int chara,costume;};struct BtlActJGauge{int health,max,unused,ki,kiMax,blast;};
static BtlActJChr actors[3];static BtlActJMember members[3]={{3,0},{6,0},{46,0}};static BtlActJGauge gauges[3]={{100000,100000,0,100000,100000,300000},{100000,100000,0,100000,100000,300000},{100000,100000,0,100000,100000,300000}};
static int human[3]={0,0,0},scope=1,enemyScope=-1,preset,net,replay,mode,modMode,state=3,frozen,busy,blocked[4],admissions,act;
static int policyFrame;int BtlChar_FrameMod(int){return policyFrame;}static int denyAll,denyGiant,chance=100,exception,sizeClass;static int dest[4]={4,5,6,255},costs[4]={100000,200000,300000,0};
int Port_Setting(const char*k,int d){if(!strcmp(k,"disable_npc_transformations"))return denyAll;if(!strcmp(k,"disable_npc_giant_transformations"))return denyGiant;if(!strcmp(k,"npc_transform_chance_percent"))return chance;if(!strncmp(k,"cpu_transform_",14)&&k[14]>='0'&&k[14]<='9')return exception;if(!strcmp(k,"cpu_transform_allies"))return scope;if(!strcmp(k,"cpu_transform_enemies"))return enemyScope<0?scope:enemyScope;if(!strcmp(k,"cpu_tactics_preset"))return preset;return d;}
int Port_Beta8SizeClass(int,int){return sizeClass;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlSeq_GetState(){return state;}int Battle_GetMode(){return mode;}int Port_ModesFight(){return modMode;}
int Port_TargetHuman(int who){return human[who];}int Port_Team(int who){return who==2?0:who;}int Port_CamTarget(int who){return who==2?1:who^1;}int BtlChar_GetCount(){return 3;}BtlActJChr *BtlChar_Get(int who){return &actors[who];}
BtlActJMember *BtlMember_GetActive(BtlActJChr*c){return &members[c->player];}BtlActJGauge *BtlMember_GetActiveGauge(BtlActJChr*c){return &gauges[c->player];}
int BtlChar_IsFrozen(BtlActJChr*){return frozen;}int ChrCam_IsCutActive(BtlActJChr*){return 0;}int BtlAct_GetCurrent(BtlActJChr*){return busy?100:(act?act:11);}
int BtlParam_GetSlotId(BtlActJChr*,int slot){return dest[slot];}int BtlParam_GetSlotCost(BtlActJChr*,int slot){return costs[slot];}
int BtlAct_CanTransform(BtlActJChr*c,int slot,int blast,int allowed){assert(blast==1&&allowed==1);admissions++;return !blocked[slot]&&gauges[c->player].blast>=costs[slot];}
int fusionAllowed,fusionQueued,fusionSlot=-1,formOwner=-1;
int Port_FormPresentationOwner(){return formOwner;}int Port_FusionPartnerActor(int,int){return 2;}int Port_FusionConsumedActor(int){return 0;}
int BtlAct_CanFuse(BtlActJChr*,int slot,int needBlast,int needAllowed,int*member){assert(needBlast==1&&needAllowed==0);*member=1;return (fusionAllowed&(1<<slot))!=0;}
int BtlAct_QueueFusion(BtlActJChr*,int slot,int){fusionSlot=slot;fusionQueued++;return 1;}
void Port_CpuTacticsReset();void init(){Port_CpuTacticsReset();for(int i=0;i<2;i++){actors[i].player=i;*(int*)((char*)&actors[i]+0x1278)=1;for(int q:{0x94c,0x954,0x958,0x95c,0x960})*(int*)((char*)&actors[i]+q)=-1;}}
"""
foot=r"""int main(){init();assert(ct_pressure(100000,100000,100000,100000,100000,100000,100000,0)==0);assert(ct_pressure(40000,100000,50000,100000,100000,100000,50000,1)==7);assert(ct_pressure(2000000000,2000000000,0,2000000000,2000000000,0,2000000000,0)==0);
 assert(Port_CpuTransformSlot(&actors[0])==2&&admissions);assert(gauges[0].blast==300000);Port_CpuTacticsQueued(&actors[0],2,1);assert(sCpuTactics[0].nextTransform==240);for(int i=0;i<239;i++)Port_CpuTacticsTick();assert(Port_CpuTransformSlot(&actors[0])==-1);for(int i=0;i<11;i++)Port_CpuTacticsTick();assert(Port_CpuTransformSlot(&actors[0])==2);
 init();assert(Port_CpuTransformOverride(&actors[0]));human[0]=1;assert(!Port_CpuTransformOverride(&actors[0]));human[0]=0;scope=0;assert(!Port_CpuTransformOverride(&actors[0]));scope=1;
 init();act=7;assert(Port_CpuTransformSlot(&actors[0])==2);act=0; /* 2026-10-26: the CPU fights mostly in the combat-stance action 7; the old 11/15-only window almost never matched (Juan: "the CPU doesn't try to transform") */
 init();scope=2;members[1].chara=3;assert(Port_CpuTransformSlot(&actors[0])==-1);gauges[0].health=40000;for(int i=0;i<10;i++)Port_CpuTacticsTick();assert(Port_CpuTransformSlot(&actors[0])==2);
 init();scope=1;human[0]=1;assert(Port_CpuTransformSlot(&actors[0])==-1);human[0]=0;net=1;assert(Port_CpuTransformSlot(&actors[0])==-1);net=0;replay=1;assert(Port_CpuTransformSlot(&actors[0])==-1);replay=0;mode=6;assert(Port_CpuTransformSlot(&actors[0])==-1);mode=1;assert(Port_CpuTransformSlot(&actors[0])==-1);mode=0;busy=1;assert(Port_CpuTransformSlot(&actors[0])==-1);busy=0;
 init();gauges[0].blast=100000;assert(Port_CpuTransformSlot(&actors[0])==0);assert(gauges[0].blast==100000);init();gauges[0].blast=0;assert(Port_CpuTransformSlot(&actors[0])==-1);
 init();dest[0]=3;dest[1]=4;dest[2]=255;costs[0]=100000;costs[1]=0;assert(Port_CpuTransformSlot(&actors[0])==1);init();dest[1]=280;assert(Port_CpuTransformSlot(&actors[0])==-1);
 init();scope=0;admissions=0;assert(Port_CpuTransformSlot(&actors[0])==-1&&!admissions);scope=1;frozen=1;int age=sCpuTactics[0].age;Port_CpuTacticsTick();assert(sCpuTactics[0].age==age);frozen=0;
 init();scope=1;enemyScope=0;modMode=2;assert(Port_CpuTransformSlot(&actors[0])==-1);modMode=0;enemyScope=-1;
 for(preset=0;preset<3;preset++){init();Port_CpuTacticsQueued(&actors[0],0,1);assert(sCpuTactics[0].nextTransform==(preset==0?240:preset==1?150:90));}Port_CpuTacticsReset();assert(!sCpuTactics[0].nextTransform);
 init();denyAll=1;assert(!Port_CpuFormAllowed(&actors[0],0));exception=1;assert(Port_CpuFormAllowed(&actors[0],0));exception=2;assert(!Port_CpuFormAllowed(&actors[0],0));exception=0;denyAll=0;denyGiant=1;sizeClass=4;assert(!Port_CpuFormAllowed(&actors[0],0));sizeClass=-1;assert(Port_CpuFormAllowed(&actors[0],0));denyGiant=0;chance=0;assert(!Port_CpuFormAllowed(&actors[0],0));human[0]=1;assert(Port_CpuFormAllowed(&actors[0],0));human[0]=0;chance=50;int first=Port_CpuFormAllowed(&actors[0],0);for(int i=0;i<64;i++){sCpuTactics[0].age=i;policyFrame=i;assert(Port_CpuFormAllowed(&actors[0],0)==first);}chance=100;
 init();scope=1;enemyScope=-1;modMode=0;chance=100;gauges[0].blast=0;
 for(int base:{214,215,224,234}){members[0].chara=base;for(int k=0;k<4;k++){dest[k]=255;costs[k]=0;}dest[0]=base==214?216:base==215?220:base+1;init();assert(Port_CpuTransformSlot(&actors[0])==0);}
 members[0].chara=29;dest[0]=244;costs[0]=100000;gauges[0].blast=100000;init();assert(Port_CpuTransformSlot(&actors[0])==0);
 init();members[0].chara=44;members[1].chara=6;scope=1;gauges[1].max=200000;act=7;fusionAllowed=1;human[2]=1;assert(!Port_CpuTryFusion(&actors[0]));human[2]=0;init();formOwner=1;assert(!Port_CpuTryFusion(&actors[0]));formOwner=-1;init();assert(Port_CpuTryFusion(&actors[0])&&fusionQueued==1);assert(!Port_CpuTryFusion(&actors[0]));
 for(int cid:{3,4,5,6,200,34,35,36,20,202,7,8,10,122,240,38,51,52,166,149,59,110,53,249,172,54,21}){
  members[0].chara=cid;members[1].chara=6;gauges[1].max=200000;scope=1;human[0]=human[2]=0;fusionAllowed=3;act=7;formOwner=-1;init();assert(Port_CpuTryFusion(&actors[0])&&fusionSlot==0);
  init();sCpuFusionChoice[0]=1;assert(Port_CpuTryFusion(&actors[0])&&fusionSlot==1);
 }
 puts("PASS actual CPU policy/global and character precedence/ISO giant/chance stability/human isolation; actual CPU transformation planner: pressure64bit math, normal resource/model admission, best upward form,3Hz/cooldowns, no free stocks, unknown zero-cost/reverts excluded, human/native/training/online/replay/busy/frozen/reset isolation; ASan/UBSan");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text('#include <initializer_list>\n'+head+body+foot)
 subprocess.run(['g++','-std=c++17','-I'+str(r/'port/include'),'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
source=(r/'src/battle/btl_act_j.c').read_text()
assert 'int slot=Port_CpuTransformSlot(chr);' in source and 'Port_CpuTacticsQueued(chr,slot,result)' in source
assert 'BtlCharApi_Transform' not in body
