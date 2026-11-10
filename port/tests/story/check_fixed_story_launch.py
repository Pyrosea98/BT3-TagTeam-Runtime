"""Real launch prefix: story commit before viewer, frames or active snapshots."""
from pathlib import Path
import subprocess
import tempfile

r=Path.cwd();s=(r/'port/include/mod_select_game.inc').read_text()
helper=s[s.index('static int ms_story_fixed('):s.index('int Port_ModSelectRun(')]
start=s.index('int Port_ModSelectRun(');end=s.index(' v.a=MS_Size(',start)
prefix=s[start:end]+' Test_ViewerStarted();return -2;\n}\n'
assert s.index('ms_story_fixed(&v)',start)<s.index('Port_ModSelectPublish(&v)',start)<s.index('ChrView_Init();',start)
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_story.h"
typedef unsigned int u32;
typedef int Pad;
static struct{unsigned flags;}progress,*gProgress=&progress;
#define MPROG_FREEZE 8
static int story,chapter,assetOk=1,fixedResult=2,publishes,activePublishes,commits,viewer,clears,lastEvent;
static MsView plan;static MsPick *sMsTourOutput;
int Port_StoryActive(void){return story;}
int Port_ModesFight(void){return 1;}int Port_ModSelectPlayerCount(void){return 1;}
int Port_ScenarioDraft(MsView*v){ST_BuildDraft(v,chapter);return fixedResult;}
void Port_ModSelectClear(void){clears++;}
void Port_ModSelectPublish(const MsView*v){publishes++;if(v->active)activePublishes++;}
int Port_ModSelectCommit(const MsView*v){commits++;if(!assetOk||!MS_Valid(v))return 0;plan=*v;return 1;}
void Port_ModSelectTrace(int event,int a,int b){lastEvent=event;}
void Test_ViewerStarted(void){viewer++;}
'''+helper+prefix+r'''
int main(void){
 for(chapter=0;chapter<ST_COUNT;chapter++){
  story=1;progress.flags=MPROG_FREEZE;int before=publishes;
  assert(Port_ModSelectRun()==1&&publishes==before+1&&!activePublishes&&!viewer&&lastEvent==3);
  assert(!plan.active&&MS_Valid(&plan)&&!(progress.flags&MPROG_FREEZE));
  if(chapter==0)assert(plan.total==2&&plan.pick[0].costume==1);
  else if(chapter==1)assert(plan.total==5&&plan.pick[0].costume==2&&plan.pick[1].costume==1&&plan.pick[3].costume==3);
  else if(chapter==2)assert(plan.total==4&&plan.pick[0].costume==3&&plan.pick[1].costume==0&&plan.pick[3].costume==3);
  else if(chapter==5)assert(stCasts[chapter].actor[2].costume==stCasts[chapter].actor[7].costume&&stCasts[chapter].actor[3].costume==stCasts[chapter].actor[8].costume);
  else if(chapter==6)assert(stCasts[chapter].actor[0].costume==1&&stCasts[chapter].actor[5].costume==1&&stCasts[chapter].actor[3].costume==0&&stChapterPower[chapter][3]==3500);
  else if(chapter==17)assert(stCasts[chapter].actor[2].costume==1&&stCasts[chapter].actor[3].costume==1);
  else assert(plan.total<=stCasts[chapter].count&&plan.pick[0].chara==stCasts[chapter].actor[0].chara&&plan.total<=16);
 }
 assetOk=0;assert(Port_ModSelectRun()==0&&!activePublishes&&!viewer&&lastEvent==4);
 assetOk=1;fixedResult=0;assert(Port_ModSelectRun()==0&&!activePublishes&&!viewer);
 story=0;int before=publishes;assert(Port_ModSelectRun()==-2&&viewer==1&&publishes==before);
 puts("PASS actual launch prefix: all authored story drafts commit without viewer/frame/input/active snapshot, canonical costumes retained; invalid assets fail closed; ordinary selector/Scenario path retained");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'fixed.c';p.write_text(code)
    subprocess.run(['cc','-w','-Iinclude','-Iport/include','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
    subprocess.run([str(p.with_suffix(''))],check=True)
