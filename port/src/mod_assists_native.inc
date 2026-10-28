/* Local team assists. Game-thread state; render thread receives snapshots only. */
extern void BtlAct_Request(void*,int),BtlChar_SetHeldFlag(void*,int);
extern int Port_ClashBusy(void);
extern int gPortClash[2];
/* The game frees gBtlSeq with the battle while Battle_GetWork can outlive it (live crash
   2026-10-26: a bobj animation outside battle reached as_local() and BtlSeq_GetState
   dereferenced NULL). Test the sequence object before asking it for its state. */
#ifndef GAME_PTR
#define GAME_PTR *
#endif
extern void GAME_PTR gBtlSeq;
typedef struct NativeAssist {int target,kind,state,age,hp,action,paid,blocked,forming,slot,placeTicks,poseTicks;float start[3];} NativeAssist;
static NativeAssist sAssist[12];
static unsigned sAssistHeld[12];
/* Energy is paid into this attack. Losing the helper's pose stops further push,
   but does not withdraw already-confirmed energy from its scoring/damage. */
static int sAssistPaidCount[12][4];
static unsigned sAssistPaidMask[12][4];
typedef struct AssistRushOutcome{int state,winner,participants,count,damage;unsigned victims;}AssistRushOutcome;
static AssistRushOutcome sRushOutcome[12];
extern void Port_AssistRushVictim(int,int,int);
static void as_credit(int target,int kind){if(target>=0&&target<12&&kind>=1&&kind<=3&&sAssistPaidCount[target][kind]<100)sAssistPaidCount[target][kind]++;}
static void as_credit_for(int target,int kind,int helper){if(target>=0&&target<12&&kind>=1&&kind<=3&&helper>=0&&helper<12&&!(sAssistPaidMask[target][kind]&(1u<<helper))){sAssistPaidMask[target][kind]|=1u<<helper;as_credit(target,kind);}}
static int as_contributions(int target,int kind){return target>=0&&target<12&&kind>=1&&kind<=3?sAssistPaidCount[target][kind]:0;}
/* A clash throw outlives the helpers' struggle stance. Snapshot its beam multiplier for the later cinematic queue. */
static int sBeamPendingWinner, sBeamPendingLoser, sBeamPendingMult;
static int sBeamQueueTarget[12], sBeamQueueMult[12];
static int as_read(const void*p,int o){return *(const int*)((const unsigned char*)p+o);}
static int as_bound(const char*k,int d,int lo,int hi){int v=Port_Setting(k,d);return v<lo?lo:v>hi?hi:v;}
static int as_local(void){void*w=Battle_GetWork();return w&&gBtlSeq!=NULL&&!Port_NetActive()&&!BattleReplay_IsActive()&&BtlSeq_GetState()==3&&BtlChar_GetCount()>2;}
static int as_paused(void){void*w=Battle_GetWork();return !w||(as_read(w,0x19f0)&0x100);}
/* Rush-sequence actions 0x139..0x13B belong to the victim, not an ultimate caster. */
static int as_ultimate(int a){return a==261||(a>=264&&a<=312&&(a-264)%3==0);}
static int as_kind(int who){void*c=BtlChar_Get(who);int a;if(!c)return 0;a=BtlAct_GetCurrent(c);
 if(Port_ClashBusy()&&(who==gPortClash[0]||who==gPortClash[1])){if(a>=304&&a<=306)return 1;if(a>=250&&a<=252)return 2;}
 if(a>=304&&a<=306)return 0;return as_ultimate(a)?3:0;
}
static const char*as_enabled(int k){return k==1?"beam_assist_enabled":k==2?"rush_assist_enabled":"ultimate_boost_enabled";}
static const char*as_cpu(int k){return k==1?"beam_assist_cpu":k==2?"rush_assist_cpu":"ultimate_boost_cpu";}
static int as_range(int k){return as_bound(k==1?"beam_assist_range":k==2?"rush_assist_range":"ultimate_boost_range",1500,10,10000);}
static int as_max(int k){return as_bound(k==1?"beam_assist_max_helpers":k==2?"rush_assist_max_helpers":"ultimate_boost_max_helpers",4,1,10);}
static int as_consumed(int i){return i>=2&&Port_FightConsumed(i-2);}
static int as_alive(int i){void*c=BtlChar_Get(i),*m=c?BtlMember_GetActive(c):0;return m&&as_read(m,8)&&as_read(m,0x40)>0&&!as_consumed(i);}
static int as_near(int a,int b,int range){float*x=(float*)BtlChar_GetPos(BtlChar_Get(a)),*y=(float*)BtlChar_GetPos(BtlChar_Get(b)),d=0;int k;if(!x||!y)return 0;for(k=0;k<3;k++){float v=x[k]-y[k];d+=v*v;}return d>=0&&d<=(float)range*range;}
static int as_free(int i){static const int q[]={0x94c,0x954,0x958,0x95c,0x960};void*c=BtlChar_Get(i);int a,k;
 if(!as_alive(i)||BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c)||Port_ReviveProtected(i))return 0;a=BtlAct_GetCurrent(c);if(!((a>=11&&a<=26)||a==67))return 0;
 for(k=0;k<5;k++)if(as_read(c,q[k])!=-1)return 0;
 return !as_read(c,0xd98)&&!as_read(c,0xdac)&&!as_read(c,0xdb8)&&!as_read(c,0xfe0);
}
static int as_cost(int i,int k){void*m=BtlMember_GetActive(BtlChar_Get(i));int max,p;
 if(k==3)return 100000;max=as_read(m,0x50);if(max<0)max=0;p=as_bound(k==1?"beam_assist_ki_cost_percent":"rush_assist_ki_cost_percent",10,0,100);
 return (int)(((long long)max*p+99)/100);
}
static int as_canpay(int i,int k){void*m=BtlMember_GetActive(BtlChar_Get(i));return as_read(m,k==3?0x54:0x4c)>=as_cost(i,k);}
static int as_count(int target,int kind,int pending){int j,n=0;for(j=0;j<12;j++)if(sAssist[j].target==target+1&&sAssist[j].kind==kind&&sAssist[j].state>=(pending?1:2))n++;return n;}
static int as_find(int who){int j,n=BtlChar_GetCount();if(!as_local()||as_paused()||who<0||who>=n||who>=12||!as_free(who))return -1;
 for(j=0;j<n&&j<12;j++)if(j!=who&&Port_Team(j)==Port_Team(who)&&as_alive(j)){
  int k=as_kind(j);if(k&&Port_Setting(as_enabled(k),1)&&as_count(j,k,1)<as_max(k)&&!(sAssistPaidMask[j][k]&(1u<<who))&&as_near(who,j,as_range(k))&&as_canpay(who,k))return j;
 }return -1;
}
static void as_release(int i){NativeAssist*s=&sAssist[i];void*c=BtlChar_Get(i);
 /* Do not overwrite a hit, death, transformation or another owner's action. */
 if(c&&s->state&&(BtlAct_GetCurrent(c)==7||BtlAct_GetCurrent(c)==15))BtlAct_Request(c,11);{int blocked=s->blocked||sAssistHeld[i];memset(s,0,sizeof(*s));s->blocked=blocked;}
}
void Port_AssistReset(void){memset(sRushOutcome,0,sizeof(sRushOutcome));memset(sAssistPaidCount,0,sizeof(sAssistPaidCount));memset(sAssistPaidMask,0,sizeof(sAssistPaidMask));memset(sAssist,0,sizeof(sAssist));memset(sAssistHeld,0,sizeof(sAssistHeld));memset(sBeamQueueTarget,0,sizeof(sBeamQueueTarget));memset(sBeamQueueMult,0,sizeof(sBeamQueueMult));sBeamPendingWinner=sBeamPendingLoser=sBeamPendingMult=0;}
extern int Port_AssistSafePoint(int,float*),Port_AssistCommitPosition(int,const float*,float),Port_CamTarget(int);
extern float BtlCharApi_GetRadius(int);
static int as_place(int who){
 NativeAssist*s=&sAssist[who];int target=s->target-1,enemy,j,row=s->slot/2;float*leader,*opp,point[4],dx,dz,len,ux,uz,behind,side,progress,ease,ownRadius,casterRadius;
 if(!s->forming&&!(s->kind==2&&s->state))return 1;if(target<0||target>=BtlChar_GetCount())return 0;
 leader=(float*)BtlChar_GetPos(BtlChar_Get(target));if(!leader)return 0;
 enemy=(s->kind==1||s->kind==2)?(target==gPortClash[0]?gPortClash[1]:gPortClash[0]):Port_CamTarget(target);
 opp=enemy>=0&&enemy<BtlChar_GetCount()?(float*)BtlChar_GetPos(BtlChar_Get(enemy)):0;
 dx=opp?opp[0]-leader[0]:0;dz=opp?opp[2]-leader[2]:1;len=sqrtf(dx*dx+dz*dz);
 if(!(len>=0&&len<1000000))return 0;if(len<.01f){ux=0;uz=1;}else{ux=dx/len;uz=dz/len;}
 ownRadius=BtlCharApi_GetRadius(as_read(BtlChar_Get(who),12));casterRadius=BtlCharApi_GetRadius(as_read(BtlChar_Get(target),12));
 if(!(ownRadius>0&&ownRadius<1000&&casterRadius>0&&casterRadius<1000))return 0;
 /* Radius-aware compact rows: close to the caster without intersecting either
    the caster or the helper on the opposite side. Later rows remain separated. */
 if(s->kind==2){
  /* Stand on the same flank on both teams, toward the exchange rather than
     behind it. Quarter-gap advance leaves the central fighters clear; side
     clearance uses both radii and separate rows keep helpers apart. */
  behind=-fminf(len*.25f,(ownRadius+casterRadius)*.5f);
  side=ownRadius+casterRadius+1+row*(2*ownRadius+1);
  if(target==gPortClash[1])side=-side;
 }
 else {behind=4+row*6;side=7+row*7;if(behind<ownRadius+casterRadius+2)behind=ownRadius+casterRadius+2;if(side<ownRadius+2)side=ownRadius+2;}
 if(s->slot&1)side=-side;
 point[0]=leader[0]-ux*behind+uz*side;point[1]=leader[1];point[2]=leader[2]-uz*behind-ux*side;point[3]=1;
 if(!Port_AssistSafePoint(who,point))return 0;
 progress=s->placeTicks>=15?1:(s->placeTicks+1)/15.f;ease=progress*progress*(3-2*progress);
 if(s->placeTicks<15)for(j=0;j<3;j++)point[j]=s->start[j]+(point[j]-s->start[j])*ease;
 /* The interpolated path is checked too: never glide under terrain or beyond the arena. */
 if(!Port_AssistSafePoint(who,point)||!Port_AssistCommitPosition(who,point,leader[5]))return 0;
 if(s->placeTicks<15)s->placeTicks++;return 1;
}
static void as_begin(int i,int target){NativeAssist*s=&sAssist[i];void*c=BtlChar_Get(i);float*p=(float*)BtlChar_GetPos(c);unsigned used=0;int j;
 memset(s,0,sizeof(*s));s->target=target+1;s->kind=as_kind(target);s->state=1;s->hp=as_read(BtlMember_GetActive(c),0x40);s->action=BtlAct_GetCurrent(BtlChar_Get(target));
 s->forming=Port_Setting("assist_formation_enabled",1)!=0;
 for(j=0;j<12;j++)if(j!=i&&sAssist[j].state&&sAssist[j].target==target+1)used|=1u<<sAssist[j].slot;
 while(s->slot<11&&(used&(1u<<s->slot)))s->slot++;
 if(p)for(j=0;j<3;j++)s->start[j]=p[j];
 BtlChar_SetHeldFlag(c,0xAF);BtlChar_SetHeldFlag(c,0xB0);BtlAct_Request(c,(s->kind==2||s->forming)?15:7);
}
/* Called before BuildRecord: consume R3 on an available invitation, including release,
   so delayed release-based transformation/fusion queries never see the assist press. */
unsigned Port_AssistInput(int who,unsigned raw,unsigned r3){int press;if(who<0||who>=12)return raw;
 if(!as_local()||as_paused()){sAssistHeld[who]=raw&r3;return raw;}
 press=(raw&r3)&&!sAssistHeld[who];sAssistHeld[who]=raw&r3;
 if(sAssist[who].blocked){if(!(raw&r3))sAssist[who].blocked=0;return raw&~r3;}
 if(sAssist[who].state){sAssist[who].blocked=1;return 0;}
 if(press&&!(raw&0xFF00u)&&Port_TargetHuman(who)){int t=as_find(who);if(t>=0){as_begin(who,t);sAssist[who].blocked=1;return raw&~r3;}}
 return raw;
}
void Port_AssistTick(void){int i,n=BtlChar_GetCount();if(!as_local()){for(i=0;i<n&&i<12;i++)as_release(i);Port_AssistReset();return;}if(as_paused())return;if(n>12)n=12;
 for(i=0;i<n;i++)if(!as_kind(i)){memset(sAssistPaidCount[i],0,sizeof(sAssistPaidCount[i]));memset(sAssistPaidMask[i],0,sizeof(sAssistPaidMask[i]));}
 for(i=0;i<n;i++){
  NativeAssist*s=&sAssist[i];void*c=BtlChar_Get(i),*m;int t,k,a,want;
  if(!s->state){if(!Port_TargetHuman(i)){t=as_find(i);if(t>=0&&Port_Setting(as_cpu(as_kind(t)),1))as_begin(i,t);}continue;}
  t=s->target-1;k=s->kind;a=BtlAct_GetCurrent(c);m=BtlMember_GetActive(c);
  /* All joined helpers hold action 7; visual-only clash loops supply the
     punches/kicks without travel, offensive events or another clash actor. */
  want=7;
  if(t<0||t>=n||!as_alive(i)||!as_alive(t)||as_kind(t)!=k||!Port_Setting(as_enabled(k),1)||Port_Team(i)!=Port_Team(t)||!as_near(i,t,as_range(k))||as_read(m,0x40)<s->hp||as_read(c,0xd98)||as_read(c,0xdac)||as_read(c,0xdb8)||as_read(c,0xfe0)){as_release(i);continue;}
  if(s->forming&&(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))){as_release(i);continue;}
  if(s->state==1){
   if(s->forming){
    if(!((a>=11&&a<=26)||a==67||a==7)||!as_place(i)){as_release(i);continue;}
    BtlChar_SetHeldFlag(c,0xAF);BtlChar_SetHeldFlag(c,0xB0);
    if(s->placeTicks<15)continue;
    if(a!=want){BtlAct_Request(c,want);if(++s->age>6)as_release(i);continue;}
   }
   if(a!=want){BtlAct_Request(c,want);if(++s->age>6||!((a>=11&&a<=26)||a==67))as_release(i);continue;}
   if(!as_canpay(i,k)){as_release(i);continue;}
   *(int*)((unsigned char*)m+(k==3?0x54:0x4c))-=as_cost(i,k);s->paid=1;s->state=2;as_credit_for(t,k,i);
   fprintf(stderr,"bt3: assist confirmed: helper%d caster%d kind%d cost%d %s\n",i,t,k,as_cost(i,k),k==3?"stocks":"Ki");
  }else if(a!=want||(s->forming&&!as_place(i))){as_release(i);continue;}
  /* Wait stance has no Ki regeneration and cannot fire a technique. Hold only
     this action's lifetime flags; interrupted helpers are released above. */
  if(s->poseTicks<1000000)s->poseTicks++;
  BtlChar_SetHeldFlag(c,0xAF);BtlChar_SetHeldFlag(c,0xB0);
  if(k==1||k==2){int*counter=(int*)((unsigned char*)BtlChar_Get(t)+(k==1?0xE4C:0xE50));if(*counter>=0&&*counter<=2147483643)*counter+=4;}
 }
}
int Port_AssistScore(int who,int count,int beam){int k=beam?1:2,n,percent;long long v;
 if(!as_local()||who<0||who>=12||as_kind(who)!=k||count<=0)return count;n=as_contributions(who,k);if(!n)return count;
 percent=as_bound(beam?"beam_assist_multiplier_percent":"rush_assist_multiplier_percent",300,100,1000);
 {long long mult=100+(long long)n*(percent-100);if(beam&&mult>300)mult=300;v=(long long)count*mult/100;}return v>2147483647?2147483647:(int)v;
}
/* Count mode includes the caster. Fixed mode applies once only when energy was
   confirmed; pending invitations and repeat presses never multiply damage. */
static int as_damage_multiplier(int who,int kind){
 int n=as_contributions(who,kind),mode=as_bound(kind==1?"beam_assist_damage_mode":"ultimate_boost_damage_mode",0,0,2),bonus,cap;long long p;
 if(!n)return 100;
 if(mode==1){p=100LL*(1+n);return p>1000?1000:(int)p;}
 if(mode==2)return as_bound(kind==1?"beam_assist_damage_fixed_percent":"ultimate_boost_fixed_percent",200,100,1000);
 bonus=kind==1?as_bound("beam_assist_damage_bonus_percent",as_bound("beam_assist_multiplier_percent",300,100,1000)-100,0,900):as_bound("ultimate_boost_percent",50,0,500);
 cap=as_bound(kind==1?"beam_assist_damage_cap_percent":"ultimate_boost_total_cap",300,100,1000);
 p=100+(long long)n*bonus;return p>cap?cap:(int)p;
}
int Port_AssistDamage(int who,int slot,int damage){long long v;
 if(slot!=4||damage<=0||who<0||who>=12||!as_local()||as_kind(who)!=3)return damage;
 v=(long long)damage*as_damage_multiplier(who,3)/100;return v>2147483647?2147483647:(int)v;
}
void Port_AssistHud(int who,int*kind,int*state,int*helpers){int t;*kind=*state=*helpers=0;if(who<0||who>=12||!as_local())return;
 if(sAssist[who].state){*kind=sAssist[who].kind;*state=sAssist[who].state+1;return;}
 *kind=as_kind(who);if(*kind){*helpers=as_contributions(who,*kind);return;}
 if(Port_TargetHuman(who)&&(t=as_find(who))>=0){*kind=as_kind(t);*state=1;}
}

int Port_AssistHolding(int who){return who>=0&&who<12&&as_local()&&sAssist[who].state;}

static int as_beam_multiplier(int winner){return as_damage_multiplier(winner,1);}
static int as_beam_scale(int damage,int mult){if(mult==100)return damage;long long v=(long long)damage*mult/100;return v>2000000?2000000:(int)v;}
void Port_AssistClashThrow(int winner,int loser){sBeamPendingWinner=winner;sBeamPendingLoser=loser;sBeamPendingMult=as_beam_multiplier(winner);}
void Port_AssistRushSetup(int who,int cls,int fromAnim){
 if(who<0||who>=12)return;
 /* The closing animation refills the queue for the same cinematic; keep its snapshot. */
 if(fromAnim){if(BtlAct_GetCurrent(BtlChar_Get(who))==cls+0x128&&sBeamQueueMult[who])return;sBeamQueueTarget[who]=0;sBeamQueueMult[who]=0;return;}
 sBeamQueueTarget[who]=0;sBeamQueueMult[who]=0;
 /* Only the clash-winning rush sequence inherits the snapshot, never an ordinary rush. */
 if(sBeamPendingMult&&who==sBeamPendingWinner&&BtlAct_GetCurrent(BtlChar_Get(who))==cls+0x12B){
  sBeamQueueTarget[who]=sBeamPendingLoser;sBeamQueueMult[who]=sBeamPendingMult;sBeamPendingMult=0;
 }
}
int Port_AssistBeamQueuedDamage(int who,int target,int damage){int mult,a;
 if(damage<=0||who<0||who>=12||target<0||target>=BtlChar_GetCount()||!as_local())return damage;mult=sBeamQueueMult[who];
 a=BtlAct_GetCurrent(BtlChar_Get(target));if(!mult||target!=sBeamQueueTarget[who]||a<313||a>315)return damage;
 return as_beam_scale(damage,mult);
}
void Port_AssistQueueDone(int who){if(who>=0&&who<12){sBeamQueueTarget[who]=0;sBeamQueueMult[who]=0;}}
int Port_AssistBeamClashDamage(int winner,int damage){
 if(!as_local()||damage<=0||winner<0||winner>=12)return damage;
 return as_beam_scale(damage,as_beam_multiplier(winner));
}
/* Compatibility entry; the native outcome passes its actual winner explicitly. */
int Port_AssistClashDamage(int damage){return Port_AssistBeamClashDamage(gPortClash[0],damage);}

int Port_AssistRushLocal(void){return Battle_GetWork()&&gBtlSeq!=NULL&&!Port_NetActive()&&!BattleReplay_IsActive()&&BtlSeq_GetState()==3&&BtlChar_GetCount()>=2;}
void Port_AssistRushOutcome(int winner,int loser){int i;AssistRushOutcome*r;
 if(!Port_AssistRushLocal()||winner<0||winner>=12||loser<0||loser>=12||winner==loser)return;
 r=&sRushOutcome[loser];r->state=1;r->winner=winner;r->participants=1+as_contributions(winner,2);r->count=as_read(BtlChar_Get(winner),0xE50);r->victims=0;
 for(i=0;i<BtlChar_GetCount()&&i<12;i++)if(i!=loser&&i!=winner&&(sAssistPaidMask[loser][2]&(1u<<i))&&Port_Team(i)==Port_Team(loser))r->victims|=1u<<i;
}
/* Snapshot precedes pose release. One finisher per losing fighter; no native
   defense/rounding stage may turn the requested10,000 ceiling into a larger hit. */
int Port_AssistRushFinish(int loser,int count,int unit){AssistRushOutcome*r;long long value;int damage,i;
 if(!Port_AssistRushLocal()||loser<0||loser>=12)return 0;r=&sRushOutcome[loser];
 if(r->state>=2)return 0;
 if(r->state==1)count=r->count;
 value=(long long)(count>0?count:0)*(unit==600?600:60)*(r->state==1?r->participants:1);damage=value>10000?10000:(int)value;
 if(r->state!=1)return damage;r->state=2;
 r->damage=damage;
 fprintf(stderr,"bt3: rush finisher: winner%d loser%d participants%d damage%d maximum10000 per fighter\n",r->winner,loser,r->participants,damage);
 return damage;
}

/* The primary loser's authored hit-reaction ends here. Release every confirmed
   losing participant on this same game update, rather than at the earlier hit. */
void Port_AssistRushLaunch(int loser){AssistRushOutcome*r;int i;
 if(!Port_AssistRushLocal()||loser<0||loser>=12)return;r=&sRushOutcome[loser];if(r->state!=2)return;r->state=3;
 for(i=0;i<BtlChar_GetCount()&&i<12;i++)if(r->victims&(1u<<i)){
  int a=BtlAct_GetCurrent(BtlChar_Get(i));
  if(!as_alive(i)||Port_ReviveProtected(i)||(a>=0xEC&&a<=0xF8)||(a>=0xFD&&a<=0x13B))continue;
  if(sAssist[i].state&&sAssist[i].target==loser+1)as_release(i);
  Port_AssistRushVictim(i,r->winner,r->damage);
 }
}

/* Camera lookup only. Does not change pad ownership, targets or the fighter's camera state. */
int Port_AssistView(int who){NativeAssist*s;if(who<0||who>=12||who>=BtlChar_GetCount()||!as_local()||!Port_Setting("assist_follow_camera",1)||!Port_TargetHuman(who))return -1;
 s=&sAssist[who];if(s->state!=2||s->target<=0||s->target>BtlChar_GetCount()||!as_alive(who)||!as_alive(s->target-1)||as_kind(s->target-1)!=s->kind||BtlAct_GetCurrent(BtlChar_Get(who))!=7)return -1;
 return s->target-1;
}

/* Visual ownership only: no technique action, attack events or extra Ki gain. */
int Port_AssistVisual(int objectId,int*ticks){int i,n=BtlChar_GetCount();
 if(!as_local())return 0;
 for(i=0;i<n&&i<12;i++){NativeAssist*s=&sAssist[i];void*c;
  if(s->state!=2||!s->paid)continue;c=BtlChar_Get(i);
  if(c&&as_read(c,12)==objectId&&BtlAct_GetCurrent(c)==7&&as_alive(i)){
   *ticks=s->poseTicks;return s->kind;
  }
 }return 0;
}

/* Match the assisted caster's native first/second blow-clash animation. */
int Port_AssistRushMotion(int objectId){int i,n=BtlChar_GetCount();
 if(!as_local())return 371;
 for(i=0;i<n&&i<12;i++)if(sAssist[i].state==2&&sAssist[i].kind==2&&sAssist[i].paid){
  void*c=BtlChar_Get(i);if(c&&as_read(c,12)==objectId)return sAssist[i].target-1==gPortClash[0]?371:372;
 }return 371;
}

/* Presentation lookup only: QTE helpers never become extra clash actors. */
int Port_AssistQteLeader(int objectId){int i,n=BtlChar_GetCount();
 if(!as_local()||!Port_ClashBusy())return -1;
 for(i=0;i<n&&i<12;i++){NativeAssist*s=&sAssist[i];void*c;int t,a;
  if(s->state!=2||s->kind!=2||!s->paid)continue;
  t=s->target-1;if(t<0||t>=n||!as_alive(i)||!as_alive(t))continue;
  c=BtlChar_Get(i);if(!c||as_read(c,12)!=objectId||BtlAct_GetCurrent(c)!=7)continue;
  if(t!=gPortClash[0]&&t!=gPortClash[1])continue;
  a=BtlAct_GetCurrent(BtlChar_Get(t));if(a==0xFB||a==0xFC)return t;
 }return -1;
}
