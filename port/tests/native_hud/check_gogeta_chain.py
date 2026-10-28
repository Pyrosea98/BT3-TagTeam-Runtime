from pathlib import Path
r=Path.cwd()
src=(r/'port/src/plat_beta8.c').read_text()
a=src.index('typedef struct B8FormParam {')
b=src.index('int Port_Beta8FormTarget')
body=src[a:b]
head=r'''#include <cassert>
#include <cstring>
#include <cstdio>
'''
foot=r'''static unsigned char *fresh(){static unsigned char p[0x100];memset(p,0x77,sizeof(p));return p;}
int main(){
 /* Juan 2026-10-27: every Gogeta form must offer SSJ4 (54), like the Gogetto family offers 230. */
 unsigned char *p=fresh();
 assert(Port_Beta8PatchFormParam(110,p)==1);
 assert(p[0x98]==53&&p[0x99]==249&&p[0x9A]==172&&p[0x9B]==54);
 assert(p[0x9C]==0&&p[0x9D]==0&&p[0x9E]==0&&p[0x9F]==1); /* SSJ4 costs one blast, the ascents are free */
 assert(p[0xA0]==0&&p[0xA1]==0&&p[0xA2]==0&&p[0xA3]==0);
 assert(p[0xA4]==2&&p[0xA5]==2&&p[0xA6]==2&&p[0xA7]==2);
 assert(p[0xAC]==0&&p[3]==0);
 p=fresh();
 assert(Port_Beta8PatchFormParam(53,p)==1);
 assert(p[0x98]==249&&p[0x99]==172&&p[0x9A]==54&&p[0x9B]==110);
 assert(p[0x9C]==0&&p[0x9D]==0&&p[0x9E]==1&&p[0x9F]==0);
 assert(p[0xA0]==0&&p[0xA3]==1&&p[0xA4]==2&&p[0xA5]==2&&p[0xA6]==2&&p[0xA7]==1);
 p=fresh();
 assert(Port_Beta8PatchFormParam(249,p)==1);
 assert(p[0x98]==172&&p[0x99]==54&&p[0x9A]==255&&p[0x9B]==110);
 assert(p[0x9C]==0&&p[0x9D]==1&&p[0x9E]==0&&p[0x9F]==0);
 assert(p[0xA4]==2&&p[0xA5]==2&&p[0xA6]==0&&p[0xA7]==1&&p[0xA3]==1);
 p=fresh();
 assert(Port_Beta8PatchFormParam(172,p)==1);
 assert(p[0x98]==54&&p[0x99]==255&&p[0x9A]==255&&p[0x9B]==110&&p[0x9C]==1&&p[0xAC]==0);
 p=fresh();
 assert(Port_Beta8PatchFormParam(54,p)==1);
 assert(p[0x98]==255&&p[0x99]==255&&p[0x9A]==255&&p[0x9B]==110);
 assert(p[0x9C]==0&&p[0x9D]==0&&p[0x9E]==0&&p[0x9F]==0);
 assert(p[0xA4]==0&&p[0xA5]==0&&p[0xA6]==0&&p[0xA7]==1&&p[0xA3]==1);
 assert(p[0xAC]==255); /* SSJ4 has no default: revert needs the explicit slot, like 230/59 */
 /* The Gogetto family and stock rows are untouched by this change. */
 p=fresh();
 assert(Port_Beta8PatchFormParam(224,p)==1);
 assert(p[0x98]==225&&p[0x99]==226&&p[0x9A]==229&&p[0x9B]==230&&p[3]==5&&p[0]==1&&p[0xAD]==0&&p[0x9F]==1);
 p=fresh();
 assert(Port_Beta8PatchFormParam(36,p)==0); /* stock non-family character: every sentinel byte kept */
 assert(p[0x98]==0x77&&p[0x9B]==0x77&&p[0xAC]==0x77&&p[3]==0x77&&p[0]==0x77);
 /* Invariant: the new 54 links each occupy exactly one slot with cost 1 on their row. */
 {int c,k;const int fam[3]={110,53,249};for(k=0;k<3;k++){unsigned char *q=fresh();int n=0;int cc;int idx=0;
   Port_Beta8PatchFormParam(fam[k],q);for(cc=0;cc<4;cc++){if(q[0x98+cc]==54){n++;assert(q[0x9C+cc]==1);idx=cc;}}
   assert(n==1);assert(q[0xA4+idx]==2);}}
 puts("PASS actual Gogeta SSJ4 chain: 110/53/249 rows list 54 once with the one-blast cost and the ascend kind bits, 172 unchanged, 54 gains the back-to-base revert with the no-default top form, Gogetto family and stock rows untouched; ASan");
 return 0;}
'''
import subprocess,tempfile
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
