from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/src/gs/mod_menu_ui.inc').read_text()
a=s.index('static std::atomic<unsigned> sStorySagaState');b=s.index('static void mod_story_publish(',a)
code=r"""
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "mod_story.h"
unsigned progress[ST_COUNT];int seen[8],writes;
unsigned Port_StoryProgress(int c){return progress[c];}
int Port_StorySagaState(int saga){return saga==0?3:0;}
int Port_Setting(const char*k,int d){int n=k[strlen(k)-1]-'0';return !strncmp(k,"story_guide_seen_saga",20)&&n>=0&&n<8?seen[n]:d;}
void Port_SettingSave(const char*k,int v){seen[k[strlen(k)-1]-'0']=v;}
void Port_SettingsWrite(){writes++;}
"""+s[a:b]+r"""
int main(){
 assert(ST_SagaCanOpen(0,0)&&!ST_SagaCanOpen(1,0));
 assert(ST_SagaCanOpen(1,1)&&!ST_SagaCanOpen(2,1)&&ST_SagaCanOpen(2,3));
 assert(!ST_SagaCanOpen(-1,255)&&!ST_SagaCanOpen(8,255));
 assert(ST_SagaChapterCount(0)==ST_COUNT&&ST_SagaFirst(1)==-1);
 unsigned p[ST_COUNT];for(int i=0;i<ST_COUNT;i++)p[i]=1;assert(!ST_SagaCleared(0,0,p));assert(ST_SagaCleared(0,1,p));p[1]=0;assert(!ST_SagaCleared(0,1,p));assert(!ST_SagaCleared(1,1,p));
 mod_story_sagas_publish();assert(sStoryWelcomeRequest[0]==1&&!writes&&seen[0]==0);
 sStoryWelcomeAck[0]=1;mod_story_sagas_publish();assert(seen[0]==1&&writes==1&&sStoryWelcomeRequest[0]==0);
 mod_story_sagas_publish();assert(writes==1&&sStoryWelcomeRequest[0]==0); // revisit/restart same persisted profile
 progress[0]=1;mod_story_sagas_publish();assert(sStoryWelcomeRequest[0]==2);
 sStoryWelcomeAck[0]=2;mod_story_sagas_publish();assert(seen[0]==2&&writes==2);
 for(int n=1;n<8;n++)assert(!sStoryWelcomeRequest[n]);
 puts("PASS saga gates: all prior finalized arcs required, partial demo never completes saga, unavailable saga stays closed; welcome acknowledged/persisted once, repeats quiet, next chapter gets new conversation");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code);subprocess.run(['g++','-std=c++17','-Iport/include','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
