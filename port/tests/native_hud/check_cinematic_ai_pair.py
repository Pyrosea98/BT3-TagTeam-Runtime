from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd()
def ex(s,n):
 a=re.search(r'(?m)^[^;\n]*\b'+n+r'\([^;]*?\)\s*\{',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
api=(r/'src/battle/btl_char_api.c').read_text();seq=(r/'src/battle/btl_ai_seq.c').read_text()
assert seq.count('if (AI_PAIR_WAIT(ai)')==3
head=r"""#include <cassert>
#include <cstring>
using s32=int;using u32=unsigned;using u8=unsigned char;using s8=signed char;
struct BtlCharApiChr{int side;};static BtlCharApiChr actors[10];static int bits[10],gPortTarget[10],net,replay,count=10;
int BtlChar_GetCount(){return count;}BtlCharApiChr*BtlChar_Get(int i){return &actors[i];}BtlCharApiChr*BtlChar_FindByObjId(int id){return id>=0&&id<count?&actors[id]:nullptr;}
int BtlChar_TestFlag(BtlCharApiChr*c,int){return bits[c->side];}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}
int BtlCharApi_AnyHasFlag128(){for(int i=0;i<count;i++)if(bits[i])return 1;return 0;}
struct BtlAiSeqEntry{int id;};struct BtlAiSeqA{int timer,flags,startState,waitTimer,phase,group,step,depth;BtlAiSeqEntry stack[1];};struct BtlAiWork{int objId;BtlAiSeqA seq;};
struct BtlAiSeqStep{int cls,handler,hold,press,once,special;};struct BtlAiSeqBody{signed char timeout[4],stateClass[4];unsigned char stateFlags[4];int first[4];};struct BtlAiSeqActTable{BtlAiSeqBody body;BtlAiSeqStep steps[4];};struct Data{BtlAiSeqActTable*act;};static BtlAiSeqActTable table;static Data data{&table};
#define AI_DATA (&data)
#define SEQA(ai) (&(ai)->seq)
#define SEQ_TOP(seq) ((seq)->stack[0])
#define AI_OTHER(n) gPortTarget[n]
#define AI_PAIR_WAIT(ai) Port_AiPairHasFlag128((ai)->objId)
#define BTLAI_SEQ_TOOK_EFFECT 1
#define BTLAI_SEQ_RELEASE 2
int BtlCharApi_GetUnk974(int){return 0;}int BtlCharApi_CheckUnkCAC(int){return 0;}int BtlAiSeq_IsInterrupted(BtlAiWork*,int){return 0;}int BtlAiSeq_CheckStageAbort(BtlAiWork*,int){return 0;}
void BtlAiPad_Set(BtlAiWork*,int,int,int,int,float,float){}int step(BtlAiWork*){return 0;}int(*gBtlAiStepFuncs[1])(BtlAiWork*)={step};
"""
foot=r"""int main(){for(int i=0;i<10;i++)actors[i].side=i;table.body.timeout[0]=1;table.steps[0].cls=1;
 for(int who=0;who<10;who++)for(int target=0;target<10;target++)if(who!=target)for(int held=0;held<10;held++){
  memset(bits,0,sizeof(bits));gPortTarget[who]=target;bits[held]=1;BtlAiWork ai{};ai.objId=who;ai.seq.timer=1;
  int blocked=held==who||held==target;assert(Port_AiPairHasFlag128(who)==blocked);
  assert(BtlAiStep_Countdown(&ai)==!blocked&&ai.seq.timer==blocked);
  ai.seq.timer=0;assert(BtlAiStep_GuardCountdown(&ai)==!blocked&&ai.seq.timer==(blocked?0:-1));
  BtlAiSeq_PhaseRun(&ai);assert(ai.seq.waitTimer==!blocked);
  assert(BtlCharApi_AnyHasFlag128());
 }
 memset(bits,0,sizeof(bits));bits[9]=1;gPortTarget[0]=1;
 net=1;assert(Port_AiPairHasFlag128(0));net=0;replay=1;assert(Port_AiPairHasFlag128(0));replay=0;
 assert(Port_AiPairHasFlag128(-1)&&Port_AiPairHasFlag128(99));gPortTarget[0]=-1;assert(Port_AiPairHasFlag128(0));gPortTarget[0]=0;assert(Port_AiPairHasFlag128(0));
 count=2;bits[1]=1;assert(Port_AiPairHasFlag128(0));
}"""
body=ex(api,'Port_AiPairHasFlag128')+''.join(ex(seq,n) for n in ['BtlAiStep_Countdown','BtlAiStep_GuardCountdown','BtlAiSeq_PhaseRun'])
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(head+body+foot);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('PASS actual AI wait helper +3 native callers:900 pair/holder cases, unrelated cinematics progress, participant hold, global battle-end and online/replay/invalid/twofighter fallback')
