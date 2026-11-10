from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3];source=(r/'port/include/mod_select_game.inc').read_text(encoding='utf-8')
def extract(name):
 a=source.index('static int '+name+'(');i=source.index('{',a)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]
code=r"""
#include <cassert>
#include <cstring>
#include <cstdio>
#include "mod_select.h"
#include "tag_tournament_port.h"
#include "tag_tournament_view.h"
static MsPick *sMsTourOutput;static int sMsTourSize,normalCommits;
extern "C" int Port_NetActive(){return 0;}
extern "C" void Port_TagTourHudState(int,int,int,int){}
extern "C" int Port_ModSelectCostumeOk(int c,int k){return c>=0&&c<302&&k>=0&&k<8;}
extern "C" int Port_ModSelectCommit(const MsView*){normalCommits++;return 1;}
"""+extract('ms_tour_init')+extract('ms_commit_draft')+r"""
int main(){
 for(int size=1;size<=5;size++)for(int players=0;players<=4;players++)for(int assignment=0;assignment<256;assignment++){
  TTConfig c={};c.players=players;c.team_size=size;c.difficulty=1;int counts[4]={};
  for(int p=0;p<players;p++){c.player_team[p]=(assignment>>(p*2))&3;counts[c.player_team[p]]++;}
  bool valid=true;for(int n:counts)if(n>size)valid=false;
  assert(bool(Port_TagTourBegin(&c))==valid);if(!valid)continue;
  while(Port_TagTourDraftTeam()>=0){
   int team=Port_TagTourDraftTeam();MsPick out[5]={};sMsTourOutput=out;sMsTourSize=size;MsView v={};
   assert(ms_tour_init(&v)&&v.phase==1&&v.total==size&&v.a==size&&v.mode==4&&v.stage==team&&MS_Teams(&v)==1&&MS_Count(&v,0)==size);
   assert(!MS_TournamentValid(&v)&&!ms_commit_draft(&v));
   for(int m=0;m<size;m++){assert(MS_Side(&v,m)==0);v.pick[m].chara=30+team*5+m;v.pick[m].costume=2;for(int j=0;j<8;j++)v.pick[m].items[j]=j+1;}
   assert(MS_TournamentValid(&v)&&!MS_Valid(&v)&&ms_commit_draft(&v));assert(Port_TagTourDraftTeam()==team); // no double commit
   for(int m=0;m<size;m++){assert(out[m].chara==v.pick[m].chara&&out[m].items[7]==8);unsigned short items[8];for(int j=0;j<8;j++)items[j]=(unsigned short)out[m].items[j];assert(Port_TagTourDraftMember(m,out[m].chara,out[m].costume,items));}
   assert(Port_TagTourCommit());
  }Port_TagTourCancel();
 }assert(normalCommits==0);sMsTourOutput=nullptr;MsView ordinary={};assert(ms_commit_draft(&ordinary)&&normalCommits==1);
 TTBracketView b={};b.match=0;b.entrant[0][1]=1;b.next[0]=8;
 assert(TT_BracketWinner(&b,0)==-1);b.match_flags[0]=1|4;b.win_side=1;assert(TT_BracketWinner(&b,0)==1);
 b.match=1;b.entrant[8][0]=1;assert(TT_BracketWinner(&b,0)==1);b.match=15;b.match_flags[15]=1;b.entrant[15][0]=16;b.win_side=0;assert(TT_BracketWinner(&b,15)==16);
 assert(TT_BracketWinner(&b,-1)==-1&&TT_BracketWinner(&b,16)==-1);
 puts("PASS actual modern tournament adapter: fixed1..5 members,0..4 players/team assignments, items/costumes, single native commit, battle-plan isolation, pending/history/final bracket winners");
}
"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'test.cpp').write_text(code,encoding='utf-8');subprocess.run(['c++','-std=c++17','-w','-fsanitize=address,undefined','-I'+str(r/'port/include'),str(p/'test.cpp'),str(r/'port/src/gs/tag_tournament.cpp'),str(r/'port/src/gs/tag_tournament_core.c'),'-o',str(p/'test')],check=True)
 with (p/'out.log').open('w')as f:subprocess.run([str(p/'test')],stdout=f,stderr=f,check=True)
 print((p/'out.log').read_text(encoding='utf-8').splitlines()[-1])
