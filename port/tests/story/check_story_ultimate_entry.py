from pathlib import Path
import re,subprocess,tempfile
s=Path('src/battle/btl_capi_b.c').read_text();t=Path('src/battle/btl_char_action.c').read_text();u=Path('src/battle/btl_act_f.c').read_text()
def fn(src,name):
 a=re.search(r'(?m)^(?:void|s32) '+name+r'\(',src).start();i=src.index('{',a)+1;d=1
 while d:d+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[a:i]
entry=fn(s,'Port_StoryUseTechnique')
for decl in ['extern s32 BtlAct_GetTechniqueAction(void*,s32);','extern void BtlAct_Request(void*,s32);','extern void BtlAct_SetQueue(void*,u32,s32);']:entry=entry.replace(decl,'')
code=r"""#include <assert.h>
#include <string.h>
typedef int s32;typedef unsigned u32;typedef struct{struct{int request,queue[4];}act;int flags[512];}BtlCapiBChr;typedef BtlCapiBChr BtlActChr;typedef BtlCapiBChr BtlSuperChr;static BtlCapiBChr c;static int story=1,prelude=1,uses;
BtlCapiBChr*BtlChar_Get(int p){return p==0?&c:0;}int Port_StoryActive(void){return story;}int Port_StoryUltimatePrelude(int p){return prelude;}int BtlCtrl_UseTechnique(int p,int s){uses++;c.flags[0x11e]=c.flags[0x120+s]=1;return 1;}void BtlChar_ClearFlag(BtlCapiBChr*c,int f){c->flags[f]=0;}void BtlChar_SetFlag(BtlCapiBChr*c,int f){c->flags[f]=1;}int BtlAct_GetTechniqueAction(BtlCapiBChr*c,int s){return 0x104+s;}void Port_Dbg3(const char*s,int a,int b,int c,int d){}
"""+fn(t,'BtlAct_Request')+fn(t,'BtlAct_SetQueue')+fn(t,'BtlAct_GetQueued')+fn(t,'BtlAct_PopQueue')+fn(u,'BtlSuper_SetClassFlag')+entry+r"""int main(void){BtlSuper_SetClassFlag(&c,4,0);assert(!c.flags[0xE2]);BtlSuper_SetClassFlag(&c,4,1);assert(c.flags[0xE2]);c.flags[0xE2]=0;c.act.request=-1;assert(Port_StoryUseTechnique(0,4));assert(c.act.request==0x105&&c.act.queue[0]==0x105&&c.act.queue[1]==0x108&&c.act.queue[2]==-1&&!c.flags[0x124]&&!c.flags[0x11e]);BtlAct_PopQueue(&c);assert(BtlAct_GetQueued(&c)==0x108);BtlAct_Request(&c,BtlAct_GetQueued(&c));BtlAct_PopQueue(&c);assert(c.act.request==0x108&&BtlAct_GetQueued(&c)==-1);prelude=0;assert(Port_StoryUseTechnique(0,4)&&c.act.request==0x108);prelude=1;assert(Port_StoryUseTechnique(0,2)&&c.act.request==0x106);story=0;c.act.request=-1;assert(!Port_StoryUseTechnique(0,4)&&c.act.request==-1&&uses==3);assert(!Port_StoryUseTechnique(-1,4));}"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'entry';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual Story native technique entry: Masenko queues105 ->108 using real action queue primitives; other Story moves retain direct dispatch, ordinary modes untouched')
