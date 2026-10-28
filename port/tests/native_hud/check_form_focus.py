from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd()
def ex(s,n):
 a=re.search(r'(?m)^[^;\n]*\b'+n+r'\([^;]*?\)\s*\{',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
s=(r/'src/battle/btl_char_mgr.c').read_text()
head=r"""#include <cassert>
#include <cstring>
using s32=int;using u32=unsigned;using u8=unsigned char;
#define PORT 1
struct BtlMgrChr{int player,action,freeze,freezeNext,freezeDelay;char rest[0x94c-20];int queue,prev,followup[4];};static BtlMgrChr actors[10];static int gPortTarget[10],flags[10][0x140],defuse[10],net,replay,loadOwner=-1;
int BtlChar_GetCount(){return 10;}BtlMgrChr*BtlChar_Get(int i){return &actors[i];}int BtlAct_GetCurrent(BtlMgrChr*c){return c->action;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int Port_DefusionPresenting(int i){return defuse[i];}
int BtlChar_TestFlag(BtlMgrChr*c,int bit){return flags[c->player][bit];}int Port_ChangeOwner(){return loadOwner;}int Port_AboutPlayer(int){return 0;}void Port_FreezeLog(int,int,int,int,int){}void Port_HoldLog(int,int,int){}void Port_FreezeCount(int,int){}
"""
foot=r"""void init(){memset(actors,0,sizeof(actors));memset(flags,0,sizeof(flags));memset(defuse,0,sizeof(defuse));for(int i=0;i<10;i++){actors[i].player=i;actors[i].action=7;actors[i].queue=-1;for(int k=0;k<4;k++)actors[i].followup[k]=-1;gPortTarget[i]=(i+1)%10;}}
int main(){for(int owner=0;owner<10;owner++){init();actors[owner].action=0xEC;assert(Port_FormPresentationOwner()==owner);BtlChars_UpdateFreeze();for(int i=0;i<10;i++)assert(actors[i].freeze==(i!=owner));actors[owner].action=7;BtlChars_UpdateFreeze();for(auto&a:actors)assert(!a.freeze);}
 init();actors[3].queue=0xEF;assert(Port_FormPresentationOwner()==3);actors[5].action=0xF1;assert(Port_FormPresentationOwner()==5);
 init();actors[2].action=0x105;flags[2][0x125]=1;BtlChars_UpdateFreeze();assert(actors[3].freeze&&!actors[8].freeze&&!actors[2].freeze);
 for(int loader=0;loader<10;loader++)for(int fuser=0;fuser<10;fuser++)if(loader!=fuser){
  init();loadOwner=loader;actors[loader].action=0x104;actors[fuser].queue=0xF1;
  assert(Port_FormPresentationOwner()==loader);BtlChars_UpdateFreeze();assert(!actors[loader].freeze&&actors[fuser].freeze);
  loadOwner=-1;assert(Port_FormPresentationOwner()==fuser);BtlChars_UpdateFreeze();assert(!actors[fuser].freeze);
 }loadOwner=-1;
 for(int slot=0;slot<4;slot++){init();loadOwner=9;actors[9].action=0x104;actors[4].followup[slot]=0xF1;
 assert(Port_FormPresentationOwner()==9);BtlChars_UpdateFreeze();assert(!actors[9].freeze&&actors[4].freeze);loadOwner=-1;assert(Port_FormPresentationOwner()==4);}
 init();loadOwner=9;actors[9].action=0x104;actors[4].action=0xF1;
 assert(Port_FormPresentationOwner()==9);BtlChars_UpdateFreeze();assert(!actors[9].freeze&&actors[4].freeze);
 loadOwner=-1;assert(Port_FormPresentationOwner()==4);BtlChars_UpdateFreeze();assert(!actors[4].freeze&&actors[9].freeze);
 init();loadOwner=8;actors[8].action=0x105;assert(Port_FormPresentationOwner()==-1);loadOwner=-1;
 init();defuse[4]=1;actors[4].action=0xB4;assert(Port_FormPresentationOwner()==4);BtlChars_UpdateFreeze();assert(!actors[5].freeze);net=1;assert(Port_FormPresentationOwner()==-1);net=0;replay=1;assert(Port_FormPresentationOwner()==-1);
}
"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(head+ex(s,'Port_FormPresentationOwner')+ex(s,'BtlChars_UpdateFreeze')+foot);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('PASS actual global form focus/freeze: all10owners, pending queue, active priority, automatic release, unrelated attack combat remains local, paired defusion not frozen, online/replay isolation;90 loader/fuser ownership pairs drain safely')
