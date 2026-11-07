/* Dragon History chapter 1 (node 01, Piccolo vs Raditz): offline checks of the real chapter table,
   the story runtime riding the Scenario fight path, progress in the game save word and the menu flow.
   Built against the shipped sources with small stubs for the game save, settings and the song list. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_songs.h"
/* stubs: settings, songs, game save */
static int sDifficulty=1;
int Port_Setting(const char *name,int def){return !strcmp(name,"story_difficulty")?sDifficulty:def;}
char gPortSongNames[PORT_SONG_MAX][64];int gPortSongCount,gPortSongOffsets[PORT_SONG_MAX];
void PortSongs_Init(void){gPortSongCount=2;strcpy(gPortSongNames[0],"Saga de Cell");gPortSongOffsets[0]=900;strcpy(gPortSongNames[1],"Saga de los Saiyajin");gPortSongOffsets[1]=901;}
static int sSlotFlags=3,sMoney;
int Save_GetStoryBits(void){return (sSlotFlags>>24)&0xFF;}
void Save_OrStoryBits(int bits){sSlotFlags=(int)((unsigned)sSlotFlags|((unsigned)(bits&0xFF)<<24));} static int sStubGroups[9]; int Save_GetStoryBitsGroup(int g){return g>0&&g<9?sStubGroups[g]&0xFF:0;} void Save_OrStoryBitsGroup(int g,int b){if(g>0&&g<9)sStubGroups[g]|=b&0xFF;}
void Save_AddMoney(int amount){sMoney+=amount;}
int Save_GetMoney(void){return sMoney;}
static int sRanks[72];
int Save_GetStoryRank(int c){return c>=0&&c<72?sRanks[c]:0;}
void Save_ClearStoryRank(int c){if(c>=0&&c<72)sRanks[c]=0;}
void Save_RaiseStoryRank(int c,int r){if(c>=0&&c<72&&r>sRanks[c])sRanks[c]=r;}
int Save_GrantDragonBall(int b){(void)b;return 0;}
int BattleReplay_IsActive(void){return 0;}
int Port_NetActive(void){return 0;}
int Port_StoryReviveActor(int a, int h) { (void)a; (void)h; return 1; }
void Port_StoryMusicStart(int id){(void)id;}void Port_StoryMusicEnd(void){}
void Port_StoryReleaseActor(int a){(void)a;}
#include "mod_scenarios_runtime.inc"
#include "../../src/gs/mod_menu_model.h"

static void fight(MsView *v){memset(v,0,sizeof(*v));assert(Port_ScenarioDraft(v)==2);Port_ScenarioRoundReset();}
static int sTestAddedStage = -1;
int PortStages_Find(const char *name) { (void)name; return sTestAddedStage; }

int main(void){
 MsView v;int ch,zeni,forms[2]={22,77};
 /* table: the script of record's cast, stage, music and lines */
 const StChapter *c=&stChapters[0];
 assert(!strcmp(c->id,"saiyan_01_raditz_wasteland")&&c->player==22&&c->rival==77&&c->stage==0);
 assert(c->introCount==3&&c->canonCount==3&&c->altCount==2);
 assert(!strcmp(c->intro[0].text[0],"I haven't seen you before. Explain what you're looking for."));
 assert(!strcmp(c->canon[2].text[0],"So he wasn't here for me. He's after Goku.")&&!strcmp(c->alt[1].speaker,"Raditz"));
 for(int n=0;n<3;n++)assert(c->intro[n].text[0][0]&&c->intro[n].text[1][0]&&c->canon[n].text[1][0]);
 assert(c->zeniCanonFirst==1000&&c->zeniCanonRepeat==200&&c->zeniAltFirst==3000&&c->zeniAltRepeat==500);
 /* nothing chosen: scenarios and story off */
 assert(!Port_ScenarioActive()&&!Port_StoryActive()&&Port_StoryLastOutcome(&ch,&zeni)==ST_NONE);
 /* choose chapter 1: fixed 1v1 draft, stage 0, Saiyan saga music by title */
 Port_ScenarioClear();Port_StoryChoose(0);
 assert(Port_ScenarioActive()&&Port_StoryActive()&&Port_ScenarioStage()==0&&Port_ScenarioBgm()==901);
 assert(!Port_ScenarioIndependentTeams()&&!Port_ScenarioBossBuff(1)&&Port_ScenarioHealthMax(1,50000)==50000);
 fight(&v);assert(v.total==2&&v.a==1&&v.pick[0].chara==22&&v.pick[0].pad==0&&v.pick[0].team==0&&v.pick[1].chara==77&&v.pick[1].team==1&&v.slot==-1);
 /* rules: no Great Ape Raditz (78), no fusion, no weak actors */
 assert(!Port_ScenarioFormAllowed(1,78)&&!Port_ScenarioFusionAllowed(0,51)&&!Port_ScenarioWeak(0)&&!Port_ScenarioWeak(1));
 /* backing out before the fight is not a failure; a started fight left without a result is */
 assert(Port_StoryLastOutcome(&ch,&zeni)==ST_FAIL&&ch==0);
 /* "Fight Again" (restart) records nothing */
 assert(Port_ScenarioResult(-1,0,0x8000,forms,2,1)==0&&Port_StoryLastOutcome(&ch,&zeni)==ST_FAIL);
 /* canonical: Raditz wins -> first clear 1000 Zeni, bit 0 in the save word, no trophy bits */
 assert(Port_ScenarioResult(1,2,1,forms,2,1)==0);
 assert(Port_ScenarioResult(1,2,1,forms,2,1)==0&&sMoney==1000);
 assert(Port_StoryBalanceBefore()==0&&Port_StoryBalanceAfter()==1000);
 assert(Port_StoryTakeReturn()==1&&Port_StoryTakeReturn()==0);
 assert(Port_StoryLastOutcome(&ch,&zeni)==ST_CANON&&zeni==1000&&sMoney==1000&&Port_StoryProgress(0)==ST_DONE_CANON);
 assert(sSlotFlags==(3|(1<<24)));
 /* main menu clears the chapter but keeps the ending for the page */
 Port_ScenarioClear();assert(!Port_ScenarioActive()&&Port_StoryLastOutcome(&ch,&zeni)==ST_CANON);
 Port_StoryAcknowledge();assert(Port_StoryLastOutcome(&ch,&zeni)==ST_NONE);
 /* repeat canonical: 200 */
 Port_StoryChoose(0);fight(&v);Port_ScenarioResult(1,2,1,forms,2,1);assert(Port_StoryLastOutcome(&ch,&zeni)==ST_CANON&&zeni==200&&sMoney==1200);
 /* alternative: Piccolo wins -> 3000 first, then 500; both bits */
 Port_StoryChoose(0);fight(&v);Port_ScenarioResult(0,1,1,forms,2,1);assert(Port_StoryLastOutcome(&ch,&zeni)==ST_ALT&&zeni==3000&&sMoney==4200&&Port_StoryProgress(0)==(ST_DONE_CANON|ST_DONE_ALT));
 Port_StoryChoose(0);fight(&v);Port_ScenarioResult(0,1,1,forms,2,1);Port_StoryLastOutcome(&ch,&zeni);assert(zeni==500&&sMoney==4700);
 /* A time-out draw is a failure, never a free canonical clear. */
 Port_StoryChoose(0);fight(&v);Port_ScenarioResult(-1,0,2,forms,2,1);assert(Port_StoryLastOutcome(&ch,&zeni)==ST_FAIL&&zeni==0);
 /* ineligible (replay / online) fight: ending shown, no Zeni, progress unchanged */
 Port_StoryChoose(0);fight(&v);Port_ScenarioResult(0,1,1,forms,2,0);assert(Port_StoryLastOutcome(&ch,&zeni)==ST_ALT&&zeni==0&&sMoney==4700);
 /* choosing a Scenario ends the chapter; its routes and awards are unchanged */
 Port_ScenarioChoose(3,0);assert(!Port_StoryActive()&&Port_ScenarioActive());memset(&v,0,sizeof(v));assert(Port_ScenarioDraft(&v)>0);Port_ScenarioRoundReset();
 {int f[10],hp[10];for(int i=0;i<v.total;i++){f[i]=v.pick[MS_RootSlot(&v,i)].chara;hp[i]=10000;}Port_ScenarioSetFinalHp(hp,v.total);assert(Port_ScenarioResult(0,1,1,f,v.total,1)==(1u<<2));}
 Port_ScenarioClear();
 /* menu: Play has 8 entries, the last opens the chapter page; Cross launches (41); back returns to Play */
 {struct ModMenuState s={0};int players=0,sound;
  s.page=MM_PLAY;s.cursor=7;assert(mm_step(&s,0x200,0,&players,&sound)==0&&s.page==MM_STORY_SAGAS);
  assert(mm_step(&s,0x200,0,&players,&sound)==0&&s.page==MM_STORY);
  assert(mm_step(&s,0x200,0,&players,&sound)==41&&players==1);
  assert(mm_step(&s,0x400,0,&players,&sound)==0&&s.page==MM_STORY_SAGAS&&s.cursor==0);
  s.page=MM_PLAY;s.cursor=6;assert(mm_step(&s,0,2,&players,&sound)==0&&s.cursor==7);
  mm_resume(&s,7);assert(s.page==MM_STORY);mm_resume(&s,5);assert(s.page==MM_PLAY&&s.cursor==5);
  s.page=MM_STORY;assert(mm_step(&s,0x2000,0,&players,&sound)==10&&s.page==0);}
 {struct ModMenuState s={0};int players=0,sound;s.page=MM_STORY_RESULT;
  assert(mm_step(&s,0x200,0,&players,&sound)==42&&s.page==MM_STORY&&players==0);
  assert(mm_step(&s,0,2,&players,&sound)==0&&s.pick==1);
  assert(mm_step(&s,0,2,&players,&sound)==0&&s.pick==2);
  assert(mm_step(&s,0,2,&players,&sound)==0&&s.pick==0);
 }
 assert(Port_StoryCpuLevel()==13);
 /* Difficulty is latched on chapter choice, never inferred for an old clear.
    Changing a preference mid-fight cannot upgrade the earned rank. */
 sRanks[0]=0;sDifficulty=2;Port_StoryChoose(0);fight(&v);assert(Port_StoryResult(1,1,1)==0);assert(!Port_StoryClearRank(0));
 memset(sRanks,0,sizeof(sRanks));assert(Port_StoryProgress(0)==3&&Port_StoryClearRank(0)==0);
 sDifficulty=0;Port_StoryChoose(0);fight(&v);sDifficulty=2;
 assert(Port_StoryCpuLevel()==6);Port_StoryResult(0,1,1);
 assert(Port_StoryClearRank(0)==1);
 sDifficulty=1;Port_StoryChoose(0);fight(&v);Port_StoryResult(0,1,1);
 assert(Port_StoryClearRank(0)==2);
 sDifficulty=2;Port_StoryChoose(0);fight(&v);Port_StoryResult(0,1,1);
 assert(Port_StoryClearRank(0)==3);
 sDifficulty=0;Port_StoryChoose(0);fight(&v);Port_StoryResult(0,1,1);
 assert(Port_StoryClearRank(0)==3); /* never downgrade */
 sRanks[0]=0;sDifficulty=2;Port_StoryChoose(0);fight(&v);Port_StoryResult(-1,2,1);assert(!Port_StoryClearRank(0));
 Port_StoryChoose(0);fight(&v);Port_StoryResult(1,1,0);assert(!Port_StoryClearRank(0));
 Port_StoryChoose(0);fight(&v);Port_StoryResult(1,0x8000,1);assert(!Port_StoryClearRank(0));
 assert(!Port_StoryClearRank(-1)&&!Port_StoryClearRank(ST_COUNT));
 {int r[]={3,2,1};assert(ST_CampaignTrophies(r,3,1)==MA_HISTORY_EASY);
  r[2]=2;assert(ST_CampaignTrophies(r,3,1)==(MA_HISTORY_EASY|MA_HISTORY_NORMAL));
  r[1]=r[2]=3;assert(ST_CampaignTrophies(r,3,1)==(MA_HISTORY_EASY|MA_HISTORY_NORMAL|MA_HISTORY_HARD));
  /* Saiyan saga trophies: lowest won rank; any never-won chapter (0) awards none; Hard awards all three. */
  {int s[]={1,3,2};assert(ST_SaiyanTrophies(s,3)==MA_SAIYAN_EASY);s[0]=2;assert(ST_SaiyanTrophies(s,3)==(MA_SAIYAN_EASY|MA_SAIYAN_NORMAL));
   s[0]=s[2]=3;assert(ST_SaiyanTrophies(s,3)==(MA_SAIYAN_EASY|MA_SAIYAN_NORMAL|MA_SAIYAN_HARD));s[1]=0;assert(!ST_SaiyanTrophies(s,3));}
  assert(!ST_CampaignTrophies(r,3,0));r[1]=0;assert(!ST_CampaignTrophies(r,3,1));
  assert(!ST_CampaignTrophies(NULL,1,1)&&!ST_CampaignTrophies(r,0,1));}
 Port_StoryClear();sDifficulty=1;
 puts("PASS Dragon History chapter 1: table, fixed Piccolo vs Raditz draft, stage/music, form lock, restart/fail/canonical/alternative outcomes, one-time Zeni, save-word progress, Scenario isolation, menu flow");
 return 0;
}
