/* Single-character library/editor and native Ultimate Battle selection. */
extern void Port_RosterFocusPublish(int,int,int);
extern int Port_StagePreviewStep(int);extern void Port_StagePreviewEnd(void);
extern s32 Port_EvoSaveRun(void),ChrTbl_GetExp(s32,s32);
extern void Port_ReferenceBegin(void),Port_ReferenceSelect(int),Port_ReferenceAction(int,int),Port_ReferenceTick(void),Port_ReferenceEnd(void);
static MsView *sRbView;

extern void OrbitCam_SetTarget(f32,f32,f32),OrbitCam_SetAngles(f32,f32,f32),OrbitCam_SetDist(f32),OrbitCam_SetDistRange(f32,f32);
extern void Port_RosterInfoPublish(int,int,int,int,const int*,int,int);
extern void Port_RosterNamesPublish(const char*,const char*);
extern int Port_RosterCommand(void);
extern int Port_RosterPresetRead(int,int,u16*),Port_RosterPresetWrite(int,int,const u16*);
extern void Port_RosterItemDetailPublish(int,const char*);
extern void Port_RosterItemPublish(int,int,int,int,const char*);
extern int PortA11y_Utf8(char*,int,const u16*);
static int rb_cell(int cid){int i,j;for(i=0;i<97;i++){if(sMsAuthored[i].id==cid)return i;for(j=0;j<sMsAuthored[i].formCount;j++)if(sMsAuthored[i].form[j]==cid)return i;}return -1;}
static int rb_owner(int cid){int owner=Port_Beta8SelectFormOwner(cid);return owner>=0?owner:cid;}
static void rb_read(int cid,int set,u16*out){int cell=rb_cell(cid);memset(out,0,16);if(cell>=0)memcpy(out,gSaveData->custom[cell].item[set],16);else Port_RosterPresetRead(rb_owner(cid),set,out);}
static int rb_write(int cid,int set,const u16*in){int cell=rb_cell(cid);if(cell>=0){u16 before[8];int ok;memcpy(before,gSaveData->custom[cell].item[set],16);memcpy(gSaveData->custom[cell].item[set],in,16);sRbView->active=0;Port_ModSelectPublish(sRbView);ok=Port_EvoSaveRun();sRbView->active=1;Port_ModSelectPublish(sRbView);if(!ok)memcpy(gSaveData->custom[cell].item[set],before,16);return ok;}return Port_RosterPresetWrite(rb_owner(cid),set,in);}
typedef struct RbItem{u8 type,group,picture,slots;u8 unused[16];s32 flags;u8 pad[16];} RbItem;
extern s32 ChrTbl_GetLevel(s32,s32,s32),ItemTbl_CanEquip(s32,s32,RbItem*);
static int rb_cost(const u16*ids,RbItem*t){int i,n=0;for(i=0;i<7;i++)if(ids[i]>0&&ids[i]<=350)n+=t[ids[i]-1].slots;return n;}
static void rb_publish_items(void){int i;char name[240];RbItem*t=(RbItem*)MPACK_AT(gCommonRes->data[2],2);void*texts=NULL;
 if(gMenuArc7){u32*pack=(u32*)MPACK_AT(gMenuArc7,3);void*res=Sprite_Unpack(pack,NULL,NULL);if(res){texts=MPACK_AT(res,37);for(i=0;i<350;i++){u16*line=(u16*)MPACK_AT(texts,i+1);name[0]=0;PortA11y_Utf8(name,sizeof(name),line);Port_RosterItemPublish(i,gSaveData->item[i]&1,t[i].slots,t[i].flags,name);{char detail[512];void *help=MPACK_AT(res,2),*descs=MPACK_AT(help,5);PortA11y_Utf8(detail,sizeof(detail),(u16*)MPACK_AT(descs,i+1));Port_RosterItemDetailPublish(i,detail);}}Heap_Free(res);return;}}
 for(i=0;i<350;i++){snprintf(name,sizeof(name),"Potara %d",i+1);Port_RosterItemPublish(i,gSaveData->item[i]&1,t[i].slots,t[i].flags,name);Port_RosterItemDetailPublish(i,"");}}
extern int Port_RosterLevelRead(int,int*),Port_RosterLevelWrite(int,int,int);
static int rb_progress(int cid,int*exp,int*next,int*level){int cell=rb_cell(cid),d=Port_Beta8StockAlias(cid),saved[2]={0,0};if(d<0||d>160)d=0;u8*row=(u8*)MPACK_AT(gCommonRes->data[2],1)+d*60;if(cell>=0){*level=gSaveData->custom[cell].level;*exp=gSaveData->custom[cell].unk30;}else{Port_RosterLevelRead(rb_owner(cid),saved);*level=saved[0];*exp=saved[1];}int capacity=*(u16*)(row+14)+*level;*next=*level>=0&&*level<7&&capacity<7?((s32*)(row+16))[*level]:0;return capacity<1?1:capacity>7?7:capacity;}
static int rb_capacity(int cid){int e,n,l;return rb_progress(cid,&e,&n,&l);}
static int rb_upgrade(int cid){int e,n,l,capacity=rb_progress(cid,&e,&n,&l),cell=rb_cell(cid),pay=n-e;if(capacity>=7||n<=0||pay<=0||pay>gSaveData->money)return 2;int money=gSaveData->money;SaveCustom before;if(cell>=0)before=gSaveData->custom[cell];else if(!Port_RosterLevelWrite(rb_owner(cid),l+1,n))return 3;gSaveData->money-=pay;if(cell>=0){gSaveData->custom[cell].unk30=n;gSaveData->custom[cell].level=l+1;}sRbView->active=0;Port_ModSelectPublish(sRbView);int ok=Port_EvoSaveRun();sRbView->active=1;Port_ModSelectPublish(sRbView);if(!ok){gSaveData->money=money;if(cell>=0)gSaveData->custom[cell]=before;else Port_RosterLevelWrite(rb_owner(cid),l,e);}return ok?1:3;}
int Port_RosterBrowse(int kind,MsPick*out,int count,int dp){
 MsView v;int lastModel=-1,lastModelCost=-1;int inventoryCursor=0;int release=1;int result=0,old=-1,done=0,i,set=0,slot=0,status=0,focus=0;u16 equipped[8]={0},drafts[3][8]={{0}};
 if(kind<5||kind>7||count<1||count>5)return 0;
 memset(&v,0,sizeof(v));v.active=1;v.phase=1;v.mode=kind;v.player_count=1;v.total=count;v.a=count;v.slot=0;
 if(!ms_catalog(&v,0))return 0;gProgress->flags&=~MPROG_FREEZE;Port_ModSelectPublish(&v);
 if(kind==6)rb_publish_items();
 sRbView=&v;if(kind==5){Port_ReferenceBegin();if(!File_IsPartitionLoaded(2)){File_LoadPartitionNw(2);File_WaitPartition(2);}OrbitCam_SetPadLocked(1);}if(kind==5||kind==6)Bgm_Play(0x10B18);Port_RosterCommand();if(kind==6||kind==7){Port_StagePreviewStep(41);if(!File_IsPartitionLoaded(2)){File_LoadPartitionNw(2);File_WaitPartition(2);}OrbitCam_SetPadLocked(1);Port_MenuScene3D(1);}else Port_MenuScene3D(0);PortInput_KeyboardContext(1);
 ColorFade_StartIn(0,0,0,20);
 for(;!done;){unsigned pressed,repeat;int cid,cmd;Pad*pad;
 Gfx_BeginFrame();Pad_Update();Snd_Update();ColorFade_Update();pad=Port_Pad(0);pressed=pad->pressed;repeat=pad->repeat;cmd=Port_RosterCommand();if(release){if(!(pad->held&(PAD_CROSS|PAD_CIRCLE|PAD_TRIANGLE|PAD_SQUARE|PAD_START)))release=0;pressed=repeat=0;cmd=-1;}if(v.phase!=4&&v.phase!=6&&(pressed&PAD_R3))v.preset=(v.preset+1)%4;
 if(pressed&PAD_CIRCLE){done=1;goto rb_draw;}
 if(kind==7&&v.slot>0&&(pressed&PAD_SQUARE)){result=v.slot;done=1;goto rb_draw;}
 cid=(v.phase==3||v.phase==4||v.phase==5||v.phase==6)?v.pick[0].chara:ms_view_chara(&v);
 if(v.phase==6){
  if((pressed&PAD_TRIANGLE)||cmd==6){v.phase=4;status=0;release=1;}
  else if((pressed&PAD_CROSS)||cmd==5){status=rb_upgrade(cid);if(status==1)status=4;v.phase=4;release=1;Snd_PlaySe(1,status==4?1:7);}
 }else if(kind==5&&(v.phase==3||v.phase==5)){
   if(v.phase==3){if(repeat&(PAD_UP|PAD_LSTICK_UP))focus=(focus+3)%4;if(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))focus=(focus+1)%4;if(cmd>=4000&&cmd<4004){focus=cmd-4000;pressed|=PAD_CROSS;}if(pressed&PAD_CROSS){Port_ReferenceAction(focus,cid);if(focus==3){v.phase=5;Port_MenuScene3D(1);lastModel=-1;}Snd_PlaySe(1,1);}}
   else if(pressed&(PAD_LEFT|PAD_RIGHT|PAD_L2|PAD_R2)){v.costume=ms_next_costume(cid,v.costume,pressed&(PAD_RIGHT|PAD_R2)?1:-1);lastModel=-1;}
   if(pressed&PAD_TRIANGLE){if(v.phase==5){ChrView_Hide();Port_MenuScene3D(0);v.phase=3;}else v.phase=2;Snd_PlaySe(1,2);}Port_RosterFocusPublish(focus,gSaveData->money,0);goto rb_draw;
  }else if(v.phase==4){
  cid=v.pick[0].chara;
  if(repeat&(PAD_UP|PAD_DOWN|PAD_LSTICK_UP|PAD_LSTICK_DOWN)){int dir=repeat&(PAD_UP|PAD_LSTICK_UP)?-1:1,k;RbItem*t=(RbItem*)MPACK_AT(gCommonRes->data[2],2);for(k=0;k<350;k++){inventoryCursor=(inventoryCursor+dir+350)%350;if((gSaveData->item[inventoryCursor]&1)&&!(t[inventoryCursor].flags&5)&&(t[inventoryCursor].flags&64))break;}}
  if(pressed&PAD_CROSS)cmd=1000+inventoryCursor;
  if(pressed&PAD_START)cmd=2;
  if(pressed&PAD_R3)cmd=4;

  if(pressed&PAD_TRIANGLE){v.phase=1;focus=0;status=0;}
  if((pressed&(PAD_L1|PAD_R1))||(cmd>=5000&&cmd<5003)){memcpy(drafts[set],equipped,16);set=cmd>=5000&&cmd<5003?cmd-5000:(set+(pressed&PAD_R1?1:2))%3;memcpy(equipped,drafts[set],16);status=0;}
  if(cmd>=6000&&cmd<6007)slot=cmd-6000;
  if(pressed&(PAD_L2|PAD_R2)){slot=(slot+(pressed&PAD_R2?1:6))%7;status=0;}
  if(cmd>=1000&&cmd<1350){int item=cmd-1000,j,donor=Port_Beta8StockAlias(cid),cell=rb_cell(cid),capacity;RbItem*t=(RbItem*)MPACK_AT(gCommonRes->data[2],2);u16 proposed[8];inventoryCursor=item;if(donor<0||donor>160)donor=0;capacity=rb_capacity(cid);memcpy(proposed,equipped,16);proposed[slot]=item+1;
   status=2;if((gSaveData->item[item]&1)&&ItemTbl_CanEquip(item,donor,t)&&rb_cost(proposed,t)<=capacity){int valid=1;for(j=0;j<7;j++)if(j!=slot&&proposed[j]>0&&proposed[j]<=350&&t[proposed[j]-1].type==t[item].type&&t[proposed[j]-1].group==t[item].group)valid=0;if(valid){memcpy(equipped,proposed,16);status=0;}}Snd_PlaySe(1,status?7:1);}
  if(cmd==4){v.phase=6;status=0;release=1;Snd_PlaySe(1,1);}
  if(cmd==2){equipped[slot]=0;status=0;}
  if((pressed&PAD_SQUARE)||cmd==1){status=rb_write(cid,set,equipped)?1:3;release=1;Snd_PlaySe(1,status==1?1:7);}
 }else{
  int delta=(repeat&(PAD_LEFT|PAD_LSTICK_LEFT))?-1:(repeat&(PAD_RIGHT|PAD_LSTICK_RIGHT))?1:(repeat&(PAD_UP|PAD_LSTICK_UP))?-(v.phase==2?3:MS_COLS):(repeat&(PAD_DOWN|PAD_LSTICK_DOWN))?(v.phase==2?3:MS_COLS):0;
  if(delta){if(v.phase==2)v.form_cursor=MS_Move(v.form_cursor,delta,v.form_count);else v.cursor=MS_Move(v.cursor,delta,v.roster_count);v.costume=0;Snd_PlaySe(1,0);}
  if(v.phase==1&&(pressed&(PAD_L1|PAD_R1))){v.cursor=MS_Move(v.cursor,pressed&PAD_R1?MS_PAGE:-MS_PAGE,v.roster_count);Snd_PlaySe(1,0);}
  if(v.phase==1&&cmd>=2000&&cmd<2000+v.roster_count){v.cursor=cmd-2000;v.costume=0;}
  if(v.phase==2&&cmd>=3000&&cmd<3000+v.form_count)v.form_cursor=cmd-3000;
  cid=ms_view_chara(&v);
  if(pressed&(PAD_L2|PAD_R2)){v.costume=ms_next_costume(cid,v.costume,pressed&PAD_R2?1:-1);Snd_PlaySe(1,0);if(v.phase==2)ms_refresh_forms(&v,cid,v.costume,cid);}
  if(pressed&PAD_TRIANGLE){Snd_PlaySe(1,2);if(v.phase==2){v.phase=1;v.form_count=0;}else if(v.slot>0){v.slot--;v.total=count;}else done=1;}
  if((pressed&PAD_CROSS)||cmd==3){
   if(v.phase==1){ms_refresh_forms(&v,cid,v.costume,cid);if(v.form_count>1)v.phase=2;else if(kind==5){v.form_count=1;v.forms[0]=cid;v.form_cursor=0;v.phase=2;}else goto rb_accept;}
   else{
 rb_accept:
    cid=ms_view_chara(&v);if(kind==5){v.pick[0].chara=cid;v.pick[0].costume=v.costume;v.phase=3;focus=0;Port_ReferenceSelect(cid);Port_ReferenceAction(0,cid);}else if(kind==6){memset(&v.pick[0],0,sizeof(v.pick[0]));v.pick[0].chara=cid;v.pick[0].costume=v.costume;for(i=0;i<3;i++)rb_read(cid,i,drafts[i]);memcpy(equipped,drafts[set],16);v.phase=4;}
    else if(kind==7){int used=0,j;for(j=0;j<v.slot;j++){int d=Port_Beta8StockAlias(out[j].chara);used+=ChrTbl_GetCost(d>=0&&d<=160?d:0);}int d=Port_Beta8StockAlias(cid);used+=ChrTbl_GetCost(d>=0&&d<=160?d:0);if(dp>0&&used>dp){status=2;Snd_PlaySe(1,7);}else{memset(&out[v.slot],0,sizeof(MsPick));out[v.slot].chara=cid;out[v.slot].costume=v.costume;out[v.slot].pad=0;ms_items(&v,cid,out[v.slot].items);v.pick[v.slot]=out[v.slot];if(++v.slot>=count){result=v.slot;done=1;}else{v.phase=1;v.form_count=0;v.costume=0;}}}
   }Snd_PlaySe(1,1);
  }
 }
 rb_draw:
 cid=(v.phase==4||v.phase==3||v.phase==5||v.phase==6)?v.pick[0].chara:ms_view_chara(&v);if(cid!=old){old=cid;status=0;if(kind==5)Port_ReferenceSelect(cid);}
 if(v.phase==4||v.phase==6){int values[8],cell=rb_cell(cid),d=Port_Beta8StockAlias(cid);if(d<0||d>160)d=0;for(i=0;i<8;i++)values[i]=equipped[i];Port_RosterInfoPublish(kind,cid,set,slot,values,rb_capacity(cid),status);}
 else Port_RosterInfoPublish(kind,cid,set,v.slot,NULL,0,status);
 {int e,n,l;rb_progress(cid,&e,&n,&l);Port_RosterFocusPublish(kind==5&&(v.phase==3||v.phase==5)?focus:inventoryCursor,gSaveData->money,n>e?n-e:0);}
 if(kind==5){Job_Run();Port_ReferenceTick();if(v.phase==5){Job_Run();v.loading=ChrView_IsLoading();if(!v.loading&&(lastModel!=cid||lastModelCost!=v.costume)){ChrView_Show(cid,v.costume,0);lastModel=cid;lastModelCost=v.costume;v.loading=1;}OrbitCam_SetPadLocked(0);ChrView_Update();}}if(kind==6||kind==7){Job_Run();v.loading=ChrView_IsLoading();if(!v.loading&&(lastModel!=cid||lastModelCost!=v.costume)){ChrView_Show(cid,v.costume,0);lastModel=cid;lastModelCost=v.costume;v.loading=1;}OrbitCam_SetPadLocked(1);ChrView_Update();}Port_ModSelectPublish(&v);ColorFade_Draw();Gfx_EndFrame(1);Dma_Flush();File_Stub264D90();
 }
 sRbView=NULL;if(kind==5)Port_ReferenceEnd();if(kind==6||kind==7)Port_StagePreviewEnd();if(kind==5||kind==6)Adx_StopAll();Port_MenuScene3D(0);PortInput_KeyboardContext(0);v.active=0;Port_ModSelectPublish(&v);Dma_ResetBuffers();return result;
}
