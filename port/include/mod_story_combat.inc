/* Local authored Story casts keep their original combat data when clothes tear.
   Owned numeric tables only: models, skeletons, motions and shared asset bytes stay untouched. */
extern int Port_StoryActive(void),Port_StoryChapter(void),Port_StoryCastActor(int);
typedef struct StCombatAtk {u32 flags;u16 damage;u8 rest[0x2C-6];s8 armorIgnore;u8 unused;u16 damageHi;} StCombatAtk;
#define ST_COMBAT_ATKS 163
typedef struct StCombatData {
 int valid,chara; /* chara: the form the tables were taken from */
 BtlTechBParam param;
 StCombatAtk atk[ST_COMBAT_ATKS];
 BtlKiBlastData ki[BTL_KIBLAST_COUNT];
 BtlSuperData super;
 BtlSkillData skill;
} StCombatData;
/* Story has a small authored form graph; cache each visited form independently.
   Costume swaps never create a new profile, and returning to base restores its original data. */
#define ST_COMBAT_FORMS 8
static StCombatData sStCombat[16][ST_COMBAT_FORMS];
static StCombatData *st_combat_form(int actor,int form,int create){
 int i;StCombatData*empty=0;
 for(i=0;i<ST_COMBAT_FORMS;i++){StCombatData*s=&sStCombat[actor][i];if(s->valid&&s->chara==form)return s;if(!s->valid&&!empty)empty=s;}
 if(create&&empty){empty->chara=form;return empty;}return 0;
}
void Port_StoryCombatReset(void){memset(sStCombat,0,sizeof(sStCombat));}
static int st_combat_raw(const StCombatAtk*r){return MD_Clamp((long long)r->damage+((long long)r->damageHi<<16));}
static void st_combat_write(StCombatAtk*r,int v){r->damage=(unsigned)v&65535;r->damageHi=(unsigned)v>>16;}
static int st_combat_scale(int v,int top,int old){return v>0&&top>0&&old>0?MD_MulDiv(v,top,old):v;}
static void st_combat_krillin(StCombatData*k,const StCombatData*g){
 int i,old=0,top=0,v;
 /* Authored CH2 balance: below Kame House Goku, not costume 1's later-saga Krillin.
    Half Goku's corresponding punch, and no superior armour bypass. Keep all moves/zero-damage skills. */
 for(i=0;i<ST_COMBAT_ATKS;i++){
  v=st_combat_raw(&g->atk[i])/2;if(st_combat_raw(&k->atk[i])>v)st_combat_write(&k->atk[i],v);
  if(k->atk[i].armorIgnore>g->atk[i].armorIgnore)k->atk[i].armorIgnore=g->atk[i].armorIgnore;
 }
 for(i=0;i<3;i++){if(k->super.damage[i]>old)old=k->super.damage[i];if(g->super.damage[i]>top)top=g->super.damage[i];}
 top/=2;
 for(i=0;i<BTL_KIBLAST_COUNT;i++){k->ki[i].damage=st_combat_scale(k->ki[i].damage,top,old);k->ki[i].guardDamage=st_combat_scale(k->ki[i].guardDamage,top,old);}
 for(i=0;i<3;i++){k->super.damage[i]=st_combat_scale(k->super.damage[i],top,old);k->super.guardDamage[i]=st_combat_scale(k->super.guardDamage[i],top,old);}
 for(i=0;i<2;i++){k->skill.damage[i]=st_combat_scale(k->skill.damage[i],top,old);k->skill.guardDamage[i]=st_combat_scale(k->skill.guardDamage[i],top,old);}
 /* Same power-gap flags as Goku; weaker defence, with native difficulty/stat curves still applied later. */
 {u32 own,donor;memcpy(&own,k->param.unk0+0x10,4);memcpy(&donor,g->param.unk0+0x10,4);donor=(donor&~0x1000u)|(own&0x1000u);memcpy(k->param.unk0+0x10,&donor,4);}
 k->param.damageTaken=g->param.damageTaken*1.25f;
 k->param.hitReactScale=g->param.hitReactScale;
}
extern int Port_StoryEraPower(int,int);
static void st_combat_era(StCombatData*s,int actor){
 int p=Port_StoryEraPower(actor,s->chara),i,j,n=0,positive[64],median,old=0,top;float scale;
 if(p<=0)return;scale=sqrtf((float)p/1200.f);if(scale<.2f)scale=.2f;if(scale>13)scale=13;
 for(i=0;i<64;i++){int d=st_combat_raw(s->atk+i);if(d>0)positive[n++]=d;}
 for(i=1;i<n;i++){int v=positive[i];for(j=i;j>0&&positive[j-1]>v;j--)positive[j]=positive[j-1];positive[j]=v;}
 median=n?positive[n/2]:1;top=(int)(220.f*scale);if(top<1)top=1;
 for(i=0;i<ST_COMBAT_ATKS;i++){
  int d=st_combat_raw(s->atk+i),v=st_combat_scale(d,top,median);if(v>top*4)v=top*4;st_combat_write(s->atk+i,v);
  if(s->atk[i].armorIgnore>1)s->atk[i].armorIgnore=1;
 }
 for(i=0;i<3;i++)if(s->super.damage[i]>old)old=s->super.damage[i];
 top=(int)(12000.f*scale);
 for(i=0;i<3;i++){int budget=(int)((i==0?6500.f:i==1?8500.f:12000.f)*scale),raw=s->super.damage[i];
  if(!(s->super.chargeScale[i]>=1.f)||s->super.chargeScale[i]>1.5f)s->super.chargeScale[i]=1.5f;
  if(raw>0){s->super.damage[i]=budget;s->super.guardDamage[i]=st_combat_scale(s->super.guardDamage[i],budget,raw);}}
 old=0;for(i=0;i<BTL_KIBLAST_COUNT;i++)if(s->ki[i].damage>old)old=s->ki[i].damage;
 top=(int)(1200.f*scale);for(i=0;i<BTL_KIBLAST_COUNT;i++){s->ki[i].damage=st_combat_scale(s->ki[i].damage,top,old);s->ki[i].guardDamage=st_combat_scale(s->ki[i].guardDamage,top,old);}
 old=0;for(i=0;i<2;i++)if(s->skill.damage[i]>old)old=s->skill.damage[i];
 top=(int)(5000.f*scale);for(i=0;i<2;i++){s->skill.damage[i]=st_combat_scale(s->skill.damage[i],top,old);s->skill.guardDamage[i]=st_combat_scale(s->skill.guardDamage[i],top,old);}
 s->param.damageTaken=1.f/sqrtf(scale);s->param.hitReactScale=1.f;
 /* Remove borrowed costume power-class/no-flinch ranks; native motions/effects remain. */
 {unsigned short cls=0;unsigned flags;memcpy(s->param.unk0,&cls,2);memcpy(&flags,s->param.unk0+0x10,4);flags&=~12u;memcpy(s->param.unk0+0x10,&flags,4);}
}
void Port_StoryCombatBind(void *fighter,int initial){
 BtlTechBChr*c=fighter;BtlTechBObj*o;StCombatData*s;int a,k,role=-1,g;
 if(!c||!Port_StoryActive()||Port_NetActive()||BattleReplay_IsActive())return;
 a=*(int*)c;if(a<0||a>=16)return;
 for(k=0;k<16;k++)if(Port_StoryCastActor(k)==a){role=k;break;}
 if(role<0)return;
 o=BtlChar_GetObj(c);if(!o||!o->param||!o->atk||!o->kiBlast||!o->super||!o->skill)return;
 if(!initial)return;{BtlTechBMember*m=BtlMember_GetActive(c);s=st_combat_form(a,m?m->chara:-1,1);}if(!s||s->valid)return;
 memcpy(&s->param,o->param,sizeof(s->param));memcpy(s->atk,o->atk,sizeof(s->atk));
 memcpy(s->ki,o->kiBlast,sizeof(s->ki));memcpy(&s->super,o->super,sizeof(s->super));memcpy(&s->skill,o->skill,sizeof(s->skill));s->valid=1;{BtlTechBMember*m=BtlMember_GetActive(c);s->chara=m?m->chara:-1;}
 /* CH3/CH7 Piccolo removed his cape, not his accepted Story combat profile.
    ISO22-1 ->22-0 differs only in Ki recovery and raw defence within these tables.
    Keep donor22-1 recovery; authored era defence below already ignores both raw values. */
 if(s->chara==22&&(Port_StoryChapter()==2||Port_StoryChapter()==6)){
  const int recovery[3]={20000,16624,8432};const int offsets[3]={0x28,0x30,0x34};int j;
  for(j=0;j<3;j++)memcpy(s->param.unk0+offsets[j],recovery+j,4);
 }
 st_combat_era(s,a);
 {extern int Port_StoryFormSlot(int);int target=Port_StoryFormSlot(s->chara),j,have=0;unsigned char*p=(unsigned char*)&s->param;
  if(target>=0&&target<255){for(j=0;j<4;j++)if(p[0x98+j]==target)have=1;
   if(!have){for(j=0;j<4&&p[0x98+j]!=255;j++);if(j==4)j=3;
    p[0x98+j]=target;p[0x9c+j]=1;p[0xa0+j]=0;p[0xa4+j]=(target==30||target==180)?3:2;p[0xa8+j]=255;}}}
 {extern char*getenv(const char*);extern void Port_Dbg3(const char*,int,int,int,int);static int log=-1;if(log<0)log=getenv("BT3_STORY_FORM_LOG")!=0;
  if(log){unsigned char*p=(unsigned char*)&s->param;int j;for(j=0;j<4;j++)Port_Dbg3("story form slot",s->chara,j|p[0x98+j]<<8,p[0xa4+j],p[0x9c+j]|p[0xa0+j]<<8);}}
 /* Saiyan-era Goku already knows flight; retain that across damaged costume reloads. */
 if(BtlMember_GetActive(c)&&BtlMember_GetActive(c)->chara==0){u32 flags;memcpy(&flags,s->param.unk0+0x10,4);flags&=~0x1000u;memcpy(s->param.unk0+0x10,&flags,4);}
 if(Port_StoryChapter()==1&&role==1){g=Port_StoryCastActor(0);if(g>=0&&g<16){StCombatData*base=st_combat_form(g,0,0);if(base)st_combat_krillin(s,base);}}
}
void *Port_StoryCombatTable(void *fighter,int kind,void *original){
 int a;StCombatData*s;
 if(!fighter||!Port_StoryActive()||Port_NetActive()||BattleReplay_IsActive())return original;
 a=*(int*)fighter;if(a<0||a>=16)return original;
 if(kind==3){extern int Port_StoryTechniqueDonor(int);int donor=Port_StoryTechniqueDonor(a);
  if(donor>=0&&donor<16){BtlTechBChr*d=BtlChar_Get(donor);BtlTechBObj*o=d?BtlChar_GetObj(d):0;
   BtlTechBMember*m=d?BtlMember_GetActive(d):0;StCombatData*saved=m?st_combat_form(donor,m->chara,0):0;
   if(saved)return &saved->super;if(o&&o->super)return o->super;}}
 {BtlTechBMember*m=BtlMember_GetActive((BtlTechBChr*)fighter);s=st_combat_form(a,m?m->chara:-1,0);}if(!s)return original;
 switch(kind){case 0:return &s->param;case 1:return s->atk;case 2:return s->ki;case 3:return &s->super;case 4:return &s->skill;default:return original;}
}
