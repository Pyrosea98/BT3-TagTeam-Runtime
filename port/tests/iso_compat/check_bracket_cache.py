import subprocess,tempfile
from pathlib import Path
r=Path.cwd();s=(r/'src/menu/menu_k_f.c').read_text();a=s.index('static void *sBracketPortraitRaw');z=s.index('\n#else',a);helper=s[a:z]
head='''#include <assert.h>
#include <stdlib.h>
typedef int s32;
#define LTOUR_ENTRANT_MAX 17
struct MTexRes{int value;};struct Entrant{int chara,costume;};struct LBracket{Entrant entrant[17];unsigned *chips;};
static MTexRes fallback[300];static int allocated,freed;
#define MPACK_AT(p,n) (&fallback[n])
void *Port_Beta8LoadSmallPortrait(int c,int k){if(c<165)return NULL;allocated++;return malloc(sizeof(MTexRes));}
void Port_Beta8FreeSmallPortrait(void *p){if(p){freed++;free(p);}}
void Res_RelocateOffsets(MTexRes**,void*,void*){}
'''
test='''int main(){unsigned chips[1]={291};LBracket b={};b.chips=chips;b.entrant[0]={165,0};b.entrant[1]={165,1};b.entrant[2]={165,0};
auto p=Port_BracketChip(&b,0);assert(p);assert(Port_BracketChip(&b,2)==p);assert(allocated==1);assert(Port_BracketChip(&b,1)!=p);assert(allocated==2);
b.entrant[3]={3,0};assert(Port_BracketChip(&b,3)==&fallback[4]);assert(allocated==2);
Port_BracketFreePortraits();assert(freed==2);Port_BracketFreePortraits();assert(freed==2);
for(int i=0;i<17;i++){b.entrant[i]={165+i,0};assert(Port_BracketChip(&b,i));}assert(allocated==19);
Port_BracketFreePortraits();assert(freed==19);}'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+helper+test);subprocess.run(['c++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual bracket cache: costume distinction, duplicate reuse,17slots, stock fallback, reset/free')
