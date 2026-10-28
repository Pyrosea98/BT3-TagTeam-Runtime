from pathlib import Path
import ast,subprocess,tempfile
r=Path.cwd()
def extract(text,name):
 a=text.index(name+'(');a=text.rfind('\n',0,a)+1;b=text.index('{',a)+1;depth=1
 while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[a:b]
member=(r/'src/battle/btl_char_member.c').read_text()
next_=extract(member,'void BtlMember_NextSwitchTarget')
prev_=extract(member,'void BtlMember_PrevSwitchTarget')
get_=extract(member,'s32 BtlMember_GetSwitchTarget')
bench=extract((r/'port/src/plat_fighters.c').read_text(),'int Port_TagBenchAlive')
head=r'''#include <cassert>
#include <cstdio>
#include <cstring>
#define PORT 1
using s32=int;
struct BtlMemberGauge{int health;};struct BtlMember{int present,chara;BtlMemberGauge gauge;};
struct BtlMemberChr{int side,active,switchTarget,memberCount;BtlMember members[5];};
BtlMember*BtlMember_Get(BtlMemberChr*c,int i){return &c->members[i];}
BtlMemberGauge*BtlMember_GetGauge(BtlMemberChr*c,int i){return &c->members[i].gauge;}
static int sFusionMirror[12][5];
'''
impl=r'''int Port_IsFusionMirror(const void *chr,int slot){int side=*(const int*)chr;return side>=0&&side<12&&slot>0&&slot<5&&sFusionMirror[side][slot]!=0;}
'''
foot=r'''static void row(BtlMemberChr*c,int i,int present,int health){c->members[i].present=present;c->members[i].gauge.health=health;}
static unsigned char fake[12][0x9A4+5*0xA4];
static void frow(int side,int i,int present,int health){unsigned char*m=fake[side]+0x9A4+i*0xA4;*(int*)(m+8)=present;*(int*)(m+0x40)=health;}
int main(){
 /* The live regression (Juan 2026-10-26): a KO'd fighter must NOT tag in the mirrored ally who is
    already fighting as her own actor. Side 0: active member dead, slot1 = mirrored living ally. */
 memset(sFusionMirror,0,sizeof(sFusionMirror));
 BtlMemberChr c0{};c0.side=0;c0.active=0;c0.switchTarget=0;c0.memberCount=2;row(&c0,0,1,0);row(&c0,1,1,50000);
 sFusionMirror[0][1]=4 /* ally actor 3 + 1 */;
 BtlMember_NextSwitchTarget(&c0);assert(c0.switchTarget!=1);assert(c0.switchTarget==0);
 assert(BtlMember_GetSwitchTarget(&c0)!=1);
 BtlMember_PrevSwitchTarget(&c0);assert(c0.switchTarget!=1);
 *(int*)fake[0]=0;*(int*)(fake[0]+0x998)=2;frow(0,0,1,0);frow(0,1,1,50000);sFusionMirror[0][1]=4;
 assert(Port_TagBenchAlive((const void*)fake[0])==0); /* only the mirror is alive: no bench, no substitute */

 /* With a real (non-mirrored) living bench member the native switch still works. */
 c0.memberCount=3;row(&c0,2,1,60000);frow(0,2,1,60000);*(int*)(fake[0]+0x998)=3;
 BtlMember_NextSwitchTarget(&c0);assert(c0.switchTarget==2);
 BtlMember_PrevSwitchTarget(&c0);assert(c0.switchTarget==2);
 assert(Port_TagBenchAlive((const void*)fake[0])==1);

 /* Side>=2 mirrors (serial-fusion extras) are skipped the same way. */
 memset(sFusionMirror,0,sizeof(sFusionMirror));
 BtlMemberChr c2{};c2.side=2;c2.active=0;c2.switchTarget=0;c2.memberCount=3;row(&c2,0,1,0);row(&c2,1,1,40000);row(&c2,2,1,50000);
 sFusionMirror[2][1]=3;sFusionMirror[2][2]=4;
 BtlMember_NextSwitchTarget(&c2);assert(c2.switchTarget==0);
 assert(BtlMember_GetSwitchTarget(&c2)==0);

 /* Untouched native duo (no mirrors at all): the switch behaves exactly like the original game. */
 memset(sFusionMirror,0,sizeof(sFusionMirror));
 BtlMemberChr cn{};cn.side=1;cn.active=0;cn.switchTarget=0;cn.memberCount=2;row(&cn,0,1,0);row(&cn,1,1,70000);
 BtlMember_NextSwitchTarget(&cn);assert(cn.switchTarget==1);
 assert(BtlMember_GetSwitchTarget(&cn)==1);
 puts("PASS tag switch: mirrored fusion rows are never bench candidates (Next/Prev/Get), Port_TagBenchAlive blocks the KO substitute, real members and untouched native duos switch exactly as before");
 return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+impl+next_+prev_+get_+bench+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
