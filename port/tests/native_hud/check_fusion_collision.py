from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd()
def fn(s,name):
 m=re.search(r'^(?:s32|void) '+name+r'\([^;{}]*\)\s*\{',s,re.M);assert m,name;i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
head=r'''#include <cassert>
#include <cstdio>
using s32=int;
struct HitStatus{int lastHitNo=-1;};struct HitReact{int reaction=14;};struct HitObj{int hitNo=-1,unkCAC=0;};
struct HitChr{int player,freeze;HitStatus status;HitReact react;HitObj object;};using BtlMgrChr=HitChr;using BtlCharGetChr=HitChr;
HitChr actors[8];int count,consumed[8],bodyCalls,collisionCalls,frozenCalls;
int BtlChar_GetCount(){return count;}HitChr*BtlChar_Get(int i){return actors+i;}
int Port_FusionConsumedActor(int i){return consumed[i];}HitObj*BtlChar_GetObj(HitChr*c){return &c->object;}
int BtlChar_TestFlag(HitChr*,int){return 0;}void BtlChar_ClearFlag(HitChr*,int){}void BtlChar_SetHeldFlag(HitChr*,int){}
void BtlBodyHit_Update(){bodyCalls++;}void BtlColl_UpdateFrozen(HitChr*){frozenCalls++;}
'''
body=fn((r/'src/battle/btl_char_get.c').read_text(),'BtlChar_IsFrozen')+fn((r/'src/battle/btl_char_hit.c').read_text(),'BtlHit_BeginFrame')+'''void BtlColl_Update(){collisionCalls++;for(int i=0;i<count;i++)BtlHit_BeginFrame(actors+i);}'''+fn((r/'src/battle/btl_char_mgr.c').read_text(),'BtlChars_UpdateCollision')
tail=r'''int main(){for(count=2;count<=8;count++)for(int absent=-1;absent<count;absent++)for(int stopped=-1;stopped<count;stopped++){
bodyCalls=collisionCalls=frozenCalls=0;for(int i=0;i<count;i++){actors[i]=HitChr{};actors[i].player=i;actors[i].react.reaction=14;actors[i].freeze=i==stopped?3:0;consumed[i]=i==absent;}
BtlChars_UpdateCollision();int realStop=stopped>=0&&stopped!=absent;
assert(bodyCalls==!realStop&&collisionCalls==!realStop);assert(frozenCalls==(realStop?count:0));
for(int i=0;i<count;i++){assert(actors[i].react.reaction==(realStop?14:1));assert(BtlChar_IsFrozen(actors+i)==(i==absent||i==stopped));}
// Subsequent frames must keep clearing the old beam reaction while the
// hidden partner remains inert. Releasing it restores the native gate.
if(!realStop){for(int f=0;f<60;f++){BtlChars_UpdateCollision();assert(actors[0].react.reaction==1);}}
}
puts("PASS actual fusion collision gate: 2..8 actors, every consumed/real-hitstop combination; actual native reaction reset runs, consumed actor remains frozen, genuine hitstop retained, repeated-frame recovery");}'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+'\n#define PORT 1\n'+body+tail);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
