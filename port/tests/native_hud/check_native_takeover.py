from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_char_flag_opp.c').read_text()
def fn(name):
 start=s.index(name);start=s.rfind('\n',0,start)+1;brace=s.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
body=s[s.index('static int sControlOwner'):s.index('/* Input bridge:')]
for name in ['static int Port_TakeoverCandidate(', 'int Port_TakeoverHud(', 'static void Port_UpdateSpectate(', 's32 Port_Spectated(']:body+='\n'+fn(name)
head=r"""#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
using s32=int;using u32=unsigned;using u8=unsigned char;
#define PORT_FIGHTERS 10
#define PAD_L2 0x100
#define PAD_SQUARE 0x8000
struct BtlFlagChr{int player,pad;unsigned char rest[0x1600-8];};struct Pad{unsigned held,pressed;};static BtlFlagChr actors[4];static Pad pads[4];
static int dead[4],teams[4]={0,1,0,1},net,replay,enabled=1,fallen=1,press,freeze,cut,seq=3;static int sPortWatch[10],sPortWatchWait[10];
BtlFlagChr*BtlChar_Get(int i){return &actors[i];}int BtlChar_GetCount(){return 4;}int BtlChar_IsDead(BtlFlagChr*c){return dead[c->player];}int BtlChar_IsFrozen(void*){return freeze;}int ChrCam_IsCutActive(void*){return cut;}
int BattleSide_IsCpu(int i){return i!=0;}int Port_TargetTestFlick(int){return 0;}int Port_FightPad(int){return -1;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlSeq_GetState(){return seq;}
int Port_Setting(const char*k,int d){if(!strcmp(k,"spectator_takeover_enabled"))return enabled;if(!strcmp(k,"spectate_fallen_fighters"))return fallen;return d;}
int Port_IsEnemy(int a,int b){return teams[a]!=teams[b];}int Port_WatchOk(int,int k){return !dead[k];}int Port_WatchNext(int,int){return 2;}void Port_SpectateLog(int,int,int){}unsigned Port_PadPressed(int p){return pads[p].pressed;}Pad*Port_Pad(int p){return &pads[p];}static int assistRedirect=-1;int Port_FusionViewOwner(int who){return who;}
int Port_AssistView(int who){return who==2?assistRedirect:-1;}void BtlInput_Init(void*){}void BtlAct_ClearQueue(void*){}
"""
foot=r"""int main(){for(int i=0;i<4;i++){actors[i].player=i;actors[i].pad=i;sPortWatch[i]=-1;}Port_TakeoverReset();dead[0]=1;sPortWatch[0]=2;sPortWatchWait[0]=60;
 assert(Port_TakeoverCandidate(0)==2&&Port_TakeoverHud(2)==1);net=1;assert(Port_TakeoverCandidate(0)==-1);net=0;replay=1;assert(Port_TakeoverCandidate(0)==-1);replay=0;freeze=1;assert(Port_TakeoverCandidate(0)==-1);freeze=0;teams[2]=1;assert(Port_TakeoverCandidate(0)==-1);teams[2]=0;enabled=0;assert(Port_TakeoverCandidate(0)==-1);enabled=1;
 pads[0].pressed=PAD_SQUARE;Port_UpdateSpectate();assert(Port_TargetHuman(0)&&!Port_TargetHuman(2));pads[0].held=PAD_L2;Port_UpdateSpectate();assert(!Port_TargetHuman(0)&&Port_TargetHuman(2));assert(actors[2].pad==0&&*(int*)((char*)&actors[2]+0x1278)==0&&*(int*)((char*)&actors[0]+0x1278)==1);assert(Port_Spectated(0)==2&&Port_TakeoverHud(2)==2);assistRedirect=1;assert(Port_Spectated(0)==1);assistRedirect=-1;assert(Port_Spectated(0)==2);assert(Port_TakeoverCandidate(0)==-1);dead[0]=0;assert(!Port_TargetHuman(0));*(int*)((char*)&actors[2]+0x1278)=1;Port_TakeoverApply(&actors[2]);assert(*(int*)((char*)&actors[2]+0x1278)==0);Port_TakeoverReset();assert(Port_TargetHuman(0)&&!Port_TargetHuman(2));
 puts("PASS actual native takeover: L2+Square, living allied CPU ownership, no duplicate human ownership, online/replay/frozen/enemy/disabled rejection, viewport follow, transform persistence, round reset; ASan/UBSan");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
