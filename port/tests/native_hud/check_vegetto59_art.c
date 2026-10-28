#include "../../src/plat_beta8.c"
#include <assert.h>
void*Port_LowAlloc(size_t n){return malloc(n);}void Port_LowFree(void*p){free(p);}
int main(int argc,char**argv){assert(argc==2);setenv("BT3_DATA",argv[1],1);static B8PortraitRow rows[2]={{59,0,0x3000958,132471},{59,1,0x3000959,132471}};sPortraits=rows;sPortraitCount=2;
assert(Port_Beta8PortraitFileId(59,0)==0x43001997);assert(Port_Beta8PortraitFileId(59,1)==0x43000959);assert(Port_Beta8PortraitFileId(223,0)==0x43000959);assert(Port_Beta8PortraitResolve(59,0,123)==0x43001997);
void*p=Port_Beta8LoadChipBundle(59);assert(p);assert(((unsigned*)p)[0]==1);Port_Beta8FreeChipBundle(p);
p=Port_Beta8LoadSmallPortrait(59,0);assert(p);assert(((unsigned*)p)[0]==2);Port_Beta8FreeSmallPortrait(p);assert(Port_Beta8LoadSmallPortrait(59,1)==NULL);
char path[1024],backup[1032];snprintf(path,sizeof(path),"%s/mods/pak3us3/06551.bin",argv[1]);snprintf(backup,sizeof(backup),"%s.hide",path);assert(!rename(path,backup));assert(Port_Beta8PortraitFileId(59,0)==0x43000958);assert(!rename(backup,path));
printf("PASS actual new Vegetto59 artwork routes: valid native portrait, direct/form resolver, actual64x64 chip and two64x64 member bundle, GT223/costume1 untouched, missing portrait falls back to original\n");}
