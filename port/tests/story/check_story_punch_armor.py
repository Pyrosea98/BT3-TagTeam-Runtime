"""Juan 2026-11-05: rivals take listed allies' blows without reacting; damage is unchanged.

Compiles the ACTUAL native BtlHit_ApplyHit (src/battle/btl_char_hit.c), the ACTUAL Story policy table
(port/include/mod_story.h stPunchArmor / ST_PunchArmor) and the ACTUAL runtime gate
(port/include/mod_story_scene.inc Port_StoryPunchArmor). Only engine boundaries are stubbed: the attack record
readers, the fighter flags and the draft order (cast -> battle actor is a permutation, as in a real draft).
"""
from pathlib import Path
import subprocess, tempfile

r = Path.cwd()
hit = (r / 'src/battle/btl_char_hit.c').read_text()
scene = (r / 'port/include/mod_story_scene.inc').read_text()


def function(text, head):
    start = text.index(head)
    i = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[start:i]


externs = hit[hit.index('/* Fighter accessors and flags. */'):hit.index('/* 1 when the attacker')]
apply_hit = function(hit, 'void BtlHit_ApplyHit(HitChr *atk) {')
gate = function(scene, 'static int st_punch_armor(int by,int at){') + '\n' + function(scene, 'int Port_StoryPunchArmor(int by,int at,int kind,int react,int byChara){')

code = r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_damage_safe.h"
#include "common.h"
#include "battle/btl_char_hit.h"
#include "mod_story.h"
''' + externs + r'''
static HitChr chars[16];static HitObj objs[16];
static int kind=0x2,flags=0,reaction=5,damage=1000,chapter=-1,armorWord=0;
static int damageCalls,damageTaken,voices,pushes,contacts,launchFlags,e18[16];
static int castToActor[ST_ACTORS];
/* Story state the gate reads (mod_story_scene.inc). */
static struct {int active;} sStRun;static int sStAtkFrames,sStAtkBy=-1,sStAtkBack=-1;
static int st_chapter(void){return chapter;}
void Port_Dbg3(const char*s,int a,int b,int c,int d){}
int Port_StoryCastActor(int cast){return chapter>=0&&cast>=0&&cast<stCasts[chapter].count?castToActor[cast]:-1;}
''' + gate + r'''
static HitMember members[16];HitMember*BtlMember_GetActive(HitChr*c){return members+c->player;}
s32 BtlChar_GetCount(void){return 16;}
HitChr*BtlChar_Get(s32 i){return chars+i;}
HitPose*BtlChar_GetPos(HitChr*c){return &c->pose;}
HitObj*BtlChar_GetObj(HitChr*c){return objs+c->player;}
s32 BtlChar_IsFree(HitChr*c){return 1;}
s32 BtlChar_TestMemberUnk70(HitChr*c){return 0;}
s32 BtlChar_TestFlag(HitChr*c,s32 f){return 0;}
void BtlChar_SetFlag(HitChr*c,s32 f){if(f==0x44)launchFlags++;}
void BtlChar_SetHeldFlag(HitChr*c,s32 f){}
void BtlChar_ClearFlag(HitChr*c,s32 f){}
void BtlChar_SetFxBit(HitChr*c,s32 b){}
s32 BtlChar_FrameMod(s32 n){return 0;}
void BtlChar_PlayVoice(HitChr*c,s32 k){voices++;}
void BtlChar_Vibrate(HitChr*c,f32 p,f32 s){}
void BtlChar_AddStageTimer(f32 s){}
void BtlChar_RaiseFirstClash(HitChr*c){}
void ChrCam_AddShake(HitChr*c,f32 p,f32 s){}
f32 BtlOpp_GetYaw(HitChr*c){return 0;}
void BtlMove_SetImpulseYaw(HitChr*c,f32 y,f32 s){pushes++;}
void BtlMove_AimVerticalAtOpponent(HitChr*c,s32 a){}
s32 BtlAnim_GetId(HitChr*c){return 0;}
u32 BtlAnim_GetFlags(s32 id){return 0;}
s32 BtlAct_GetCurrent(HitChr*c){return 0;}
s32 BtlAct_TestPoweredSkill(HitChr*c,s32 m){return 0;}
void BtlMember_Damage(HitChr*c,s32 a,s32 f){damageCalls++;damageTaken+=a;}
void BtlMember_AddKi(HitChr*c,s32 a){}
s32 BtlMember_SpendKi(HitChr*c,s32 a,s32 f){return 1;}
s32 BtlMember_HasAbility(HitChr*c,s32 a){return 0;}
s32 BtlAtk_GetId(HitChr*c){return kind;}
s32 BtlAtk_GetFlags(HitChr*c){return flags;}
s32 BtlAtk_GetDamage(HitChr*c){return damage;}
s32 BtlAtk_GetKiGain(HitChr*c){return 0;}
s32 BtlAtk_GetUnk10(HitChr*c){return 0;}
s32 BtlAtk_GetReaction(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionB(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionC(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionD(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionE(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionF(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionG(HitChr*c){return reaction;}
s32 BtlAtk_GetReactionSub(HitChr*c){return 0;}
f32 BtlAtk_GetPushOnHit(HitChr*c){return 1;}
f32 BtlAtk_GetLaunchAngleA(HitChr*c){return 0;}
f32 BtlAtk_GetLaunchAngleB(HitChr*c){return 0;}
f32 BtlAtk_GetShakePower(HitChr*c){return 1;}
f32 BtlAtk_GetShakeTime(HitChr*c){return 1;}
s32 BtlAtk_GetHitFxKind(HitChr*c){return 1;}
s32 BtlAtk_GetUnk2C(HitChr*c){return 0;}
s32 BtlParam_GetFlags(HitChr*c){return c->side==armorWord?0:0;}
f32 BtlParam_GetHitReactScale(HitChr*c){return 1;}
s32 BtlObjAnim_QueryEvent(HitObj*o,u64 a,s32 b,s32 c){return 0;}
f32 BtlUtil_WrapAngle(f32 a){return a;}
s32 BtlUtil_Clamp(s32 v,s32 lo,s32 hi){return v<lo?lo:v>hi?hi:v;}
s32 BtlHit_IsFromBehind(HitChr*a,HitChr*d){return 0;}
s32 BtlHit_GetVoiceKind(s32 r){return 0;}
void Port_StoryAttackContact(int by,int at,int k){contacts++;}
''' + apply_hit + r'''
/* One hit of cast `by` on cast `at`: returns the defender's reaction; checks damage and contact always land. */
static int strike(int ch,int by,int at){
 int a,d,i;chapter=ch;
 for(i=0;i<ST_ACTORS;i++)castToActor[i]=(i*5+3)%16; /* draft order differs from cast order */
 a=ch>=0?castToActor[by]:by;d=ch>=0?castToActor[at]:at;
 memset(chars,0,sizeof(chars));for(i=0;i<16;i++){chars[i].player=i;chars[i].side=i<2?i:(i&1);chars[i].unkE18=e18[i];} /* real layout: side is the view/team for fighters 2+ */
 damageCalls=damageTaken=voices=pushes=contacts=launchFlags=0;
 chars[a].status.target=d;BtlHit_ApplyHit(&chars[a]);
 assert(contacts==1);
 if(chars[d].react.reaction==2){assert(damageCalls==1&&damageTaken==damage&&!voices&&!pushes&&!launchFlags&&chars[d].react.unk38==3);}
 return chars[d].react.reaction;
}
static int cast_of(int ch,const char*name,int team){int i;for(i=0;i<stCasts[ch].count;i++)if(!strcmp(stCasts[ch].actor[i].name,name)&&stCasts[ch].actor[i].team==team)return i;assert(!"cast");return -1;}
static unsigned names(int ch,const char*const*list,int team){unsigned m=0;for(;*list;list++)m|=1u<<cast_of(ch,*list,team);return m;}
int main(void){
 /* Index -> displayed chapter number, and Juan's list by NAME rather than by bit. */
 static const struct {int index,shown;const char*by[8];const char*at[2];} juan[]={
  {0,1,{"Piccolo"},{"Raditz"}},
  {1,2,{"Goku","Krillin"},{"Raditz"}},
  {2,3,{"Goku","Piccolo"},{"Raditz"}},
  {3,4,{"Piccolo"},{"Gohan"}},
  {4,5,{"Gohan"},{"Piccolo"}},
  {6,7,{"Tenshinhan","Yamcha","Krillin","Piccolo","Gohan","Chaozu"},{"Nappa"}},
  {11,8,{"Gohan","Krillin","Yajirobe"},{"Vegeta"}}};
 assert(stCasts[11].count==5); /* no duplicate Goku / projectile actor */
 unsigned j,k;int ch,by,at;
 assert(sizeof(stPunchArmor)/sizeof(stPunchArmor[0])==sizeof(juan)/sizeof(juan[0]));
 for(j=0;j<sizeof(juan)/sizeof(juan[0]);j++){
  ch=juan[j].index;assert(stChapters[ch].saga==0&&stChapters[ch].sequence==juan[j].shown);
  for(k=0;k<sizeof(stPunchArmor)/sizeof(stPunchArmor[0]);k++)if(stPunchArmor[k].chapter==ch){
   assert(stPunchArmor[k].attackers==names(ch,juan[j].by,0));assert(stPunchArmor[k].rivals==names(ch,juan[j].at,1));}
 }
 /* CH7/CH8: every ally except Goku; CH6 untouched. */
 for(by=0;by<stCasts[6].count;by++)if(stCasts[6].actor[by].team==0)assert(ST_PunchArmor(6,by,8)==(strcmp(stCasts[6].actor[by].name,"Goku")!=0));
 for(by=0;by<stCasts[11].count;by++)if(stCasts[11].actor[by].team==0)assert(ST_PunchArmor(11,by,4)==(strcmp(stCasts[11].actor[by].name,"Goku")!=0));
 for(by=0;by<ST_ACTORS;by++)for(at=0;at<ST_ACTORS;at++)assert(!ST_PunchArmor(5,by,at));

 /* Actual native resolver. Ordinary hit (reaction5) and launching smash (0x14). */
 for(j=0;j<sizeof(juan)/sizeof(juan[0]);j++){ch=juan[j].index;
  for(by=0;by<stCasts[ch].count;by++)for(at=0;at<stCasts[ch].count;at++){
   int listed=ST_PunchArmor(ch,by,at);if(by==at)continue;
   reaction=5;kind=2;assert(strike(ch,by,at)==(listed?2:5));
   reaction=0x14;kind=0x13;{int got=strike(ch,by,at);assert(got==(listed?2:0x14));
    if(!listed)assert(launchFlags&&chars[castToActor[at]].react.damage==damage&&!damageCalls); /* native deferred launch damage */}
  }}
 /* Named spot checks. */
 reaction=5;kind=2;
 assert(strike(6,cast_of(6,"Goku",0),cast_of(6,"Nappa",1))==5);
 assert(strike(6,cast_of(6,"Yamcha",0),cast_of(6,"Nappa",1))==2);
 assert(strike(6,cast_of(6,"Yamcha",0),cast_of(6,"Saibaman",1))==5);   /* Saibamen still react */
 assert(strike(6,cast_of(6,"Nappa",1),cast_of(6,"Piccolo",0))==5);     /* the rival's blows unchanged */
 assert(strike(11,cast_of(11,"Goku",0),cast_of(11,"Vegeta",1))==5);
 assert(strike(11,cast_of(11,"Krillin",0),cast_of(11,"Vegeta",1))==2);
 assert(strike(2,cast_of(2,"Gohan",1),cast_of(2,"Raditz",1))==5);     /* CH3 headbutt Gohan is not listed */
 assert(strike(5,cast_of(5,"Krillin",0),cast_of(5,"Krillin's double",1))==5); /* CH6 unchanged */
 /* Damage parity: the same blow deals the same health with or without armour. */
 {int plain,armour;assert(strike(6,cast_of(6,"Goku",0),cast_of(6,"Nappa",1))==5);plain=damageTaken;
  assert(strike(6,cast_of(6,"Krillin",0),cast_of(6,"Nappa",1))==2);armour=damageTaken;assert(plain==armour&&armour==damage);}
 /* CH8 (Juan): the Great Ape Gohan (form 180) makes Vegeta flinch; Krillin never makes Vegeta or the ape Vegeta flinch. */
 {int goh=cast_of(11,"Gohan",0),kri=cast_of(11,"Krillin",0),veg=cast_of(11,"Vegeta",1);int i;for(i=0;i<16;i++)members[i].chara=-1;
  for(i=0;i<16;i++)members[i].chara=13;assert(strike(11,goh,veg)==2);
  for(i=0;i<16;i++)members[i].chara=180;assert(strike(11,goh,veg)==5);
  for(i=0;i<16;i++)members[i].chara=25;assert(strike(11,kri,veg)==2);for(i=0;i<16;i++)members[i].chara=0;}
 /* Grabs/throws keep their paired native motions. */
 kind=0x55;assert(strike(0,0,1)==5);kind=0x56;assert(strike(0,0,1)==5);kind=2;
 /* Authored attacks and cutscenes keep the reactions their staging needs. */
 chapter=0;sStAtkFrames=10;sStAtkBy=castToActor[0];assert(strike(0,0,1)==5);
 sStAtkBy=-1;sStAtkBack=castToActor[0];assert(strike(0,0,1)==5);sStAtkBack=-1;sStAtkFrames=0;
 sStRun.active=1;assert(strike(0,0,1)==5);sStRun.active=0;
 assert(strike(0,0,1)==2);
 /* Outside Story (st_chapter -1): ordinary modes untouched. */
 assert(strike(-1,0,1)==5);
 /* A native no-flinch already chosen stays native (no double handling). */
 e18[castToActor[1]]=1;chapter=0;assert(strike(0,0,1)==2);e18[castToActor[1]]=0;
 puts("PASS actual native melee resolver + Story policy: CH1-5/7/8 listed allies' blows deal full damage with native no-flinch on the rival; Goku in CH7/CH8, other enemies, CH6, throws, authored attacks, cutscenes and non-Story modes keep native reactions");
 return 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / 't.c').write_text(code)
    subprocess.run(['cc', '-std=gnu99', '-DPORT', '-w', '-fsanitize=address,undefined', '-Iinclude', '-Iport/include',
                    str(p / 't.c'), '-lm', '-o', str(p / 't')], check=True)
    subprocess.run([str(p / 't')], check=True)
