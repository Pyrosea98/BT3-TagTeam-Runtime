/* Private local battle tables. Rebuilt from original object tables after every form/costume change. */
#include "mod_battle_scale.h"
static StCombatData sBtCombat[16];
static int sBtCostume[16],sBtMode[16],sBtRoundPreset=3;
extern int Port_Setting(const char*,int),Port_ScenarioActive(void);
extern long long Port_CharaPowerValue(int,int);
int Port_BattleScaleMode(void){int m;if(Port_StoryActive()||Port_ScenarioActive()||Port_NetActive()||BattleReplay_IsActive())return 3;m=sBtRoundPreset;return m>=0&&m<=3?m:3;}
void Port_BattleScaleReset(void){memset(sBtCombat,0,sizeof(sBtCombat));sBtRoundPreset=Port_Setting("power_scale_preset",3);}
static void bt_scale_table(StCombatData*s,float strength){
 int i,old=0,top;
 for(i=0;i<64;i++){int v=st_combat_raw(s->atk+i);if(v>old)old=v;}
 top=(int)(2000.f*strength);
 for(i=0;i<ST_COMBAT_ATKS;i++)st_combat_write(s->atk+i,st_combat_scale(st_combat_raw(s->atk+i),top,old));
 old=0;for(i=0;i<BTL_KIBLAST_COUNT;i++)if(s->ki[i].damage>old)old=s->ki[i].damage;
 top=(int)(1200.f*strength);for(i=0;i<BTL_KIBLAST_COUNT;i++){s->ki[i].damage=st_combat_scale(s->ki[i].damage,top,old);s->ki[i].guardDamage=st_combat_scale(s->ki[i].guardDamage,top,old);}
 for(i=0;i<3;i++){int raw=s->super.damage[i];top=(int)((i==0?6500.f:i==1?8500.f:12000.f)*strength);if(raw>0){s->super.damage[i]=top;s->super.guardDamage[i]=st_combat_scale(s->super.guardDamage[i],top,raw);}}
 old=0;for(i=0;i<2;i++)if(s->skill.damage[i]>old)old=s->skill.damage[i];top=(int)(5000.f*strength);
 for(i=0;i<2;i++){s->skill.damage[i]=st_combat_scale(s->skill.damage[i],top,old);s->skill.guardDamage[i]=st_combat_scale(s->skill.guardDamage[i],top,old);}
 s->param.damageTaken=1.f/strength;s->param.hitReactScale=1.f;
 {unsigned short cls=0;unsigned flags;memcpy(s->param.unk0,&cls,2);memcpy(&flags,s->param.unk0+0x10,4);flags&=~12u;memcpy(s->param.unk0+0x10,&flags,4);}
}
static void *bt_combat_table(void*fighter,int kind,void*original){
 BtlTechBChr*c=fighter;BtlTechBMember*m;BtlTechBObj*o;StCombatData*s;int a,mode;float strength;
 if(!c||Port_StoryActive()||Port_NetActive()||BattleReplay_IsActive())return original;
 mode=Port_BattleScaleMode();m=BtlMember_GetActive(c);if(!m||(mode==3&&m->chara!=301))return original;
 a=*(int*)fighter;if(a<0||a>=16)return original;o=BtlChar_GetObj(c);if(!o||!o->param||!o->atk||!o->kiBlast||!o->super||!o->skill)return original;
 s=&sBtCombat[a];if(!s->valid||s->chara!=m->chara||sBtCostume[a]!=m->costume||sBtMode[a]!=mode){
 memcpy(&s->param,o->param,sizeof(s->param));memcpy(s->atk,o->atk,sizeof(s->atk));memcpy(s->ki,o->kiBlast,sizeof(s->ki));memcpy(&s->super,o->super,sizeof(s->super));memcpy(&s->skill,o->skill,sizeof(s->skill));
 strength=mode==3?sqrtf(970.f/1200.f):PS_Strength(Port_CharaPowerValue(m->chara,m->costume),mode);
 bt_scale_table(s,strength);s->valid=1;s->chara=m->chara;sBtCostume[a]=m->costume;sBtMode[a]=mode;
 }
 switch(kind){case 0:return &s->param;case 1:return s->atk;case 2:return s->ki;case 3:return &s->super;case 4:return &s->skill;default:return original;}
}
int Port_BattleScaleHealth(int chara,int costume,int native){int mode=Port_BattleScaleMode();
 if(mode==3)return chara==301&&!Port_StoryActive()?30000:native;
 return (int)(60000.f*(mode==2?PS_Strength(Port_CharaPowerValue(chara,costume),mode):1.f));
}
int Port_BattleScaleDamageCap(int amount,int maximum){int mode=Port_BattleScaleMode(),cap;if(mode==3||amount<=0||maximum<=0)return amount;cap=maximum/(mode==2?4:mode==1?8:4);return amount>cap?cap:amount;}
