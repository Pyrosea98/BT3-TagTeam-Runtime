/* Native dance-fusion lifecycle. Captured only at a committed fusion, never for a preselected fused fighter. */
#ifdef PORT
extern int Port_Setting(const char *,int),Port_NetActive(void),BattleReplay_IsActive(void);
extern s32 BtlChar_GetCount(void),BtlChar_IsFrozen(BtlActJChr *);
extern BtlActJChr *BtlChar_Get(s32);
extern void BtlAct_Request(BtlActJChr *,s32);
extern int Port_ExtraFusionRelease(int,int,const void *,const void *);
extern int BtlSeq_GetState(void);
extern s32 ChrCam_IsCutActive(BtlActJChr *);
typedef struct PortFusionReceipt {
 int state,member,slot,remaining,total,kind,hp,hpMax,ki,blast;
 unsigned char leader[0xA4],partner[0xA4];
} PortFusionReceipt;
static PortFusionReceipt sFusionReceipts[12];
extern int Port_FusionPartnerActor(int,int),Port_DefusionPresenting(int);
static int sDanceCooldown[12];
int Port_DanceCooldown(int who){return !Port_NetActive()&&!BattleReplay_IsActive()&&who>=0&&who<12?sDanceCooldown[who]:0;}
static void pf_cooldown_start(int who,int member){
 int sec=Port_Setting("fusion_dance_cooldown_seconds",10),partner=Port_FusionPartnerActor(who,member);
 if(sec<0)sec=0;if(sec>300)sec=300;
 if(who>=0&&who<12)sDanceCooldown[who]=sec*30;
 if(partner>=0&&partner<12)sDanceCooldown[partner]=sec*30;
}

static int pf_rd(const void *p,int o){return *(const int*)((const unsigned char*)p+o);}
static void pf_wr(void *p,int o,int v){*(int*)((unsigned char*)p+o)=v;}
static void pf_copy(void *d,const void *s,int n){int i;for(i=0;i<n;i++)((unsigned char*)d)[i]=((const unsigned char*)s)[i];}
#include "mod_defuse_cinematic.inc"
typedef struct PortFusionControls {int active,slot,pad[2],ticks;} PortFusionControls;
static PortFusionControls sFusionControls[12];
extern int Port_FusionPartnerPad(int,int);
void Port_FusionControlsCapture(void *actor,int member){BtlActJChr *c=(BtlActJChr*)actor;int who=c->player,pad=Port_FusionPartnerPad(who,member);PortFusionControls *f;
 if(who<0||who>=12||Port_NetActive()||BattleReplay_IsActive())return;f=&sFusionControls[who];f->active=0;
 if(pad<0||pad>3||c->pad<0||c->pad>3||pad==c->pad)return;
 f->active=1;f->slot=pf_rd(c,0x994);f->pad[0]=c->pad;f->pad[1]=pad;f->ticks=0;
}
int Port_FusionControlPads(int who,int *move,int *attack){PortFusionControls *f;int mode,sec,turn;
 if(who<0||who>=12||Port_NetActive()||BattleReplay_IsActive())return 0;f=&sFusionControls[who];if(!f->active)return 0;
 mode=Port_Setting("coop_fusion_controls",0);sec=Port_Setting("coop_fusion_swap_seconds",10);if(sec<5)sec=5;if(sec>60)sec=60;turn=(f->ticks/(sec*30))&1;
 if(mode==4)turn=0;else if(mode==5)turn=1;
 if(mode==1||mode==2||mode==3){int role=mode==1?0:mode==2?1:turn;*move=f->pad[role];*attack=f->pad[role^1];}
 else *move=*attack=f->pad[turn];return 1;
}
int Port_FusionControlSeconds(int who){int sec;if(who<0||who>=12||!sFusionControls[who].active)return 0;sec=Port_Setting("coop_fusion_swap_seconds",10);if(sec<5)sec=5;if(sec>60)sec=60;return sec-(sFusionControls[who].ticks/30)%sec;}
void Port_FusionControlsTick(void){int i;for(i=0;i<12;i++)if(sFusionControls[i].active){BtlActJChr *c=i<BtlChar_GetCount()?BtlChar_Get(i):0;PortFusionControls *f=&sFusionControls[i];
 if(!c||pf_rd(c,0x994)!=f->slot){f->active=0;continue;}
 if(BtlMember_GetActiveGauge(c)->health<=0)continue;
 if(BtlSeq_GetState()==3&&!BtlChar_IsFrozen(c)&&!ChrCam_IsCutActive(c))f->ticks=(f->ticks+1)%108000;
}}
void Port_FusionClockReset(void){extern void Port_FusionConsentReset(void);int i;Port_FusionConsentReset();for(i=0;i<12;i++){sFusionReceipts[i].state=0;sFusionControls[i].active=0;sDanceCooldown[i]=0;}pd_reset();}
int Port_FusionClockSeconds(int who){return who>=0&&who<12&&sFusionReceipts[who].state?((sFusionReceipts[who].remaining+65535)/65536+29)/30:0;}
int Port_FusionClockActive(int who){return who>=0&&who<12&&sFusionReceipts[who].state!=0;}
int Port_FusionClockProgress(int who){PortFusionReceipt*r;if(who<0||who>=12)return 0;r=&sFusionReceipts[who];return r->state&&r->total>0?(int)((long long)r->remaining*1000/r->total):0;}
void Port_FusionClockCapture(void *actor,int partner,int kind){
 BtlActJChr *c=(BtlActJChr*)actor;int who=c->player,sec;PortFusionReceipt *r;
 if(who<0||who>=12||kind!=0xF1||Port_NetActive()||BattleReplay_IsActive()||!Port_Setting("fusion_duration_enabled",1))return;
 r=&sFusionReceipts[who];if(r->state||partner<0||partner>=5)return;
 r->slot=pf_rd(c,0x994);if(r->slot<0||r->slot>=5||r->slot==partner)return;
 sec=Port_Setting("fusion_duration_seconds",100);if(sec<1)sec=1;if(sec>300)sec=300;
 pf_copy(r->leader,BtlMember_GetActive(c),0xA4);pf_copy(r->partner,BtlMember_Get(c,partner),0xA4);
 r->member=partner;r->kind=kind;r->remaining=r->total=sec*30*65536;r->state=1;
 fprintf(stderr,"bt3: fusion timer: actor%d dance committed, %d combat seconds\n",who,sec);
}
static int pf_rate(int chara){
 int tier=0,preset=Port_Setting("fusion_form_drain_preset",1);
 static const int penalties[3][6]={{0,5,10,15,20,25},{0,10,20,30,40,50},{0,20,35,50,60,70}};
 if(!Port_Setting("fusion_time_by_form",1))return 65536;
 switch(chara){
 case 49:case 52:case 53:case 216:case 220:case 225:case 235:tier=1;break;
 case 166:case 249:case 218:case 221:case 226:case 238:tier=2;break;
 case 50:case 149:case 172:case 219:case 222:case 229:case 239:tier=3;break;
 case 54:case 59:case 223:case 230:case 241:tier=4;break;
 default:break;}

 if(preset<0||preset>2)preset=1;
 return (6553600+(100-penalties[preset][tier])/2)/(100-penalties[preset][tier]);
}
void Port_FusionClockTick(void){
 int i,a;BtlActJChr *c;PortFusionReceipt *r;BtlActJGauge *g;
 pd_preview_tick();
 if(Port_NetActive()||BattleReplay_IsActive()||BtlSeq_GetState()!=3)return;
 for(i=0;i<BtlChar_GetCount()&&i<12;i++){
  c=BtlChar_Get(i);
  if(sDanceCooldown[i]>0&&c&&!BtlChar_IsFrozen(c)&&!ChrCam_IsCutActive(c)&&!Port_DefusionPresenting(i))sDanceCooldown[i]--;
 }
 for(i=0;i<BtlChar_GetCount()&&i<12;i++){
  r=&sFusionReceipts[i];if(r->state!=1&&r->state!=2&&r->state!=3)continue;c=BtlChar_Get(i);if(!c||pf_rd(c,0x994)!=r->slot)continue;g=BtlMember_GetActiveGauge(c);
  if(r->state==1&&!Port_Setting("fusion_duration_enabled",1))continue;
  if(g->health<=0){
   /* KO is revivable, not the end of a committed dance fusion. Suspend the
      remaining clock and partner receipt; an interrupted expiry retries safely
      after get-up. Once originals loaded, retain the existing reveal cleanup. */
   if(r->state==3){pf_cooldown_start(c->player,r->member);Port_ExtraFusionRelease(c->player,r->member,r->partner,c);sFusionControls[c->player].active=0;r->state=0;}
   else if(r->state==2){pd_end(&sDefuse[c->player]);r->state=1;}
   continue;
  }
  a=BtlAct_GetCurrent(c);
  /* Reload callback occurs at the cinematic's temporary centre. Wait until
     native action exit has restored the saved fight placement, then reveal both. */
  if(r->state==3){
   if(!pd_release_now(c)&&(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c)||(a>=0xEC&&a<=0xF8)))continue;
   pf_cooldown_start(c->player,r->member);
   if(Port_ExtraFusionRelease(c->player,r->member,r->partner,c))pd_push(c,r->member);else pd_end(&sDefuse[c->player]);
   sFusionControls[c->player].active=0;r->state=0;
   fprintf(stderr,"bt3: defusion: actor%d paired reveal after original placement restored\n",c->player);continue;
  }
  if(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c)||(a>=0xEC&&a<=0xF8))continue;
  /* State 2 stays pending until the original-body reload callback confirms it. A native request can be superseded before admission; retry only at the same safe idle gate, with current gauges. */
  if(r->state==1&&r->remaining>0){int rate=pf_rate(BtlMember_GetActive(c)->chara);r->remaining=r->remaining>rate?r->remaining-rate:0;}
  /* Expiry is mandatory lifecycle cleanup, not a voluntary transformation:
     the saved native CPU rule may be disabled even while mod fusion is enabled. */
  if(r->remaining||a>0x37||!BtlChar_IsFree(c))continue;
  if(!Port_ModelUsable(pf_rd(r->leader,0),pf_rd(r->leader,4),pf_rd(r->leader,0x60)))continue;
  r->hp=g->health;r->hpMax=pf_rd(g,4);r->ki=pf_rd(g,12);r->blast=pf_rd(g,20);
  c->form.index=-1;c->form.chara=pf_rd(r->leader,0);c->form.costume=pf_rd(r->leader,4);c->form.cost=0;
  c->form.kind=0;c->form.partner=-1;c->form.variant=pf_rd(r->leader,0x60);c->form.unk24=0;c->form.unk28=0;
  c->form.animChara=c->form.chara;c->form.unk1C=c->form.chara;c->form.voiceChara=c->form.chara;
  r->state=2;BtlAct_Request(c,0xEC);pd_begin(c);
  fprintf(stderr,"bt3: fusion timer: actor%d expiry requesting native original-body reload\n",i);
 }
 pd_tick();
}
void Port_FusionClockOnReload(void *actor){
 BtlActJChr *c=(BtlActJChr*)actor;PortFusionReceipt *r;void *m,*p;int hp1,hp2,max1,max2,ki1,ki2,blast1,blast2;
 if(c->player<0||c->player>=12)return;r=&sFusionReceipts[c->player];
 if(r->state!=2||pf_rd(c,0x994)!=r->slot||BtlMember_GetActive(c)->chara!=pf_rd(r->leader,0))return;
 if(BtlMember_GetActiveGauge(c)->health<=0){r->state=0;return;}
 max1=pf_rd(r->leader,0x44);max2=pf_rd(r->partner,0x44);if(r->hpMax<1)r->hpMax=max1+max2;
 if(r->hpMax<1){r->state=0;return;}
 hp1=(int)((long long)r->hp*max1/r->hpMax);hp2=(int)((long long)r->hp*max2/r->hpMax);
 if(hp1<1)hp1=1;if(hp1>max1)hp1=max1;if(hp2<1)hp2=1;if(hp2>max2)hp2=max2;
 ki1=r->ki/2;ki2=r->ki-ki1;blast1=r->blast/2;blast2=r->blast-blast1;
 pf_wr(r->leader,0x40,hp1);pf_wr(r->partner,0x40,hp2);
 pf_wr(r->leader,0x4C,ki1);pf_wr(r->partner,0x4C,ki2);
 if(blast1>pf_rd(r->leader,0x58))blast1=pf_rd(r->leader,0x58);if(blast2>pf_rd(r->partner,0x58))blast2=pf_rd(r->partner,0x58);
 pf_wr(r->leader,0x54,blast1);pf_wr(r->partner,0x54,blast2);
 pf_wr(r->leader,0x5C,0);pf_wr(r->partner,0x5C,0);pf_wr(r->leader,0x74,0);pf_wr(r->partner,0x74,0);
 m=BtlMember_GetActive(c);p=BtlMember_Get(c,r->member);pf_copy(m,r->leader,0xA4);if(p)pf_copy(p,r->partner,0xA4);
 r->state=3;fprintf(stderr,"bt3: defusion: actor%d original bodies loaded; waiting for native placement restore\n",c->player);
}
#endif

#ifdef PORT
int Port_DefusionPresenting(int who){return who>=0&&who<12&&(sFusionReceipts[who].state>=2||sDefuse[who].stage!=0);}
#endif
