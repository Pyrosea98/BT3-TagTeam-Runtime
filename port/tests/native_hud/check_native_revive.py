from pathlib import Path
import subprocess,tempfile
r=Path.cwd();body=(r/'port/src/mod_revive_native.inc').read_text()
head=r"""#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
static unsigned char actors[4][0x1600],work[0x2000];static int actions[4]={11,11,216,11},teams[4]={0,1,0,0},humans[4]={1,0,0,1};static int expectedTarget=2,requestedActor=-1,requestedAction=-1;void BtlAct_Request(void*c,int a);
static int consumed,net,replay,state=3,enabled=1,revived,frozen;
int Port_Setting(const char *k,int def){return !strcmp(k,"revive_enabled")?enabled:def;}
int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlSeq_GetState(){return state;}int BtlChar_GetCount(){return 4;}void *Battle_GetWork(){return work;}
void *BtlChar_Get(int who){return actors[who];}void *BtlMember_GetActive(void*c){return (char*)c+0x9a4;}void *BtlChar_GetPos(void*c){return (char*)c+16;}
int index(void*c){return ((unsigned char*)c-&actors[0][0])/0x1600;}int BtlAct_GetCurrent(void*c){return actions[index(c)];}int BtlChar_IsFrozen(void*){return frozen;}int ChrCam_IsCutActive(void*){return 0;}
int Port_TargetHuman(int i){return humans[i];}int Port_Team(int i){return teams[i];}int Port_FightConsumed(int i){return i==0&&consumed;}void Port_ReviveSpectator(int i){assert(i==expectedTarget);revived++;}
void BtlAct_Request(void*c,int a){requestedActor=index(c);requestedAction=a;}
int &word(int i,int off){return *(int*)(actors[i]+off);}int &hp(int i){return word(i,0x9e4);}int &stocks(int i){return word(i,0x9f8);}
void init(){memset(actors,0,sizeof(actors));memset(work,0,sizeof(work));for(int i=0;i<4;i++){for(int q:{0x94c,0x954,0x958,0x95c,0x960})word(i,q)=-1;word(i,0x9ac)=1;word(i,0x9e8)=100000;hp(i)=100000;stocks(i)=200000;}hp(2)=0;((float*)(actors[3]+16))[0]=1000;}
"""
foot=r"""int main(){init();for(int i=0;i<44;i++)Port_ReviveTick();assert(!hp(2)&&stocks(0)==200000);Port_ReviveTick();assert(hp(2)==20000&&stocks(0)==100000&&revived==1&&word(2,0x964)==91&&Port_ReviveProtected(2));for(int i=0;i<40;i++)Port_ReviveTick();assert(stocks(0)==100000&&revived==1&&Port_ReviveProtected(2));actions[2]=225;Port_ReviveTick();assert(Port_ReviveProtected(2));actions[2]=11;Port_ReviveTick();assert(!Port_ReviveProtected(2));
 init();Port_ReviveReset();actions[2]=216;stocks(0)=0;for(int i=0;i<50;i++)Port_ReviveTick();assert(!hp(2)&&sRevive[0].insufficient);stocks(0)=200000;for(int i=0;i<20;i++)Port_ReviveTick();hp(0)--;Port_ReviveTick();assert(sRevive[0].ticks==1);actions[0]=55;Port_ReviveTick();assert(!sRevive[0].ticks);actions[0]=11;((float*)(actors[0]+16))[0]=1000;Port_ReviveTick();assert(!sRevive[0].ticks);((float*)(actors[0]+16))[0]=0;consumed=1;for(int i=0;i<50;i++)Port_ReviveTick();assert(!hp(2));consumed=0;
 for(int i=0;i<44;i++)Port_ReviveTick();*(int*)(work+0x19f0)=0x100;Port_ReviveTick();assert(!hp(2)&&sRevive[0].ticks==44);*(int*)(work+0x19f0)=0;((float*)(actors[3]+16))[0]=0;sRevive[3]=sRevive[0];Port_ReviveTick();assert(hp(2)&&stocks(0)==100000&&stocks(3)==200000);
 net=1;Port_ReviveTick();assert(!Port_ReviveProtected(2));net=0;init();Port_ReviveReset();replay=1;for(int i=0;i<50;i++)Port_ReviveTick();assert(!hp(2)&&stocks(0)==200000);replay=0;enabled=0;Port_ReviveTick();assert(!sRevive[0].ticks);enabled=1;teams[2]=2;for(int i=0;i<50;i++)Port_ReviveTick();assert(!hp(2));Port_ReviveReset();assert(!sRevive[2].recovering);init();Port_ReviveReset();teams[2]=0;actions[2]=216;humans[0]=0;word(0,0x1278)=1;actions[0]=7;assert(Port_ReviveCpuHold(0));for(int i=0;i<45;i++)Port_ReviveTick();assert(hp(2)==20000&&stocks(0)==100000);assert(!Port_ReviveCpuHold(0));humans[0]=1;actions[0]=11;
 init();Port_ReviveReset();actions[2]=0xEB;revived=0;for(int i=0;i<45;i++)Port_ReviveTick();assert(hp(2)==20000&&requestedActor==2&&requestedAction==0xE1&&revived==1);
 init();Port_ReviveReset();expectedTarget=0;revived=0;hp(0)=0;hp(2)=100000;actions[0]=0xEB;actions[2]=7;humans[0]=1;humans[2]=0;word(2,0x1278)=1;
 assert(Port_ReviveCpuHold(2));for(int i=0;i<45;i++)Port_ReviveTick();assert(hp(0)==20000&&requestedActor==0&&requestedAction==0xE1&&revived==1&&stocks(2)==100000);
 puts("PASS actual native revival: proximity/channel, stock-once commit, health cap, damage/attack/range reset, consumed exclusion, pause, two helpers, protected native get-up, enemy/online/replay/disabled/reset guards; ASan/UBSan");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text('#include <initializer_list>\n'+head+body+foot);subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
