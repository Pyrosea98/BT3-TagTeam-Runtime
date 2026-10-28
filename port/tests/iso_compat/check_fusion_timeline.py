import subprocess,tempfile,re
from pathlib import Path
r=Path.cwd()
def function(src,name):
 a=re.search(r'\ns32 '+name+r'\([^;{}]*\)\s*\{',src).start()+1;b=src.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[a:i]
head='''#include <assert.h>
#include <stdlib.h>
typedef int s32;typedef float f32;
#define PORT 1
struct FxObjAnim {float frame,prevFrame,step,end;};
struct FxPartner {int active,objId;};struct FxChr {FxPartner partner;};
struct FxObj{FxObjAnim anim;};static FxObj obj;static int action,aid;static float ownerFrame;
FxObj* BtlObj_Get(int){return &obj;}int BtlAct_GetCurrent(FxChr*){return action;}int BtlAnim_GetId(FxChr*){return aid;}float BtlAnim_GetFrame(FxChr*){return ownerFrame;}
'''
src=(r/'src/battle/btl_char_fx_c.c').read_text();body=function(src,'BtlPartner_StepAnim')
test='''int main(){FxChr c={{1,1}};obj.anim={0,0,1,100};action=0xF2;aid=0x1A3;ownerFrame=30;
assert(!BtlPartner_StepAnim(&c));assert(obj.anim.frame==30);assert(!BtlPartner_StepAnim(&c));assert(obj.anim.frame==30&&obj.anim.prevFrame==30);
ownerFrame=102;assert(BtlPartner_StepAnim(&c));assert(obj.anim.frame==100);
action=5;obj.anim.frame=10;assert(!BtlPartner_StepAnim(&c));assert(obj.anim.frame==11);
action=0xF1;aid=0x1A2;ownerFrame=0;assert(!BtlPartner_StepAnim(&c));assert(obj.anim.frame==0);
c.partner.active=0;assert(BtlPartner_StepAnim(&c));}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+body+test);subprocess.run(['c++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual fusion partner timeline: paired frame, hold, clamp, reset, nonfusion isolation')


