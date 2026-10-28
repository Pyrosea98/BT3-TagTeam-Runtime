from pathlib import Path
import re,tempfile,subprocess
r=Path.cwd();s=(r/'src/battle/btl_char_mgr.c').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:int|void) '+name+r'\([^;{}]*\)\s*\{',text,re.M);assert m,name;i=text.index('{',m.start())+1;d=1
 while d:d+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
helper=fn(s,'Port_GtBaseClosingReload');callback=fn(s,'BtlChar_OnModelLoaded');prefix=callback[:callback.index('    BtlObjAnim_SamplePose(obj);')]+'}\n'
anim=(r/'src/battle/btl_char_status_anim.c').read_text();flush=fn(anim,'BtlAnim_FlushRequest')
h=r'''#include <cassert>
#include <cstdio>
#define PORT 1
using s32=int;using f32=float;
struct BtlMgrMember{int chara,costume;};struct BtlMgrObj{};struct BtlMgrGauge{};
struct BtlMgrChr{BtlMgrMember member;int action,newChara;struct{int next;}anim;int request,boundModel,played,newFlag;float blendTime;};
using BtlStatChr=BtlMgrChr;
enum{BTL_ANIM_FLAG_REQ=1};
int Port_ChangeWaited(int){return 0;}
void Port_FusionStageLog(const char*,int,int,int,int,int,int,int,int){}
BtlMgrMember*BtlMember_GetActive(BtlMgrChr*c){return &c->member;}
int BtlAct_GetCurrent(BtlMgrChr*c){return c->action;}
void BtlChar_BindObject(BtlMgrChr*c){c->boundModel=c->newChara;}
BtlMgrObj*BtlChar_GetObj(BtlMgrChr*){static BtlMgrObj o;return &o;}
void BtlChar_SetFlag(BtlMgrChr*,int){}
int BtlChar_TestFlag(BtlMgrChr*c,int){return c->request;}
void BtlAnim_Play(BtlMgrChr*c,int a,float){assert(c->boundModel==c->newChara);c->played=a;c->request=0;c->newFlag=1;}
'''
t=r'''int main(){for(int action:{0xF1,0xF2})for(int result:{110,215,54,59}){
 BtlMgrChr c={};c.member={7,2};c.action=action;c.newChara=result;c.anim.next=0x1A4;
 /* The source has already consumed its request on an empty closing entry. */
 BtlChar_OnModelLoaded(&c);assert(c.played==0x1A4&&c.newFlag&&c.boundModel==result);
}
for(int id:{3,8,10,51,52,59,122,200,202,237}){BtlMgrChr c={};c.member={id,0};c.action=0xF1;c.newChara=110;assert(Port_GtBaseClosingReload(&c));BtlChar_OnModelLoaded(&c);assert(c.played==0x1A4);}
BtlMgrChr c={};c.member={7,2};c.action=0xEC;c.newChara=8;assert(!Port_GtBaseClosingReload(&c));c.request=1;c.anim.next=0x1A0;BtlChar_OnModelLoaded(&c);assert(c.played==0x1A0);
puts("PASS actual all-source fusion reload prefix +native FlushRequest: consumed source request restarted on committed result; ordinary transformations preserved");}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'test.cpp').write_text(h+'\n#include <initializer_list>\n'+flush+helper+prefix+t);subprocess.run(['c++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
