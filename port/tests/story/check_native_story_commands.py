from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fun(file,sig):
 s=(r/file).read_text();a=s.index(sig);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
source=fun('src/battle/btl_capi_b.c','void BtlCtrl_PlayMotion(')+'\n'+fun('src/battle/btl_capi_b.c','s32 Port_StoryUseTechnique(')
code=r"""
#include <assert.h>
#include <stdio.h>
#define PORT 1
typedef int s32;typedef unsigned u32;typedef float f32;
typedef struct{int motion;float motionBlend;int motionLoop,flags[512];}BtlCapiBChr;
static BtlCapiBChr actors[16];static int active=1,used,requested,type;
BtlCapiBChr*BtlChar_Get(int a){return a>=0&&a<16?actors+a:0;}
int Port_StoryActive(void){return active;}
int Port_StoryUltimatePrelude(int a){return 0;}
void BtlAct_SetQueue(void*c,u32 slot,s32 act){assert(0);}
void BtlChar_ClearFlag(BtlCapiBChr*c,int f){c->flags[f]=0;}
void BtlChar_SetHeldFlag(BtlCapiBChr*c,int f){c->flags[f]=1;}
int BtlCtrl_UseTechnique(int a,int slot){used++;actors[a].flags[0x11E]=1;actors[a].flags[0x120+slot]=1;return 1;}
int BtlAct_GetTechniqueAction(void*c,int slot){return 0x106+slot-2;}
void BtlAct_Request(void*ptr,int act){BtlCapiBChr*c=ptr;assert(!c->flags[0xFA]&&!c->flags[0xFB]&&!c->flags[0x11E]);for(int f=0x120;f<=0x124;f++)assert(!c->flags[f]);requested=act;}
void Port_Dbg3(const char*s,int a,int b,int c,int d){}
"""+source+r"""
int main(){for(int a=0;a<16;a++){actors[a].flags[0xFB]=1;BtlCtrl_PlayMotion(a,0x173,1,.1f);assert(!actors[a].flags[0xFB]&&actors[a].flags[0xFA]&&actors[a].motion==0x173);
 for(int k=2;k<=4;k++){actors[a].flags[0xFB]=1;assert(Port_StoryUseTechnique(a,k));assert(requested==0x106+k-2);}}
 active=0;used=0;requested=-1;actors[0].flags[0xFB]=1;BtlCtrl_PlayMotion(0,2,0,0);assert(actors[0].flags[0xFB]);assert(!Port_StoryUseTechnique(0,4)&&!used&&requested==-1);
 puts("PASS actual native Story command: actors0..15, slots2..4, stale pose/stop/virtual-pad flags cleared before direct dispatch; other modes untouched");}
"""
# Execute actual battle result submission body through the native16 capacity.
s=(r/'src/battle/battle_load.c').read_text();a=s.index('{extern unsigned Port_ScenarioResult');b=s.index('/* Evaluate the immutable draft',a)
receipt=s[a:b]
code2=r"""
#include <assert.h>
#include <stdio.h>
typedef struct{int winner,reason;}BattleResult;static BattleResult data={1,1},*res=&data;static int count,submitted;
int BtlChar_GetCount(){return count;}int Port_ScenarioActorForm(int a){return 100+a;}int BtlCharApi_GetHp(int a){return a+1;}
void Port_StoryFoundBall(int v){}void Port_ScenarioSetFinalHp(const int*h,int n){assert(n==count&&h[n-1]==n);}
int Port_ModesFight(){return 1;}int BattleReplay_IsActive(){return 0;}int Port_NetActive(){return 0;}
unsigned Port_ScenarioResult(int winner,int flags,int reason,const int*f,int n,int eligible){assert(winner==0&&flags==1&&reason==1&&eligible&&n==count&&f[n-1]==99+n);submitted++;return 0;}
void Port_AchievementSubmit(unsigned a){}
void submit(void){
"""+receipt+r"""
}
int main(){for(count=1;count<=16;count++)submit();assert(submitted==16);count=17;submit();assert(submitted==16);puts("PASS actual native result bridge: story results/rewards submitted for11..16 actors, safe capacity boundary17");}
"""

code3=r"""
#include <assert.h>
#include <stdio.h>
typedef int s32;static struct{int flags;}mgr,*gBtlChars=&mgr;
#define BTL_CHARS_STARTED 1
static int state=2,mode,active=1,events=1,resets;
int BtlSeq_GetState(){return state;}int Battle_GetMode(){return mode;}int Port_StoryActive(){return active;}unsigned Port_StoryEvents(){return events;}void BtlChar_ResetAll(){resets++;}
"""+fun('src/battle/btl_char_mgr.c','s32 BtlChars_CheckStart(')+r"""
int main(){assert(!BtlChars_CheckStart()&&!resets&&(mgr.flags&1));mgr.flags=0;events=0;assert(BtlChars_CheckStart()&&resets==1);mgr.flags=0;active=0;events=1;assert(BtlChars_CheckStart()&&resets==2);mgr.flags=0;mode=1;assert(!BtlChars_CheckStart()&&resets==2);puts("PASS actual Ready reset: preserve authored intro form/events, ordinary duel and native History unchanged");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 for n,c in [('command',code),('receipt',code2),('ready',code3)]:
  f=p/(n+'.c');f.write_text(c);subprocess.run(['cc','-DPORT','-std=gnu99','-fsanitize=address,undefined',str(f),'-o',str(p/n)],check=True);subprocess.run([str(p/n)],check=True)
