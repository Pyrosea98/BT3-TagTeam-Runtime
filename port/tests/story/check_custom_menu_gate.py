from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/menu/menu_a.c').read_text();a=s.index('static s32 MainMenu_PortModes(void)');fn=s[a:s.index('\n#endif',a)]
code=r"""
#include <assert.h>
#include <stdio.h>
#include "gs/mod_menu_model.h"
typedef int s32;typedef unsigned char u8;
static struct {int flags,voiceLine;struct{int flags;}flash[4];} menu,*gMainMenu=&menu;
static struct{unsigned gamePressed,gameRepeat;}gPad[1];static unsigned char progress[0x800];static void*gProgress=progress;static int sPortModesDuel[2];
static ModMenuState state={MM_STORY_RESULT,4,0};static int steps,acks,native_choices;
#define MFLASH_PAD 1
#define MAINMENU_STARTED 2
#define MAINMENU_CHOOSE(n) (native_choices++)
int Port_ModMenuActive(void){return state.page!=MM_CLOSED;}int Port_NetSession(void){return 0;}void MainMenu_PortSceneAudio(int){}void Port_ModMenuReset(void){state.page=0;}int Port_ModesMenu(void){return 0;}
int Port_ModMenuStep(unsigned p,unsigned q,int*players,int*sound){steps++;int n=mm_step(&state,p,q,players,sound);if(n==42){acks++;return 0;}return n;}
void Snd_PlaySe(int,int){}void Voice_StopWithLip(void){}int Port_ModesChoosePlayers(int,int){return -1;}int Port_CoopActive(void){return 0;}int Port_CoopLeader(int){return 0;}
"""+fn+r"""
int main(){
 for(int ready=0;ready<4;ready++){
  state={MM_STORY_RESULT,4,0};menu.flags=(ready&1)?MAINMENU_STARTED:0;menu.flash[0].flags=(ready&2)?MFLASH_PAD:0;gPad[0].gamePressed=0x200;int before=steps,old=acks;
  assert(MainMenu_PortModes()==1);assert(steps==before+1&&acks==old+1&&state.page==MM_STORY&&state.cursor==4);assert(!native_choices);
 }
 state={MM_CLOSED,0,0};menu.flags=menu.flash[0].flags=0;int before=steps;assert(MainMenu_PortModes()==0&&steps==before);
 state={MM_STORY_RESULT,4,0};gPad[0].gamePressed=0;before=acks;assert(MainMenu_PortModes()==1&&state.page==MM_STORY_RESULT&&acks==before);
 puts("PASS actual native custom-menu gate + real result reducer: Cross leaves CH5 result despite native Flash not ready; ordinary native startup remains gated; no underlying native choice");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'gate';p.with_suffix('.cpp').write_text(code);subprocess.run(['c++','-w','-std=c++17','-fsanitize=address,undefined','-Iport/src','-Iport/include','-Iinclude',str(p.with_suffix('.cpp')),'port/src/gs/tag_tournament_core.c','-o',str(p)],check=True);subprocess.run([str(p)],check=True)
