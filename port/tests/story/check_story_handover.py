from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/btl_char_flag_opp.c').read_text()
def fn(sig):
 a=s.index(sig);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
code=r"""
#include <assert.h>
#include <string.h>
typedef int s32;typedef unsigned u32;typedef unsigned char u8;
#define PORT_FIGHTERS 16
#define PAD_R3 4
struct C{int player,pad;unsigned char rest[0x1278-8];int cpu;};typedef struct C BtlFlagChr;
static BtlFlagChr c[16];static int sControlOwner[16],sControlTarget[16],sControlNotice[16],sPortWatch[16],sPortWatchWait[16];static unsigned press[4];
int BtlChar_GetCount(void){return 5;}BtlFlagChr*BtlChar_Get(int a){return c+a;}
int Port_TargetHuman(int a){return sControlOwner[a]?sControlOwner[a]>=2:a==0;}
int Port_Team(int a){return a==1;}void BtlInput_Init(void*p){}void BtlAct_ClearQueue(void*p){}
void ChrCam_EndCut(void*p){}void BtlChar_ClearFlag(void*p,int f){}void Port_SpectateLog(int a,int b,int d){}unsigned Port_PadPressed(int p){return press[p];}
"""+fn('void Port_TakeoverApply(void*actor)')+fn('void Port_StoryHandControl(int to)')+fn('s32 Port_StoryR3Pressed(void)')+r"""
int main(){int i;for(i=0;i<5;i++){c[i].player=i;c[i].pad=0;c[i].cpu=i!=0;}
 Port_StoryHandControl(2);assert(sControlTarget[0]==3&&Port_TargetHuman(2)&&!Port_TargetHuman(0));
 Port_StoryHandControl(3);assert(sControlTarget[0]==4&&sControlTarget[2]==4&&sControlTarget[3]==0&&Port_TargetHuman(3)&&!Port_TargetHuman(2));
 press[0]=PAD_R3;assert(Port_StoryR3Pressed());press[0]=0;assert(!Port_StoryR3Pressed());
 Port_StoryHandControl(4);assert(sControlTarget[0]==5&&sControlTarget[2]==5&&sControlTarget[3]==5&&!c[4].cpu&&c[4].pad==0);
}
"""
assert 'Port_StoryR3Pressed()' in Path('src/battle/btl_script.c').read_text()
assert 'if(Port_StoryActive())return -1' not in s
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'handover.c';p.write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
print('PASS actual native chained control/camera handover and current-player R3; Story takeover enabled')
