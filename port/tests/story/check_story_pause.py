from pathlib import Path
import re,subprocess,tempfile
src=Path("src/battle/hud_0.c").read_text()
start=src.index("BtlMenu *PauseMenu_GetMenu(s32 which)")
brace=src.index("{",start);level=1;end=brace+1
while level:
 level += (src[end]=="{")-(src[end]=="}");end+=1
body=src[start:end]
names=sorted(set(re.findall(r"gPauseMenu\w+",body)))
code="""#include <assert.h>
#include <stddef.h>
typedef int s32;
typedef struct BtlMenuItem {int type,id,disabled;void(*func)(void);} BtlMenuItem;
typedef struct BtlMenu {BtlMenuItem*items;int count,open;} BtlMenu;
#define BTL_MENU_ITEM_BACK 2
static int active=1,mode;
int Port_StoryActive(void){return active;}
int Battle_GetMode(void){return mode;}
int BattleReplay_IsActive(void){return 0;}
int BattleReplay_IsLoaded(void){return 0;}
void PauseMenu_SkillListFunc(void){}
"""+"BtlMenu "+",".join(names)+";\n"+body+"""
int main(){BtlMenuItem items[5]={{2,1,1,0},{2,8,0,0},{3,9,0,PauseMenu_SkillListFunc},{3,20,0,0},{3,99,0,0}};
BtlMenu*m;gPauseMenuMode1.items=items;gPauseMenuMode1.count=5;
m=PauseMenu_GetMenu(0);assert(m->count==3&&m->items[0].id==8&&m->items[1].func==PauseMenu_SkillListFunc&&m->items[2].id==20);
assert(PauseMenu_GetMenu(0)==m);active=0;assert(PauseMenu_GetMenu(0)==&gPauseMenuVersus);mode=1;assert(PauseMenu_GetMenu(0)==&gPauseMenuMode1);assert(PauseMenu_GetMenu(1)==&gPauseMenuResultMode1);}
"""
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/"pause.c";exe=Path(td)/"pause";c.write_text(code)
 subprocess.run(["cc","-DPORT","-fsanitize=address,undefined",str(c),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print("PASS actual Story pause function: Continue / Skill list / Exit only; ordinary modes and results retain native menus")
