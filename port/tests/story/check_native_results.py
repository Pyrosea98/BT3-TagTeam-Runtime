from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def function(path,start,end):
 s=(r/path).read_text();return s[s.index(start):s.index(end,s.index(start))]
# Exercise the actual native result state/input/reward functions, not a parallel implementation.
head=r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef int s32;typedef long long s64;
#define PORT 1
#define SAVE_MONEY_MAX 999999999
struct {int money;} save,*gSaveData=&save;
"""
code=function('src/sys/save.c','void Save_AddMoney(', '\n#ifdef PORT\n/* Port Dragon')
head+=code
head+=r"""
#define MFLASH_PAD 2
#define HISTRESULT_STARTED 1
#define HISTRESULT_LOST 2
#define HISTRESULT_OUT 4
#define MSLOT_OUTRO_SEEN 8
struct {int flags;} slot[8];struct {int subMenu;} progress,*gProgress=&progress;
struct {int flags,unk8C,state,seState,points,pointsTotal,rewardCount,rewardIdx;struct {int flags;}flash[1];struct {int kind,id;}reward[20];} result,*gHistResult=&result;
struct {int gamePressed;}gPad[1];
struct {struct {int flags;}slot[8];} msave;
#define MSAVE (&msave)
static int sPortHistoryOutcome,sPortHistoryZeni,cleared,paid;
void Snd_PlaySe(int a,int b){}
int HistResult_IsMenuCleared(void){return cleared;}
void GetWin_Setup(int a,int b){}void GetWin_Next(void){}void GetWin_Open(void){}
void GetWin_Close(void){}int GetWin_IsAnimating(void){return 1;}
void Flash_GotoLabel(void*f,char*s,int a){}
"""
head+=function('src/menu/menu_c_c.c','void HistResult_Input(', '/* The point count')
head+=function('src/menu/menu_c_c.c','void HistResult_UpdateState(', '/* The jingle')
head+=r"""
int main(void){
 save.money=9999998;Save_AddMoney(3000);assert(save.money==10002998);
 save.money=999999998;Save_AddMoney(2147483647);assert(save.money==999999999);
 Save_AddMoney(-2147483647-1);assert(save.money==0);
 Save_AddMoney(-1);assert(save.money==0);
 save.money=10003000;sPortHistoryOutcome=2;result.pointsTotal=3000;
 result.state=1;HistResult_UpdateState();assert(result.state==2);
 while(result.state==2)HistResult_UpdateState();assert(result.points==3000);
 gPad[0].gamePressed=0x200;HistResult_UpdateState();assert(result.state==4);
 gPad[0].gamePressed=0;while(result.state==4)HistResult_UpdateState();
 assert(save.money==10003000&&result.points==0); // already paid: no duplicate
 result.state=0;result.flags=0;result.flash[0].flags=MFLASH_PAD;
 {int ret=0;HistResult_Input(&ret);assert(result.state==1);}
 result.flags=HISTRESULT_STARTED;result.unk8C=0;gPad[0].gamePressed=0x200;
 {int ret=0;HistResult_Input(&ret);assert(ret==1&&result.state==51);}
 // Failure also exits to the custom recap, not stock retry/outro.
 sPortHistoryOutcome=3;result.flags=HISTRESULT_STARTED|HISTRESULT_LOST;
 {int ret=0;HistResult_Input(&ret);assert(ret==1&&result.state==51);}
 // Native history still pays normally.
 sPortHistoryOutcome=0;save.money=10;result.state=4;result.points=100;
 HistResult_UpdateState();assert(save.money==110&&result.points==0);
 puts("PASS native History success/failure, count-up, no double pay; larger Zeni cap and signed overflow bounds");
}
"""
# Native seven-cell balance rendering uses original digit UVs and fits its existing field.
s=(r/'src/sys/gfxm_c.c').read_text();wide=s[s.index('static void FlashDraw_WideNumber('):s.index('\n#endif',s.index('static void FlashDraw_WideNumber('))]
number=r"""
#include <assert.h>
#include <stdio.h>
typedef int s32;typedef float f32;
typedef struct{int u0,v0,u1,v1,tex;}UV;
typedef struct{int flags;float pt[4][2];UV uv;int rgba[4];}FlashQuad;
typedef int FlashTex;
static int count;static float left,right;
void FlashDraw_Quad(FlashTex*t,FlashQuad*q){
 assert(q->uv.tex==0&&q->uv.u0>=0&&q->uv.u1<=256&&q->uv.v0>=0&&q->uv.v1<=192);
 assert(q->pt[1][0]>q->pt[0][0]);assert(q->pt[0][1]==20&&q->pt[2][1]==84);
 if(!count)right=q->pt[1][0];left=q->pt[0][0];count++;
}
"""+wide+r"""
int main(void){FlashQuad q={0};q.pt[0][0]=q.pt[2][0]=640;q.pt[1][0]=q.pt[3][0]=704;
 q.pt[0][1]=q.pt[1][1]=20;q.pt[2][1]=q.pt[3][1]=84;
 FlashDraw_WideNumber(0,&q,999999999,64,64,26);assert(count==9&&right==704&&left>=484&&left<500);
 count=0;FlashDraw_WideNumber(0,&q,10000000,64,64,26);assert(count==8);
 puts("PASS original atlas digits: 8/9 digits, fixed seven-cell width, same height, valid UVs");}
"""
with tempfile.TemporaryDirectory() as d:
 for name,body in [('results',head),('numbers',number)]:
  p=Path(d)/(name+'.c');p.write_text(body)
  subprocess.run(['cc','-std=gnu99','-DPORT','-fsanitize=address,undefined','-g',str(p),'-o',str(p.with_suffix(''))],check=True)
  subprocess.run([str(p.with_suffix(''))],check=True)
# Integration guards: result BEFORE save, custom branch avoids stock reward mutation.
s=(r/'src/menu/menu_a_b.c').read_text();b=s[s.index('if(Port_StoryTakeReturn())'):s.index('flags=0; /* A chapter')]
assert b.index('HistResult_Run(3)')<b.index('HistSave_Run(4)')
s=(r/'src/menu/menu_c_c.c').read_text();assert 'if (sPortHistoryOutcome)' in s[s.index('void HistResult_BuildRewards'):s.index('unlock[0] =')]
for h,m in [('menu_o.h','UO'),('menu_p.h','P'),('menu_r.h','SIM'),('menu_s.h','S')]:
 assert f'#define {m}_MONEY_MAX 999999999' in (r/'include/menu'/h).read_text()
print('PASS native result/save/recap integration and matching native-mode caps')

assert "sPortHistoryOutcome ? Port_StoryBalanceAfter() :" in (r/"src/menu/menu_c_c.c").read_text()
print("PASS custom native result displays actual post-reward total immediately")
