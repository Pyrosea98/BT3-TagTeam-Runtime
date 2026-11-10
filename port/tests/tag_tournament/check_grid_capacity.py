from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'src/battle/view_a_e.c').read_text(encoding='utf-8');start=s.index('void ChrGrid_Build(');end=s.index('\n}\n',start)+3;producer=s[start:end]
max_promoted=int(re.search(r'sPromoted\[(\d+)\]',producer).group(1))
def block(text,start):
 at=text.index(start);depth=0
 for i in range(at,len(text)):
  if text[i]=='{':depth+=1
  if text[i]=='}':
   depth-=1
   if depth==0:return text[at:i+1]
entry=(r/'src/menu/menu_i_d.c').read_text(encoding='utf-8');bracket=(r/'src/menu/menu_k_b.c').read_text(encoding='utf-8')
e=block(entry,'if(gEntrySel->gridCount>0');b=block(bracket,'if(gBracket->gridCount>0')
assert 'TT_GridCapacity' in e and 'TT_GridCapacity' in b
assert 'if(sPortBracketGrid){Heap_Free(sPortBracketGrid);sPortBracketGrid=NULL;}'in bracket
h=r"""
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "tag_tournament_grid.h"
#define PORT 1
#define formCount count
#define CHRGRID_ALL 1
#define CHRGRID_NO_CUSTOM 2
#define CHRGRID_NO_RANDOM 4
#define CHRGRID_NO_FORMS 8
#define CHRGRID_ID_CUSTOM 163
#define CHRGRID_ID_RANDOM 161
#define CHRGRID_ID_EMPTY 164
#define CHRGRID_ID_LOCKED 162
#define CHRGRID_COLS 7
#define CHARA_UNLOCKED(id) 1
#define SAVE_REC_COUNT 1
typedef int s32;typedef struct TTGridCell ChrGridCell;typedef ChrGridCell ESelCell;
struct Progress{int mode,unk624;}progress={34,0},*gProgress=&progress;
struct Save{struct{int chara;}rec[1];}save,*gSaveData=&save;
static int promoted;static int ChrGrid_PromoteAdded(ChrGridCell*out,int cap,ChrGridCell*in,int count){
 (void)in;(void)count;assert(cap==promoted);for(int i=0;i<cap;i++){out[i].id=i%161;out[i].count=0;}return cap;
}
struct View{int gridCount,gridOutCount;ChrGridCell *grid;ChrGridCell gridBuf[165];unsigned canary;};
static struct View entry,bracket,*gEntrySel=&entry,*gBracket=&bracket;
static void *sPortTournamentGrid,*sPortBracketGrid;static int tag,allocated;
int Port_TagTourActive(void){return tag;}
void *Heap_Alloc(int size,int align,int a,int b){(void)align;(void)a;(void)b;allocated=size;return calloc(1,size);}
void Heap_Free(void*p){free(p);}
"""
main=r"""
int main(void){
 int counts[]={1,109,161,165,210,1024};promoted=PROMOTED;
 for(int t=0;t<2;t++)for(int n=0;n<6;n++)for(int retry=0;retry<3;retry++){
  tag=t;memset(&entry,0,sizeof(entry));memset(&bracket,0,sizeof(bracket));entry.canary=bracket.canary=0x12345678;
  entry.gridCount=bracket.gridCount=counts[n];build_entry();assert(entry.gridOutCount==168&&allocated>=168*(int)sizeof(ChrGridCell)&&entry.canary==0x12345678);Heap_Free(sPortTournamentGrid);sPortTournamentGrid=0;
  build_bracket();assert(bracket.gridOutCount==168&&allocated>=168*(int)sizeof(ChrGridCell)&&bracket.canary==0x12345678);Heap_Free(sPortBracketGrid);sPortBracketGrid=0;
 }
 puts("PASS actual tournament allocation blocks + native ChrGrid_Build:109 master ->165 promoted ->168 padded; normal/tag, large grids and retries preserve adjacent state");return 0;
}
""".replace('PROMOTED',str(max_promoted))
with tempfile.TemporaryDirectory()as d:
 p=Path(d);src=p/'grid.c';src.write_text(h+producer+'\nstatic void build_entry(void){'+e+'}\nstatic void build_bracket(void){'+b+'}\n'+main,encoding='utf-8')
 subprocess.run(['cc','-std=gnu99','-w','-fsanitize=address,undefined','-I'+str(r/'port/include'),str(src),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
