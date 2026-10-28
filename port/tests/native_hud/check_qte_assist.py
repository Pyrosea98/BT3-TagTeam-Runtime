from pathlib import Path
import ast,subprocess,tempfile
r=Path.cwd()
def ex(s,name):
 a=s.index(name+'(');a=s.rfind('\n',0,a)+1;i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
tree=ast.parse((r/'port/tests/native_hud/check_native_assists.py').read_text());v={}
for node in tree.body:
 if isinstance(node,ast.Assign) and isinstance(node.value,ast.Constant):
  for k in node.targets:
   if isinstance(k,ast.Name):v[k.id]=node.value.value
head=v['head'].replace('alignas(8) unsigned char data[0x1600];','union{alignas(8) unsigned char data[0x1600];struct{char pad[12];int objId;};};')
head+='''
using BtlMgrChr=Actor;using s32=int;struct Obj{int flags;};static Obj objs[4];static int hidden[4],nativeAnim[4];static float phase[4];
int BtlChar_TestFlag(void*p,int f){return f==0xB?hidden[idx(p)]:0;}
Obj*BtlChar_GetObj(void*p){return &objs[idx(p)];}int Port_AboutPlayer(int){return 0;}
int BtlAnim_GetId(BtlMgrChr*c){return nativeAnim[idx(c)];}float BtlAnim_GetProgress(BtlMgrChr*c){return phase[idx(c)];}
'''
mgr=(r/'src/battle/btl_char_mgr.c').read_text()
sample=ex(mgr,'Port_AssistQteSample').replace('c=BtlChar_Get(leader);','c=(BtlMgrChr*)BtlChar_Get(leader);')
visibility=mgr[mgr.index('void BtlChars_UpdateObjFlag2(void)'):mgr.index('/* Clears bit 1')].replace('chr = BtlChar_Get(i);','chr = (BtlMgrChr*)BtlChar_Get(i);').replace('chr=BtlChar_Get(i);','chr=(BtlMgrChr*)BtlChar_Get(i);')
foot=r'''int main(){init();for(int i=0;i<4;i++)actors[i].objId=i;
 sAssist[2].state=2;sAssist[2].kind=2;sAssist[2].paid=1;sAssist[2].target=1;action[2]=7;
 int anim=-1;float ratio=-1;
 for(int a:{0xFB,0xFC}){action[0]=a;
  for(int m:{0xF6,0x4C,0x3B,0x4F,0x40})for(float p:{0.f,.1f,.5f,1.f}){
   nativeAnim[0]=m;phase[0]=p;assert(Port_AssistQteLeader(2)==0);
   assert(Port_AssistQteSample(2,&anim,&ratio)&&anim==m&&ratio==p);
  }
 }
 for(int h:{0,1,0,1,0}){hidden[0]=h;BtlChars_UpdateObjFlag2();assert((objs[2].flags&2)==(h?0:2));}
 sAssist[2].state=0;hidden[0]=1;BtlChars_UpdateObjFlag2();assert(objs[2].flags&2);sAssist[2].state=2;
 phase[0]=NAN;assert(!Port_AssistQteSample(2,&anim,&ratio));phase[0]=INFINITY;assert(!Port_AssistQteSample(2,&anim,&ratio));phase[0]=2;assert(Port_AssistQteSample(2,&anim,&ratio)&&ratio==1);
 nativeAnim[0]=0x175;assert(!Port_AssistQteSample(2,&anim,&ratio));nativeAnim[0]=0x4C;
 for(int a:{0xFA,11,304}){action[0]=a;assert(Port_AssistQteLeader(2)==-1);}action[0]=0xFC;
 action[2]=183;assert(Port_AssistQteLeader(2)==-1);action[2]=7;net=1;assert(Port_AssistQteLeader(2)==-1);net=0;replay=1;assert(Port_AssistQteLeader(2)==-1);replay=0;
 members[0][0x40/4]=0;assert(Port_AssistQteLeader(2)==-1);members[0][0x40/4]=100000;
 sAssist[2].paid=0;assert(Port_AssistQteLeader(2)==-1);sAssist[2].paid=1;clash=0;assert(Port_AssistQteLeader(2)==-1);clash=1;
 assert(Port_AssistQteLeader(1)==-1);Port_AssistReset();assert(Port_AssistQteLeader(2)==-1);
 puts("PASS actual QTE assist lookup/sample/visibility: 2actions x5motions x4phases, native vanish timing/restoration, ending fallback, badphase, payment/interruption/death/reset/online/replay isolation; ASan/UBSan");}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text('#define PORT 1\n#include <initializer_list>\n'+head+(r/'port/src/mod_assists_native.inc').read_text()+sample+visibility+v['foot'].split('int main()')[0]+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
