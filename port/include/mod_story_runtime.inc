/* Dragon History chapters: game-thread state. Included by mod_scenarios_runtime.inc, whose
   fixed-draft fight path a chapter rides on (Port_ScenarioActive is true while one is chosen). */
#include "mod_story.h"
#include <stdio.h>
extern void Save_AddMoney(int amount);
extern int Save_GetStoryBits(void);
extern void Save_OrStoryBits(int bits);
extern int Save_GetStoryBitsGroup(int group);
extern void Save_OrStoryBitsGroup(int group,int bits);
extern int Save_GetMoney(void);
extern int Save_GetStoryRank(int chapter);
extern void Save_RaiseStoryRank(int chapter,int rank);
extern void Save_ClearStoryRank(int chapter);
extern int Save_GrantDragonBall(int ball);
extern int BattleReplay_IsActive(void),Port_NetActive(void);
/* Dragon Balls: a story fight hides the first ball the save lacks (battle_load.c BattleSetup_FinishEx); breaking
   an item holder may find it (stg_a_b.c BtlStage_DestroyObj, 1 in 8). Found = 1..7 for this fight; granted =
   0..6 once the successful result put it into the save, for the result screen; -1 none. */
static int sStFoundBall,sStGrantedBall=-1,sStNewChapter=-1,sStNewSaga=-1;
int Port_StoryDragonBallSearch(void);
int Port_StoryDragonBallAlways(void){return Port_StoryDragonBallSearch()&&Port_Setting("story_dragonball_always",0)!=0;}
int Port_StoryDragonBallSearch(void){return Port_StoryActive()&&!BattleReplay_IsActive()&&!Port_NetActive();}
void Port_StoryFoundBall(int ball){if(Port_StoryActive())sStFoundBall=ball>=1&&ball<=7?ball:0;}
int Port_StoryGrantedBall(void){return sStGrantedBall;}
int Port_StoryFoundBallValue(void){return sStFoundBall;}
/* Native GetWin text: original font consumes UTF-16 codepoints, not UTF-8 bytes. */
int Port_StoryRewardText(int chapter,int line,unsigned short*out,int capacity){
 const unsigned char*p;int n=0;unsigned c;
 if(!out||capacity<1||chapter<0||chapter>=ST_COUNT)return 0;
 p=(const unsigned char*)(line==0?(Port_Setting("mod_language",1)==1?"Historia Drag\303\263n":"Dragon History"):stChapters[chapter].title[Port_Setting("mod_language",1)==1?1:0]);
 while(*p&&n<capacity-1){c=*p++;if(c>=0xc0&&c<0xe0&&p[0]){c=((c&31)<<6)|(p[0]&63);p++;}else if(c>=0xe0&&c<0xf0&&p[0]&&p[1]){c=((c&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}out[n++]=(unsigned short)c;}
 out[n]=0;return 1;
}
int Port_StoryUnlockedChapter(void){return sStNewChapter;}
int Port_StoryUnlockedSaga(void){return sStNewSaga;}
void Port_StoryLogBallSearch(int chapter,int holders){if(chapter>=0&&chapter<ST_COUNT)fprintf(stderr,"bt3: story dragon ball search chapter %s item holders %d\n",stChapters[chapter].id,holders);}
static int sStChapter=-1,sStLaunched=-1,sStOutcome,sStOutcomeChapter=-1,sStZeni;
static int sStDifficulty=1,sStInterludeShown,sStWishShown;
void Port_StoryInterludeShow(int on){sStInterludeShown=on!=0;}
int Port_StoryInterludeLoading(void){return ST_Interlude(sStChapter)&&!sStInterludeShown;}
int Port_StoryDifficulty(void){return sStDifficulty;}
static int sStCommitted,sStReturn,sStBalanceBefore,sStBalanceAfter;
static unsigned sStEvents;
/* Checkpoint of a phased chapter (this session): the chapter and the phase a retry after a failure resumes at. */
static int sStCheckChapter=-1,sStCheckEvent,sStResume,sStCheckFailed,sStCheckDifficulty,sStCheckDensity,sStRetryRequested;
int Port_StoryHasCheckpoint(void){return sStCheckChapter>=0&&sStCheckFailed&&sStCheckDifficulty==Port_Setting("story_difficulty",1)&&sStCheckDensity==Port_Setting("story_density_full",0);}
void Port_StoryRequestCheckpoint(void){sStRetryRequested=Port_StoryHasCheckpoint();}
void Port_StoryRestartCheckpoint(void){sStRetryRequested=0;sStCheckChapter=-1;sStCheckEvent=sStResume=sStCheckFailed=0;}
void Port_StoryCheckpoint(int event){if(Port_StoryActive()&&event>0){sStCheckChapter=sStChapter;sStCheckEvent=event;sStCheckDifficulty=sStDifficulty;sStCheckDensity=Port_Setting("story_density_full",0);}}
/* The phase this fight resumes at (0: from the start). */
int Port_StoryResumeEvent(void){return Port_StoryActive()&&sStResume&&sStCheckChapter==sStChapter?sStCheckEvent:0;}
/* The subtitle of a story scene, for the UI thread: (chapter<<16 | set<<8 | line) + 1, 0 = none. */
static int sStSubtitle;
void Port_StorySubtitle(int chapter,int set,int line){__atomic_store_n(&sStSubtitle,chapter<0||chapter>=ST_COUNT?0:((chapter<<16)|((set&255)<<8)|(line&255))+1,__ATOMIC_RELEASE);}
void Port_StoryLogStats(int chapter,int side,int obj,int health,int healthMax){if(chapter>=0&&chapter<ST_COUNT)fprintf(stderr,"bt3: story stats chapter %s side %d obj %d health %d / %d\n",stChapters[chapter].id,side,obj,health,healthMax);}
int Port_StorySubtitleView(int *chapter,int *set,int *line){int v=__atomic_load_n(&sStSubtitle,__ATOMIC_ACQUIRE)-1;if(v<0)return 0;if(chapter)*chapter=v>>16;if(set)*set=(v>>8)&255;if(line)*line=v&255;return 1;}
static int sStPromptView;
void Port_StoryPrompt(int on){__atomic_store_n(&sStPromptView,on!=0,__ATOMIC_RELEASE);}
int Port_StoryPromptView(void){return __atomic_load_n(&sStPromptView,__ATOMIC_ACQUIRE);}
static unsigned sStPreludeView;
void Port_StoryPresentation(int chapter,int phase,int progress){unsigned v=0;if(chapter>=0&&chapter<ST_COUNT&&phase>=1&&phase<=2){if(progress<0)progress=0;if(progress>65535)progress=65535;v=((unsigned)phase<<24)|((unsigned)chapter<<16)|(unsigned)progress;}__atomic_store_n(&sStPreludeView,v,__ATOMIC_RELEASE);}
int Port_StoryPresentationView(int*chapter,int*phase,int*progress){unsigned v=__atomic_load_n(&sStPreludeView,__ATOMIC_ACQUIRE);if(!v)return 0;if(chapter)*chapter=(v>>16)&255;if(phase)*phase=v>>24;if(progress)*progress=v&65535;return 1;}
int Port_StoryBalanceBefore(void){return sStBalanceBefore;}
int Port_StoryBalanceAfter(void){return sStBalanceAfter;}
int Port_StoryCpuLevel(void){int level=sStChapter>=0?sStDifficulty:Port_Setting("story_difficulty",1);return level<=0?6:level>=2?21:13;}
int Port_StoryTakeReturn(void){
 if(!sStReturn&&sStLaunched<0)return 0;
 if(sStOutcome==ST_NONE){if(sStCheckChapter==sStLaunched)sStCheckFailed=1;sStOutcome=ST_FAIL;sStOutcomeChapter=sStLaunched;sStBalanceBefore=sStBalanceAfter=Save_GetMoney();sStZeni=0;}
 sStReturn=0;sStLaunched=-1;return 1;
}
/* Progress is kept in the game's own save (src/sys/save.c Save_GetStoryBits): two bits per
   chapter, chapter n at bit 2n. It reaches the memory card whenever the game saves. */
/* Four chapters per group: group 0 is slot0's byte (the original four), group g the byte of slot g. */
#define ST_PROGRESS_GROUPS 9
static unsigned st_group_bits(int group){return group==0?(unsigned)Save_GetStoryBits():(unsigned)Save_GetStoryBitsGroup(group);}
static void st_group_or(int group,int bits){if(group==0)Save_OrStoryBits(bits);else Save_OrStoryBitsGroup(group,bits);}
/* The two expanded battles must not inherit a partial legacy node's clear/rank.
   Physical progress/rank slots32/33 were unused in the17-node format. Original bits remain untouched. */
static int st_save_index(int ch){return ch==6?32:ch==11?33:ch>=32?ch+2:ch;}
static unsigned st_raw_progress(int ch){return ch>=0&&ch<4*ST_PROGRESS_GROUPS?(st_group_bits(ch/4)>>(2*(ch%4)))&3u:0;}
static unsigned st_progress(int chapter){return chapter>=0&&chapter<ST_COUNT?st_raw_progress(st_save_index(chapter)):0;}
int Port_StoryLegacyClear(int ch){int i,first=ch==6?6:ch==11?11:-1,last=ch==6?10:16;if(first<0)return 0;for(i=first;i<=last;i++)if(!st_raw_progress(i))return 0;return 1;}

int Port_StoryActive(void){return sStChapter>=0&&sStChapter<ST_COUNT;}
int Port_StoryChapter(void){return Port_StoryActive()?sStChapter:-1;}
/* Port_ScenarioClear (main menu, other modes) ends the chapter; the outcome and the
   launched marker stay for the chapter page shown when the menu resumes. */
void Port_StoryClear(void){Port_StoryPrompt(0);sStChapter=-1;Port_StoryPresentation(-1,0,0);}
void Port_StoryChoose(int chapter){sStWishShown=0;sStInterludeShown=0;sStResume=sStRetryRequested&&chapter==sStCheckChapter&&sStCheckFailed&&sStCheckDifficulty==Port_Setting("story_difficulty",1)&&sStCheckDensity==Port_Setting("story_density_full",0);sStRetryRequested=0;sStCheckFailed=0;if(!sStResume)sStCheckChapter=-1;int d=Port_Setting("story_difficulty",1);sStDifficulty=d<0?0:d>2?2:d;Port_StorySubtitle(-1,0,0);Port_StoryClear();sStOutcome=ST_NONE;sStOutcomeChapter=-1;sStZeni=0;sStLaunched=-1;sStCommitted=sStReturn=0;sStFoundBall=0;sStEvents=0;sStGrantedBall=sStNewChapter=sStNewSaga=-1;if(chapter>=0&&chapter<ST_COUNT)sStChapter=chapter;}
/* A story fight has started (round reset); until its result it counts as left unfinished. */
void Port_StoryFightStarted(void){
 /* Explicit isolated release audit fixture; never enabled by installed launchers. */
 if(getenv("BT3_STORY_COMPLETION_PROBE")&&sStChapter==11){int ch;for(ch=0;ch<ST_COUNT;ch++)if(stChapters[ch].saga==0&&ch!=11){int k=st_save_index(ch);st_group_or(k/4,3<<(2*(k%4)));Save_RaiseStoryRank(k,3);}fprintf(stderr,"bt3: isolated completion fixture seeded preceding victories\n");}
 if(Port_StoryActive()){sStLaunched=sStChapter;sStCommitted=0;sStFoundBall=0;sStEvents=0;sStGrantedBall=sStNewChapter=sStNewSaga=-1;}}
unsigned Port_StoryProgress(int chapter){return st_progress(chapter);}
int Port_StorySagaState(int saga){
 unsigned progress[ST_COUNT],completed=0;int i,state=0;
 if(saga<0||saga>=ST_SAGA_COUNT)return 0;
 for(i=0;i<ST_COUNT;i++)progress[i]=st_progress(i);
 for(i=0;i<ST_SAGA_COUNT;i++)if(ST_SagaCleared(i,stSagaFinalized[i],progress))completed|=1u<<i;
 if(ST_SagaFirst(saga)>=0)state|=1;
 if(ST_SagaCanOpen(saga,completed))state|=2;
 if(completed&(1u<<saga))state|=4;
 return state;
}
int Port_StoryClearRank(int chapter){return chapter>=0&&chapter<ST_COUNT?((st_progress(chapter)&ST_WinBits(chapter))?Save_GetStoryRank(st_save_index(chapter)):0):0;}
/* Authored R3 events completed in this fight (bit k = stEvents event k); the chapter outcome depends on them. */
void Port_StoryEventDone(int event){if(Port_StoryActive()&&event>=0&&event<ST_EVENTS)sStEvents|=1u<<event;}
unsigned Port_StoryEvents(void){return sStEvents;}
extern int PortStages_Find(const char*);
int Port_StoryStage(void){char key[40];int stage;if(!Port_StoryActive())return -1;stage=stChapters[sStChapter].stage;
 if(stAddedStage[sStChapter]&&PortStages_Find(stAddedStage[sStChapter])>=0)stage=PortStages_Find(stAddedStage[sStChapter]); /* an installed added stage */
 snprintf(key,sizeof(key),"story_stage_%d",sStChapter);stage=Port_Setting(key,stage);
 {static int lastChapter=-1,lastStage=-1;extern char*getenv(const char*);
  if(getenv("BT3_STORY_SCENE_LOG")&&(lastChapter!=sStChapter||lastStage!=stage)){fprintf(stderr,"bt3: story selected stage chapter%d stage%d\n",sStChapter,stage);lastChapter=sStChapter;lastStage=stage;}}
 return stage;}
int Port_StoryBgm(void){char key[40];int i,bgm=-1;if(!Port_StoryActive())return -1;PortSongs_Init();for(i=0;i<gPortSongCount;i++)if(!strcmp(gPortSongNames[i],stChapters[sStChapter].music)){bgm=gPortSongOffsets[i];break;}snprintf(key,sizeof(key),"story_bgm_%d",sStChapter);return Port_Setting(key,bgm);}
int Port_StoryDense(void);
int Port_StoryDraft(MsView*v){return Port_StoryActive()?ST_BuildDraftEx(v,sStChapter,Port_StoryDense()):0;}
/* Victory transition of a story fight. Rewards only for an eligible local fight. */
unsigned Port_StoryResult(int winner,int reason,int eligible){
 int outcome,i,ranks[ST_COUNT];unsigned oldProgress,bits,awards=0;if(!Port_StoryActive()||sStCommitted)return 0;outcome=ST_OutcomeFor(sStChapter,winner,reason,sStEvents);
 if(outcome==ST_NONE)return 0;
 sStCommitted=1;sStReturn=1;sStBalanceBefore=Save_GetMoney();
 if(ST_Interlude(sStChapter)){ /* a scene-only interlude: nothing is saved, granted or announced */
  sStBalanceAfter=sStBalanceBefore;sStZeni=0;sStOutcome=outcome;sStOutcomeChapter=sStChapter;sStLaunched=-1;
  fprintf(stderr,"bt3: story interlude %s played\n",stChapters[sStChapter].id);return 0;}
 oldProgress=bits=st_progress(sStChapter);
 /* This receipt belongs to THIS result, not the current menu lock state.
    Replays/repeats/failures must never announce a new chapter. */
 /* Announced on the chapter's first clear, and again on its first real WIN when the first clear was the narrative
    defeat (chapters 1-2: Raditz winning opens the next chapter; the win the player then earns announces it too). */
 if(eligible&&(outcome==ST_CANON||outcome==ST_ALT)&&ST_NextVisible(sStChapter)>=0&&
    st_progress(ST_NextVisible(sStChapter))==0&&(bits==0||(winner==0&&!(bits&ST_WinBits(sStChapter)))))sStNewChapter=ST_NextVisible(sStChapter);
 if(eligible&&Port_StoryLegacyClear(sStChapter)&&!bits){unsigned legacy=ST_DONE_CANON;sStZeni=ST_Reward(sStChapter,outcome,&legacy);bits=legacy;}else sStZeni=eligible?ST_Reward(sStChapter,outcome,&bits):0;
 if(eligible&&bits!=st_progress(sStChapter)){int k=st_save_index(sStChapter);st_group_or(k/4,(int)(bits<<(2*(k%4))));}
 if(eligible&&(outcome==ST_CANON||outcome==ST_ALT)){
  if(winner==0){
   if(!(oldProgress&ST_WinBits(sStChapter)))Save_ClearStoryRank(st_save_index(sStChapter));
   Save_RaiseStoryRank(st_save_index(sStChapter),sStDifficulty+1);
  }
  {int n=0;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga!=ST_SAGA_RETIRED&&!ST_Interlude(i))ranks[n++]=Port_StoryClearRank(i); /* the active list only */
  awards=ST_CampaignTrophies(ranks,n,ST_CAMPAIGN_FINALIZED);
  n=0;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==0&&!ST_Interlude(i))ranks[n++]=Port_StoryClearRank(i);
  awards|=ST_SaiyanTrophies(ranks,n);}
  if(sStFoundBall>0&&Save_GrantDragonBall(sStFoundBall))sStGrantedBall=sStFoundBall-1;
 }
 /* Group8 bits0..3 hold expanded CH7/8 progress; bit4 records the first saga handoff. */
 if(eligible&&(outcome==ST_CANON||outcome==ST_ALT)&&sStChapter==11&&(Port_StorySagaState(0)&4)&&!(Save_GetStoryBitsGroup(8)&16)){
  Save_OrStoryBitsGroup(8,16);sStNewSaga=1;fprintf(stderr,"bt3: story saga unlocked 1 after Saiyan completion\n");
 }
 if(sStZeni>0)Save_AddMoney(sStZeni);
 sStBalanceAfter=Save_GetMoney();sStZeni=sStBalanceAfter-sStBalanceBefore;
 if(outcome==ST_CANON||outcome==ST_ALT)sStCheckChapter=-1; /* cleared: the next attempt starts over */
 else if(outcome==ST_FAIL&&sStCheckChapter==sStChapter)sStCheckFailed=1; /* a retry resumes at the checkpoint */
 sStOutcome=outcome;sStOutcomeChapter=sStChapter;sStLaunched=-1;
 fprintf(stderr,"bt3: story result chapter %s winner%d reason%x eligible%d outcome%d zeni%d progress%x difficulty%d best_rank%d ball%d\n",stChapters[sStChapter].id,winner,reason,eligible,outcome,sStZeni,st_progress(sStChapter),sStDifficulty,Port_StoryClearRank(sStChapter),sStGrantedBall+1);
 /* IDs18/19 name chapter1 specifically; later chapters must not award them. */
 return !eligible||winner!=0||outcome==ST_FAIL?0:(sStChapter==0?((1u<<18)|(outcome==ST_ALT?(1u<<19):0)):0)|awards;
}
/* For the chapter page after a fight: the recorded outcome, or ST_FAIL when a launched fight
   came back without one (left from the pause menu), or ST_NONE. */
int Port_StoryLastOutcome(int *chapter,int *zeni){
 if(chapter)*chapter=sStOutcomeChapter>=0?sStOutcomeChapter:sStLaunched>=0?sStLaunched:sStChapter;if(zeni)*zeni=sStZeni;
 if(sStOutcome!=ST_NONE)return sStOutcome;
 return sStLaunched>=0?ST_FAIL:ST_NONE;
}
extern void Port_StoryMusicEnd(void);
void Port_StoryAcknowledge(void){Port_StoryMusicEnd();Port_StoryClear();sStGrantedBall=sStNewChapter=sStNewSaga=-1;sStOutcome=ST_NONE;sStOutcomeChapter=-1;sStZeni=0;sStLaunched=-1;sStReturn=0;}

/* Scene chain: battle resources load under black, never through the Story selector or a result page. */
static int sStChainSaved,sStChainReceipt[8];
int Port_StoryInterludeAdvance(void){
 int next=-1;
 if(!sStChainSaved&&(sStOutcomeChapter==4||sStOutcomeChapter==5)&&(sStOutcome==ST_CANON||sStOutcome==ST_ALT)){
  sStChainReceipt[0]=sStOutcome;sStChainReceipt[1]=sStZeni;sStChainReceipt[2]=sStBalanceBefore;sStChainReceipt[3]=sStBalanceAfter;
  sStChainReceipt[4]=sStGrantedBall;sStChainReceipt[5]=sStNewChapter;sStChainReceipt[6]=sStDifficulty;sStChainReceipt[7]=sStChapter;sStChainSaved=1;next=sStOutcomeChapter==4?17:19;
 }else if(sStChainSaved&&sStOutcomeChapter==17)next=18;
 else if(sStChainSaved&&sStOutcomeChapter==19)next=20;
 else if(sStChainSaved&&(sStOutcomeChapter==18||sStOutcomeChapter==20)){
  Port_StoryMusicEnd();
  sStOutcome=sStChainReceipt[0];sStZeni=sStChainReceipt[1];sStBalanceBefore=sStChainReceipt[2];sStBalanceAfter=sStChainReceipt[3];
  sStGrantedBall=sStChainReceipt[4];sStNewChapter=sStChainReceipt[5];sStDifficulty=sStChainReceipt[6];sStChapter=sStOutcomeChapter=sStChainReceipt[7];sStReturn=1;sStChainSaved=0;
 }
 if(next>=0){Port_StoryChoose(next);return 1;}return 0;
}

int Port_StoryWishPending(void){if(sStOutcomeChapter==19&&!sStWishShown&&(sStOutcome==ST_CANON||sStOutcome==ST_ALT)){sStWishShown=1;return 1;}return 0;}
