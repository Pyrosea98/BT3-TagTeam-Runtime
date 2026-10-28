/* Native local-team revival. Human/CPU proximity channel; no remote memory or forced action queues. */
extern void *BtlChar_Get(int),*BtlMember_GetActive(void*),*BtlChar_GetPos(void*);
extern int BtlChar_GetCount(void),BtlAct_GetCurrent(void*),BtlChar_IsFrozen(void*),ChrCam_IsCutActive(void*);
extern int Port_TargetHuman(int),Port_Team(int),BattleReplay_IsActive(void),BtlSeq_GetState(void);
extern void *Battle_GetWork(void);
extern void Port_ReviveSpectator(int),BtlAct_Request(void*,int);
typedef struct NativeRevive {int target,ticks,hp,lock,recovering,recovery,insufficient;} NativeRevive;
static NativeRevive sRevive[12];
static int rv_read(const void *p,int off){return *(const int*)((const unsigned char*)p+off);}
static int rv_gate(void){void *w=Battle_GetWork();return !Port_NetActive()&&!BattleReplay_IsActive()&&Port_Setting("revive_enabled",1)&&BtlChar_GetCount()>2&&BtlSeq_GetState()==3&&w&&!(rv_read(w,0x19f0)&0x100);}
void Port_ReviveReset(void){memset(sRevive,0,sizeof(sRevive));}
int Port_ReviveProtected(int who){return who>=0&&who<12&&sRevive[who].recovering&&!Port_NetActive()&&!BattleReplay_IsActive();}
void Port_ReviveHud(int who,int *target,int *progress,int *recovering,int *insufficient){
 int ms=Port_Setting("revive_channel_ms",1500),ticks;if(ms<250)ms=250;if(ms>10000)ms=10000;ticks=(ms*30+999)/1000;
 *target=-1;*progress=*recovering=*insufficient=0;if(who<0||who>=12)return;
 *target=sRevive[who].target-1;*progress=sRevive[who].ticks*1000/ticks;*recovering=sRevive[who].recovering;*insufficient=sRevive[who].insufficient;
}
static int rv_eligible(void *c,int dead){
 static const int queues[]={0x94c,0x954,0x958,0x95c,0x960};int i,a=BtlAct_GetCurrent(c);
 if(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))return 0;
 if(dead){if(a!=216&&a!=0xEB)return 0;}else if(!(a==7||(a>=11&&a<=26)||a==67))return 0;
 for(i=0;i<5;i++){int q=rv_read(c,queues[i]);if(q!=-1&&!( !dead&&(q==7||(q>=11&&q<=26)||q==67)))return 0;}
 if(rv_read(c,0xd98)||rv_read(c,0xdac)||rv_read(c,0xdb8)||rv_read(c,0xfe0))return 0;
 return 1;
}
void Port_ReviveTick(void){
 int i,j,n=BtlChar_GetCount(),cost,channel,recovery,health,radius;
 if(!rv_gate()){void *w=Battle_GetWork();if(Port_NetActive()||BattleReplay_IsActive()||!w||BtlSeq_GetState()!=3||!Port_Setting("revive_enabled",1))Port_ReviveReset();return;}
 if(n>12)n=12;cost=Port_Setting("revive_stock_cost",1);if(cost<0)cost=0;if(cost>10)cost=10;cost*=100000;
 channel=Port_Setting("revive_channel_ms",1500);if(channel<250)channel=250;if(channel>10000)channel=10000;channel=(channel*30+999)/1000;
 recovery=Port_Setting("revive_recovery_ms",1000);if(recovery<250)recovery=250;if(recovery>10000)recovery=10000;recovery=(recovery*30+999)/1000;
 health=Port_Setting("revive_health_quarters",8);if(health<1)health=1;if(health>40)health=40;health*=2500;
 radius=Port_Setting("revive_radius",80);if(radius<5)radius=5;if(radius>400)radius=400;
 for(i=0;i<n;i++){
  NativeRevive *r=&sRevive[i];void *c=BtlChar_Get(i),*m=c?BtlMember_GetActive(c):0;int target=-1;float *pos;
  if(!m||rv_read(m,0x40)<=0||(i>=2&&Port_FightConsumed(i-2))){memset(r,0,sizeof(*r));continue;}
  if(r->recovering){int a=BtlAct_GetCurrent(c);if(r->recovery>0)r->recovery--;if(!r->recovery&&a!=216&&a!=225&&a!=230)r->recovering=0;continue;}
  if(!rv_eligible(c,0)){memset(r,0,sizeof(*r));continue;}
  pos=(float*)BtlChar_GetPos(c);if(!pos){memset(r,0,sizeof(*r));continue;}
  for(j=0;j<n;j++)if(j!=i&&Port_Team(i)==Port_Team(j)&&!(j>=2&&Port_FightConsumed(j-2))){
   void *t=BtlChar_Get(j),*tm=t?BtlMember_GetActive(t):0;float *p;float d=0;int k;
   if(!tm||rv_read(tm,0x40)>0||rv_read(tm,0x44)<=0||!rv_read(tm,8)||!rv_eligible(t,1))continue;
   p=(float*)BtlChar_GetPos(t);if(!p)continue;for(k=0;k<3;k++){float v=pos[k]-p[k];d+=v*v;}if(!(d>=0&&d<=(float)radius*radius))continue;
   target=j;break;
  }
  if(target<0){memset(r,0,sizeof(*r));continue;}
  if(rv_read(m,0x54)<cost){r->target=target+1;r->ticks=0;r->insufficient=1;continue;}
  r->insufficient=0;
  if(r->target!=target+1||r->hp!=rv_read(m,0x40)){r->target=target+1;r->ticks=0;r->hp=rv_read(m,0x40);}
  if(++r->ticks>=channel){
   void *t=BtlChar_Get(target),*tm=BtlMember_GetActive(t);int hp=rv_read(tm,0x44);if(hp>health)hp=health;
   /* Recheck before a one-shot commit; a prior helper may have revived this corpse. */
   if(rv_read(tm,0x40)<=0&&rv_read(m,0x54)>=cost){
    *(int*)((unsigned char*)m+0x54)-=cost;*(int*)((unsigned char*)tm+0x40)=hp;
    if(BtlAct_GetCurrent(t)==0xEB)BtlAct_Request(t,0xE1); /* Ultimate KO standby has no LieDown recovery decision. */
    else *(int*)((unsigned char*)t+0x964)=91; /* Let native LieDown choose the proper get-up. */
    memset(&sRevive[target],0,sizeof(sRevive[target]));sRevive[target].recovering=1;sRevive[target].recovery=recovery;
    Port_ReviveSpectator(target);fprintf(stderr,"bt3: revive: fighter%d restored fighter%d hp%d cost%d stocks\n",i,target,hp,cost/100000);
   }
   memset(r,0,sizeof(*r));
  }
 }
}

int Port_ReviveCpuHold(int who){
 void*c,*m;float*pos;int j,n=BtlChar_GetCount(),radius,cost;
 if(who<0||who>=n||who>=12||!rv_gate()||Port_TargetHuman(who))return 0;
 c=BtlChar_Get(who);m=c?BtlMember_GetActive(c):0;
 if(!m||rv_read(c,0x1278)!=1||rv_read(m,0x40)<=0||!rv_eligible(c,0)||(who>=2&&Port_FightConsumed(who-2)))return 0;
 cost=Port_Setting("revive_stock_cost",1);if(cost<0)cost=0;if(cost>10)cost=10;
 if(rv_read(m,0x54)<cost*100000)return 0;
 radius=Port_Setting("revive_radius",80);if(radius<5)radius=5;if(radius>400)radius=400;
 pos=(float*)BtlChar_GetPos(c);if(!pos)return 0;
 for(j=0;j<n&&j<12;j++)if(j!=who&&Port_Team(who)==Port_Team(j)&&!(j>=2&&Port_FightConsumed(j-2))){
  void*t=BtlChar_Get(j),*tm=t?BtlMember_GetActive(t):0;float*p,d=0;int k;
  if(!tm||rv_read(tm,0x40)>0||rv_read(tm,0x44)<=0||!rv_read(tm,8)||!rv_eligible(t,1))continue;
  p=(float*)BtlChar_GetPos(t);if(!p)continue;for(k=0;k<3;k++){float v=pos[k]-p[k];d+=v*v;}
  if(d>=0&&d<=(float)radius*radius)return 1;
 }return 0;
}
