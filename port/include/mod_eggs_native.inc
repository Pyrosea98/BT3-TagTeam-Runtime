/* Match-local transaction: admission is read-only; consumption happens only at transform payment. */
#include "plat_eggs.h"
#include "mod_scenarios.h"
extern int Port_NetActive(void),BattleReplay_IsActive(void),Port_Setting(const char*,int),Port_TargetHuman(int),Port_Team(int),BtlChar_GetCount(void),Port_FusionConsumedActor(int);
extern BtlActJChr *BtlChar_Get(int);
extern void Port_EggConsumeActor(int,int);
extern void Port_EggMergeDragonBonus(int,int);
extern void Port_EggDragonAbsorbBegin(int,int);
typedef struct EggPlan {int source,target,cost,count,actor[16],identity[16];} EggPlan;
static EggPlan sEggPlan[16];
static int eg_on(BtlActJChr*c){if(Port_ScenarioActive())return !Port_NetActive()&&!BattleReplay_IsActive();return !Port_NetActive()&&!BattleReplay_IsActive()&&Port_Setting("easter_eggs",0)&&(Port_TargetHuman(c->player)||Port_Setting("easter_eggs_cpu",0));}
static int eg_matches(const EggRule*r,int id){int k;
 if(r->absorber==255&&r->result==140&&(id==240||id==236))return 1;
 if(r->absorber==291){switch(id){case 29:case 30:case 31:case 32:case 33:case 34:case 35:case 36:case 38:case 122:case 170:case 237:case 202:case 240:case 236:case 244:return 1;default:return 0;}}
 if(r->absorber==292){switch(id){case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 11:case 161:case 169:case 180:case 200:case 231:case 232:case 233:return 1;default:return 0;}}
 if(r->absorber==255&&r->result==272&&id==233)return 1;if(r->result==274&&r->targets[0]==91&&(id==95||id==96))return 1;for(k=0;k<4;k++)if(r->targets[k]!=255&&r->targets[k]==id)return 1;return 0;}
static int eg_pick(BtlActJChr*c,const EggRule*r,int exact){
 int i,best=-1,bestEnemy=-1,bestHp=0x7fffffff;
 for(i=0;i<BtlChar_GetCount()&&i<16;i++){
  BtlActJChr*t=BtlChar_Get(i);BtlActJGauge*g;int id,hp,enemy;
  if(!t||i==c->player||Port_FusionConsumedActor(i)||!Port_ScenarioAbsorbAllowed(c->player,i))continue;g=BtlMember_GetActiveGauge(t);if(!g)continue;hp=g->health;id=BtlMember_GetActive(t)->chara;
  if(exact>=0?id!=exact:!eg_matches(r,id))continue;
  /* Fallen, unconsumed targets are eligible for every absorption. Specific
     * defeated-only rules (Android13) still reject living targets. */
  if((r->flags&EGG_FLAG_REQUIRE_DEAD)&&hp>0)continue;
  enemy=Port_Team(i)!=Port_Team(c->player);if(best<0||enemy>bestEnemy||(enemy==bestEnemy&&hp<bestHp)){best=i;bestEnemy=enemy;bestHp=hp;}
 }return best;
}
static void eg_add(EggPlan*p,int who){BtlActJChr*t;int j;if(who<0||p->count>=16)return;for(j=0;j<p->count;j++)if(p->actor[j]==who)return;t=BtlChar_Get(who);p->actor[p->count]=who;p->identity[p->count++]=BtlMember_GetActive(t)->chara;}
static int eg_price(BtlActJChr*c,int target,int normal,EggPlan*out){
 EggPlan p;int i,j,groups=0,required=0,cost=0;const EggGate*gate;memset(&p,0,sizeof(p));p.source=BtlMember_GetActive(c)->chara;p.target=target;p.cost=normal;
 if(!eg_on(c)){if(out)*out=p;return normal;}Port_EggsSetEnabled(1);gate=Port_EggsGate(p.source,target);
 if(gate){for(i=0;i<BtlChar_GetCount()&&i<16;i++){BtlActJChr*t=BtlChar_Get(i);int id;if(!t||i==c->player||Port_FusionConsumedActor(i))continue;id=BtlMember_GetActive(t)->chara;for(j=0;j<8;j++)if(gate->others[j]!=255&&gate->others[j]==id){eg_add(&p,i);break;}}for(i=0;i<p.count;i++){int hp=BtlMember_GetActiveGauge(BtlChar_Get(p.actor[i]))->health;if(hp>0)cost+=hp/100000;}if(cost>21474)return -1;p.cost=(cost<1?1:cost)*100000;if(out)*out=p;return p.cost;}
 for(i=0;i<Port_EggsRuleCount();i++){
  const EggRule*r=Port_EggsRuleAt(i);if(r->absorber!=p.source||r->trigger!=target)continue;required++;groups++;
  if((r->flags&EGG_FLAG_ALL_TARGETS)&&r->result!=274){for(j=0;j<4;j++)if(r->targets[j]!=255){int a=eg_pick(c,r,r->targets[j]);if(a<0)return -1;eg_add(&p,a);}}
  else {int a=eg_pick(c,r,-1);if(a<0){if(target==274||p.source==291||p.source==292)return -1;groups--;continue;}eg_add(&p,a);}
 }
 if(required&&!groups&&Port_ScenarioActive())return -1;
 if(required&&groups){for(i=0;i<p.count;i++)cost+=Port_EggsCost(BtlMember_GetActiveGauge(BtlChar_Get(p.actor[i]))->health);if(cost>21474)return -1;p.cost=cost*100000;}
 if(out)*out=p;return p.cost;
}
static int eg_queue(BtlActJChr*c,int target,int normal){EggPlan p;int cost=eg_price(c,target,normal,&p);if(cost<0||!BtlMember_HasBlast(c,cost))return -1;if(c->player>=0&&c->player<16)sEggPlan[c->player]=p;return cost;}
int Port_EggTransformPay(void*actor,int cost){
 BtlActJChr*c=actor;EggPlan*p;int i;if(!c||c->player<0||c->player>=16)return cost;p=&sEggPlan[c->player];
 if(!p->count||!eg_on(c)||p->source!=BtlMember_GetActive(c)->chara||p->target!=c->form.chara||p->cost!=cost){p->count=0;return cost;}
 /* Revalidate all participants before touching any of them. */
 for(i=0;i<p->count;i++){BtlActJChr*t=BtlChar_Get(p->actor[i]);if(!t||Port_FusionConsumedActor(p->actor[i])||BtlMember_GetActive(t)->chara!=p->identity[i]){p->count=0;return cost;}}
 /* Only a paid, revalidated One-Star absorption grants this bonus. Applied
    * before consumption and normal parameter reload; member bonuses survive it. */
 if(p->source==146&&p->target==147){for(i=0;i<p->count;i++)Port_EggMergeDragonBonus(c->player,p->actor[i]);Port_EggDragonAbsorbBegin(c->player,p->count);}
 for(i=0;i<p->count;i++){Port_EggConsumeActor(c->player,p->actor[i]);Port_ScenarioConsumed(c->player,p->actor[i]);}
 fprintf(stderr,"bt3: easter egg actor%d %d->%d consumed%d cost%d stocks\n",c->player,p->source,p->target,p->count,cost/100000);p->count=0;return cost;
}
