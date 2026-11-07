from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/btl_capi_b.c').read_text();a=s.index('void Port_StoryReleaseActor(');b=s.index('\n#endif',a);fn=s[a:b].replace("extern void BtlAct_Request(void*,s32);","");t=Path('src/battle/btl_char_action.c').read_text();x=t.index('void BtlAct_Request(');request=t[x:t.index('\n/* Puts an action',x)]
code=r"""
#include <assert.h>
#include <string.h>
typedef int s32;typedef struct{int dead,action,flag[512];struct{int request;}act;}BtlCapiBChr;static BtlCapiBChr chr;typedef BtlCapiBChr BtlActChr;static int story=1,human,stops,resets;
void BtlAiMgr_ResetSide(s32 p){assert(p==0);resets++;}
BtlCapiBChr*BtlChar_Get(int p){return p==0?&chr:0;}int Port_StoryActive(void){return story;}int Port_TargetHuman(s32 p){assert(p==0);return human;}int BtlChar_IsDead(BtlCapiBChr*c){return c->dead;}void BtlCtrl_StopMotion(int p){stops++;}void BtlChar_ClearFlag(BtlCapiBChr*c,int f){c->flag[f]=0;}int BtlAct_GetCurrent(BtlCapiBChr*c){return c->action;}
"""+request+fn+r"""
int main(void){int f;for(f=0;f<512;f++)chr.flag[f]=1;chr.action=4;Port_StoryReleaseActor(0);assert(stops==1&&resets==1&&chr.act.request==0xB);chr.flag[0xFB]=0;assert(chr.act.request==0xB);for(f=0x42;f<=0x46;f++)assert(!chr.flag[f]);assert(!chr.flag[0x128]&&!chr.flag[0x30]&&!chr.flag[0x11e]);assert(chr.flag[0x41]&&chr.flag[0x47]);chr.act.request=-1;chr.action=0x108;chr.flag[0x42]=1;Port_StoryReleaseActor(0);assert(chr.flag[0x42]&&chr.act.request==-1&&resets==2);human=1;Port_StoryReleaseActor(0);assert(stops==3&&resets==2);human=0;chr.action=4;chr.dead=1;Port_StoryReleaseActor(0);assert(chr.flag[0x42]&&resets==2);chr.dead=0;story=0;Port_StoryReleaseActor(0);assert(chr.flag[0x42]&&resets==2);Port_StoryReleaseActor(-1);assert(stops==3&&resets==2);}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual native pose release: punch/technique collision immunities cleared and native idle exit queued only on living Story pose actors; KO, active attacks and ordinary gameplay untouched')
