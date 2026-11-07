"""Actual native routing/form/stat/partner checks for the isolated v0.5 roster candidate."""
from pathlib import Path
import tempfile,struct,subprocess,os,json
r=Path.cwd()
with tempfile.TemporaryDirectory()as d:
 t=Path(d);g=t/'data';(g/'beta8').mkdir(parents=True)
 rows=[(134,0,0,0x30014f0,0x3001500,0x3001508,0,0),(145,0,0,0x30016a8,0x30016b8,0x30016c0,1,0),(3,0,0,0x3000078,0x3000088,0x3000090,2,0)]
 (g/'beta8/bundle-map.bin').write_bytes(struct.pack('<4sIII',b'B8RM',1,len(rows),32)+b''.join(struct.pack('<8I',*x)for x in rows));(g/'beta8/btlparam.bin').write_bytes(bytes(3*256))
 code=r'''
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
extern "C" {
#include "plat_beta8.h"
int Port_Setting(const char*,int d){return d;}
void *Port_LowAlloc(size_t n){return calloc(1,n);} void Port_LowFree(void*p){free(p);}
}
#include "v05_roster_rules.h"
int main(){
 assert(Port_Beta8FileId(213,0,0,0)==0x430018b2);
 assert(Port_Beta8FileId(245,0,0,0)==0x430018b3);
 assert(Port_Beta8FileId(247,0,0,0)==0x43001a2c);
 assert(Port_Beta8FileId(213,0,0,1)==0x43001500);
 assert(Port_Beta8FileId(245,0,0,2)==0x430016c0);
 assert(Port_Beta8FileId(247,0,0,1)==0x43000088);
 assert(Port_Beta8StockAlias(245)==134);
 assert(Port_Beta8HasParam(213)&&Port_Beta8HasCharacter(247));
 int f[7];assert(Port_Beta8FormList(213,f,7)==2&&f[0]==213&&f[1]==245);
 assert(Port_Beta8IsTransform(213,245)&&Port_Beta8IsTransform(245,213));
 assert(!Port_Beta8IsTransform(213,52)&&Port_Beta8IsAnyTransform(245));
 assert(Port_Beta8FusionResult(3,0,23,0,51)==247);
 assert(Port_Beta8FusionResult(23,0,3,0,51)==247);
 assert(Port_Beta8FusionResult(3,0,34,0,51)==51);
 assert(Port_Beta8FusionResult(3,0,23,0,110)==110);
 int costume=3;assert(Port_Beta8CostumeCount(110,&costume)==3&&costume==0);
 for(int cid: {213,245,247}){costume=4;assert(Port_Beta8CostumeCount(cid,&costume)==1&&costume==0);}
 unsigned char p[256];memset(p,255,sizeof(p));assert(Port_Beta8PatchFormParam(213,p));assert(p[0x98]==245&&p[0xA4]==2);
 assert(Port_Beta8PatchFormParam(245,p));assert(p[0x9B]==213&&p[0xA7]==1);
 int stats[291][4]={};Port_Beta8ApplyMemberStats(stats,291);
 assert(stats[213][0]==225000&&stats[245][0]==450000&&stats[247][0]==350000);
 for(int costumes=0;costumes<8;costumes++){
  memset(p,255,sizeof(p));for(int j=0;j<4;j++)p[0xBA + j]=34;p[0xC2]=21;p[0xB6]=214;
  V05GokoroRecipes(3,p);int hasVegeta=0,hasPiccolo=0;
  for(int j=0;j<4;j++){hasVegeta|=p[0xBA + j]==34;hasPiccolo|=p[0xBA + j]==23;}
  assert(hasVegeta&&hasPiccolo&&p[0xC2]==21);for(int j=0;j<4;j++)assert(p[0xBE + j]!=23);
  unsigned char copy[256];memcpy(copy,p,256);V05GokoroRecipes(3,p);assert(!memcmp(copy,p,256));
 }
 memset(p,255,256);V05GokoroRecipes(3,p);assert(p[0xB6]==247&&p[0xB9]==23);
 memset(p,255,256);V05GokoroRecipes(23,p);assert(p[0xB3]==2&&p[0xB6]==247&&p[0xB9]==3&&p[0xC2]==3);
 V05GokoroRecipes(247,p);for(int j=0;j<4;j++)assert(p[0x98+j]==255);for(int j=0;j<3;j++)assert(p[0xB4+j]==255);
 puts("PASS actual v0.5 routing, paired forms/transform bits, permanent Potara result, costs/host isolation, partner admission/idempotence and calibrated HP; ASan/UBSan");
}
'''
 (t/'test.cpp').write_text('#include <initializer_list>\n'+code)
 o=t/'beta8.o';subprocess.run(['gcc','-std=gnu99','-g','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Iport/include','-Iport/src','-c','port/src/plat_beta8.c','-o',str(o)],check=True)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-Iport/include',str(t/'test.cpp'),str(o),'-Wl,--gc-sections','-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],env=dict(os.environ,BT3_DATA=str(g)),check=True)
