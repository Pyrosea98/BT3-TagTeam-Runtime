from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/btl_char_coll_b.c').read_text();a=s.index('s32 BtlColl_Hit(');i=s.index('{',a)+1;d=1
while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i]
head=r"""#include <assert.h>
#include <stddef.h>
#define PORT 1
#define BTL_HIT_BLAST 1
typedef int s32;typedef struct {int id;} React;typedef struct{React react;int side;}BtlCollChr;typedef struct{int slot;}Owner;typedef struct{int type,ownerId;Owner*owner;}BtlCollHit;
static BtlCollChr c;static int protects,calls,clashes;
BtlCollChr*BtlChar_FindByObjId(int x){return x<0?NULL:&c;}BtlCollChr*BtlChar_Get(int x){return &c;}
int Port_StoryShieldBystander(int side){assert(side==c.side);return protects;}
int BtlColl_HitByBlast(BtlCollChr*c,BtlCollHit*h){calls++;return 1;}int BtlColl_HitByStrike(BtlCollChr*c,BtlCollHit*h){calls++;return 1;}int BtlColl_HitByRush(BtlCollChr*c,BtlCollHit*h){calls++;return 1;}
void BtlChar_RaiseFirstClash(BtlCollChr*c){clashes++;}
"""
foot=r"""int main(void){BtlCollHit h={1,8,NULL};c.side=5;protects=1;assert(BtlColl_Hit(5,&h)==1&&!calls&&!clashes);protects=0;c.side=4;assert(BtlColl_Hit(4,&h)==1&&calls==1&&clashes==1);c.react.id=9;assert(BtlColl_Hit(4,&h)==1&&calls==1);assert(!BtlColl_Hit(-1,&h));}"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'hit';p.with_suffix('.c').write_text(head+body+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual native hit dispatcher: protected shield bystanders receive no reaction/resource action; real victim and ordinary hits retain native dispatch')
