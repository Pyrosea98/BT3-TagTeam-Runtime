/* Game thread owns this immutable-per-match plan; render thread only sees MsView snapshots. */
#include "mod_select.h"
extern int Port_ScenarioActive(void);
extern int Port_ScenarioStage(void);
extern int Port_ScenarioBgm(void);
extern void Battle_SetStartStage(int stage);
extern void Battle_SetBgm(int bgm);
static void fight_log(void);
static MsView sMsPlan;
static int sMsReady,sMsPlayers=1,sMsFight;
void Port_ModSelectPlayers(int n){sMsPlayers=n<1?1:n>4?4:n;}
int Port_ModSelectPlayerCount(void){return sMsPlayers;}
void Port_ModSelectClear(void){sMsReady=0;sMsPlan.result_team=0;}
int Port_ModSelectMulti(void){return sMsReady&&sMsPlan.team_count>2&&!Port_ScenarioActive();}
void Port_ModSelectResult(int team){MsView view;sMsPlan.result_team=team+1;view=sMsPlan;view.active=0;Port_ModSelectPublish(&view);}
int Port_ModSelectWinnerPad(void){int i,t=sMsPlan.result_team-1;for(i=0;i<sMsPlan.total;i++)if(MS_Side(&sMsPlan,i)==t&&sMsPlan.pick[i].pad>=0)return sMsPlan.pick[i].pad;return 0;}
int Port_ModSelectPlayerWin(void){int i;for(i=0;i<sMsPlan.total;i++)if(sMsPlan.pick[i].pad==0)return MS_Side(&sMsPlan,i)==sMsPlan.result_team-1;return 0;}
int Port_ModSelectEnabled(void){return !Port_NetActive()&&!Port_TagTourActive()&&Port_ModesFight()>0&&Port_ModesFight()<4;}
int Port_ModSelectCostumeOk(int chara,int costume){
 char why[160],path[1024];int id,volume=1,entry;long size;FILE *f;
 if(!Port_Beta8CostumeSelectable(chara,costume)||costume<0||costume>7||!extra_costume_ok(chara,costume,why,sizeof(why)))return 0;
 id=Port_Beta8ResolveFile(chara,costume,0,0,chara*10+FILE_CHARA+costume);
 if(id<0)return 0;
 if((unsigned)id&0x40000000u){volume=(id>>24)&63;entry=id&0xffffff;}else entry=id-1;
 if(!Port_FilePath(volume,entry,NULL,path,sizeof(path)))return 0;
 f=fopen(path,"rb");if(!f)return 0;fseek(f,0,SEEK_END);size=ftell(f);fclose(f);
 return size>128; /* exclude real archive placeholders, not just missing paths */
}
int Port_ModSelectCommit(const MsView *v){
 int i;if(!Port_ModSelectEnabled()||!MS_Valid(v))return 0;
 for(i=0;i<v->total;i++)if(!Port_ModSelectCostumeOk(v->pick[i].chara,v->pick[i].costume))return 0;
 sMsPlan=*v;sMsReady=1;return 1;
}
int Port_ModSelectPlanCount(void){return sMsReady&&Port_ModSelectEnabled()?sMsPlan.total:0;}
int Port_ModSelectItems(int actor,unsigned short *out){MsPick p;int i;if(!out||!Port_ModSelectPlan(&p,actor))return 0;for(i=0;i<8;i++)out[i]=(unsigned short)p.items[i];return 1;}
int Port_ModSelectPlan(MsPick *out,int actor){int slot;if(!out||!Port_ModSelectPlanCount())return 0;slot=MS_RootSlot(&sMsPlan,actor);if(slot<0)return 0;*out=sMsPlan.pick[slot];return 1;}
static int ms_fight_setup(int mode,int replay,int script){
 int i,stage,bgm;MsPick p;
 if(!Port_ModSelectPlanCount()||replay||script||((mode!=0)&&(mode!=6||sMsPlan.mode!=3)))return 0;
 /* Validate all assets before admitting anyone: no partially filled fight. */
 for(i=0;i<sMsPlan.total;i++)if(!Port_ModSelectPlan(&p,i)||!Port_ModSelectCostumeOk(p.chara,p.costume))return 0;
 sFightRoster=sMsPlan.mode;sFightFrom="modern modded select";
 for(i=2;i<sMsPlan.total;i++){
  Port_ModSelectPlan(&p,i);
  if(!fight_add(p.chara,p.costume,p.team,-1,-1)){sFightCount=0;sFightRoster=0;return 0;}
  sFight[sFightCount-1].pad=p.pad;
 }
 /* Scenario drafts bypass the native stage/music screen. Apply optional
    numeric overrides at the final pre-load boundary instead. */
 if(Port_ScenarioActive()){
  stage=Port_ScenarioStage();bgm=Port_ScenarioBgm();
  if(stage>=0)Battle_SetStartStage(stage);
  if(bgm>=0)Battle_SetBgm(bgm);
  if(stage>=0||bgm>=0)fprintf(stderr,"bt3: scenario rules route stage=%d bgm=%d\n",stage,bgm);
 }
 sMsFight=1;sLast=EXTRA_IN;fight_log();return 1;
}

void Port_ModSelectTrace(int event,int a,int b){static const char *names[]={"begin","viewer initialized","preview character/costume","committed","cancelled","preview camera fitted (character/distance x100)","preview ground lift (character/offset x100)"};if(event>=0&&event<7)fprintf(stderr,"bt3: modern select: %s %d %d\n",names[event],a,b);}
