from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd();s=(r/'src/battle/btl_obj.c').read_text();start=s.index('static s32 sPortFixedModelRoom[3];');end=s.index('\n#endif',start);code=s[start:end]
head=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
using s32=int;using u8=unsigned char;
#define BTL_RES_FIXED_MAX 2
#define HEAP_ANY 2
struct BtlResFile{void*buf;int size,id;void*orig;};
struct BtlResWork{struct{BtlResFile file[3];}fixed[2];BtlResFile spare[3];};
static BtlResWork work;
static unsigned char initial[3][0xD0000],grown[3][0x200000];
static int count,freed,fail,roomGot;static void*lastFreed;
BtlResWork*BtlRes_GetWork(){return &work;}
unsigned Port_GameBigAlloc(int room){roomGot=room;if(fail)return 0;assert(count<3);return (unsigned)(uintptr_t)grown[count++];}
void*Heap_Alloc(int,int,int,int){return nullptr;}
void Heap_Free(void*p){lastFreed=p;freed++;}
'''
test=r'''
int main(){for(int i=0;i<3;i++){work.spare[i].buf=work.spare[i].orig=initial[i];sPortFixedModelRoom[i]=0xD0000;}
 for(int i=0;i<2;i++)work.fixed[i].file[0]=work.spare[i];
 auto*f=&work.spare[2];f->size=873265;
 assert(Port_ResFitFixedModel(f));assert(roomGot==874496&&freed==1&&lastFreed==initial[2]);assert(f->buf==grown[0]&&f->orig==grown[0]);
 /* Commit swaps only buf/size/id; orig retains physical ownership. */
 auto*active=&work.fixed[0].file[0];void*old=active->buf;active->buf=f->buf;f->buf=old;
 f->size=1100001;assert(Port_ResFitFixedModel(f));assert(f->buf==grown[1]&&work.spare[0].orig==grown[1]);assert(active->buf==grown[0]);
 active->size=1500001;assert(Port_ResFitFixedModel(active));assert(active->buf==grown[2]&&work.spare[2].orig==grown[2]);assert(freed==3);
 active->size=1500000;assert(Port_ResFitFixedModel(active)&&count==3);
 fail=1;auto saved=*f;f->size=1800000;saved=*f;assert(!Port_ResFitFixedModel(f)&&!memcmp(f,&saved,sizeof(saved))&&freed==3);
 BtlResFile heapFile={initial[1]+4,2000000,0,nullptr};assert(Port_ResFitFixedModel(&heapFile)&&heapFile.buf==initial[1]+4);
 puts("PASS actual fixed-model fit: sector-rounded growth, swapped physical ownership, initial/repeated loads, aliases, one free per replaced buffer, OOM preservation and unrelated heap isolation; ASan/UBSan");}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'test.cpp').write_text(head+code+test);subprocess.run(['g++','-no-pie','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
