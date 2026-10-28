from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'src/battle/battle_load.c').read_text();a=s.index('void BattleSetup_FixForMode(');b=s.index('\n}\n',a)+3
clamp=s[a:b]
head=r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "tag_tournament_port.h"
#include "tag_tournament_grid.h"
#include "plat_fighters.h"
#include "coop_selection.h"
#define PORT 1
using s32=int;
extern "C" int Port_FightPad(int);
extern "C" int Port_LocalQuadFight(int,int,int,int,int);
struct BattleMember{int cpuLevel,aiType;};
struct BattleSide{int memberCount,control;BattleMember members[5];};
struct BattleRule{int screenMode,timeLimit,mode;};
struct Setup{BattleRule rule;BattleSide sides[2];} setup;
#define SETUP() (&setup)
int Battle_GetMode(){return setup.rule.mode;}
int online;
extern "C" int Port_NetActive(){return online;}
static unsigned available=15;extern "C" int PortInput_PlayerAvailable(int p){return p>=0&&p<4&&(available&(1u<<p));}
extern "C" int Port_Setting(const char*,int d){return d;}
extern "C" void Port_SettingSave(const char*,int){}
extern "C" void Port_SettingsWrite(){}
extern "C" int Port_ModesFight(){return Port_CoopTraining()?PORT_MODE_TRAINING:0;}
extern "C" int Port_ModesCount(){return 0;}
extern "C" const char *Port_ModesName(int){return "test team";}
extern "C" int Port_FilePath(int,int,const char*,char *out,int size){snprintf(out,size,"fixture.bin");return 1;}
extern "C" void *Port_LowAlloc(size_t n){return calloc(1,n);}
extern "C" void Port_LowFree(void *p){free(p);}
extern "C" void Port_TagTourHudState(int,int,int,int){}
"""
body=r"""
int main(){
 FILE *file=fopen("fixture.bin","wb");fputc(1,file);fclose(file);
 TTConfig c={4,3,0,1,{0,0,1,1}};assert(Port_TagTourBegin(&c));
 while(Port_TagTourDraftTeam()>=0){int team=Port_TagTourDraftTeam();for(int m=0;m<3;m++)assert(Port_TagTourDraftMember(m,team*5+m,2,nullptr));assert(Port_TagTourCommit());}
 TTFighter roster[17]={};for(int i=0;i<17;i++)roster[i].character=i;assert(Port_TagTourSeed(roster,17,1));assert(Port_TagTourMatch(0,1,0));
 setup.rule.mode=4;setup.rule.screenMode=1;setup.sides[0].memberCount=setup.sides[1].memberCount=3;
 BattleSetup_FixForMode();assert(setup.sides[0].memberCount==3&&setup.sides[1].memberCount==3);
 assert(Port_ExtraSetup(4,3,3,1,0,0)==PORT_MODE_TEAM);
 for(int side=0;side<2;side++)for(int m=1;m<3;m++)assert(Port_ExtraAdd(side,m,side*5+m,2,10));
 Port_ExtraDone();assert(Port_FightExtras()==4&&Port_FightRoster()==PORT_MODE_TEAM);
 assert(Port_FightPad(0)==2&&Port_FightPad(2)==3&&Port_FightCostume(0)==2);
 assert(!Port_LocalQuadFight(4,3,3,0,0));
 assert(Port_ExtraSetup(4,3,3,1,1,0)==0);assert(Port_ExtraSetup(4,3,3,1,0,1)==0);
 online=1;assert(Port_ExtraSetup(4,3,3,1,0,0)==0);online=0;
 Port_TagTourCancel();BattleSetup_FixForMode();assert(setup.sides[0].memberCount==1&&setup.sides[1].memberCount==1);
 assert(Port_ExtraSetup(4,3,3,1,0,0)==0);

 // Real co-op functions: swapped leaders, same-team humans and CPU-team drafting.
 Port_CoopConfigure(2,0,0,0,0,0);assert(Port_CoopLeader(0)==0&&Port_CoopLeader(1)==-1);
 assert(Port_CoopSelectionPad(0,1,1)==1);assert(Port_CoopSelectionPad(0,1,0)==0);
 assert(Port_CoopSelectionPad(1,1,1)==0); // CPU team never waits on absent pad3.
 Port_CoopConfigure(2,1,0,0,0,0);assert(Port_CoopLeader(0)==1&&Port_CoopLeader(1)==0);
 assert(Port_CoopSelectionPad(0,0,1)==1&&Port_CoopSelectionPad(1,0,1)==0);
 for(int n=2;n<=4;n++)for(int mask=0;mask<(1<<n);mask++){
  Port_CoopConfigure(n,mask&1,(mask>>1)&1,(mask>>2)&1,(mask>>3)&1,1);Port_LocalPlayersMatchSet(n);
  setup.rule.mode=6;setup.rule.screenMode=1;setup.sides[0].memberCount=setup.sides[1].memberCount=5;
  BattleSetup_FixForMode();assert(setup.rule.screenMode==1&&setup.sides[0].memberCount==5&&setup.sides[1].memberCount==5);
  assert(Port_ExtraSetup(6,5,5,1,0,0)!=0);
  for(int side=0;side<2;side++)for(int m=1;m<5;m++)assert(Port_ExtraAdd(side,m,side*5+m,2,10));
  Port_ExtraDone();for(int k=0;k<8;k++){int member=Port_FightMember(k);assert(Port_FightPad(k)==Port_CoopMemberPad(member/8,member%8));}
  online=1;assert(!Port_CoopActive());online=0;
 }
 available=1;Port_CoopConfigure(4,0,0,1,1,1);
 assert(Port_CoopSelectionPad(0,1,1)==0&&Port_CoopSelectionPad(1,1,1)==0);
 available=3;assert(Port_CoopSelectionPad(0,1,1)==1&&Port_CoopSelectionPad(1,1,1)==0);
 available=12;assert(Port_CoopSelectionPad(1,1,1)==3&&Port_CoopSelectionPad(0,0,1)==2);
 available=0;assert(Port_CoopSelectionPad(1,1,1)==0);available=15;
 Port_CoopReset();setup.rule.mode=6;BattleSetup_FixForMode();assert(setup.rule.screenMode==0&&setup.sides[0].memberCount==1&&setup.sides[1].memberCount==1);
 TTGridCell cells[14]={};cells[0].id=162;cells[1].id=0;cells[2].id=164;cells[3].id=5;cells[3].count=2;cells[3].form[1]=7;cells[4].id=163;cells[5].id=161;
 assert(TT_CompactGrid(cells,6,14)==7);assert(cells[0].id==0&&cells[1].id==5&&cells[1].form[1]==7&&cells[2].id==161);for(int i=3;i<7;i++)assert(cells[i].id==164);
 assert(TT_CompactGrid(cells,15,14)==-1);
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+clamp+body)
 subprocess.run(['gcc','-std=gnu99','-w','-ffunction-sections','-fdata-sections','-I'+str(r/'port/include'),'-I'+str(r/'port/src'),'-c',str(r/'port/src/plat_fighters.c'),'-o',str(p/'fighters.o')],check=True)
 subprocess.run(['gcc','-std=gnu99','-ffunction-sections','-fdata-sections','-I'+str(r/'port/include'),'-c',str(r/'port/src/plat_beta8.c'),'-o',str(p/'beta8.o')],check=True)
 subprocess.run(['c++','-std=c++17','-ffunction-sections','-fdata-sections','-I'+str(r/'port/include'),'-I'+str(r/'port/src'),str(p/'test.cpp'),str(r/'port/src/gs/tag_tournament.cpp'),str(r/'port/src/gs/tag_tournament_core.c'),str(p/'fighters.o'),str(p/'beta8.o'),'-Wl,--gc-sections','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
print('PASS actual tournament clamp + extra loader: 3v3 spawning, pad ownership, native/replay/script/online isolation; compact grid preserves playable forms')
