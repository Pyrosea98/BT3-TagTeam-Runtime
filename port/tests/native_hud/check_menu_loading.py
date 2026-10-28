from pathlib import Path
import subprocess,tempfile
r=Path.cwd();p=r/'port/src/fight_panels.c';s=p.read_text(encoding='utf-8')
gate=s[s.index('static int sSceneVisible;'):s.index('\nextern int gPortResim;')]
body=s[s.index('void PortPanels_GameTick(void) {'):]
head=r'''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "fight_panels.h"
#define GAME_PTR *
#define HUD_SHOW_GAUGES 1
typedef struct {char pad[0x30];unsigned flags;} FpHud;
static int gPortResim,sWritten,reads;static unsigned sSeq;static FpState sState;static FpActor sHudLastAlive[12];static int sPathKey[12];
static int dummy;static void *gBtlChars=&dummy,*gBtlSeq=&dummy;static FpHud hud={{0},1},*gHud=&hud;static unsigned char work[0x1a00];static int missing;
void *Battle_GetWork(void){return missing?0:work;}int rd(void*p,int off){int v;memcpy(&v,(char*)p+off,4);return v;}
void read_side(FpState*s,int i){s->side[i].found=1;}void read_actors(FpState*s){reads++;s->actorCount=1;s->actor[0].present=1;}void read_fusion_partners(FpState*s){(void)s;}void read_defuse_lights(FpState*s){(void)s;}
'''
foot=r'''int main(){FpState out;PortPanels_GameTick();assert(PortPanels_Read(&out)&&!out.battle&&!out.gauges&&!reads);
PortPanels_SetSceneVisible(1);PortPanels_GameTick();assert(PortPanels_Read(&out)&&out.gauges&&out.actorCount==1&&reads==1);
/* Clear must hide the old snapshot before another game tick, including during resimulation. */
gPortResim=1;PortPanels_SetSceneVisible(0);PortPanels_GameTick();assert(PortPanels_Read(&out)&&!out.battle&&!out.actorCount);gPortResim=0;PortPanels_GameTick();
assert(!sHudLastAlive[0].present);PortPanels_SetSceneVisible(1);
for(int flags=0;flags<3;flags++){int f=(int[]){0x2000,0x8000,0x4000}[flags];memcpy(work+0x19f0,&f,4);PortPanels_GameTick();assert(PortPanels_Read(&out)&&!out.gauges&&!out.actorCount&&reads==1);}
memset(work,0,sizeof(work));missing=1;PortPanels_GameTick();assert(PortPanels_Read(&out)&&!out.gauges&&reads==1);missing=0;PortPanels_GameTick();assert(PortPanels_Read(&out)&&out.gauges&&reads==2);
puts("PASS actual HUD producer/reader: stale snapshot hidden immediately; load/restart/pause/null-work blocked; fresh battle restored");}
'''
m=(r/'src/menu/menu_a.c').read_text(encoding='utf-8');a=m.index('    for (i = 0; i < MAINMENU_ITEM_MAX; i++) {');b=m.index('\n#ifdef PORT\n    /* the second sheet',a);loop=m[a:b]
f=m[m.index('static s32 PortMainMenu_PixelAt('):m.index('/* The label in the disc',m.index('static s32 PortMainMenu_PixelAt('))]
menu=r'''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef int s32;typedef unsigned char u8;typedef struct {u8 y,len,x,v;} PortLabelRun;
#define PORT 1
#define MAINMENU_ITEM_MAX 12
#define MAINMENU_TBO 11
static struct {int items[12],itemCount;} menu,*gMainMenu=&menu;static struct {int unlockFlags;} save={127},*gSaveData=&save;
int Port_NetMenuListed(void){return 1;}
void list(void){int i;
'''+loop+r'''}
'''+f+r'''
int main(){list();int online=0;for(int i=0;i<menu.itemCount;i++){assert(menu.items[i]!=4);online+=menu.items[i]==11;}assert(menu.itemCount==11&&online==1);
unsigned char tex[128]={0},pixels[65536];unsigned char *ptr=pixels-0x60;memcpy(tex+0x38,&ptr,sizeof(ptr));memset(pixels,99,sizeof(pixels));PortLabelRun run={4,2,10,17};PortMainMenu_WriteRuns(tex,3,&run,1);
for(int y=0;y<32;y++)for(int x=0;x<512;x++)assert(pixels[PortMainMenu_PixelAt(x,96+y)]==((y==4&&x>=10&&x<12)?17:0));
puts("PASS actual menu list/fallback writer: one online entry; old ISO text cleared across all 512 pixels");}
'''
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp)
 for name,code in [('hud',head+gate+body+foot),('menu',menu)]:
  src=t/(name+'.c');src.write_text(code,encoding='utf-8');exe=t/name
  subprocess.run(['gcc','-std=c99','-I'+str(r/'port/src/gs'),str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
b=(r/'src/battle/battle.c').read_text(encoding='utf-8')
assert b.index('PortPanels_SetSceneVisible(0);',b.index('s32 Battle_Main'))<b.index('Battle_Init();',b.index('s32 Battle_Main'))
assert b.index('PortPanels_SetSceneVisible(0);',b.index('    Battle_Loop();'))<b.index('    Battle_Term();',b.index('    Battle_Loop();'))
print('PASS native scene boundary callsites')
