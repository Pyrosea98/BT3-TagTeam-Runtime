from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/menu/menu_a.c').read_text();a=s.index('static s32 sPortSceneAudio,');b=s.index('static s32 MainMenu_PortModes(',a);fn=s[a:b]
head=r"""
#include <assert.h>
#include <stdio.h>
typedef int s32;
static int wanted,bgm,bgmCalls,se,seCalls,stops,stopped;
int Port_ModMenuAtmosphere(void){return wanted;}
void Bgm_Play(int id){bgm=id;bgmCalls++;}
void StreamSe_Play(int channel,int id,int vol,int pan){assert(channel==0&&vol==0x28&&!pan);se=id;seCalls++;stopped=0;}
void StreamSe_Stop(int channel){assert(channel==0);stops++;}
int StreamSe_IsStopped(int channel){assert(channel==0);return stopped;}
"""
foot=r"""
int main(void){MainMenu_PortSceneAudio(0);assert(!stops&&!seCalls&&!bgmCalls);
 wanted=1;MainMenu_PortSceneAudio(0);assert(bgm==0x10B18&&se==0x10BDB&&bgmCalls==1&&seCalls==1);
 for(int i=0;i<300;i++)MainMenu_PortSceneAudio(0);assert(bgmCalls==1&&seCalls==1);
 stopped=1;for(int i=0;i<120;i++)MainMenu_PortSceneAudio(0);assert(seCalls==2&&bgmCalls==1);
 wanted=2;MainMenu_PortSceneAudio(0);assert(stops==1&&bgm==0x10B1C&&se==0x10BDD);
 wanted=0;MainMenu_PortSceneAudio(0);assert(stops==2&&bgm==0x10B16);
 wanted=1;MainMenu_PortSceneAudio(0);int calls=bgmCalls;MainMenu_PortSceneAudio(1);assert(stops==3&&bgmCalls==calls);
 MainMenu_PortSceneAudio(1);assert(stops==3);
 puts("PASS actual native scene audio: distinct BGM/ambience, no per-frame restart, ended-loop recovery, back restores title, fight/exit cleans SE without restarting title");}
"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.c').write_text(head+fn+foot)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
