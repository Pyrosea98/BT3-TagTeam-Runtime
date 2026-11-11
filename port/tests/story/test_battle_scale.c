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
static int preset=3,scenario;int Port_Setting(const char*k,int d){return !strcmp(k,"power_scale_preset")?preset:d;}int Port_ScenarioActive(void){return scenario;}long long Port_CharaPowerValue(int c,int co){return c==60?5000000000000000000LL:c==164?9223372036854775807LL:970;}
extern int Port_BattleScaleMode(void),Port_BattleScaleHealth(int,int,int),Port_BattleScaleDamageCap(int,int);
int main(void){TestAtk*p;int mode,v;seed();active=0;
 for(mode=0;mode<3;mode++){preset=mode;Port_StoryCombatReset();member[0].chara=0;member[1].chara=164;
 p=Port_StoryCombatTable(&fighters[0],1,atk[0]);assert(p!=atk[0]&&p[0].damage>0);assert(atk[0][0].damage==100);
 if(mode==0){TestAtk*q=Port_StoryCombatTable(&fighters[1],1,atk[1]);assert(p[0].damage==q[0].damage);assert(Port_BattleScaleHealth(164,0,999999)==60000);}
 assert(Port_BattleScaleDamageCap(2147483647,60000)<=15000);
 assert(Port_BattleScaleHealth(164,0,10000)>=60000);
 v=p[0].damage;assert(((TestAtk*)Port_StoryCombatTable(&fighters[0],1,atk[0]))[0].damage==v);
 member[0].costume++;assert(((TestAtk*)Port_StoryCombatTable(&fighters[0],1,atk[0]))[0].damage==v);
 assert(Port_BattleScaleMode()==mode);preset=3;assert(Port_BattleScaleMode()==mode); /* no mid-fight switch */
 }
 preset=3;Port_StoryCombatReset();member[0].chara=0;assert(Port_StoryCombatTable(&fighters[0],1,atk[0])==atk[0]);assert(Port_BattleScaleDamageCap(2147483647,60000)==2147483647);
 member[0].chara=301;p=Port_StoryCombatTable(&fighters[0],1,atk[0]);assert(p!=atk[0]&&p[0].damage<2000);assert(Port_BattleScaleHealth(301,0,9999999)==30000);
 net=1;assert(Port_StoryCombatTable(&fighters[0],1,atk[0])==atk[0]);net=0;replay=1;assert(Port_StoryCombatTable(&fighters[0],1,atk[0])==atk[0]);replay=0;
 preset=0;scenario=1;Port_StoryCombatReset();member[0].chara=0;assert(Port_BattleScaleMode()==3&&Port_StoryCombatTable(&fighters[0],1,atk[0])==atk[0]);
 puts("PASS actual private combat tables: four presets, canonical extremes, no shared mutation/compound scaling, costume reload, next-fight latch, Yajirobe, real/network/replay/scenario preservation");return 0;}
