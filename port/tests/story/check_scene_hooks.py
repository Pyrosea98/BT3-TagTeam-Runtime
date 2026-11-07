from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fn(path,start,end):
 s=(r/path).read_text();return s[s.index(start):s.index(end,s.index(start))]
code=r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define PORT 1
#define BATTLE_FLAG_DEMO 1
typedef int s32;typedef struct{int side;int flags[400];}BtlActJChr;
static int mode,scene,queued,transforms,fusions;
int Battle_GetMode(void){return mode;}
int Port_StorySceneActorShown(int side){return scene&&side<2;}
int BtlChar_TestFlag(BtlActJChr*c,int f){return c->flags[f];}
void BtlChar_ClearFlag(BtlActJChr*c,int f){c->flags[f]=0;}
void BtlAct_SetQueue(BtlActJChr*c,int i,int action){queued=action;}
int BtlAct_GetCurrent(BtlActJChr*c){return 0;}
int BtlAct_IsTechniqueId(int i){return 0;}
void BtlAct_QueueTransform(BtlActJChr*c,int n){transforms++;}
void BtlAct_QueueFusion(BtlActJChr*c,int n,int m){fusions++;}
void BtlAct_QueueSwitch(BtlActJChr*c){}
"""+fn('src/battle/btl_act_j.c','s32 BtlAct_CheckStoryForced(', '/* Action a fighter on the ground')
code+=r"""
static int chapter=-1,cuts;
int st_chapter(void){return chapter;}
struct{int flags;}bw;
#define Battle_GetWork() (&bw)
typedef struct{int step;int (*poll)(void);}BtlSeqWaitCtx;
void DemoCam_PlayStageAnim(int n){cuts++;}int DemoCam_IsActive(void){return 0;}
void ScrXfade_RequestCapture(void){}void ScrXfade_Start(int a,float b){}
"""+fn('src/battle/btl_seq.c','s32 BtlSeqStageIntro_Enter(', '/* State 0 exit:')
code+=r"""
int main(void){BtlActJChr c={0};BtlSeqWaitCtx ctx={0};
 c.flags[0xFA]=1;assert(!BtlAct_CheckStoryForced(&c)&&!queued);
 scene=1;assert(BtlAct_CheckStoryForced(&c)&&queued==4);
 queued=0;c.side=2;assert(!BtlAct_CheckStoryForced(&c)&&!queued);
 c.side=0;c.flags[0xFA]=0;c.flags[0x116]=1;assert(!BtlAct_CheckStoryForced(&c)&&!transforms);
 mode=1;assert(BtlAct_CheckStoryForced(&c)&&transforms==1);
 chapter=0;BtlSeqStageIntro_Enter(&ctx);BtlSeqStageIntro_PreUpdate(&ctx);
 assert(ctx.step==6&&!cuts&&BtlSeqStageIntro_Update(&ctx)==1);
 chapter=-1;BtlSeqStageIntro_Enter(&ctx);BtlSeqStageIntro_PreUpdate(&ctx);assert(ctx.step==1&&cuts==1);
 puts("PASS actual scene-only motion gate, stock story unchanged, no forced transform/fusion bypass; chapter skips Duel stage preview, normal Duel retains it");}
"""
# Exercise the actual pre-hold portion: KO holds must not reject an owned scripted pose.
source=(r/'src/battle/btl_char_action.c').read_text()
start=source.index('void BtlAct_CheckForced(');end=source.index('    if (chr->holdAction != 0)',start)
early=source[start:end]+'    return;\n}\n'
pre=r"""
#include <assert.h>
#include <stdio.h>
#define PORT 1
typedef int s32;typedef struct{int current;}BtlActState;
typedef struct{BtlActState act;int side,holdAction,flag;}BtlActChr;
static int scene,request;
int Port_StorySceneActorShown(int side){return scene&&side<2;}
int BtlChar_TestFlag(BtlActChr*c,int f){return f==0xFA&&c->flag;}
void BtlAct_Request(BtlActChr*c,int n){request=n;}
"""+early+r"""
int main(void){BtlActChr c={{0xF5},1,1,1};
 BtlAct_CheckForced(&c);assert(!request);
 scene=1;BtlAct_CheckForced(&c);assert(request==4&&c.holdAction==1);
 request=0;c.side=2;BtlAct_CheckForced(&c);assert(!request);
 c.side=1;c.flag=0;BtlAct_CheckForced(&c);assert(!request);
 puts("PASS actual KO-hold admission: only explicit scene-owned motion can replace it");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'hold.c';p.write_text(pre)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)

with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'hooks.c';p.write_text(code)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
