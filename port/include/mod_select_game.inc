#include "mod_scenarios.h"
extern int Port_StoryActive(void);
static TsCell sMsCatalog[MS_ROSTER];
/* Familiar stock-family order; stable insertion keeps added siblings together. */
static int ms_roster_key(int cid){
 /* Era/family anchors follow authored menu cells, not numerical donor IDs. */
 static const int order[]={11,0,1,3,7,161,13,14,17,159,20,12,29,31,34,122,39,44,46,81,74,51,110,210,22,23,25,26,27,28,66,68,69,70,63,186,187,58,55,61,73,75,77,79,80,82,84,85,87,88,90,91,97,98,99,100,101,102,103,105,109,111,113,114,117,118,121,123,124,126,129,131,134,135,140,144,261,275,276,213,145,76,146,151,152,154,155,156,157};
 int i,anchor=cid,added=0;
 for(i=0;i<(int)(sizeof(sMsFamily)/sizeof(sMsFamily[0]));i++)if(sMsFamily[i][0]==cid){anchor=sMsFamily[i][1];added=1;break;}
 /* These are independent GT dragons, not the donor characters they borrow. */
 if(cid==261||cid==275||cid==276||cid==213)anchor=cid;
 if(cid==291){anchor=114;added=2;}
 if(cid==300){anchor=55;added=2;}
 for(i=0;i<(int)(sizeof(order)/sizeof(order[0]));i++)if(order[i]==anchor)return i*32+added;
 return 10000+cid;
}
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
static void ms_items(MsView *v,int chara,int *out){
 int j,cell=ms_save_cell(chara);const u16 *items=NULL;
 if(v->preset>=1&&v->preset<=3&&cell>=0)items=TS_SAVE->custom[cell].set[v->preset-1].id;
 if(v->preset>=4&&v->preset<18&&gSaveData->rec[v->preset-4].chara==chara)items=TS_SAVE->rec[v->preset-4].items.id;
 for(j=0;j<8;j++)out[j]=items?items[j]:0;
}
static void ms_cycle_preset(MsView *v,int chara){
 int i;for(i=0;i<18;i++){v->preset=(v->preset+1)%18;if(!v->preset)return;if(v->preset<=3&&ms_save_cell(chara)>=0)return;if(v->preset>=4&&gSaveData->rec[v->preset-4].chara==chara)return;}
 v->preset=0;
}
static int ms_family(int cid){
 int i,j,owner=Port_Beta8SelectFormOwner(cid);if(owner>=0)cid=owner;
 for(i=0;i<(int)(sizeof(sMsAuthored)/sizeof(sMsAuthored[0]));i++){
  if(sMsAuthored[i].id==cid)return cid;
  for(j=0;j<sMsAuthored[i].formCount;j++)if(sMsAuthored[i].form[j]==cid)return sMsAuthored[i].id;
 }return cid;
}
static int ms_taken(const MsView *v,int cid){
 if(Port_ScenarioActive())return 0;
 int i,side;if(cid<0||v->slot<0)return 0;side=MS_Side(v,v->slot);cid=ms_family(cid);
 for(i=0;i<v->total;i++)if(i!=v->slot&&v->pick[i].chara>=0&&MS_Side(v,i)==side&&ms_family(v->pick[i].chara)==cid)return 1;
 return 0;
}
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
static int ms_fill(MsView *v){int i,saved=v->slot;if(v->roster_count<1)return 0;for(i=0;i<v->total;i++)if(v->pick[i].chara<0){int c;v->slot=i;c=ms_random(v);if(c<0){v->slot=saved;return 0;}v->pick[i].chara=c;v->pick[i].costume=0;memset(v->pick[i].items,0,sizeof(v->pick[i].items));}v->slot=saved;return MS_Valid(v);}

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

int Port_ModSelectRun(void){
 MsView v;int i,ret=0,viewer=0,last=-1,lastCost=-1,settle=0,old=-1,oldCost=-1,quitting=0;
 int frameCursor,framePhase,frameCost;
 unsigned pressed,repeat;Pad *pad;int cursors[4]={0},costumes[4]={0};
 Port_ModSelectClear();memset(&v,0,sizeof(v));v.active=1;v.mode=Port_ModesFight();v.player_count=Port_ModSelectPlayerCount();
 /* TeamSel can arrive here with the menu's loader freeze bit still set. That bit is a gate for the
    stock menu update, not for this self-contained loop: holding it would draw the new select while
    ignoring every button until the screen is abandoned. The draft owns its loader (Job_Run below). */
 gProgress->flags&=~MPROG_FREEZE;
 {int fixed=ms_story_fixed(&v);if(fixed>=0)return fixed;}
 v.a=MS_Size(Port_Setting("mod_team_a",5));
 {static TsCell cells[MS_ROSTER],custom[14];int count=0,customCount=0,n=sizeof(sMsAuthored)/sizeof(sMsAuthored[0]);
  ChrGrid_Build(&count,cells,&n,sMsAuthored,&customCount,custom);
  for(i=0;i<count&&v.roster_count<MS_ROSTER;i++){
   int cid=cells[i].id;if(cid<0||cid>=MS_ROSTER||(cid>=161&&cid<=164)||Port_Beta8SelectFormOwner(cid)>=0||!Port_ModSelectCostumeOk(cid,0))continue;
   sMsCatalog[v.roster_count]=cells[i];v.roster[v.roster_count++]=cid;
  }
 }
 /* Verified fighters share IDs with stock menu markers: insert only after marker filtering. */
 {static const int ids[]={161,163,164,291,300,301};int k,j,have;for(k=0;k<6;k++){ /* 301: Yajirobe (Juan approved, power 970) */have=0;for(j=0;j<v.roster_count;j++)if(v.roster[j]==ids[k])have=1;if(!have&&Port_Beta8SelectFormOwner(ids[k])<0&&v.roster_count<MS_ROSTER&&Port_ModSelectCostumeOk(ids[k],0)){int at=v.roster_count++;v.roster[at]=ids[k];memset(&sMsCatalog[at],0,sizeof(sMsCatalog[at]));sMsCatalog[at].id=ids[k];sMsCatalog[at].formCount=Port_Beta8FormList(ids[k],sMsCatalog[at].form,MS_FORMS);}}}
 ms_sort_catalog(&v);
 if(!v.roster_count)return 0;
 if(v.roster_count<MS_ROSTER){memmove(v.roster+1,v.roster,v.roster_count*sizeof(int));memmove(sMsCatalog+1,sMsCatalog,v.roster_count*sizeof(TsCell));v.roster[0]=-1;memset(sMsCatalog,0,sizeof(TsCell));v.roster_count++;}
 Port_ModSelectTrace(0,v.mode,v.roster_count);
 {int p;for(p=0;p<4;p++){int team=p&1;if(Port_CoopActive()){int t,m;for(t=0;t<2;t++)for(m=0;m<10;m++)if(Port_CoopMemberPad(t,m)==p)team=t;}v.human_team[p]=team;}}
 ms_layout(&v,v.mode==2?5:v.a,v.mode==2?5:Port_Setting("mod_team_b",5));Port_ModSelectPublish(&v);
 if(v.mode!=2&&!Port_ScenarioActive()){int n=Port_Setting("mod_team_count",2),sizes[10],p;char key[40];if(n>=2&&n<=10){for(p=0;p<n;p++){snprintf(key,sizeof(key),"mod_team_size_%d",p);sizes[p]=Port_Setting(key,p==0?v.a:p==1?v.total-v.a:1);}if(MS_Layout(&v,sizes,n)){for(p=0;p<4;p++){snprintf(key,sizeof(key),"mod_human_team_%d",p);int t=Port_Setting(key,v.human_team[p]);v.human_team[p]=t>=0&&t<n?t:0;}if(!ms_owners(&v))v.error=1;}}}

 if(Port_ScenarioActive()){int fixed=Port_ScenarioDraft(&v);if(fixed==2)ms_review(&v);Port_ModSelectPublish(&v);}
 /* Init owns the viewer's jobs/resources only until the draft ends. No battle is running. */
 ChrView_Init();if(!File_IsPartitionLoaded(2)){File_LoadPartitionNw(2);File_WaitPartition(2);}Bgm_Play(PORT_BGM_FILE(Port_ModSelectMusic()));viewer=1;Port_ModSelectTrace(1,0,0);OrbitCam_SetPadLocked(1);Port_MenuScene3D(1);
 ColorFade_StartIn(0,0,0,0x14);
 for(;;){
  i=(v.phase==1||v.phase==2)?ms_owner(&v):0;
  if(i!=v.pad){cursors[v.pad]=v.cursor;costumes[v.pad]=v.costume;v.cursor=cursors[i];v.costume=costumes[i];}
  v.pad=i;PortInput_KeyboardContext(1u<<v.pad);
  Gfx_BeginFrame();Pad_Update();Snd_Update();ColorFade_Update();
  if(v.phase==1){
   int p;for(p=0;p<Port_ModSelectPlayerCount();p++)if(p!=v.pad&&PortInput_PlayerAvailable(p)){
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
   if((pressed&PAD_TRIANGLE)||click==-2){
    if(Port_ScenarioActive()){quitting=1;goto ms_end_frame;}
    v.slot=v.total-1;v.pick[v.slot].chara=-1;v.phase=1;v.cursor=0;v.costume=0;
   }else if((pressed&PAD_CROSS)||click==-1){if(Port_ModSelectCommit(&v)){ret=1;quitting=1;goto ms_end_frame;}v.error=1;}
  }else if(v.phase==2){
   int delta=0;
   if(repeat&(PAD_LEFT|PAD_LSTICK_LEFT))delta=-1;
   else if(repeat&(PAD_RIGHT|PAD_LSTICK_RIGHT))delta=1;
   else if(repeat&(PAD_UP|PAD_LSTICK_UP))delta=-3;
   else if(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))delta=3;
   if(delta){int c=v.costume;v.form_cursor=MS_Move(v.form_cursor,delta,v.form_count);ChrTbl_WrapCostume(v.forms[v.form_cursor],&c);v.costume=c;}
   if(pressed&(PAD_L2|PAD_R2)){
    int c=ms_next_costume(v.forms[v.form_cursor],v.costume,pressed&PAD_R2?1:-1);if(c>=0&&Port_ModSelectCostumeOk(v.forms[v.form_cursor],c))v.costume=c;
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
    if(pressed&PAD_TRIANGLE){if(v.slot>0){v.slot--;v.pick[v.slot].chara=-1;}else {if(Port_ScenarioActive()){quitting=1;goto ms_end_frame;}v.phase=0;v.cursor=0;}}
    else if(pressed&PAD_SQUARE){if(ms_fill(&v))ms_review(&v);else v.error=1;}
    else if(pressed&PAD_CROSS){
     if(ms_taken(&v,v.roster[v.cursor])){v.error=2;goto ms_publish;}
     if(v.roster[v.cursor]<0){int random=ms_random(&v);if(random<0){v.error=2;goto ms_publish;}ms_accept(&v,random,0);if(v.slot<0)ms_review(&v);goto ms_publish;}
     v.form_count=sMsCatalog[v.cursor].formCount;
     if(v.form_count>MS_FORMS)v.form_count=MS_FORMS;
     for(i=0;i<v.form_count;i++)v.forms[i]=sMsCatalog[v.cursor].form[i];
     if(!v.form_count)v.form_count=Port_Beta8FormList(v.roster[v.cursor],v.forms,MS_FORMS);
     /* Never offer a form whose model is missing. */
     {int n=0,f;for(f=0;f<v.form_count;f++)if(Port_ModSelectCostumeOk(v.forms[f],0))v.forms[n++]=v.forms[f];v.form_count=n;}
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
 Port_MenuScene3D(0);PortInput_KeyboardContext(0);v.active=0;Port_ModSelectPublish(&v);
 Port_ModSelectTrace(ret?3:4,v.a,v.total-v.a);
 if(!ret)Port_ModSelectClear();
 return ret;
}
