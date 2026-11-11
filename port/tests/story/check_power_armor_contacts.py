from pathlib import Path
import re,subprocess,tempfile
s=Path('src/battle/btl_char_coll_b.c').read_text()
def function(name):
 m=re.search(r'^(?:s32|void) '+name+r'\(',s,re.M);start=m.start();i=s.index('{',start)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
names=['BtlColl_GuardBlast','BtlColl_GuardStrike','BtlColl_GuardRush','BtlColl_HitByBlast','BtlColl_HitByStrike','BtlColl_HitByRush','BtlColl_ApplyRushNoFlinch','BtlColl_ApplyRushHit']
protos=re.findall(r'extern\s+([^;{}]+?)\b([A-Za-z_]\w*)\(([^;{}]*?)\);',s)
head='#include <assert.h>\n#include <string.h>\n#include "common.h"\n#include "battle/btl_char_coll.h"\nstatic BtlCollChr chars[10];static BtlCollObj object;static BtlCollGauge gauge;static int tier,damage,pushes,catches,throws,react=5;\n'
special={
 'Port_PowerArmorTier':'assert(a==3&&b==7);return tier;',
 'BtlChar_Get':'return chars+i;', 'BtlChar_GetObj':'return &object;', 'BtlChar_GetPos':'return &chr->pose;', 'BtlChar_TestFlag':'return flag==3;', 'BtlMember_GetActiveGauge':'return &gauge;',
 'BtlMember_Damage':'damage+=damageArg;return damageArg;',
 'BtlMove_SetImpulseDir':'pushes++;',
 'BtlColl_GetGuardKind':'return 1;', 'BtlKiBlast_GetGuardDamageOfHit':'return 123;', 'BtlSkill_GetGuardDamage':'return 123;', 'BtlKiBlast_GetDamageOfHit':'return 123;', 'BtlSkill_GetDamage':'return 123;', 'BtlSuper_GetDamage':'return 123;',
 'BtlKiBlast_GetReactOfHit':'return react;', 'BtlSkill_GetReact':'return react;', 'BtlSuper_GetReact':'return react;',
}
# Reuse native declarations verbatim; override only engine/table boundaries used by the test.
custom={
 'Port_PowerArmorTier':'int Port_PowerArmorTier(int a,int b){assert(a==3&&b==7);return tier;}',
 'BtlChar_Get':'BtlCollChr *BtlChar_Get(int i){return chars+i;}',
 'BtlMember_Damage':'int BtlMember_Damage(BtlCollChr*c,int damageArg,int f){damage+=damageArg;return damageArg;}',
}
seen=set()
for ret,name,args in protos:
 if name in seen or name in names:continue
 seen.add(name);head+=f'extern {ret} {name}({args});\n'
 if name in custom:head+=custom[name]+'\n';continue
 body=special.get(name)
 if body is None:body='' if ret.strip()=='void' else 'return 0;'
 head+=f'{ret} {name}({args})'+'{'+body+'}\n'
head+='int BtlColl_StartThrow(BtlCollChr*a,BtlCollChr*b,int slot,u32 f){throws++;return 1;}\nint BtlColl_StartRushCatch(BtlCollChr*a,BtlCollChr*b,BtlCollHit*h,int r,float y){catches++;return 1;}\nvoid BtlColl_PlayHitSound(BtlCollChr*a,BtlCollHit*h,u32 k){}\nfloat BtlColl_GetHitDir(BtlCollChr*a,BtlCollHit*h,Vec4*v){memset(v,0,sizeof(*v));return 0;}\nint BtlColl_IsFacing(BtlCollChr*a,float y){return 0;}\n'
body='\n'.join(function(n)for n in names)+'\n'+function('BtlColl_StartThrow').replace('BtlColl_StartThrow(', 'Actual_StartThrow(',1)
foot=r'''
int main(void){BtlCollHit h={0};BtlCollOwner owner={0};BtlCollAtk atk={0};
 chars[3].side=3;chars[7].side=7;owner.side=3;h.owner=&owner;atk.side=3;atk.hits=1;h.atk=&atk;
 for(int t=1;t<=2;t++){tier=t;react=5;owner.slot=2;damage=pushes=0;assert(BtlColl_GuardBlast(chars+7,&h));assert(damage==123&&pushes==(t==2?0:1));damage=pushes=0;assert(BtlColl_GuardStrike(chars+7,&h));assert(damage==123&&pushes==(t==2?0:1));h.flags=0x10;damage=pushes=0;assert(BtlColl_GuardRush(chars+7,&h));assert(damage==123&&pushes==(t==2?0:1));h.flags=0;react=5;damage=pushes=0;memset(&chars[7].react,0,sizeof(chars[7].react));assert(BtlColl_HitByBlast(chars+7,&h));assert(damage==123);assert(chars[7].react.id==(t==2?2:5));assert(pushes==(t==2?0:1));
 damage=pushes=0;owner.slot=0;assert(BtlColl_HitByStrike(chars+7,&h));assert(damage==123);assert(chars[7].react.id==(t==2?2:5));assert(pushes==(t==2?0:1));
 for(int slot=2;slot<=4;slot++){owner.slot=slot;for(int r=0;r<4;r++){react=(int[]){5,0x1d,0x1e,0x22}[r];damage=catches=throws=pushes=0;assert(BtlColl_HitByRush(chars+7,&h));if(t==2){assert(damage==123&&chars[7].react.id==2&&!pushes&&!catches&&!throws);}else if(r==1||r==2)assert(catches==1);else if(r==3)assert(throws==1);}}
 }
 tier=2;memset(&chars[3].thr,0x5a,sizeof(chars[3].thr));memset(&chars[7].thr,0x5a,sizeof(chars[7].thr));BtlCollThrow before=chars[7].thr;assert(!Actual_StartThrow(chars+3,chars+7,4,0));assert(!memcmp(&before,&chars[7].thr,sizeof(before)));tier=1;chars[3].react.id=0;assert(Actual_StartThrow(chars+3,chars+7,4,0));assert(chars[7].react.id==BTL_REACT_CAUGHT);
 puts("PASS actual blast/strike/rush slot2..4 reactions: 50x damage preserved, no pushes/catch/throw; 5x leaves non-melee unchanged, actor3/7 indices");return 0;}
'''
# Header name is read from the actual declaration, not guessed.
print('Preparing actual collision functions')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(head+body+foot)
 subprocess.run(['cc','-std=gnu99','-DPORT','-w','-fsanitize=address,undefined','-Iinclude',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
