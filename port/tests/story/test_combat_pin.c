#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "battle/btl_tech_b.h"
typedef struct TestAtk {unsigned flags;unsigned short damage;unsigned char rest[38];signed char armorIgnore;unsigned char unused;unsigned short damageHi;} TestAtk;
extern void Port_StoryCombatReset(void),Port_StoryCombatBind(void*,int);
extern void *Port_StoryCombatTable(void*,int,void*),*BtlAtk_GetRecordOf(void*,unsigned);
extern int BtlParam_GetFlags(void*);
static BtlTechBChr fighters[16];static BtlTechBObj objects[16];static BtlTechBParam param[16];static TestAtk atk[16][163];static BtlKiBlastData ki[16][13];static BtlSuperData super[16];static BtlSkillData skill[16];static BtlTechBMember member[16];
static int active=1,chapter=1,net,replay;
int Port_StoryEraPower(int a,int form){return chapter>=3?(a==2?970:a==1?1200:3500):0;}
int Port_StoryFormSlot(int source){return chapter==3&&source==13?180:-1;}
int Port_StoryActive(void){return active;}int Port_StoryChapter(void){return chapter;}
int Port_StoryTechniqueDonor(int actor){return -1;}
BtlTechBChr *BtlChar_Get(int actor){return &fighters[actor];}
int Port_StoryCastActor(int cast){return cast==15?15:cast==0?0:cast==1?2:cast==2?1:-1;}
int Port_NetActive(void){return net;}int BattleReplay_IsActive(void){return replay;}
BtlTechBObj*BtlChar_GetObj(BtlTechBChr*c){return &objects[*(int*)c];}
BtlTechBMember*BtlMember_GetActive(BtlTechBChr*c){return &member[*(int*)c];}
int BtlMember_HasAbility(BtlTechBChr*c,int a){return 0;}
float BtlStat_GetBlast2DamageScale(BtlTechBChr*c){return 1;}float BtlStat_GetUltimateDamageScale(BtlTechBChr*c){return 1;}
float BtlStat_GetKiBlastDamageScale(BtlTechBChr*c){return 1;}
static void seed(void){int a,i;for(a=0;a<16;a++){*(int*)&fighters[a]=a;objects[a].param=&param[a];objects[a].atk=atk[a];objects[a].kiBlast=ki[a];objects[a].super=&super[a];objects[a].skill=&skill[a];param[a].damageTaken=5;param[a].hitReactScale=1;memset(param[a].unk0,0,sizeof(param[a].unk0));for(i=0;i<163;i++)atk[a][i].damage=100;for(i=0;i<13;i++)ki[a][i].damage=20;for(i=0;i<3;i++)super[a].damage[i]=1000;member[a].chara=a==2?25:a==1?77:0;member[a].costume=a==2?1:3;}}
void Port_Dbg3(const char*a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;} /* debug log boundary (BT3_STORY_FORM_LOG) */
int main(void){int i,pass;TestAtk*p;assert(sizeof(TestAtk)==48);seed();
 *(unsigned*)(param[0].unk0+0x10)=0x1000;*(unsigned*)(param[2].unk0+0x10)=0x1000;
 for(pass=0;pass<3;pass++){Port_StoryCombatReset();
  /* Initial Goku then Raditz then Krillin; repeat must not compound normalization. */
  for(i=0;i<3;i++)Port_StoryCombatBind(&fighters[i],1);
  assert(!(BtlParam_GetFlags(&fighters[0])&0x1000));assert(BtlParam_GetFlags(&fighters[2])&0x1000);
  p=BtlAtk_GetRecordOf(&fighters[2],0);assert(p->damage==50&&atk[2][0].damage==100);
  assert(BtlSuper_GetBaseDamage(&fighters[2],2,1)==500);
  assert(BtlParam_GetDamageTakenScale(&fighters[2])==6.25f);
 }
 /* Damaged costume replaces every table: all authored offensive/defensive values remain stable. */
 active=1;chapter=2;Port_StoryCombatReset();Port_StoryCombatBind(&fighters[0],1);
 for(i=0;i<163;i++)atk[0][i].damage=30000;
 super[0].damage[0]=2000000;ki[0][0].damage=1000000;param[0].damageTaken=.01f;*(unsigned*)(param[0].unk0+0x10)=0xFFFFFFFF;
 Port_StoryCombatBind(&fighters[0],0);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==100);
 assert(BtlSuper_GetBaseDamage(&fighters[0],2,1)==1000);
 assert(BtlKiBlast_GetDamageOf(&fighters[0],0)==20);
 assert(BtlParam_GetDamageTakenScale(&fighters[0])==5&&BtlParam_GetFlags(&fighters[0])==0);
 /* Each legitimate form owns a snapshot, including when it returns to its old base. */
 member[0].chara=231;atk[0][0].damage=200;Port_StoryCombatBind(&fighters[0],1);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==200);
 atk[0][0].damage=30000;member[0].costume=1;Port_StoryCombatBind(&fighters[0],1);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==200);
 member[0].chara=0;Port_StoryCombatBind(&fighters[0],1);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==100);
 member[0].costume=4;Port_StoryCombatBind(&fighters[0],1);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==100);
 member[15].chara=29;Port_StoryCombatBind(&fighters[15],1);member[15].chara=30;atk[15][0].damage=777;Port_StoryCombatBind(&fighters[15],1);atk[15][0].damage=30000;Port_StoryCombatBind(&fighters[15],1);assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[15],0))->damage==777);
 /* CH4+ use authored era power, not the raw borrowed costume. Exercise native readers. */
 chapter=5;seed();Port_StoryCombatReset();for(i=0;i<3;i++)Port_StoryCombatBind(&fighters[i],1);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[2],0))->damage < ((TestAtk*)BtlAtk_GetRecordOf(&fighters[1],0))->damage);
 assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[1],0))->damage < ((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage);
 assert(BtlSuper_GetBaseDamage(&fighters[2],2,1)<BtlSuper_GetBaseDamage(&fighters[1],2,1));
 assert(BtlParam_GetDamageTakenScale(&fighters[2])>BtlParam_GetDamageTakenScale(&fighters[1]));
 {int before=BtlSuper_GetBaseDamage(&fighters[2],2,1);super[2].damage[2]=2000000;Port_StoryCombatBind(&fighters[2],1);assert(BtlSuper_GetBaseDamage(&fighters[2],2,1)==before);}
 /* Inflating another technique must not reduce the ultimate to negligible damage. */
 seed();super[0].damage[0]=1000;super[0].damage[1]=1000000000;super[0].damage[2]=1;Port_StoryCombatReset();Port_StoryCombatBind(&fighters[0],1);
 assert(BtlSuper_GetBaseDamage(&fighters[0],4,1)>BtlSuper_GetBaseDamage(&fighters[0],2,1));
 assert(BtlSuper_GetBaseDamage(&fighters[0],2,1)>5000);
 /* A donated ground smash must never retain a huge outlier relative to the era's normal punch. */
 seed();atk[0][20].damage=65535;atk[0][20].damageHi=300;super[0].chargeScale[0]=1000;
 Port_StoryCombatReset();Port_StoryCombatBind(&fighters[0],1);
 {TestAtk*a=BtlAtk_GetRecordOf(&fighters[0],0),*b=BtlAtk_GetRecordOf(&fighters[0],20);
  assert((b->damage+((unsigned)b->damageHi<<16))<=4*(a->damage+((unsigned)a->damageHi<<16)));
  BtlSuperData*t=Port_StoryCombatTable(&fighters[0],3,&super[0]);assert(t->chargeScale[0]==1.5f&&super[0].chargeScale[0]==1000);}
 /* Piccolo's cosmetic cape removal keeps the original Ki recovery and era strength. */
 chapter=2;seed();member[0].chara=22;member[0].costume=0;Port_StoryCombatReset();
 {int raw[3]={40000,28912,13808};memcpy(param[0].unk0+0x28,raw,4);memcpy(param[0].unk0+0x30,raw+1,4);memcpy(param[0].unk0+0x34,raw+2,4);}
 Port_StoryCombatBind(&fighters[0],1);
 {BtlTechBParam*p=Port_StoryCombatTable(&fighters[0],0,&param[0]);int value;memcpy(&value,p->unk0+0x28,4);assert(value==20000);memcpy(&value,p->unk0+0x30,4);assert(value==16624);memcpy(&value,p->unk0+0x34,4);assert(value==8432);assert(p->damageTaken==param[0].damageTaken);}
 member[0].costume=1;Port_StoryCombatBind(&fighters[0],1);{BtlTechBParam*p=Port_StoryCombatTable(&fighters[0],0,&param[0]);int value;memcpy(&value,p->unk0+0x28,4);assert(value==20000);}
 /* Restore the old raw table for outside-mode bypass assertions. */
 for(i=0;i<3;i++)super[0].damage[i]=2000000;param[0].damageTaken=.01f;atk[0][0].damage=30000;
 /* Outside local Story, the real current data is used, including costume damage. */
 active=0;assert(BtlSuper_GetBaseDamage(&fighters[0],2,1)==2000000);active=1;
 net=1;assert(BtlSuper_GetBaseDamage(&fighters[0],2,1)==2000000);net=0;replay=1;assert(BtlParam_GetDamageTakenScale(&fighters[0])==.01f);replay=0;
 assert(Port_StoryCombatTable(0,0,&param[0])==&param[0]);
 Port_StoryCombatReset();assert(((TestAtk*)BtlAtk_GetRecordOf(&fighters[0],0))->damage==30000);
 puts("PASS actual melee/ki/technique/defence/armour readers: Krillin below Goku, torn clothes keep chapter stats, retries never compound, source tables immutable, online/replay/other modes bypass");return 0;}
