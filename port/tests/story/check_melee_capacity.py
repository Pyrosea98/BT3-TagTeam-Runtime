from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/eft_det_a.c').read_text();a=s.index('s32 BtlBodyHit_Test(');level=0;start=s.index('{',a)
for i in range(start,len(s)):
 if s[i]=='{':level+=1
 elif s[i]=='}':
  level-=1
  if not level:fn=s[a:i+1];break
s=(r/'src/battle/btl_char_hit.c').read_text();a=s.index('        s32 other = 0;');b=s.index('\n#endif',a);consume=s[a:b]
code=r"""
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "port_body_contacts.h"
typedef int s32;typedef unsigned u32;
typedef struct{int atkBounds;unsigned hitFlags;}EftDetObjWork;typedef int EftDetPart;
typedef struct{int unkCAD,id;EftDetObjWork*work;EftDetPart*parts;int box;}EftDetObj;
unsigned int gPortBodyTouch[PORT_BODY_OBJECTS];
int ColBox_Overlaps(void*a,void*b){return 1;}int BtlBodyHit_TestVolumes(void*a,void*b){return 1;}
"""+fn+r"""
typedef struct{int objId,player;}HitChr;static HitChr fighters[32];
void Port_TargetHit(int a,int b){}int Port_IsEnemy(int a,int b){return a!=b;}int BtlChar_GetCount(void){return 32;}HitChr*BtlChar_Get(int i){assert(i>=0&&i<32);return fighters+i;}int BtlChar_TestFlag(HitChr*c,int f){return 0;}
int choose(HitChr*chr,int oppIdx){HitChr*opp=fighters+oppIdx;
"""+consume+r"""
return other<0?-1:oppIdx;}
int main(void){EftDetObjWork aw={0},bw={0};EftDetObj a={0,0,&aw,0,0},b={0,0,&bw,0,0};
 for(int i=0;i<32;i++)fighters[i]=(HitChr){i,i};
 for(int i=0;i<32;i++)for(int j=0;j<32;j++)if(i!=j){memset(gPortBodyTouch,0,sizeof(gPortBodyTouch));a.id=i;b.id=j;assert(BtlBodyHit_Test(&a,&b));assert(gPortBodyTouch[i]==(1u<<j));assert(choose(fighters+i,j)==j);int old=(j+1)%32;if(old==i)old=(old+1)%32;assert(choose(fighters+i,old)==j);}
 assert(!Port_BodyObjectBit(-1)&&!Port_BodyObjectBit(32));a.id=32;b.id=31;memset(gPortBodyTouch,0,sizeof(gPortBodyTouch));BtlBodyHit_Test(&a,&b);for(int i=0;i<32;i++)assert(!gPortBodyTouch[i]);
 puts("PASS actual native melee producer/consumer all32x32 object pairs, targeted and incidental contact, high bits16..31, invalid bounds; no shift/array UB");}
"""
assert 'gPortBodyTouch[12]' not in (r/'src/battle/eft_det_a.c').read_text()
assert 'Port_BodyObjectValid(id)' in (r/'src/battle/bobj_a.c').read_text()
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-std=gnu99','-DPORT','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Iport/include',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
