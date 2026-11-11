/* Private local battle tables. Rebuilt from original object tables after every form/costume change. */
#include "mod_battle_scale.h"
static StCombatData sBtCombat[16];
static int sBtCostume[16],sBtMode[16],sBtRoundPreset=3;
static void*sBtObjects[16];
void Port_BattleScaleObjectChanged(void*obj){int i;if(!obj)return;for(i=0;i<16;i++)if(sBtObjects[i]==obj)sBtCombat[i].valid=0;}
extern int Port_Setting(const char*,int),Port_ScenarioActive(void);
extern long long Port_CharaPowerValue(int,int);
int Port_BattleScaleMode(void){int m;if(Port_StoryActive()||Port_ScenarioActive()||Port_NetActive()||BattleReplay_IsActive())return 3;m=sBtRoundPreset;return m>=0&&m<=3?m:3;}
void Port_BattleScaleReset(void){memset(sBtCombat,0,sizeof(sBtCombat));memset(sBtObjects,0,sizeof(sBtObjects));sBtRoundPreset=Port_Setting("power_scale_preset",3);}
static int bt_scale_positive(int value,int top,int old){int n=st_combat_scale(value,top,old);return value>0&&top>0&&old>0&&n<1?1:n;}
static void bt_scale_table(StCombatData*s,float strength,int nativeYajirobe){
 int i,old=0,top;
 for(i=0;i<(nativeYajirobe?64:ST_COMBAT_ATKS);i++){int v=st_combat_raw(s->atk+i);if(v>old)old=v;}
 top=(int)((nativeYajirobe?21.f:2000.f)*strength);
 for(i=0;i<ST_COMBAT_ATKS;i++)st_combat_write(s->atk+i,bt_scale_positive(st_combat_raw(s->atk+i),top,old));
 old=0;for(i=0;i<BTL_KIBLAST_COUNT;i++)if(s->ki[i].damage>old)old=s->ki[i].damage;
 top=(int)((nativeYajirobe?730.f:1200.f)*strength);for(i=0;i<BTL_KIBLAST_COUNT;i++){s->ki[i].damage=bt_scale_positive(s->ki[i].damage,top,old);s->ki[i].guardDamage=bt_scale_positive(s->ki[i].guardDamage,top,old);}
 for(i=0;i<3;i++){int raw=s->super.damage[i];top=(int)((nativeYajirobe?(i==0?235.f:i==1?215.f:485.f):(i==0?6500.f:i==1?8500.f:12000.f))*strength);if(raw>0){s->super.damage[i]=top;s->super.guardDamage[i]=bt_scale_positive(s->super.guardDamage[i],top,raw);}}
 old=0;for(i=0;i<2;i++)if(s->skill.damage[i]>old)old=s->skill.damage[i];top=(int)((nativeYajirobe?1000.f:5000.f)*strength);
 for(i=0;i<2;i++){s->skill.damage[i]=bt_scale_positive(s->skill.damage[i],top,old);s->skill.guardDamage[i]=bt_scale_positive(s->skill.guardDamage[i],top,old);}
 s->param.damageTaken=(nativeYajirobe?15.f:1.f)/strength;s->param.hitReactScale=1.f;
 {unsigned short cls=0;unsigned flags;memcpy(s->param.unk0,&cls,2);memcpy(&flags,s->param.unk0+0x10,4);flags&=~12u;memcpy(s->param.unk0+0x10,&flags,4);}
}
static int bt_reference_power(int c){return c==301?970:c==323||c==324?800:c==326||c==327?8000:0;}
static void *bt_combat_table(void*fighter,int kind,void*original){
 BtlTechBChr*c=fighter;BtlTechBMember*m;BtlTechBObj*o;StCombatData*s;int a,mode;float strength;
 if(!c||Port_StoryActive()||Port_NetActive()||BattleReplay_IsActive())return original;
 mode=Port_BattleScaleMode();m=BtlMember_GetActive(c);if(!m||(mode==3&&!bt_reference_power(m->chara)))return original;
 a=*(int*)fighter;if(a<0||a>=16)return original;o=BtlChar_GetObj(c);if(!o||!o->param||!o->atk||!o->kiBlast||!o->super||!o->skill)return original;
 s=&sBtCombat[a];if(!s->valid||sBtObjects[a]!=o||s->chara!=m->chara||sBtCostume[a]!=m->costume||sBtMode[a]!=mode){
 memcpy(&s->param,o->param,sizeof(s->param));memcpy(s->atk,o->atk,sizeof(s->atk));memcpy(s->ki,o->kiBlast,sizeof(s->ki));memcpy(&s->super,o->super,sizeof(s->super));memcpy(&s->skill,o->skill,sizeof(s->skill));
 strength=mode==3?sqrtf((float)bt_reference_power(m->chara)/1200.f):PS_Strength(Port_CharaPowerValue(m->chara,m->costume),mode);
 bt_scale_table(s,strength,mode==3&&bt_reference_power(m->chara)>0);s->valid=1;sBtObjects[a]=o;s->chara=m->chara;sBtCostume[a]=m->costume;sBtMode[a]=mode;
 }
 switch(kind){case 0:return &s->param;case 1:return s->atk;case 2:return s->ki;case 3:return &s->super;case 4:return &s->skill;default:return original;}
}
int Port_BattleScaleHealth(int chara,int costume,int native){int mode=Port_BattleScaleMode();
 if(mode==3)return chara==301&&!Port_StoryActive()?30000:native;
 if(mode==2){float strength=PS_Strength(Port_CharaPowerValue(chara,costume),mode),hp;
 /* Compression must not discard authored HP or make weaker fighters paper. */
 hp=(float)(native>60000?native:60000)*(strength>1.f?strength:1.f);
 return hp>=2147483648.f?2147483647:(int)hp;
 }
 if(mode==1)return (int)(60000.f*PS_Strength(Port_CharaPowerValue(chara,costume),mode));
 return 60000;
}
int Port_BattleScaleDamageCap(int amount,int maximum){int mode=Port_BattleScaleMode(),cap;if(mode==3||amount<=0||maximum<=0)return amount;cap=maximum/(mode==2?4:mode==1?8:4);return amount>cap?cap:amount;}
