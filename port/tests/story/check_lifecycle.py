from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
s=(r/'src/battle/btl_seq.c').read_text();start=s.index('s32 BtlSeqEnd_Enter(');end=s.index('/* State 6 preUpdate',start);fn=s[start:end]
head=r"""
#include <assert.h>
typedef int s32;
typedef struct{int timer,step;}BtlSeqWaitCtx;
struct{int flags;}battle;
#define PORT 1
#define BATTLE_FLAG_DEMO 1
static int story,aborted,mode,fade,resultPad;
void*Battle_GetWork(void){return &battle;}
#define Battle_GetWork() (&battle)
int Port_StoryActive(void){return story;}
int BattleResult_IsAborted(void){return aborted;}
int BtlSeq_IsModeZero(void){return mode==0;}
void Fade_Start(int a,int b,float c){fade++;}
void Ramp_Start(int*t,float a,float b,float c){}
void BtlSeq_SetResultPad(void){resultPad++;}
"""
foot=r"""
int main(void){BtlSeqWaitCtx ctx={0};
 story=1;mode=0;BtlSeqEnd_Enter(&ctx);assert(ctx.step==1&&fade==1&&!resultPad);
 story=0;fade=0;BtlSeqEnd_Enter(&ctx);assert(ctx.step==0&&!fade&&resultPad==1);
 story=1;aborted=1;BtlSeqEnd_Enter(&ctx);assert(ctx.step==1&&fade==1);
 return 0;}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'native.c';p.write_text(head+fn+foot)
 subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined',str(p),'-o',str(Path(d)/'test')],check=True)
 subprocess.run([str(Path(d)/'test')],check=True)
print('PASS actual BtlSeqEnd_Enter: chapter bypasses Duel results; normal Duel and abort fade preserved')
