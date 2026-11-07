from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fun(path,name):
 s=(r/path).read_text();a=s.index(name);b=s.index('{',a);n=1;i=b+1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
text=fun('port/include/mod_story_runtime.inc','int Port_StoryRewardText(')
setup=fun('src/battle/view_a.c','void GetWin_Setup(')
code=r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int s32;typedef unsigned char u8;typedef unsigned short u16;
#define PORT 1
#define ST_COUNT 2
struct {const char*title[2];}stChapters[2]={{{"First","Primero"}},{{"The visitor","El visitante de Kame House"}}};
int language=1;int Port_Setting(const char*k,int d){return language;}
struct {int line1,line2,tall,box1,box2,se,kind;void*text[5];struct{int type;}items[10];}g,*gGetWin=&g;
int sPortStoryReward=-1;u16 sPortStoryRewardText[2][192];
#define PACK_AT(a,b) ((u16*)0)
int Font_CountLines(u16*s){return 0;}
"""+text+setup+r"""
int main(){
 GetWin_Setup(8,257);assert(g.kind==8&&g.se==8&&g.box1==3&&g.box2==4&&sPortStoryReward==1);
 assert(sPortStoryRewardText[0][13]==0xf3); // Historia Dragon: original native font codepoint
 assert(sPortStoryRewardText[1][0]=='E'&&sPortStoryRewardText[1][13]=='d');
 GetWin_Setup(9,0);assert(sPortStoryReward==-1&&g.kind==9&&g.line2==0x5b);
 GetWin_Setup(8,1);assert(sPortStoryReward==-1&&g.line1==0&&g.line2==9);
 language=0;GetWin_Setup(8,257);assert(sPortStoryRewardText[1][0]=='T');
 u16 out[4]={0};assert(Port_StoryRewardText(1,1,out,4)&&out[3]==0);
 assert(!Port_StoryRewardText(-1,0,out,4)&&!Port_StoryRewardText(2,0,out,4));
 puts("PASS original GetWin chapter reward: native kind8/animation/icon/jingle; custom EN/ES titles, UTF16 accents, bounded output, next ball/stock chapter reset");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
