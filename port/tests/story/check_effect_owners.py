"""Exercise the real reset traversal: every one of sixteen owners is isolated."""
from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/eft_ae.c').read_text();start=s.index('void BtlTaskList_Reset(');brace=s.index('{',start);end=brace+1;depth=1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
body=s[start:end]
code=r'''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include "battle/eft_owner.h"
typedef uint32_t u32;typedef int s32;
typedef struct BtlTask BtlTask;typedef struct BtlTaskList BtlTaskList;
typedef struct Cls {void (*reset)(BtlTask*);} Cls;
struct BtlTask {u32 tag;Cls*cls;BtlTask*next;BtlTaskList*child;int dead;int resets;};
struct BtlTaskList {BtlTask*head;};
#define BTL_TASK_BITS(x) (x)
void BtlTask_Kill(BtlTask*t){assert(!"Unexpected kill");}
void reset(BtlTask*t){t->resets++;}
'''+body+r'''
int main(void){BtlTask a[16]={0};Cls c={reset};BtlTaskList l={a};
for(int i=0;i<16;i++){a[i].tag=EFT_OWNER_TAG(i)|0x7c1ff;a[i].cls=&c;a[i].next=i<15?a+i+1:0;for(int j=0;j<i;j++)assert(EFT_OWNER_TAG(i)!=EFT_OWNER_TAG(j));}
for(int i=0;i<16;i++){int mode=EFT_OWNER_RESET(i);assert(mode==(i<2?i+2:i+3));u32 tag=mode==2?0x800:mode==3?0x1000:EFT_OWNER_TAG(mode-3);BtlTaskList_Reset(&l,tag);for(int j=0;j<16;j++)assert(a[j].resets==(j<=i));}
BtlTaskList_Reset(&l,0);for(int i=0;i<16;i++)assert(a[i].resets==2);
return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu99','-DPORT','-fsanitize=address,undefined','-Iinclude',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual task reset isolates all16 owners, preserves collision flags and global reset')
