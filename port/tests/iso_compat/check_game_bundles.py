"""Compile actual bundle loaders; verify every custom face remains addressable by game32."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[3]
code=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include "plat_beta8.h"
static int allocations,frees;
void *Port_LowAlloc(size_t n){void *p=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(n<=16384);if(p==MAP_FAILED)return NULL;assert((uintptr_t)p<=UINT32_MAX-16384);allocations++;return p;}
void Port_LowFree(void *p){assert(!munmap(p,16384));frees++;}
int main(){int ids[]={212,214,215,216,218,219,220,221,222,223,224,225,226,229,230,234,235,238,239,241,244};
 for(int loop=0;loop<4;loop++)for(int n=0;n<21;n++){
  unsigned char *p=Port_Beta8LoadChipBundle(ids[n]);assert(p);uint32_t address=(uint32_t)(uintptr_t)p;assert((void*)(uintptr_t)address==p);assert(((uint32_t*)p)[0]==1&&((uint32_t*)p)[1]==8);
  uint32_t *entry=(uint32_t*)(p+32);assert(entry[0]*4+96+4096<=0x1560&&entry[1]*4+96+1024<=0x1560);
  int visible=0;for(int j=0;j<256;j++)visible+=p[entry[1]*4+96+j*4+3]!=0;assert(visible>0);Port_Beta8FreeChipBundle(p);
  p=Port_Beta8LoadSmallPortrait(ids[n],0);assert(p);assert((uintptr_t)p<=UINT32_MAX);assert(((uint32_t*)p)[0]==2);Port_Beta8FreeSmallPortrait(p);
 }
 assert(allocations==168&&frees==168);puts("PASS actual custom bundle loaders: all21 chips +21 small portraits x4 cycles, 32bit-address roundtrip, texture bounds/visible CLUT, matching low-memory frees");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['gcc','-std=gnu99','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(r/'port/include'),str(p/'test.c'),str(r/'port/src/plat_beta8.c'),'-Wl,--gc-sections','-o',str(p/'test')],check=True)
 env=dict(os.environ,BT3_DATA='/profile')
 subprocess.run([str(p/'test')],env=env,check=True)
