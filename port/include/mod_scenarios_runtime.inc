
#include "mod_scenarios.h"
#include <stdio.h>
#include <string.h>
#include "plat_songs.h"
/* Game-thread-owned. No settings mutation, renderer pointers or per-frame scans. */
extern int Port_Setting(const char *name,int def);
#include "mod_story_runtime.inc"
static int sScRoute=-1,sScVariant,sScLive,sScN,sScIds[16],sScTeams[16],sScForms[16],sScFinalHp[16],sScFinalHpN;
static unsigned sScWeak,sScConsumed;static int sScAbsorptions,sScFullPower;
/* A Dragon History chapter (mod_story.h) rides the same fixed-draft fight path, so it counts as
   active for the select screen and the fight hooks; route-specific rules below stay off for it. */
static int sc_route(void){return sScRoute>=0&&sScRoute<SC_COUNT;}
int Port_ScenarioActive(void){return sc_route()||Port_StoryActive();}
/* Numeric overrides intentionally do not depend on the stock labels. Added
   maps/tracks can have different names (or no name entry at all). A value of
   -1 keeps the normal selector choice. Keys are scenario_stage_0..17 and
   scenario_bgm_0..17, so a settings UI can expose them later. */
/* Family defaults apply to every trophy route, including rematches. Resolve
   music by its catalogue title so importing another track cannot shift it. */
static const char *const scDefaultMusic[SC_FAMILIES]={
 "Saga de Cell","Saga de los Androides","Saga Majin-Buu",
 "Saga de Dragon Ball GT","Saga de Dragon Ball GT 2","Saga Majin-Buu"
};
int Port_ScenarioStage(void){char key[40];if(Port_StoryActive())return Port_StoryStage();if(!sc_route())return -1;snprintf(key,sizeof(key),"scenario_stage_%d",sScRoute);return Port_Setting(key,SC_DefaultStage(sScRoute));}
int Port_ScenarioBgm(void){char key[40];int i,bgm=-1;if(Port_StoryActive())return Port_StoryBgm();if(!sc_route())return -1;PortSongs_Init();for(i=0;i<gPortSongCount;i++)if(!strcmp(gPortSongNames[i],scDefaultMusic[scRoutes[sScRoute].family])){bgm=gPortSongOffsets[i];break;}snprintf(key,sizeof(key),"scenario_bgm_%d",sScRoute);return Port_Setting(key,bgm);}
static unsigned sStSceneMask,sStSceneShown; /* scene-only actors of the running chapter, and those a cutscene shows */
void Port_ScenarioClear(void){sStSceneMask=sStSceneShown=0;Port_StoryClear();sScRoute=-1;sScLive=sScN=0;sScFinalHpN=0;sScWeak=sScConsumed=0;}
void Port_ScenarioChoose(int route,int variant){Port_ScenarioClear();if(route>=0&&route<SC_COUNT){sScRoute=route;sScVariant=variant<0?0:variant%3;}}
int Port_ScenarioDraft(MsView*v){
 unsigned weak=0;int i,slot,result;if(!Port_ScenarioActive())return 0;
 result=Port_StoryActive()?Port_StoryDraft(v):SC_BuildDraft(v,sScRoute,sScVariant,&weak);if(!result)return 0;
 sScN=v->total;sScWeak=0;
 for(i=0;i<sScN;i++){slot=MS_RootSlot(v,i);sScIds[i]=v->pick[slot].chara;sScTeams[i]=v->pick[slot].team;if(weak&(1u<<slot))sScWeak|=1u<<i;}
 sStSceneMask=sStSceneShown=0;
 if(Port_StoryActive()){const StCast*c=&stCasts[Port_StoryChapter()];int k,a;for(k=0;k<c->count;k++)if(c->actor[k].flags&ST_ACTOR_SCENE){a=Port_StoryCastActor(k);if(a>=0&&a<32)sStSceneMask|=1u<<a;}}
 return result;
}
/* Battle actor i is draft actor i (MS_RootSlot order): match a cast entry by character and team. */
/* story_density_full=1: the Full story battle density (all of a battle's enemies at once). */
int Port_StoryDense(void){return Port_Setting("story_density_full",0)==1;}
int Port_StoryCastActor(int cast){const StCast*c;int i,nth=0;if(!Port_StoryActive()||cast<0)return -1;c=&stCasts[Port_StoryChapter()];if(cast>=c->count)return -1;
 if((c->actor[cast].flags&ST_ACTOR_DENSE)&&!Port_StoryDense())return -1; /* left out in the reduced density */
 for(i=0;i<cast;i++)if(c->actor[i].chara==c->actor[cast].chara&&c->actor[i].team==c->actor[cast].team&&(!(c->actor[i].flags&ST_ACTOR_DENSE)||Port_StoryDense()))nth++;
 for(i=0;i<sScN;i++)if(sScIds[i]==c->actor[cast].chara&&sScTeams[i]==c->actor[cast].team&&nth--==0)return i;return -1;}
int Port_StorySceneActor(int actor){return Port_StoryActive()&&actor>=0&&actor<32&&(sStSceneMask>>actor&1u);}
/* Reserve fighters (ST_ACTOR_RESERVE): a story event brings one into the fight (no longer a scene-only actor; native
   get-up with the health he had when he stepped back, or full) or sends one back to the sidelines. */
static int sStReserveHp[32];
int Port_StoryReserveHealth(int a){return a>=0&&a<32?sStReserveHp[a]:0;}
extern int Port_StoryReviveActor(int actor,int hp);
void Port_StoryEnterActor(int actor){if(!Port_StoryActive()||actor<0||actor>=32||!(sStSceneMask>>actor&1u))return;
 sStSceneMask&=~(1u<<actor);sStSceneShown&=~(1u<<actor);Port_StoryReviveActor(actor,sStReserveHp[actor]);{extern void Port_StoryReleaseActor(int);Port_StoryReleaseActor(actor);}}
void Port_StoryExitActor(int actor,int hp){if(!Port_StoryActive()||actor<0||actor>=32)return;sStReserveHp[actor]=hp;sStSceneMask|=1u<<actor;}
void Port_StorySceneShow(int actor,int on){if(actor<0||actor>=32)return;if(on&&Port_StorySceneActor(actor))sStSceneShown|=1u<<actor;else sStSceneShown&=~(1u<<actor);}
/* Kept out of the fight (plat_fighters.c treats it as a fusion-consumed partner: hidden, frozen, parked below the
   stage, no HUD, no revival, no assists, no target, not counted for the end of the battle). */
/* Difficulty, opponent only (team 1, fighters; scene-only actors are parked and need none). `side` is the fighter's
   side as btl_char_member.c passes it; `actor` is the battle actor index. */
/* One authored era power drives UI/CPU and the real local offensive/defensive profile. */
int Port_StoryActorPower(int actor,int form){int ch=Port_StoryChapter(),k,p;
 if(ch<0||ch>=ST_COUNT)return 0;
 for(k=0;k<stCasts[ch].count;k++)if(Port_StoryCastActor(k)==actor){p=stChapterPower[ch][k];
  if(p<=0&&ch==3&&k==1)p=1307; /* volatile child rage; no fixed menu reading is advertised */
  if(p<=0)return 0;
  if(form==231&&stCasts[ch].actor[k].chara==0)p*=2;
  if(form==30||form==180)p*=10;
  return p;}
 return 0;
}
int Port_StoryEraPower(int actor,int form){return Port_StoryActorPower(actor,form);}
int Port_StoryEraHealth(int actor,int base){int p=Port_StoryEraPower(actor,-1);float f;
 if(p<=0)return base;{float v=(float)p/1200.f;int i;f=v>1?v:1;for(i=0;i<12;i++)f=.5f*(f+v/f);}if(f<.5f)f=.5f;if(f>5)f=5;
 return ((int)(30000.f*f)+500)/1000*1000;
}
int Port_StoryTune(int actor,int *hpPercent,int *attack,int *defence){
 int ch=Port_StoryChapter(),k,d=Port_StoryDifficulty();const StTune*t=0;
 if(!Port_StoryActive()||actor<0||actor>=sScN||(ch<3&&Port_StorySceneActor(actor))||!hpPercent||!attack||!defence)return 0;
 for(k=0;k<stCasts[ch].count;k++)if(Port_StoryCastActor(k)==actor){t=&stTunes[ch][k];break;}
 *hpPercent=t&&t->hpPercent>0?t->hpPercent:100;*attack=t?t->attack:0;*defence=t?t->defence:0;
 if(sScTeams[actor]==1&&d>=0&&d<=2){*hpPercent=*hpPercent*stDifficulty[ch].hpPercent[d]/100;*attack+=stDifficulty[ch].attack[d];}
 return ch>=0||*hpPercent!=100||*attack||*defence;
}
/* Fighters a story event took out of the chapter for good (a story death): parked like a scene-only actor. */
static unsigned sStRemoved;
/* A story event's form change (StEvent.transformTo): allowed for that fighter from the event on, this fight only. */
static int sStFormGrant[16];
void Port_StoryGrantForm(int actor,int target){if(Port_StoryActive()&&actor>=0&&actor<16)sStFormGrant[actor]=target;}
int Port_StoryFormGranted(int actor,int target){return Port_StoryActive()&&actor>=0&&actor<16&&sStFormGrant[actor]==target;}
/* The form slot a story fight adds to a character's table (plat_beta8.c), or -1. */
int Port_StoryFormSlot(int source){if(!Port_StoryActive()||Port_NetActive()||BattleReplay_IsActive())return -1;if(source==231)return 0;if(Port_StoryChapter()==3&&source==180)return 13;return ST_StoryFormFor(Port_StoryChapter(),source);}
void Port_StoryRemoveActor(int actor){if(Port_StoryActive()&&actor>=0&&actor<32)sStRemoved|=1u<<actor;}
int Port_StorySceneParked(int actor){return (Port_StorySceneActor(actor)&&!(sStSceneShown>>actor&1u))||(Port_StoryActive()&&actor>=0&&actor<32&&(sStRemoved>>actor&1u));}
void Port_ScenarioRoundReset(void){int i;sStSceneShown=0;sStRemoved=0;memset(sStReserveHp,0,sizeof(sStReserveHp));for(i=0;i<16;i++)sStFormGrant[i]=-1;sScLive=Port_ScenarioActive();Port_StoryFightStarted();sScConsumed=0;sScAbsorptions=sScFullPower=0;sScFinalHpN=0;for(i=0;i<sScN;i++){sScForms[i]=sScIds[i];sScFinalHp[i]=-1;}}
void Port_ScenarioSetFinalHp(const int*hp,int count){int i;sScFinalHpN=0;if(!hp||count<0||count>16)return;for(i=0;i<count;i++)sScFinalHp[i]=hp[i];sScFinalHpN=count;}
int Port_ScenarioWeak(int actor){return sScLive&&actor>=0&&actor<sScN&&!!(sScWeak&(1u<<actor));}
void Port_ScenarioObserve(int actor,int chara){if(!sScLive||actor<0||actor>=sScN)return;if(sScIds[actor]<0)sScIds[actor]=chara;sScForms[actor]=chara;if(sScTeams[actor]==1&&chara==294)sScFullPower=1;}
int Port_ScenarioFormAllowed(int actor,int target){
 int id;if(!sScLive||actor<0||actor>=sScN)return 1;id=sScIds[actor];
 if(Port_StoryActive())return ST_FormAllowed(Port_StoryChapter(),sScForms[actor],target)||(sStFormGrant[actor]>=0&&sStFormGrant[actor]==target);
 if(sScWeak&(1u<<actor)){if(sScRoute==11&&id==122)return target==122||target==240||target==236;return 0;}
 /* Cell challenges stop at Perfect Cell.  The scenario may still absorb 17/18
    and reach 107, but Super Perfect (108) is deliberately out of scope. */
 if(scRoutes[sScRoute].family==0&&sScTeams[actor]==1&&
    (id==105||id==107||sScForms[actor]==105||sScForms[actor]==107))
  return target==105||target==106||target==107;
 if(sScTeams[actor]==0){if(id==21||sScRoute==3||sScRoute==16)return 0;if((sScRoute==5||sScRoute==6)&&actor==0)return target==2;}
 if(sScTeams[actor]==1){if(sScForms[actor]==272)return 0;if(scRoutes[sScRoute].family==3){if(sScRoute!=11&&sScForms[actor]>=140&&sScForms[actor]<=143)return 0;if(id==72||id==81||id==74)return 0;}}
 return 1;
}
int Port_ScenarioFusionAllowed(int actor,int result){
 if(!sScLive||actor<0||actor>=sScN)return 1;if(Port_StoryActive()||(sScWeak&(1u<<actor)))return 0;
 if(sScTeams[actor]==0)return sScRoute==16&&sScFullPower&&result==215;return 0;
}
int Port_ScenarioAbsorbAllowed(int actor,int target){
 if(!sScLive||actor<0||target<0||actor>=sScN||target>=sScN)return 1;
 if(sScTeams[actor]!=1||Port_StoryActive())return 1;
 if(scRoutes[sScRoute].family==3||scRoutes[sScRoute].family==5)return !!(sScWeak&(1u<<target));return 1;
}
void Port_ScenarioConsumed(int actor,int target){if(!sScLive||target<0||target>=sScN)return;sScConsumed|=1u<<target;if(actor>=0&&actor<sScN&&sScTeams[actor]==1)sScAbsorptions++;}
int Port_ScenarioCpuScope(int actor){return sScLive&&actor>=0&&actor<sScN?1:-1;}
/* Awards are objective predicates, never the selected tier's bit alone. */
static int sc_initial_ally(int id){int i;for(i=0;i<sScN;i++)if(sScTeams[i]==0&&sScIds[i]==id)return 1;return 0;}
static int sc_initial_weak(int id){int i;for(i=0;i<sScN;i++)if((sScWeak&(1u<<i))&&sScIds[i]==id)return 1;return 0;}
static int sc_initial_present(int id){int i;for(i=0;i<sScN;i++)if(sScIds[i]==id)return 1;return 0;}
static int sc_final_alive(int id){int i;for(i=0;i<sScN;i++)if(sScIds[i]==id)return sScFinalHpN==sScN&&sScFinalHp[i]>0;return 0;}
static int sc_gt_goku(int id){return id==7||id==8||id==9||id==10||id==169||id==233;}
unsigned Port_ScenarioResult(int winner,int flags,int reason,const int*forms,int count,int eligible){
 int i,boss=-1,form=-1,hero=-1,allies=0;unsigned award=0;const ScRoute*r;
 if(!sScLive)return 0;if(Port_StoryActive())return Port_StoryResult(winner,reason,eligible);r=&scRoutes[sScRoute];
 if(eligible&&winner==0&&flags==1&&(reason&1)&&!(reason&0x18000)&&forms&&count==sScN&&!(sScConsumed&1u)){
  for(i=0;i<count;i++){sScForms[i]=forms[i];if(sScTeams[i]==1&&boss<0)boss=i;if(sScTeams[i]==0)allies++;}
  hero=forms[0];if(boss>=0)form=forms[boss];
  switch(r->family){
  case 0:
   if(sc_initial_present(101)&&sc_initial_present(102)){
    if(sScRoute!=0|| (sc_final_alive(101)&&sc_final_alive(102))) award|=1u<<2;
    if(hero==100&&sc_initial_ally(101)&&sc_initial_ally(102))award|=1u<<3;
    if(hero==23)award|=1u<<4;
    if(hero==33&&(form==107||form==108))award|=1u<<5;
   }break;
  case 1:
   award|=1u<<6;
   if(hero==2&&sc_initial_ally(33)&&sc_initial_ally(43))award|=1u<<7;
   if(hero==2&&allies==1&&form==130)award|=1u<<8;
   break;
  case 2:
   award|=1u<<9;
   if(!sScAbsorptions)award|=1u<<10;
   if(hero==21&&sScAbsorptions)award|=1u<<11;
   break;
  case 3:
   if((sScConsumed&sScWeak)==sScWeak){
    if(sc_initial_weak(7)&&sc_initial_weak(122))award|=1u<<12;
    if(sc_gt_goku(hero)&&sc_initial_ally(122))award|=1u<<13;
    if(hero==186&&sc_initial_ally(64)&&sc_initial_ally(7))award|=1u<<14;
   }break;
  case 4:
   award|=1u;
   if(hero==10||hero==200||hero==233)award|=2u;
   break;
  case 5:
   if(sc_initial_ally(3)&&sc_initial_ally(34)){
    award|=1u<<15;
    if(hero==215&&sScFullPower&&sc_initial_ally(7)&&sc_initial_ally(122))award|=1u<<16;
    if(hero==21&&sc_initial_ally(48)&&sc_initial_ally(23))award|=1u<<17;
   }break;
  }
 }
 fprintf(stderr,"bt3: scenario result route%d winner%d flags%x reason%x eligible%d hero%d boss%d absorptions%d fullpower%d award%x requested%x\n",sScRoute,winner,flags,reason,eligible,hero,form,sScAbsorptions,sScFullPower,award,1u<<r->achievement);
 return award;
}

int Port_ScenarioIndependentTeams(void){return sScLive&&sc_route()&&scRoutes[sScRoute].family==0&&sScRoute!=1;}
int Port_ScenarioObjectiveEnd(const int*hp,int count){if(!Port_ScenarioIndependentTeams()||!hp||count!=sScN)return 0;if(hp[0]<=0)return 2;return hp[1]<=0?1:0;}
int Port_ScenarioBossBuff(int actor){return sScLive&&sScRoute==1&&actor==1;}
int Port_ScenarioHealthMax(int actor,int base){long long value;if(Port_StoryActive())return Port_StoryEraHealth(actor,base); /* era-specific baseline, difficulty at reset */if(!Port_ScenarioBossBuff(actor))return base;value=(long long)base*3;return value>2147483647?2147483647:(int)value;}
