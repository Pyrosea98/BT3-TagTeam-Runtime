from pathlib import Path
import os,re,subprocess,tempfile
r=Path.cwd();s=Path(os.environ.get('BT3_FLASH_TEST_SOURCE',str(r/'src/battle/eft_q.c'))).read_text()
def ex(n):
 a=re.search(r'(?m)^[^;\n]*\b'+n+r'\([^;]*?\)\s*\{',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i].replace('EftFlash *w = task->work;','EftFlash *w = (EftFlash*)task->work;')
head=r"""#include <cassert>
#include <cstring>
#include <cstddef>
using s32=int;using u32=unsigned;using u8=unsigned char;
#define PORT 1
struct Vec4{float x,y,z,w;};using EftQVec=Vec4;
struct EftFlashArg{Vec4 color;float in,hold,out;int objId,wait;};struct EftFlash{EftFlashArg arg;float fade,inMax,outMax;int pause,phase,frames;};
struct EftQTask{void*work;int step,dead;};struct Mgr{void*list;int flags,rushState,rushBits;};struct Work{int flags;};static Mgr mgr;static Mgr*gEftFlash=&mgr;static EftFlash flash;static EftQTask live{&flash,0,0};static Work work;static int owner,story;static bool variants[16];static u8 gEftFlashClass[24];
struct OtPrim{OtPrim*next;};struct OtEntry{OtPrim*head,*tail;};struct Last{OtEntry layer[2];};struct XYZ{int x,y,z,f;};struct RGBA{int r,g,b,a;float q;};struct EftQSpritePkt{int dmaTag,vif1,prim,vif0;unsigned long long gifTag,regs;OtPrim*next;RGBA rgbaq;XYZ xyz0,xyz1;};static Last last;static Last*gOtLast=&last;alignas(16) static u32 buf[256];static u32*gOtCur=buf;static OtPrim tail;
void Vec4_Lerp(Vec4*out,Vec4*a,Vec4*b,float t){out->x=a->x+(b->x-a->x)*t;out->y=a->y+(b->y-a->y)*t;out->z=a->z+(b->z-a->z)*t;out->w=a->w+(b->w-a->w)*t;}
int Port_StoryActive(){return story;}Work*Battle_GetWork(){return &work;}int BtlScene_GetCharCount(){return 16;}int BtlCharApi_IsFlag8Action104(int i){return i==owner;}int BtlCharApi_GetMemberUnk60(int i){return variants[i];}int EftFlash_GetRushState(){return 1;}void BtlTask_SetDead(EftQTask*t){t->dead=1;}
void EftFlash_Init(EftQTask*,EftFlashArg*);EftQTask*BtlTaskList_AddTail(void*,u8*,void*arg){live={&flash,0,0};EftFlash_Init(&live,(EftFlashArg*)arg);return &live;}
void draw_reset(){gOtCur=buf;tail.next=nullptr;last.layer[1].tail=&tail;}
"""
foot=r"""int main(){
for(owner=0;owner<16;owner++){
 mgr={};work={};memset(variants,0,sizeof(variants));EftFlashMgr_Update(nullptr);assert(flash.arg.objId==owner);story=1;draw_reset();EftFlash before=flash;mgr.flags=0;EftFlash_Draw(&live);assert(gOtCur==buf&&last.layer[1].tail==&tail&&!tail.next&&!memcmp(&before,&flash,sizeof(flash)));
 story=0;draw_reset();flash.fade=0;EftFlash_Draw(&live);assert(gOtCur!=buf&&last.layer[1].tail!=&tail);
 /* Rendering cannot skip the native reload wait or advance its task. */
 mgr.flags=1|((owner+1)<<8);if(owner==0)mgr.flags|=2;if(owner==1)mgr.flags|=4;live.step=1;flash.frames=0;flash.arg.hold=30;EftFlash_Update(&live);assert(live.step==1&&!live.dead);
}
EftFlashArg arg={{255,255,255,128},.5f,1,.5f,7,0};EftFlash_Start(&arg);assert(flash.arg.objId==-1);story=1;flash.fade=0;draw_reset();EftFlash_Draw(&live);assert(gOtCur!=buf&&tail.next); /* real attack flash still draws during Story */
}
"""
code=head+'\n'.join(ex(n) for n in ['EftFlash_Start','EftFlash_StartOwned','EftFlash_Init','EftFlashMgr_Update','EftFlash_Update','EftFlash_Draw'])+foot
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code);subprocess.run(['g++','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual costume flash owner0..15: Story cover draws no packet and changes no task/reload state; non-Story cover and generic Story attack flash still draw')
