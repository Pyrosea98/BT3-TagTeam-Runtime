from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'src/menu/menu_k_f.c').read_text();start=s.index('void Bracket_SetupBattle(');end=s.index('\n}\n',start)+3
fn=s[start:end]
a=s.index('int Port_TagTourCpuMatch(');b=s.index('\n#endif',a);skip=s[a:b]
harness=r"""
#include <cassert>
#include <cstring>
#include <cstdint>
#include "tag_tournament_port.h"
#define PORT 1
using s32=int;using f32=float;
struct LItemSet {uint16_t id[8];};
struct Entrant {int flags,unk2,chara,costume;float health;LItemSet item;};
struct Match {int ent[2],flags;};struct Cpu {LItemSet item;int cpuLevel;};
struct LBracket {Entrant entrant[17];Match match_[16];int match,round,flags,winSide;Cpu cpu[15];};
struct TourProg {int tour,level;} prog;
#define LTOUR_PROG (&prog)
enum {LTOUR_WORLD,LTOUR_BIG,LTOUR_CELL,LTOUR_OTHERWORLD,LTOUR_YAMCHA};
enum {LTOUR_ENT_LOST=2,LBRK_PLAYER_OUT=0x4000,LTOUR_MATCH_DONE=1,LTOUR_ENT_PLAYER=1,LBRK_PVP=0x8000,LBRK_LEFT_PAD=0x10000,LBRK_RIGHT_PAD=0x20000,LTOUR_ROUND_NUM=5};
struct Side {int control,pad,count,chara[5],costume[5];uint16_t items[5][8];} sides[2];
int skipAnimations;void Bracket_StartWin(LBracket*){skipAnimations++;}
int Bracket_IsPlayerOut(LBracket *b){for(int n=0;n<17;n++)if((b->entrant[n].flags&1)&&!(b->entrant[n].flags&2))return 0;return 1;}
int split;void Battle_ClearWork(){memset(sides,0,sizeof(sides));}
void BattleSetup_SetSide(int s,int c,int p,int n,int,int,int,void*){sides[s].control=c;sides[s].pad=p;sides[s].count=n;}
void BattleSetup_SetRule(int s,int,int,int,int,int,int){split=s;}
void BattleSetup_SetMember(int s,int m,int c,int costume,int,int,float,void *items){sides[s].chara[m]=c;sides[s].costume[m]=costume;if(items)memcpy(sides[s].items[m],items,16);}
void BattleSetup_Finish(){}int Bracket_PickStage(int){return 4;}int Rand_Range(int){return 0;}
extern "C" int Port_NetActive(){return 0;}
extern "C" void Port_TagTourHudState(int,int,int,int){}
void draft(const TTConfig& config){
 assert(Port_TagTourBegin(&config));
 while(Port_TagTourDraftTeam()>=0){int team=Port_TagTourDraftTeam();for(int m=0;m<config.team_size;m++){uint16_t items[8]={1,2,3,4,5,6,7,8};assert(Port_TagTourDraftMember(m,30+team*5+m,2,items));}assert(Port_TagTourCommit());}
 TTFighter roster[17]={};for(int n=0;n<17;n++)roster[n].character=n;
 assert(Port_TagTourSeed(roster,17,42));
}
LBracket bracket(int left,int right,int flags0,int flags1){LBracket b={};b.entrant[0].unk2=0x5400+left;b.entrant[1].unk2=0x5400+right;b.entrant[0].flags=flags0;b.entrant[1].flags=flags1;b.match_[0].ent[1]=1;return b;}
"""
tests=r"""
int main(){
 TTConfig c={4,3,0,1,{0,0,1,1}};draft(c);auto b=bracket(0,1,1,1);Bracket_SetupBattle(&b);
 assert(split==1&&sides[0].count==3&&sides[1].count==3&&Port_TagTourViews()==4);
 assert(sides[0].chara[2]==32&&sides[1].chara[2]==37&&sides[1].costume[2]==2&&sides[1].items[2][7]==8);
 assert(Port_TagTourPadRoute(0)==0&&Port_TagTourPadRoute(1)==2&&Port_TagTourPadRoute(2)==1&&Port_TagTourPadRoute(3)==3);
 assert(Port_TagTourResult(1)==1&&Port_TagTourResult(1)==-1); // once, stable team id
 c={2,2,0,1,{0,0,2,3}};draft(c);b=bracket(0,2,1,0);Bracket_SetupBattle(&b);
 assert(split==1&&sides[0].control==0&&sides[1].control==2&&Port_TagTourViews()==2);
 assert(Port_TagTourMemberPad(0,1)==1&&Port_TagTourPadRoute(1)==1);
 c={2,3,0,1,{0,1,2,3}};draft(c);b=bracket(2,1,0,1);Bracket_SetupBattle(&b);
 assert(sides[0].chara[0]==35&&Port_TagTourPadRoute(0)==1&&split==0);
 assert(Port_TagTourResult(0)==1); // P2 right entrant becomes actual battle side 0
 c={0,5,0,1,{0,1,2,3}};draft(c);b=bracket(0,2,1,0);Bracket_SetupBattle(&b);
 assert(sides[0].count==5&&sides[1].count==5&&sides[0].control==2&&sides[1].control==2&&split==0);
 assert(Port_TagTourViews()==0&&Port_TagTourPadRoute(0)==0); // spectator can pause
 c={0,3,0,1,{0,1,2,3}};draft(c);b=bracket(0,2,1,0);assert(Port_TagTourCpuMatch(&b));Port_TagTourSkip(&b);
 TTTeam losing;assert(skipAnimations==1&&b.winSide==0&&(b.match_[0].flags&LTOUR_MATCH_DONE));assert(Port_TagTourTeam(2,&losing)&&losing.eliminated);
 c={1,3,0,1,{0,1,2,3}};draft(c);b=bracket(0,2,1,0);assert(!Port_TagTourCpuMatch(&b));Port_TagTourSkip(&b);assert(skipAnimations==1);
 Port_TagTourCancel();assert(!Port_TagTourActive()&&Port_TagTourPadRoute(3)==3);
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(harness+fn+skip+tests)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-I'+str(r/'port/include'),str(p/'test.cpp'),str(r/'port/src/gs/tag_tournament.cpp'),str(r/'port/src/gs/tag_tournament_core.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS real native tournament battle handoff: 4P ownership, shared team, swapped P2, CPU-only 5v5, costumes/eight items, CPU skip animation and human-match skip rejection')
