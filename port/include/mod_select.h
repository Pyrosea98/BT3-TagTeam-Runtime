/* Modded select: no PS2 pointers in the render-thread snapshot. */
#ifndef MOD_SELECT_H
#define MOD_SELECT_H
#define MS_MAX 16 /* story drafts; the selector UI limits itself to MS_UI_MAX */
#define MS_UI_MAX 10
#define MS_ROSTER 302
#define MS_CHARACTERS 328
#define MS_PAGE 35
#define MS_COLS 7
#define MS_FORMS 7
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MsPick {int chara,costume,team,pad;int items[8];} MsPick;
int Port_RosterBrowse(int kind,MsPick *out,int count,int dp);
typedef struct MsView {
 int active,phase,mode,a,total,slot,cursor,costume,roster_count,stage,stage_count,error,pad,loading,form_cursor,form_count,preset;
 int disabled[MS_ROSTER],formDisabled[MS_FORMS];
 int roster[MS_ROSTER];
 int forms[MS_FORMS];
 MsPick pick[MS_MAX];
 int human_team[4],result_team,player_count; /* result_team is winning team + 1; zero hides it. */
 int team_count; /* 0 retains the original two-team/FFA layout. */
} MsView;
typedef struct MsStageView {int active,music,cursor,count,bgmCursor,bgmCount,preview;int stages[192],songs[160];} MsStageView;
void Port_ModernStagePublish(const MsStageView *view);
int Port_ModernStageActive(void);
void Port_ModSelectPublish(const MsView *view);
int Port_ModSelectActive(void);
int Port_ModSelectRun(void);
int Port_ModSelectTournamentRun(MsPick *out,int count);
void Port_ModSelectClear(void);
void Port_ModSelectPlayers(int n);
int Port_ModSelectPlayerCount(void);
int Port_ModSelectCommit(const MsView *view);
int Port_ModSelectPlan(MsPick *out,int actor);
int Port_ModSelectPlanCount(void);
int Port_ModSelectMulti(void);
void Port_ModSelectResult(int team);
int Port_ModSelectWinnerPad(void);
int Port_ModSelectWinningTeam(void);
int Port_ModSelectPlayerWin(void);
int Port_ModSelectItems(int actor,unsigned short *out);
int Port_ModSelectEnabled(void);
int Port_ModSelectCostumeOk(int chara,int costume);
#ifdef __cplusplus
}
#endif
/* Pure draft operations: capacities and ordering never depend on the stock team structures. */
static int MS_Size(int size){return size<1?1:size>9?9:size;}
/* Resize only the selected side; the other side never changes implicitly. */
static int MS_Resize(MsView *v,int side,int delta){
 int a=v->a,b=v->total-v->a;
 if(side==0)a+=delta;else b+=delta;
 if(a<1||b<1||a+b>MS_UI_MAX)return 0;
 v->a=a;v->total=a+b;return 1;
}
static int MS_Teams(const MsView*v){return v->mode==4?1:v->mode==2?v->total:v->team_count>2?v->team_count:2;}
static int MS_Count(const MsView*v,int team){int i,n=0;for(i=0;i<v->total;i++)if((v->mode==2?i:v->team_count>2?v->pick[i].team:(i<v->a?0:1))==team)n++;return n;}
/* Keep physical slots grouped by team, without changing the other team sizes. */
static int MS_Layout(MsView*v,const int*sizes,int count){int t,j,i=0,total=0;if(count<2||count>10)return 0;for(t=0;t<count;t++){if(sizes[t]<1)return 0;total+=sizes[t];}if(total>10)return 0;v->team_count=count;v->total=total;v->a=sizes[0];for(t=0;t<count;t++)for(j=0;j<sizes[t];j++)v->pick[i++].team=t;return 1;}
static int MS_Edit(MsView*v,int row,int delta){int sizes[10],n=MS_Teams(v),i,best=0,total=v->total;for(i=0;i<n;i++)sizes[i]=MS_Count(v,i);if(row==0){if(delta>0){if(n==10)return 0;if(total==10){for(i=1;i<n;i++)if(sizes[i]>sizes[best])best=i;if(sizes[best]<=1)return 0;sizes[best]--;}sizes[n++]=1;}else{if(n<=2)return 0;n--;for(i=0;i<4;i++)if(v->human_team[i]>=n)v->human_team[i]=n-1;}}else{if(row<1||row>n||sizes[row-1]+delta<1||total+delta>10)return 0;sizes[row-1]+=delta;}return MS_Layout(v,sizes,n);}
static int MS_Side(const MsView *s,int slot){return s->mode==4?0:s->mode==2?slot:s->team_count>2?s->pick[slot].team:(slot<s->a?0:1);}
static int MS_RootSlot(const MsView *s,int actor){
 if(actor<0||actor>=s->total)return -1;
 if(s->mode==2)return actor;
 if(actor==0)return 0;
 if(actor==1)return s->a;
 return actor-1<s->a?actor-1:actor;
}
static int MS_Valid(const MsView *s){
 int i,j;unsigned seen=0;if(!s||s->mode<1||s->mode>3||s->total<2||s->total>MS_MAX||s->a<1||s->a>=s->total)return 0;
 if(s->team_count>MS_MAX||s->team_count==1||s->team_count<0)return 0;
 if(s->team_count>2&&(s->pick[0].team!=0||s->pick[s->a].team!=1))return 0;
 for(i=0;i<s->total;i++){
  if(s->team_count>2&&((i<s->a?s->pick[i].team!=0:s->pick[i].team==0)||(i&&s->pick[i].team<s->pick[i-1].team)))return 0;
  if(s->pick[i].team<0||s->pick[i].team>=(s->team_count>2?s->team_count:s->mode==2?s->total:2))return 0;
  seen|=1u<<s->pick[i].team;
  if(s->pick[i].chara<0||s->pick[i].chara>=MS_CHARACTERS||s->pick[i].costume<0||s->pick[i].costume>7||s->pick[i].team!=MS_Side(s,i)||s->pick[i].pad < -1||s->pick[i].pad>3)return 0;
  for(j=0;j<i;j++)if(s->pick[i].pad>=0&&s->pick[i].pad==s->pick[j].pad)return 0;
 }
 if(s->team_count>2&&seen!=((1u<<s->team_count)-1u))return 0;
 return 1;
}
/* Tournament drafts are one fixed team, never a battle plan. */
static int MS_TournamentValid(const MsView *s){
 int i,j;if(!s||s->mode!=4||s->total<1||s->total>5||s->a!=s->total)return 0;
 for(i=0;i<s->total;i++){
  const MsPick*p=&s->pick[i];if(p->chara<0||p->chara>=MS_CHARACTERS||p->costume<0||p->costume>7||p->team!=0||p->pad< -1||p->pad>3)return 0;
  for(j=0;j<i;j++)if(p->pad>=0&&s->pick[j].pad==p->pad)return 0;
 }return 1;
}
static int MS_Move(int cursor,int delta,int count){if(count<1)return 0;return (cursor+delta%count+count)%count;}
#endif
