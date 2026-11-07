"""Actual game->UI reward snapshot and native ball presentation adapter."""
from pathlib import Path
import subprocess
import tempfile

r=Path.cwd()
source=(r/'port/src/gs/mod_menu_ui.inc').read_text()
start=source.index('static void mod_story_publish(')
end=source.index('\nstatic std::atomic<int> sCoopSetupView',start)
publish=source[start:end]
head=r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include "mod_story.h"
int Port_Setting(const char*,int d){return d;}
int Port_StoryHasCheckpoint(){return 0;}
int Port_StoryLegacyClear(int){return 0;}
static std::atomic<int> sStoryDensity,sStoryCheckpoint;
static std::atomic<int> sStoryChapter,sStoryBalance[2],sStoryClearRank,sStoryGrantedBall{-1},sStoryNewChapter{-1},sStoryNewSaga{-1};
static std::atomic<unsigned> sStoryView,sStoryUnlocked;
static int outcome,chapter,ball=-1,next=-1,progress[ST_COUNT];
int Port_StoryLastOutcome(int*c,int*z){*c=chapter;*z=200;return outcome;}
int Port_StoryBalanceBefore(){return 10000;}int Port_StoryBalanceAfter(){return 10200;}
int Port_StoryClearRank(int c){return c==0?3:1;}
unsigned Port_StoryProgress(int c){return progress[c];}
int Port_StoryUnlockedSaga(){return -1;}
int Port_StoryGrantedBall(){return ball;}int Port_StoryUnlockedChapter(){return next;}
static void mod_story_sagas_publish(){}
static int mod_story_unlocked(int c){return c<=0||(c<ST_COUNT&&progress[c-1]!=0);}
'''
tail=r'''
int main(){
 outcome=ST_CANON;chapter=0;ball=0;next=1;progress[0]=1;
 mod_story_publish(1);assert(sStoryChapter==0&&sStoryGrantedBall==0&&sStoryNewChapter==1&&sStoryUnlocked==3);
 assert(sStoryBalance[1]==10200&&sStoryClearRank==3);
 // Acknowledge then browse the next chapter: no stale rewards or wrong context.
 outcome=ST_NONE;ball=next=-1;mod_story_publish(1);
 assert(sStoryChapter==1&&sStoryGrantedBall==-1&&sStoryNewChapter==-1&&sStoryClearRank==1);
 outcome=ST_ALT;chapter=1;ball=6;mod_story_publish(0);
 assert(sStoryChapter==1&&sStoryGrantedBall==6&&sStoryNewChapter==-1);
 puts("PASS actual UI snapshot: recorded result overrides browse selection, both sphere endpoints, new chapter, balances/rank and no stale rewards after acknowledge");
}
'''
menu=(r/'src/menu/menu_c_c.c').read_text()
first=menu.index('void HistResult_BuildRewards(void) {')
last=menu.index('#endif',first)
adapter=menu[first:last]+'\n#endif\n}\n'
c=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int s32;
typedef struct{int unused;}HistReward;
typedef struct{HistReward reward;}HistProgress;
static HistProgress historyProgress;
#define HPROG (&historyProgress)
#define PORT 1
struct{struct{int kind,id;}reward[24];int rewardCount,pointsTotal;}result,*gHistResult=&result;
static int sPortHistoryOutcome=1,sPortHistoryZeni=200,ball=-1,next=-1;
int Port_StoryGrantedBall(void){return ball;}
int Port_StoryUnlockedChapter(void){return next;}
#define HR_ADD(k,v) {gHistResult->reward[gHistResult->rewardCount].kind=(k);gHistResult->reward[gHistResult->rewardCount++].id=(v);}
'''+adapter+r'''
int main(void){
 HistResult_BuildRewards();assert(result.pointsTotal==200&&result.rewardCount==0);
 ball=0;HistResult_BuildRewards();assert(result.rewardCount==1&&result.reward[0].kind==9&&result.reward[0].id==0);
 ball=6;HistResult_BuildRewards();assert(result.rewardCount==1&&result.reward[0].kind==9&&result.reward[0].id==6);
 HistResult_BuildRewards();assert(result.rewardCount==1); // no repeated accumulation
 ball=-1;HistResult_BuildRewards();assert(result.rewardCount==0);
 next=1;HistResult_BuildRewards();assert(result.rewardCount==1&&result.reward[0].kind==8&&result.reward[0].id==257);
 ball=0;HistResult_BuildRewards();assert(result.rewardCount==2&&result.reward[0].kind==9&&result.reward[1].kind==8);
 next=-1;ball=-1;HistResult_BuildRewards();assert(!result.rewardCount);
 puts("PASS actual native History adapter presents confirmed ball as reward9 once, clears absent/repeated reward, retains recorded Zeni");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)
    (p/'snapshot.cpp').write_text(head+publish+tail)
    subprocess.run(['g++','-std=c++17','-Iport/include','-fsanitize=address,undefined',str(p/'snapshot.cpp'),'-o',str(p/'snapshot')],check=True)
    subprocess.run([str(p/'snapshot')],check=True)
    (p/'adapter.c').write_text(c)
    subprocess.run(['cc','-fsanitize=address,undefined',str(p/'adapter.c'),'-o',str(p/'adapter')],check=True)
    subprocess.run([str(p/'adapter')],check=True)

assert "sModMenuGame.page!=MM_STORY_RESULT&&sModMenuGame.page!=MM_STORY_SAGAS&&r!=41" in source
