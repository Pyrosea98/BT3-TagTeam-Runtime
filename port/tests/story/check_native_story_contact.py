from pathlib import Path
import tempfile,subprocess
s=Path('src/battle/btl_char_coll_b.c').read_text();start=s.index('void BtlColl_ApplyRushNoFlinch(');b=s.index('{',start);end=b+1;n=1
while n:n+=(s[end]=='{')-(s[end]=='}');end+=1
code=r'''#include <assert.h>
#include "common.h"
#include "battle/btl_char_coll.h"
static int damage,contacts;
void BtlChar_SetFxBit(BtlCollChr*c,s32 b){}
void BtlColl_PlayHitSound(BtlCollChr*c,BtlCollHit*h,u32 k){}
s32 BtlSuper_GetHitSound(BtlCollChr*c,s32 s,s32 a){return 0;}
s32 BtlSuper_GetDamage(BtlCollChr*c,s32 s,s32 a,s32 b){return 123;}
s32 BtlSuper_GetRecoilDamage(BtlCollChr*c,s32 s){return 0;}
s32 BtlMember_Damage(BtlCollChr*c,s32 d,s32 f){assert(c->side==15&&d==123);damage++;return 0;}
void BtlChar_SetFlag(BtlCollChr*c,s32 b){}
void BtlCharApi_ShakeCamsNear(Vec4*p,f32 a,f32 b,f32 c,f32 d){}
void BtlCharApi_RumbleNear(Vec4*p,f32 a,f32 b,f32 c,f32 d){}
void Port_StoryAttackContact(int by,int at,int slot){assert(damage==contacts+1);assert(by==12&&at==15&&slot==0x102+contacts);contacts++;}
'''+s[start:end]+r'''
int main(void){BtlCollChr a={0},v={0};BtlCollOwner o={0};BtlCollHit h={0};a.side=12;v.side=15;h.owner=&o;for(int i=2;i<=4;i++){o.slot=i;BtlColl_ApplyRushNoFlinch(&v,&a,&h,0,1);}assert(contacts==3);return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.c').write_text(code)
 subprocess.run(['cc','-std=gnu99','-DPORT','-fsanitize=address,undefined','-Iinclude',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('PASS actual native super-hit producer: damage precedes owned slot2/3/4 contact, actors12/15 preserved')

# The cinematic rush queue is a distinct actual damage producer, not a projectile collision.
s=Path('src/battle/btl_char_member.c').read_text();start=s.index('static s32 Port_StoryQueuedHit(');b=s.index('{',start);end=b+1;n=1
while n:n+=(s[end]=='{')-(s[end]=='}');end+=1
code=r"""#include <assert.h>
typedef int s32;typedef struct{int side;}BtlMemberChr;static int hits,contacts,lost=7;
int BtlMember_Damage(BtlMemberChr*v,int d,int f){hits++;return lost;}
int Port_AssistQueuedDamage(BtlMemberChr*s,int d,int f){return d;}
void Port_StoryAttackContact(int by,int at,int kind){assert(hits>contacts);assert(by==12&&at==15&&kind==0x102+contacts);contacts++;}
"""+s[start:end]+r"""
int main(){BtlMemberChr a={12},b={15};int f[3]={0x800180,0x1000180,0x2000180};for(int i=0;i<3;i++)Port_StoryQueuedHit(&a,&b,100,f[i]);assert(contacts==3);lost=0;Port_StoryQueuedHit(&a,&b,100,f[2]);assert(contacts==3);lost=7;Port_StoryQueuedHit(&a,&b,100,0x180);assert(contacts==3);}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'q.c').write_text(code);subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined',str(p/'q.c'),'-o',str(p/'q')],check=True);subprocess.run([str(p/'q')],check=True)
print('PASS actual cinematic queue damage producer: real positive damage before exact-slot Story contact; zero damage/non-tech queues cannot advance Story')
