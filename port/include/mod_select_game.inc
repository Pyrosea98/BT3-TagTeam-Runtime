#include "tag_tournament_port.h"
#include "tag_tournament_theme.h"
static MsPick *sMsTourOutput;static int sMsTourSize;
extern void Port_ChrViewTournament(int);
#include "mod_scenarios.h"
extern int Port_StoryActive(void);
static TsCell sMsCatalog[MS_ROSTER];
#include "roster_era_order.h"
static int ms_roster_key(int cid){return RosterEraKey(cid);}
static void ms_sort_catalog(MsView *v){
 int i,j;for(i=1;i<v->roster_count;i++){
  int cid=v->roster[i],key=ms_roster_key(cid);TsCell cell=sMsCatalog[i];
  for(j=i;j>0&&ms_roster_key(v->roster[j-1])>key;j--){v->roster[j]=v->roster[j-1];sMsCatalog[j]=sMsCatalog[j-1];}
  v->roster[j]=cid;sMsCatalog[j]=cell;
 }
}
/* Dedicated game-thread draft. The existing native stage/music screen follows it. */
extern void ChrView_Init(void),ChrView_Update(void),ChrView_Term(void);
extern s32 ChrView_Show(s32,s32,s32),ChrView_Hide(void),ChrView_IsLoading(void);
extern void Port_MenuScene3D(int),OrbitCam_SetPadLocked(s32);
extern int PortInput_PlayerAvailable(int);
extern void PortInput_KeyboardContext(u32);
extern int Port_Setting(const char*,int);
extern void Port_SettingSave(const char*,int),Port_SettingsWrite(void);
extern s32 Job_Run(void);
extern void Port_ModSelectTrace(int,int,int);
extern int Port_ModSelectTakeMouse(void);
extern int Port_Beta8FormList(int,int*,int);
extern int Port_Beta8IsAnyTransform(int);
static int ms_next_costume(int c,int current,int delta){int n=ChrTbl_WrapCostume(c,NULL),next=current,k;if(n<1||n>8)return current;for(k=0;k<n;k++){next=(next+(delta<0?-1:1)+n)%n;if(Port_Beta8CostumeSelectable(c,next)&&Port_ModSelectCostumeOk(c,next))return next;}return current;}
static int ms_first_empty(MsView *v){int i;for(i=0;i<v->total;i++)if(v->pick[i].chara<0)return i;return -1;}
static void ms_refresh_forms(MsView*v,int source,int outfit,int keep){
 int all[MS_FORMS],n=Port_Beta8FormList(v->roster[v->cursor],all,MS_FORMS),i,count=0;
 if(!n){n=sMsCatalog[v->cursor].formCount;if(n>MS_FORMS)n=MS_FORMS;for(i=0;i<n;i++)all[i]=sMsCatalog[v->cursor].form[i];}
 for(i=0;i<n;i++)if(Port_Beta8FormAllowed(source,outfit,all[i])&&Port_ModSelectCostumeOk(all[i],0))v->forms[count++]=all[i];
 v->form_count=count;v->form_cursor=0;for(i=0;i<count;i++)if(v->forms[i]==keep)v->form_cursor=i;
}
static int ms_view_chara(const MsView *v){return v->phase==0?-1:v->phase==3?v->pick[0].chara:v->phase==2?v->forms[v->form_cursor]:v->roster[v->cursor];}
static void ms_review(MsView*v){v->phase=3;v->slot=-1;v->cursor=0;v->form_count=0;v->error=0;}
/* Save slots are keyed by the original authored cell, never the sorted roster index. */
static int ms_save_cell(int chara){
 int i,j,host=Port_Beta8StockAlias(chara);if(host<0)host=chara;
 for(i=0;i<97&&i<(int)(sizeof(sMsAuthored)/sizeof(sMsAuthored[0]));i++){
  if(sMsAuthored[i].id==chara||sMsAuthored[i].id==host)return i;
  for(j=0;j<sMsAuthored[i].formCount;j++)if(sMsAuthored[i].form[j]==chara||sMsAuthored[i].form[j]==host)return i;
 }return -1;
}
static void rb_read(int,int,u16*);
static void ms_items(MsView*v,int chara,int*out){int j;u16 custom[8];const u16*items=NULL;if(v->preset>=1&&v->preset<=3){rb_read(chara,v->preset-1,custom);items=custom;}if(v->preset>=4&&v->preset<18&&gSaveData->rec[v->preset-4].chara==chara)items=TS_SAVE->rec[v->preset-4].items.id;for(j=0;j<8;j++)out[j]=items?items[j]:0;}
static void ms_cycle_preset(MsView*v,int chara){int i;for(i=0;i<18;i++){v->preset=(v->preset+1)%18;if(v->preset<=3)return;if(gSaveData->rec[v->preset-4].chara==chara)return;}v->preset=0;}
static int ms_family(int cid){
 int i,j,owner=Port_Beta8SelectFormOwner(cid);if(owner>=0)cid=owner;
 for(i=0;i<(int)(sizeof(sMsAuthored)/sizeof(sMsAuthored[0]));i++){
  if(sMsAuthored[i].id==cid)return cid;
  for(j=0;j<sMsAuthored[i].formCount;j++)if(sMsAuthored[i].form[j]==cid)return sMsAuthored[i].id;
 }return cid;
}
/* Juan: duplicates are allowed again, including multiple forms of one family. */
static int ms_taken(const MsView*v,int cid){(void)v;(void)cid;return 0;}
static int ms_random(MsView *v){
 int choices[MS_ROSTER],n=0,i;for(i=0;i<v->roster_count;i++)if(v->roster[i]>=0&&!ms_taken(v,v->roster[i]))choices[n++]=v->roster[i];
 return n?choices[Rand_Range(n)]:-1;
}
static void ms_disabled(MsView *v){int i;for(i=0;i<v->roster_count;i++)v->disabled[i]=ms_taken(v,v->roster[i]);for(i=0;i<v->form_count;i++)v->formDisabled[i]=ms_taken(v,v->forms[i]);}
static void ms_accept(MsView *v,int chara,int costume){
 if(ms_taken(v,chara)){v->error=2;return;}v->error=0;
 v->pick[v->slot].chara=chara;v->pick[v->slot].costume=costume;ms_items(v,chara,v->pick[v->slot].items);
 v->preset=0;v->slot=ms_first_empty(v);
}
static int ms_owner(MsView *v){
 int p=v->pick[v->slot].pad,side=MS_Side(v,v->slot);
 if(p<0){int j;p=0;for(j=0;j<v->total;j++)if(MS_Side(v,j)==side&&v->pick[j].pad>=0){p=v->pick[j].pad;break;}}
 if(p<0)p=0;
 return PortInput_PlayerAvailable(p)?p:0;
}
static int ms_owners(MsView *v){
 int i,p,n=Port_ModSelectPlayerCount(),used[MS_MAX]={0};
 for(i=0;i<v->total;i++){v->pick[i].team=MS_Side(v,i);v->pick[i].pad=-1;}
 for(p=0;p<n;p++){
  int team=v->mode==2?p:v->human_team[p],slot;
  for(slot=0;slot<v->total;slot++)if(MS_Side(v,slot)==team&&!used[slot])break;
  if(slot>=v->total)return 0;v->pick[slot].pad=p;used[slot]=1;
 }

 return 1;
}
static int ms_slot_for_pad(MsView *v,int p){
 int i,j,side,leader,ready;
 for(i=0;i<v->total;i++)if(v->pick[i].chara<0&&v->pick[i].pad==p)return i;
 for(i=0;i<v->total;i++)if(v->pick[i].chara<0&&v->pick[i].pad<0){
  side=MS_Side(v,i);leader=-1;ready=1;
  for(j=0;j<v->total;j++)if(MS_Side(v,j)==side&&v->pick[j].pad>=0){if(leader<0)leader=v->pick[j].pad;if(v->pick[j].chara<0)ready=0;}
  if(leader==p&&ready)return i;
  if(leader<0&&p==0){for(j=0;j<v->total;j++)if(MS_Side(v,j)<side&&v->pick[j].chara<0)ready=0;if(ready)return i;}
 }
 return -1;
}
static void ms_layout(MsView *v,int a,int b){
 int i;v->team_count=2;v->a=MS_Size(a);b=MS_Size(b);if(v->a+b>MS_UI_MAX)b=MS_UI_MAX-v->a;v->total=v->a+b;v->slot=0;v->error=0;
 for(i=0;i<MS_MAX;i++){v->pick[i].chara=-1;v->pick[i].costume=0;memset(v->pick[i].items,0,sizeof(v->pick[i].items));}
 if(!ms_owners(v))v->error=1;
}
static int ms_fill(MsView *v){int i,saved=v->slot;if(v->roster_count<1)return 0;for(i=0;i<v->total;i++)if(v->pick[i].chara<0){int c;v->slot=i;c=ms_random(v);if(c<0){v->slot=saved;return 0;}v->pick[i].chara=c;v->pick[i].costume=0;memset(v->pick[i].items,0,sizeof(v->pick[i].items));}v->slot=saved;return v->mode==4?MS_TournamentValid(v):MS_Valid(v);}

static int ms_tour_init(MsView *v){
 struct TTTeam team;int i,t=Port_TagTourDraftTeam();
 if(!Port_TagTourActive()||t<0||!Port_TagTourTeam(t,&team)||team.count!=sMsTourSize)return 0;
 v->mode=4;v->phase=1;v->a=v->total=team.count;v->team_count=0;v->stage=t;
 v->slot=0;v->cursor=0;v->player_count=Port_TagTourConfig(0);
 for(i=0;i<MS_MAX;i++){memset(&v->pick[i],0,sizeof(v->pick[i]));v->pick[i].chara=-1;v->pick[i].pad=i<team.count?team.owner[i]:-1;}
 return 1;
}
static int ms_yamcha_catalog(MsView*v){
 static struct TTRandomChoice pool[TT_RANDOM_MAX];int n=0,i,k,j,q;
 for(i=0;i<v->roster_count;i++)if(v->roster[i]>=0){
  int root=v->roster[i],nc=ChrTbl_WrapCostume(v->roster[i],NULL),forms[MS_FORMS],nf=Port_Beta8FormList(root,forms,MS_FORMS);if(nf<1){forms[0]=root;nf=1;}
  for(k=0;k<nc&&k<8;k++)if(Port_ModSelectCostumeOk(root,k))for(j=0;j<nf;j++){
   int c=forms[j],co;if(!Port_Beta8FormAllowed(root,k,c))continue;co=c==root?k:Port_Beta8TransformCostume(root,k,c);
   if(co<0||co>=ChrTbl_WrapCostume(c,NULL)||!Port_ModSelectCostumeOk(c,co))continue;
   for(q=0;q<n;q++)if(pool[q].family==root&&pool[q].character==c&&pool[q].costume==co)break;
   if(q==n){if(n>=TT_RANDOM_MAX)return 0;pool[n].family=root;pool[n].character=c;pool[n++].costume=co;}
  }
 }
 return Port_TagTourRandomCatalog(pool,n,0x59414d43u+(unsigned)Rand_Range(65536));
}
static int ms_yamcha_draw(MsView*v){
 int i,used[5];for(i=0;i<v->total;i++){struct TTFighter f;int j;for(j=0;j<i;j++)used[j]=v->pick[j].chara;if(!Port_TagTourRandomFighter(used,i,&f))return 0;v->pick[i].chara=f.character;v->pick[i].costume=f.costume;memset(v->pick[i].items,0,sizeof(v->pick[i].items));}
 ms_review(v);return 1;
}
static int ms_commit_draft(MsView *v){
 int i;if(!sMsTourOutput)return Port_ModSelectCommit(v);
 if(!MS_TournamentValid(v))return 0;
 for(i=0;i<v->total;i++)if(!Port_ModSelectCostumeOk(v->pick[i].chara,v->pick[i].costume))return 0;
 memcpy(sMsTourOutput,v->pick,v->total*sizeof(MsPick));return 1;
}

/* Story casts are immutable. Commit them before publishing ANY active selector
   snapshot or initializing its viewer; auto-confirming inside phase3 is too late
   because the render thread can already have drawn the review for one frame. */
static int ms_story_fixed(MsView *v){
 int fixed;
 if(!Port_StoryActive())return -1;
 fixed=Port_ScenarioDraft(v);v->active=0;
 if(fixed!=2||!Port_ModSelectCommit(v)){
  Port_ModSelectClear();Port_ModSelectPublish(v);
  Port_ModSelectTrace(4,v->a,v->total-v->a);return 0;
 }
 Port_ModSelectPublish(v);
 Port_ModSelectTrace(3,v->a,v->total-v->a);return 1;
}

static int ms_catalog(MsView *v,int random){int i;
 {static TsCell cells[MS_ROSTER],custom[14];int count=0,customCount=0,n=sizeof(sMsAuthored)/sizeof(sMsAuthored[0]);
  ChrGrid_Build(&count,cells,&n,sMsAuthored,&customCount,custom);
  for(i=0;i<count&&v->roster_count<MS_ROSTER;i++){
   int cid=cells[i].id;if(cid<0||cid>=MS_CHARACTERS||(cid>=161&&cid<=164)||Port_Beta8SelectFormOwner(cid)>=0||!Port_ModSelectCostumeOk(cid,0))continue;
   sMsCatalog[v->roster_count]=cells[i];v->roster[v->roster_count++]=cid;
  }
 }
 /* Verified fighters share IDs with stock menu markers: insert only after marker filtering. */
 {static const int ids[]={161,163,164,291,300,301,317,319,320,321,322,185,271,278,323,324,325};int k,j,have;for(k=0;k<(int)(sizeof(ids)/sizeof(ids[0]));k++){ /* 301: Yajirobe (Juan approved, power 970) */have=0;for(j=0;j<v->roster_count;j++)if(v->roster[j]==ids[k])have=1;if(!have&&Port_Beta8SelectFormOwner(ids[k])<0&&v->roster_count<MS_ROSTER&&Port_ModSelectCostumeOk(ids[k],0)){int at=v->roster_count++;v->roster[at]=ids[k];memset(&sMsCatalog[at],0,sizeof(sMsCatalog[at]));sMsCatalog[at].id=ids[k];sMsCatalog[at].formCount=Port_Beta8FormList(ids[k],sMsCatalog[at].form,MS_FORMS);}}}
 ms_sort_catalog(v);
 if(!v->roster_count)return 0;
 if(random&&v->roster_count<MS_ROSTER){memmove(v->roster+1,v->roster,v->roster_count*sizeof(int));memmove(sMsCatalog+1,sMsCatalog,v->roster_count*sizeof(TsCell));v->roster[0]=-1;memset(sMsCatalog,0,sizeof(TsCell));v->roster_count++;}
return v->roster_count;
}
int Port_ModSelectRun(void){
 MsView v;int i,ret=0,viewer=0,last=-1,lastCost=-1,settle=0,old=-1,oldCost=-1,quitting=0;
 int frameCursor,framePhase,frameCost;
 unsigned pressed,repeat;Pad *pad;int cursors[4]={0},costumes[4]={0};
 Port_ModSelectClear();memset(&v,0,sizeof(v));v.active=1;v.mode=Port_ModesFight();v.player_count=Port_ModSelectPlayerCount();
 /* TeamSel can arrive here with the menu's loader freeze bit still set. That bit is a gate for the
    stock menu update, not for this self-contained loop: holding it would draw the new select while
    ignoring every button until the screen is abandoned. The draft owns its loader (Job_Run below). */
 gProgress->flags&=~MPROG_FREEZE;
 {int fixed=sMsTourOutput?-1:ms_story_fixed(&v);if(fixed>=0)return fixed;}
 v.a=MS_Size(Port_Setting("mod_team_a",5));if(sMsTourOutput)v.mode=4;
 if(!ms_catalog(&v,1))return 0;
 Port_ModSelectTrace(0,v.mode,v.roster_count);
 {int p;for(p=0;p<4;p++){int team=p&1;if(Port_CoopActive()){int t,m;for(t=0;t<2;t++)for(m=0;m<10;m++)if(Port_CoopMemberPad(t,m)==p)team=t;}v.human_team[p]=team;}}
 if(sMsTourOutput){if(!ms_tour_init(&v))return 0;if(Port_TagTourConfig(2)==4&&(!ms_yamcha_catalog(&v)||!ms_yamcha_draw(&v)))return 0;Port_ModSelectPublish(&v);}
 else{
 ms_layout(&v,v.mode==2?5:v.a,v.mode==2?5:Port_Setting("mod_team_b",5));Port_ModSelectPublish(&v);
 if(v.mode!=2&&!Port_ScenarioActive()){int n=Port_Setting("mod_team_count",2),sizes[10],p;char key[40];if(n>=2&&n<=10){for(p=0;p<n;p++){snprintf(key,sizeof(key),"mod_team_size_%d",p);sizes[p]=Port_Setting(key,p==0?v.a:p==1?v.total-v.a:1);}if(MS_Layout(&v,sizes,n)){for(p=0;p<4;p++){snprintf(key,sizeof(key),"mod_human_team_%d",p);int t=Port_Setting(key,v.human_team[p]);v.human_team[p]=t>=0&&t<n?t:0;}if(!ms_owners(&v))v.error=1;}}}

 if(Port_ScenarioActive()){int fixed=Port_ScenarioDraft(&v);if(fixed==2)ms_review(&v);Port_ModSelectPublish(&v);}
 }
 /* Init owns the viewer's jobs/resources only until the draft ends. No battle is running. */
 Port_ChrViewTournament(v.mode==4?Port_TagTourConfig(2)+1:0);ChrView_Init();if(!File_IsPartitionLoaded(2)){File_LoadPartitionNw(2);File_WaitPartition(2);}Bgm_Play(v.mode==4?TT_ThemeMusic(Port_TagTourConfig(2)):PORT_BGM_FILE(Port_ModSelectMusic()));viewer=1;Port_ModSelectTrace(1,0,0);OrbitCam_SetPadLocked(1);Port_MenuScene3D(1);
 ColorFade_StartIn(0,0,0,0x14);
 for(;;){
  i=(v.phase==1||v.phase==2)?ms_owner(&v):0;
  if(i!=v.pad){cursors[v.pad]=v.cursor;costumes[v.pad]=v.costume;v.cursor=cursors[i];v.costume=costumes[i];}
  v.pad=i;PortInput_KeyboardContext(1u<<v.pad);
  Gfx_BeginFrame();Pad_Update();Snd_Update();ColorFade_Update();
  if(v.phase==1){
   int p;for(p=0;p<v.player_count;p++)if(p!=v.pad&&PortInput_PlayerAvailable(p)){
    Pad *other=Port_Pad(p);int slot=ms_slot_for_pad(&v,p);
    if(slot>=0&&(other->pressed||other->repeat)){
     cursors[v.pad]=v.cursor;costumes[v.pad]=v.costume;
     v.pad=p;v.slot=slot;v.cursor=cursors[p];v.costume=costumes[p];break;
    }
   }
  }
  frameCursor=v.cursor;framePhase=v.phase;frameCost=v.costume;
  pad=Port_Pad(v.pad);
  pressed=pad->pressed;repeat=pad->repeat;
  if((pressed&0x2000)&&!Port_ScenarioActive()){extern void Port_ModSelectBattleConfig(void);Port_ModSelectBattleConfig();pressed=repeat=0;}
  if(v.phase!=1&&v.phase!=3)Port_ModSelectTakeMouse(); /* keep review-button clicks */
  /* Do not inherit the stock menu's freeze gate: input and the draft state machine must continue
     while the model viewer or the native menu resources are loading. */
  {
   if(pressed&PAD_CIRCLE){Snd_PlaySe(1,2);quitting=1;goto ms_end_frame;}
   if(v.phase==0){
    if(v.mode!=2){int rows=1+MS_Teams(&v)+Port_ModSelectPlayerCount();
     if(repeat&(PAD_UP|PAD_LSTICK_UP))v.cursor=MS_Move(v.cursor,-1,rows);
     else if(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))v.cursor=MS_Move(v.cursor,1,rows);
     if(repeat&(PAD_LEFT|PAD_LSTICK_LEFT|PAD_RIGHT|PAD_LSTICK_RIGHT)){
      int delta=repeat&(PAD_RIGHT|PAD_LSTICK_RIGHT)?1:-1,n=MS_Teams(&v);
      if(v.cursor>n){int p=v.cursor-n-1;v.human_team[p]=MS_Move(v.human_team[p],delta,n);}
      else MS_Edit(&v,v.cursor,delta);
      if(!ms_owners(&v))v.error=1;else v.error=0;
     }
    }
    if(pressed&PAD_CROSS){if(ms_owners(&v)){int at;for(at=0;at<v.total;at++){v.pick[at].chara=-1;v.pick[at].costume=0;memset(v.pick[at].items,0,sizeof(v.pick[at].items));}v.slot=0;v.phase=1;v.cursor=0;v.error=0;Port_SettingSave("mod_team_a",v.a);Port_SettingSave("mod_team_b",MS_Count(&v,1));{int t;char key[40];Port_SettingSave("mod_team_count",MS_Teams(&v));for(t=0;t<MS_Teams(&v);t++){snprintf(key,sizeof(key),"mod_team_size_%d",t);Port_SettingSave(key,MS_Count(&v,t));}for(t=0;t<4;t++){snprintf(key,sizeof(key),"mod_human_team_%d",t);Port_SettingSave(key,v.human_team[t]);}}Port_SettingsWrite();}else v.error=1;}
    if(pressed&PAD_TRIANGLE){quitting=1;goto ms_end_frame;}
  }else if(v.phase==3){
   int click=Port_ModSelectTakeMouse();
   if(v.mode==4&&Port_TagTourConfig(2)==4){if(pressed&PAD_SQUARE){if(!ms_yamcha_draw(&v))v.error=1;goto ms_publish;}if((pressed&PAD_TRIANGLE)||click==-2){quitting=1;goto ms_end_frame;}}
   if((pressed&PAD_TRIANGLE)||click==-2){
    if(Port_ScenarioActive()){quitting=1;goto ms_end_frame;}
    v.slot=v.total-1;v.pick[v.slot].chara=-1;v.phase=1;v.cursor=0;v.costume=0;
   }else if((pressed&PAD_CROSS)||click==-1){if(ms_commit_draft(&v)){ret=1;quitting=1;goto ms_end_frame;}v.error=1;}
  }else if(v.phase==2){
   int delta=0;
   if(repeat&(PAD_LEFT|PAD_LSTICK_LEFT))delta=-1;
   else if(repeat&(PAD_RIGHT|PAD_LSTICK_RIGHT))delta=1;
   else if(repeat&(PAD_UP|PAD_LSTICK_UP))delta=-3;
   else if(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))delta=3;
   if(delta){int c=v.costume;v.form_cursor=MS_Move(v.form_cursor,delta,v.form_count);ChrTbl_WrapCostume(v.forms[v.form_cursor],&c);if(!Port_Beta8CostumeSelectable(v.forms[v.form_cursor],c))c=ms_next_costume(v.forms[v.form_cursor],c,1);v.costume=c;}
   if(pressed&(PAD_L2|PAD_R2)){
    int root=v.forms[v.form_cursor],c=ms_next_costume(root,v.costume,pressed&PAD_R2?1:-1);if(c>=0&&Port_ModSelectCostumeOk(root,c)){v.costume=c;ms_refresh_forms(&v,root,c,root);}
   }
   if(pressed&PAD_TRIANGLE){v.phase=1;v.form_count=0;v.costume=0;}
   else if(pressed&PAD_CROSS){
    if(ms_taken(&v,v.forms[v.form_cursor])){v.error=2;goto ms_publish;}
    ms_accept(&v,v.forms[v.form_cursor],v.costume);
    if(v.slot<0)ms_review(&v);
    else v.phase=1;
   }
  }else{
    int delta=0,mouse=Port_ModSelectTakeMouse();
    if(mouse>0&&mouse<=v.roster_count){v.cursor=mouse-1;v.costume=0;}
    if(repeat&(PAD_LEFT|PAD_LSTICK_LEFT))delta=-1;
    else if(repeat&(PAD_RIGHT|PAD_LSTICK_RIGHT))delta=1;
    else if(repeat&(PAD_UP|PAD_LSTICK_UP))delta=-MS_COLS;
    else if(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))delta=MS_COLS;
    if(delta){v.cursor=MS_Move(v.cursor,delta,v.roster_count);v.costume=0;v.error=0;}
    if(pressed&(PAD_L1|PAD_R1)){v.cursor=MS_Move(v.cursor,pressed&PAD_R1?MS_PAGE:-MS_PAGE,v.roster_count);v.costume=0;}
    if(v.roster[v.cursor]>=0&&(pressed&(PAD_L2|PAD_R2))){
     int count=1,c=ms_next_costume(v.roster[v.cursor],v.costume,pressed&PAD_R2?1:-1);
     if(count>0&&Port_ModSelectCostumeOk(v.roster[v.cursor],c))v.costume=c;
    }
    if(pressed&PAD_TRIANGLE){if(v.slot>0){v.slot--;v.pick[v.slot].chara=-1;}else {if(v.mode==4||Port_ScenarioActive()){quitting=1;goto ms_end_frame;}v.phase=0;v.cursor=0;}}
    else if(pressed&PAD_SQUARE){if(ms_fill(&v))ms_review(&v);else v.error=1;}
    else if(pressed&PAD_CROSS){
     if(ms_taken(&v,v.roster[v.cursor])){v.error=2;goto ms_publish;}
     if(v.roster[v.cursor]<0){int random=ms_random(&v);if(random<0){v.error=2;goto ms_publish;}ms_accept(&v,random,0);if(v.slot<0)ms_review(&v);goto ms_publish;}
     v.form_count=Port_Beta8FormList(v.roster[v.cursor],v.forms,MS_FORMS);
     if(!v.form_count){v.form_count=sMsCatalog[v.cursor].formCount;
     if(v.form_count>MS_FORMS)v.form_count=MS_FORMS;
     for(i=0;i<v.form_count;i++)v.forms[i]=sMsCatalog[v.cursor].form[i];
     }
     /* Never offer a form whose model is missing. */
     {int n=0,f;for(f=0;f<v.form_count;f++)if(Port_Beta8FormAllowed(v.roster[v.cursor],v.costume,v.forms[f])&&Port_ModSelectCostumeOk(v.forms[f],0))v.forms[n++]=v.forms[f];v.form_count=n;}
     if(v.form_count>0){v.form_cursor=0;v.phase=2;}
     else{
      ms_accept(&v,v.roster[v.cursor],v.costume);
      if(v.slot<0)ms_review(&v);
     }
    }
   }
  }
 ms_publish:
  ms_disabled(&v);
  if(pressed&PAD_TRIANGLE)Snd_PlaySe(1,2);
  else if(pressed&(PAD_CROSS|PAD_SQUARE))Snd_PlaySe(1,v.error?7:1);
  else if(frameCursor!=v.cursor||frameCost!=v.costume||framePhase!=v.phase||(repeat&&(v.phase==2)))Snd_PlaySe(1,0);
  if(v.phase&&ms_view_chara(&v)>=0&&(pressed&PAD_R3)){ms_cycle_preset(&v,ms_view_chara(&v));Snd_PlaySe(1,0);}
  if(old>=0&&ms_view_chara(&v)!=old)v.preset=0;
  /* Hover debounce and one job at a time: rapid paging cannot churn model loads. */
  i=ms_view_chara(&v);
  if(old!=i||oldCost!=v.costume){old=i;oldCost=v.costume;settle=0;}else settle++;
  Job_Run();v.loading=ChrView_IsLoading();
  if(i>=0&&settle>=8&&!v.loading&&(last!=i||lastCost!=v.costume)){
   Port_ModSelectTrace(2,i,v.costume);ChrView_Show(i,v.costume,0);last=i;lastCost=v.costume;v.loading=1;
  }
  OrbitCam_SetPadLocked(1);ChrView_Update();Port_ModSelectPublish(&v);
 ms_end_frame:
  if(quitting&&ret)Snd_PlaySe(1,1);
  ColorFade_Draw();Gfx_EndFrame(1);Dma_Flush();File_Stub264D90();
  if(quitting)break;
 }
 if(viewer){ChrView_Hide();ChrView_Term();}
 Port_ChrViewTournament(0);Port_MenuScene3D(0);PortInput_KeyboardContext(0);v.active=0;Port_ModSelectPublish(&v);
 Port_ModSelectTrace(ret?3:4,v.a,v.total-v.a);
 if(!ret)Port_ModSelectClear();
 return ret;
}

int Port_ModSelectTournamentRun(MsPick *out,int count){
 int ret;if(!out||count<1||count>5||!Port_TagTourActive())return 0;
 Port_TagTourDraftStop();sMsTourOutput=out;sMsTourSize=count;
 ret=Port_ModSelectRun();sMsTourOutput=NULL;sMsTourSize=0;return ret;
}

#include "mod_roster_browser.inc"
