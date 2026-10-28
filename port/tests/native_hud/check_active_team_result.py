from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_seq.c').read_text()
def ex(n):
 a=re.search(r'(?m)^s32 '+n+r'\(',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
head=r"""#include <cassert>
#include <cstring>
using s32=int;using u32=unsigned;
#define PORT 1
#define PORT_SEQ_TEAMS 10
#define PORT_ENDDBG(n) ((void)0)
#define BATTLE_FLAG_PAUSE 0x100
#define BATTLE_FLAG_LOADING 0x1000
#define BTL_RESULT_WIN_P1 1
#define BTL_RESULT_WIN_P2 2
#define BTL_RESULT_OTHER 16
#define BTL_REASON_TIME_UP 1
#define BTL_REASON_FLAG7 2
#define BTL_REASON_KO 4
#define BTL_REASON_BIT18 0x40000
struct BattleResult{int winner,reason;};struct Work{int flags;};static BattleResult result;static Work work;static int count=10,hp[10],consumed[10],team[10],bench[10],wait,off,timeup,mode,replay;
BattleResult*Battle_GetResult(){return &result;}Work*Battle_GetWork(){return &work;}int BtlSeq_GetEndCheckOff(){return off;}int BtlCharApi_AnyHasFlag128(){return wait;}int BtlSeq_TickClocksExt(){return timeup;}int Battle_GetMode(){return mode;}int Port_JudgeTeams(BattleResult*){return 0;}void BtlSeq_JudgeByHealth(BattleResult*r){r->winner=1;}int BtlCtrl_TestFlag7(int){return 0;}int BtlChar_GetCount(){return count;}int Port_Team(int i){return team[i];}int BtlCharApi_GetHp(int i){return hp[i];}int Port_FusionConsumedActor(int i){return consumed[i];}int BtlCtrl_IsTeamDead(int i){return hp[i]<=0&&bench[i]<=0;}int BattleReplay_IsActive(){return replay;}int BattleReplay_TestDataFlag(){return 0;}
void reset(){result={};work={};count=10;wait=off=timeup=mode=replay=0;memset(consumed,0,sizeof(consumed));for(int i=0;i<10;i++){team[i]=i<6?0:1;hp[i]=100;bench[i]=10000;}}
"""
foot=r"""int main(){reset();assert(!BtlSeq_CheckBattleEnd());
 // Reproduce live snapshot: all physical team0 dead; leaders retain healthy mirrored rows.
 for(int i=0;i<6;i++)hp[i]=0;assert(BtlSeq_CheckBattleEnd()&&result.winner==2&&result.reason==BTL_REASON_KO);
 reset();for(int i=6;i<10;i++)hp[i]=0;assert(BtlSeq_CheckBattleEnd()&&result.winner==1);
 reset();memset(hp,0,sizeof(hp));assert(BtlSeq_CheckBattleEnd());
 reset();for(int i=0;i<6;i++)hp[i]=0;hp[2]=100;consumed[2]=1;assert(BtlSeq_CheckBattleEnd()&&result.winner==2);
 reset();for(int i=0;i<6;i++)hp[i]=0;hp[2]=20000;assert(!BtlSeq_CheckBattleEnd());
 for(int gate=0;gate<4;gate++){reset();for(int i=0;i<6;i++)hp[i]=0;if(gate==0)wait=1;if(gate==1)off=1;if(gate==2)work.flags=0x100;if(gate==3)work.flags=0x1000;assert(!BtlSeq_CheckBattleEnd());}
 reset();for(int i=0;i<10;i++)team[i]=i;for(int i=0;i<9;i++)hp[i]=0;assert(BtlSeq_CheckBattleEnd()&&result.winner==2);
 reset();count=2;hp[0]=hp[1]=0;assert(!BtlSeq_CheckBattleEnd());bench[1]=0;assert(BtlSeq_CheckBattleEnd()&&result.winner==1);
}"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(head+ex('BtlSeq_CheckBattleEnd')+foot);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('PASS actual battle-end: live stale-mirror failure, either team/all KO, consumed fusion exclusion, revived active actor, FFA, wait/pause/load gates, native twofighter bench preservation; ASan/UBSan')
