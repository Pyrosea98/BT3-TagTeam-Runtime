from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd();host=(r/'port/src/gs/snd_adx.c').read_text();game=(r/'src/sys/adx.c').read_text();clash=(r/'src/battle/btl_char_flag_clash.c').read_text()
def ex(s,n):
 a=re.search(r'(?m)^(?:static )?(?:void|int|s32) '+n+r'\(',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
h=r"""#include <cassert>
#include <cstring>
#include <cstdint>
using s32=int; using u32=unsigned;
#define PORT 1
#define MAX_PLAYERS 6
#define FRAME 18
#define PAN_AUTO 99
#define ADX_CH_SE0 2
#define ADX_CH_SE1 3
#define ADX_CH_VOICE0 4
#define ADX_CH_VOICE1 5
struct Player{void*stream;unsigned char*file;uint32_t total,pos,size,data,channels;int loop;uint32_t loopStart,loopEnd;int paused,stat,pan[2],hist[2][2],c1,c2;};static Player sPlayers[6];static struct{int left;}gPortAdxView[6];static int locks,stops,net,replay,sMono;
void SDL_LockAudioStream(void*){locks++;}void SDL_UnlockAudioStream(void*){locks--;}static unsigned be16(const unsigned char*p){return p[0]*256+p[1];}
struct AdxPlayer{void*adxt;};static AdxPlayer gAdxPlayerTbl[6];void Adx_Stop(int);void Port_AdxRepeat(void*);
int BtlScript_IsSlotBusy(int,int){return 0;}void Adx_SetPan(int,int,int){}void Adx_SetVolume(int,int){}void ADXT_Pause(void*p,int a){((Player*)p)->paused=a;}
static unsigned char frames[128];void File_StartAdxById(void*h,int){auto*p=(Player*)h;p->file=frames;p->data=0;p->size=54;p->channels=1;p->total=96;p->loop=0;p->pos=0;p->stat=3;}
void ADXT_Stop(void*h){auto*p=(Player*)h;p->stat=0;p->file=nullptr;stops++;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}
"""
f=r"""int main(){for(int i=0;i<6;i++)gAdxPlayerTbl[i].adxt=&sPlayers[i];
for(int se=0;se<2;se++){
 auto*p=&sPlayers[se+2];int16_t out[8192];Port_StreamBeamLoop(se,1);Adx_Play(se+2,0x8D30,64,0);assert(p->loop&&gPortAdxView[se+2].left==-1);
 assert(decode(p,out,64)>96*4);p->paused=1;p->stream=p;Port_AdxRepeat(p);assert(p->paused&&locks==0);p->stream=nullptr;
 p->loop=1;p->loopStart=32;p->loopEnd=64;Port_AdxRepeat(p);assert(p->loopStart==32&&p->loopEnd==64);
 Port_StreamBeamLoop(se,0);assert(!p->file&&p->stat==0);
 Port_StreamBeamLoop(se,1);Adx_Play(se+2,0x8D30,64,0);Adx_Play(se+2,0x8D31,64,0);int before=stops;Port_StreamBeamLoop(se,0);assert(stops==before&&p->stat==3&&!p->loop);
 Adx_Play(se+2,0x8D30,64,0);assert(!p->loop&&decode(p,out,64)==96&&decode(p,out,64)==0);
 Port_StreamBeamLoop(se,1);Adx_Play(se+2,0x8D30,64,0);Adx_Stop(se+2);Adx_Play(se+2,0x8D30,64,0);assert(!p->loop);
}
Port_AdxRepeat(nullptr);Port_AdxRepeat((void*)((uintptr_t)sPlayers+1));Port_StreamBeamLoop(8,1);assert(locks==0);
}"""
start=game.index('static s32 sBeamSeWant');end=game.index('#endif',start)
body=ex(host,'Port_AdxRepeat').replace('Player*p=adxt','Player*p=(Player*)adxt')+ex(host,'decode')+'\n'+game[start:end]+ex(game,'Adx_Play')+ex(game,'Adx_Stop')
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(h+body+f);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
assert 'port_beam_audio_begin(chr0->player)' in clash and '(*state<1||*state>4)' in clash
print('PASS actual ADX decoder +beam stream ownership: sound survives>4clip lengths, authoredloop retained, originalpitch frames, pause+lock, both SE slots, ending replacement/stop/reset/null isolation; ASan/UBSan')
