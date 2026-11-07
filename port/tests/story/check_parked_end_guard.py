from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_char_api.c').read_text();a=s.index('s32 BtlCharApi_AnyHasFlag128(void)');b=s.index('\n#ifdef PORT',s.index('\n}',a));fn=s[a:b]
old=(r/'analysis/codex-story-no-r3-hang-20261102/before/btl_char_api.c').read_text();a=old.index('s32 BtlCharApi_AnyHasFlag128(void)');b=old.index('\n#ifdef PORT',old.index('\n}',a));old=old[a:b].replace('BtlCharApi_AnyHasFlag128','Before_AnyHasFlag128')
head=r'''
#include <assert.h>
#include <stdio.h>
#define PORT 1
typedef int s32;typedef struct{int flag;}Chr;
static int dummy,*gBtlChars=&dummy;static Chr actors[10];static int count=4,active=1,parked[10];
int BtlChar_GetCount(void){return count;}Chr*BtlChar_Get(int a){return actors+a;}
int BtlChar_TestFlag(Chr*c,int flag){assert(flag==0x128);return c->flag;}
int Port_StorySceneParked(int a){return active&&parked[a];}
'''
foot=r'''
int main(void){
 /* The scene extra has stopped being visible; its last action4 flag persists while parked. */
 actors[3].flag=1;parked[3]=1;
 assert(Before_AnyHasFlag128()==1&&BtlCharApi_AnyHasFlag128()==0);
 /* A real visible cinematic still blocks; do not clear or ignore its flag. */
 actors[1].flag=1;assert(BtlCharApi_AnyHasFlag128()==1);actors[1].flag=0;
 parked[3]=0;assert(BtlCharApi_AnyHasFlag128()==1);parked[3]=1;
 /* Other modes retain stock behavior, as does the ordinary two-fighter path. */
 active=0;assert(BtlCharApi_AnyHasFlag128()==Before_AnyHasFlag128());active=1;
 count=2;assert(BtlCharApi_AnyHasFlag128()==0);actors[1].flag=1;assert(BtlCharApi_AnyHasFlag128()==1);
 gBtlChars=0;assert(!BtlCharApi_AnyHasFlag128());
 puts("PASS actual global cinematic/end guard: original stalls on parked Story128; fixed query ignores only parked actors, visible/native cinematics and other modes retain original guards");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'guard.c';p.write_text(head+old+fn+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
