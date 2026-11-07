/* 3Hz ordinary CPU transformation planner, ported from prior cpu_tactics.py. */
#ifdef PORT
#include "mod_cpu_form_tiers.inc"
extern int Port_TargetHuman(int),Port_Team(int),Port_CamTarget(int),Battle_GetMode(void),Port_ModesFight(void);
typedef struct CpuTacticsState {int age,nextEval,nextTransform,lastHp,chara;} CpuTacticsState;
static CpuTacticsState sCpuTactics[12];
static unsigned sCpuPolicyLogged;
static int sCpuFusionEval[12],sCpuFusionChoice[12];
/* Set when the planner jumps toward the top form because the opponent is far stronger: the next form is then
   re-evaluated as soon as the change settles instead of waiting out the ordinary cooldown. */
static int sCpuChain[12];
static int ct_read(const void *p,int o){return *(const int*)((const unsigned char*)p+o);}
void Port_CpuTacticsReset(void){int i;sCpuPolicyLogged=0;for(i=0;i<12;i++){sCpuTactics[i].age=sCpuTactics[i].nextEval=sCpuTactics[i].nextTransform=sCpuTactics[i].lastHp=0;sCpuTactics[i].chara=-1;sCpuFusionEval[i]=sCpuFusionChoice[i]=sCpuChain[i]=0;}}
static int ct_gate(void){return !Port_NetActive()&&!BattleReplay_IsActive()&&BtlSeq_GetState()==3&&Battle_GetMode()!=1&&Battle_GetMode()!=5&&Battle_GetMode()!=6&&Port_ModesFight()!=3;}
/* The stock TeamSel transformation rule is separate from the mod CPU scopes.
   Extras inherit side1's stock rule, even allied extras. An enabled scope is
   authoritative for CPU transformations only; other native admission stays intact. */
static int Port_CpuTransformOverride(BtlActJChr *c){
 int who=c->player,scope;
 if(who<0||who>=12||!ct_gate()||Port_TargetHuman(who)||ct_read(c,0x1278)!=1)return 0;
 scope=Port_Setting(Port_ModesFight()!=2&&Port_Team(who)==Port_Team(0)?"cpu_transform_allies":"cpu_transform_enemies",0);
 {int scenario=Port_ScenarioCpuScope(who);if(scenario>=0)scope=scenario;}
 return scope==1||scope==2;
}
void Port_CpuTacticsTick(void){int i;if(!ct_gate())return;for(i=0;i<BtlChar_GetCount()&&i<12;i++){
 BtlActJChr *c=BtlChar_Get(i);if(!c||Port_TargetHuman(i)||ct_read(c,0x1278)!=1||BtlMember_GetActiveGauge(c)->health<=0||BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))continue;
 /* Bounded combat age; keep remaining deadlines when rebasing after many hours. */
 if(sCpuTactics[i].age>=1000000000){sCpuTactics[i].nextEval-=sCpuTactics[i].age;sCpuTactics[i].nextTransform-=sCpuTactics[i].age;sCpuTactics[i].age=0;}
 sCpuTactics[i].age++;
}}
static int ct_pressure(int hp,int max,int ki,int enemyHp,int enemyMax,int enemyKi,int lastHp,int tierBehind){
 int ownPct,enemyPct,score=0;if(hp<=0||max<=0||enemyHp<=0||enemyMax<=0)return -1;
 ownPct=(int)((long long)hp*100/max);enemyPct=(int)((long long)enemyHp*100/enemyMax);
 if(ownPct<=50)score++;if(enemyPct-ownPct>=25)score+=2;
 if((long long)enemyKi-ki>=20000)score++;if(lastHp>hp)score++;if(tierBehind)score+=2;return score;
}
/* Form tiers (the mod's own table) for other modules: 1 when the character is known, 0 otherwise. */
int Port_CpuFormTier(int chara,int *family,int *tier){ct_tier(chara,family,tier);return *family!=0;}

/* The strength comes from the select screen's power label ("8.000", "5.4B", "400MM"), the same value the roster
   shows; when it is missing or unreadable the per-form health ceiling stands in. The engine itself carries no
   numeric battle power. */
extern long long Port_CharaPowerValue(int,int);
/* Lead in current HP the opponent needs before the CPU transforms: a tenth of the form's bars, never below the
   10000 of a stock health bar. `cpu_tactics_preset` 1 halves the lead, 2 transforms on any lost hit point. */
static int ct_deficit(int ownMax,int preset){
 int d;
 if(preset<=0)d=ownMax/10;else if(preset==1)d=ownMax/20;else return 0;
 return d>10000?d:10000;
}
static int Port_CpuTransformSlot(BtlActJChr *c){
 static const int queues[]={0x94c,0x954,0x958,0x95c,0x960};
 CpuTacticsState *s;BtlActJChr *enemy;BtlActJMember *m,*em;BtlActJGauge *g,*eg;
 int who=c->player,target,scope,preset,action,k,current,family,tier;
 int ownHpMax,ownHp,enemyHp,need,jump,best=-1,bestTier=0,bestCost=0;
 int haveFamily=0,haveAny=0;
 long long ownPow,enemyPow;
 if(who<0||who>=12||!ct_gate()||Port_TargetHuman(who)||ct_read(c,0x1278)!=1)return -1;
 scope=Port_Setting(Port_ModesFight()!=2&&Port_Team(who)==Port_Team(0)?"cpu_transform_allies":"cpu_transform_enemies",0);
 {int scenario=Port_ScenarioCpuScope(who);if(scenario>=0)scope=scenario;}
 if(!(sCpuPolicyLogged&(1u<<who))){sCpuPolicyLogged|=1u<<who;fprintf(stderr,"bt3: cpu policy: actor%d scope%d native_allowed%d effective_override%d mode%d mod%d\n",who,scope,ct_read(c,0x1300),Port_CpuTransformOverride(c),Battle_GetMode(),Port_ModesFight());}
 if(scope<1||scope>2)return -1;
 s=&sCpuTactics[who];if(s->age<s->nextEval)return -1;s->nextEval=s->age+10;
 m=BtlMember_GetActive(c);g=BtlMember_GetActiveGauge(c);current=m->chara;
 if(s->chara!=current){s->chara=current;s->lastHp=g->health;}
 target=Port_CamTarget(who);if(target<0||target>=BtlChar_GetCount()||target==who||Port_Team(target)==Port_Team(who))return -1;
 enemy=BtlChar_Get(target);if(!enemy)return -1;eg=BtlMember_GetActiveGauge(enemy);em=BtlMember_GetActive(enemy);
 ct_tier(current,&family,&tier);
 preset=Port_Setting("cpu_tactics_preset",0);if(preset<0||preset>2)preset=0;
 ownPow=Port_CharaPowerValue(m->chara,m->costume);
 enemyPow=Port_CharaPowerValue(em->chara,em->costume);
 {extern int Port_StoryActorPower(int,int);int a=Port_StoryActorPower(who,m->chara),b=Port_StoryActorPower(target,em->chara);if(a>0)ownPow=a;if(b>0)enemyPow=b;}
 if(ownPow<=0||enemyPow<=0){ownPow=ct_read(g,4);enemyPow=ct_read(eg,4);}   /* one scale for both sides */
 ownHpMax=ct_read(g,4);ownHp=g->health;enemyHp=eg->health;
 s->lastHp=ownHp;
 if(ownPow<=0||enemyPow<=0||ownHp<=0||enemyHp<=0)return -1;
 /* Only bother when the opponent is at least roughly as strong (within 10% below us). */
 if(enemyPow*100<ownPow*90LL)return -1;
 /* Far stronger opponent: transform at once, before any damage. Waiting to fall behind on health would let a
    fighter that dies to one hit never transform at all. */
 jump=enemyPow*100>=ownPow*150LL;
 if(!jump){
  /* Otherwise only once the opponent actually leads on current health. */
  need=ct_deficit(ownHpMax,preset);
  if(enemyHp-ownHp<need)return -1;
 }
 if(!jump&&scope==2&&ownHp>=enemyHp)return -1;
 if(s->age<s->nextTransform)return -1;
 action=BtlAct_GetCurrent(c);if((action!=7&&action!=11&&action!=15)||BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))return -1;
 for(k=0;k<5;k++)if(ct_read(c,queues[k])!=-1)return -1;
 if(ct_read(c,0xd98)||ct_read(c,0xdac)||ct_read(c,0xdb8))return -1;
 /* Opponent far stronger: go all the way to the top form. Otherwise advance a single step, never skipping a
    stage. */
 for(k=0;k<4;k++){
  int dest=BtlParam_GetSlotId(c,k),cost=BtlParam_GetSlotCost(c,k),df,dt,inFamily;
  if((current>=291&&current<=299)||current==95||current==97)dest=Port_Beta8FormSlotTargetForCostume(current,BtlMember_GetActive(c)->costume,k,dest);
  if(dest<0||dest>=301||dest==255||dest==current)continue;ct_tier(dest,&df,&dt);
  if(family&&family==df&&dt<=tier)continue;
  inFamily=(family&&family==df&&dt>tier);
  if(cost<=0&&!inFamily)continue;
  if(!BtlAct_CanTransform(c,k,1,1))continue;
  if(inFamily){
   if(!haveFamily){haveFamily=1;best=k;bestTier=dt;}
   else if((jump&&dt>bestTier)||(!jump&&dt<bestTier)){best=k;bestTier=dt;}
  }else if(!haveFamily){
   if(!haveAny){haveAny=1;best=k;bestCost=cost;}
   else if((jump&&cost>bestCost)||(!jump&&cost<bestCost)){best=k;bestCost=cost;}
  }
 }
 if(best>=0){
  sCpuChain[who]=jump;
  fprintf(stderr,"bt3: cpu transform: actor%d slot%d power %lld vs %lld, health %d vs %d, jump%d\n",who,best,ownPow,enemyPow,ownHp,enemyHp,jump);
 }
 return best;
}
static void Port_CpuTacticsQueued(BtlActJChr *c,int slot,int success){
 static const int cooldowns[]={240,150,90};int p=Port_Setting("cpu_tactics_preset",0),who=c->player,wait,chain;
 if(who>=0&&who<12){chain=sCpuChain[who];sCpuChain[who]=0;}else chain=0;
 if(!success||who<0||who>=12)return;if(p<0||p>2)p=0;
 /* Evaluations are3Hz: old cooldowns24/15/9 evaluations =8/5/3 active seconds.
    A jump made against a far stronger opponent re-evaluates as soon as the change settles (one evaluation),
    so the CPU keeps climbing until it is at its top form. */
 wait=chain?10:cooldowns[p];
 sCpuTactics[who].nextTransform=sCpuTactics[who].age+wait;
 fprintf(stderr,"bt3: cpu tactics: actor%d slot%d admitted through ordinary transform, cooldown%d ticks\n",who,slot,wait);
}
#endif

#ifdef PORT
extern int Port_Beta8SizeClass(int,int),BtlChar_FrameMod(int);
static int Port_CpuFormAllowed(BtlActJChr*c,int slot){
 int who=c->player,cid,dest,policy,chance;unsigned seed;char key[48];
 if(Port_NetActive()||BattleReplay_IsActive()||who<0||who>=12||Port_TargetHuman(who)||ct_read(c,0x1278)!=1)return 1;
 cid=BtlMember_GetActive(c)->chara;dest=BtlParam_GetSlotId(c,slot);
 snprintf(key,sizeof(key),"cpu_transform_%d",cid);policy=Port_Setting(key,0);
 if(Port_ScenarioActive())return 1;
 if(policy==2)return 0;
 if(policy!=1){if(Port_Setting("disable_npc_transformations",0))return 0;
  if(Port_Setting("disable_npc_giant_transformations",0)&&Port_Beta8SizeClass(dest,BtlMember_GetActive(c)->costume)==4)return 0;
 }
 chance=Port_Setting("npc_transform_chance_percent",100);if(chance>=100)return 1;if(chance<=0)return 0;
 /* Stable for64active CPU ticks. A retry/probe cannot reroll admission, and
    the battle RNG used for damage and AI remains untouched. */
 seed=(unsigned)(BtlChar_FrameMod(0x7fffffff)/64)^(unsigned)(who*0x45d9f3bu)^0x9e3779b9u;
 seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
 return (seed&255u)<(unsigned)((chance*256+50)/100);
}
#endif

#ifdef PORT
/* All native/custom pairs use their authored recipes and the ordinary fusion commit. Never
   steal a human partner; strength pressure uses native HP/ki and form tiers. */
static int Port_CpuTryFusion(BtlActJChr*c){
 extern int Port_FusionPartnerActor(int,int),Port_FusionConsumedActor(int),Port_FormPresentationOwner(void);
 int who=c->player,cid,action,target,k,slot,start,member,partner,ef,et,family,tier,score;BtlActJChr*enemy,*ally;BtlActJGauge*g,*eg;
 if(!Port_CpuTransformOverride(c)||Port_FormPresentationOwner()>=0)return 0;
 cid=BtlMember_GetActive(c)->chara;
 if(sCpuTactics[who].age<sCpuFusionEval[who])return 0;sCpuFusionEval[who]=sCpuTactics[who].age+10;
 action=BtlAct_GetCurrent(c);if((action!=7&&action!=11&&action!=15)||BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))return 0;
 for(k=0;k<5;k++){static const int queue[]={0x94c,0x954,0x958,0x95c,0x960};if(ct_read(c,queue[k])!=-1)return 0;}
 target=Port_CamTarget(who);if(target<0||target>=BtlChar_GetCount()||Port_Team(target)==Port_Team(who))return 0;
 enemy=BtlChar_Get(target);if(!enemy)return 0;g=BtlMember_GetActiveGauge(c);eg=BtlMember_GetActiveGauge(enemy);
 ct_tier(cid,&family,&tier);ct_tier(BtlMember_GetActive(enemy)->chara,&ef,&et);
 score=ct_pressure(g->health,ct_read(g,4),ct_read(g,12),eg->health,ct_read(eg,4),ct_read(eg,12),sCpuTactics[who].lastHp,et>tier);
 if(score<1&&(long long)ct_read(eg,4)*2<=(long long)ct_read(g,4)*3)return 0;
 start=(who+sCpuFusionChoice[who]++)%3;
 for(k=0;k<3;k++){slot=(start+k)%3;if(!BtlAct_CanFuse(c,slot,1,0,&member))continue;
  partner=Port_FusionPartnerActor(who,member);if(partner<0||partner>=BtlChar_GetCount()||Port_TargetHuman(partner)||Port_FusionConsumedActor(partner))continue;
  ally=BtlChar_Get(partner);if(!ally||BtlMember_GetActiveGauge(ally)->health<=0||BtlChar_IsFrozen(ally)||ChrCam_IsCutActive(ally))continue;
  action=BtlAct_GetCurrent(ally);if(action!=7&&action!=11&&action!=15)continue;
  if(BtlAct_QueueFusion(c,slot,member)){sCpuFusionEval[who]=sCpuTactics[who].age+240;fprintf(stderr,"bt3: cpu fusion: actor%d partner%d slot%d under pressure\n",who,partner,slot);return 1;}
 }return 0;
}
#endif
