from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_scene.c').read_text()
start=s.index('s32 *BtlScene_GetCharPackEntry(s32 side, s32 idx) {')
end=s.index('\n}',start)+2
code=r'''#include <assert.h>
#include <stdio.h>
typedef int s32;typedef unsigned u32;typedef unsigned char u8;
static s32 bank[4][128];static int borrowing;
int Port_StoryTechniqueDonor(int actor){return borrowing&&actor==3?0:-1;}
s32*BtlCharApi_GetPlayerObjUnk58(int actor){return actor>=0&&actor<4?bank[actor]:0;}
#define PORT 1
'''+s[start:end]+r'''
int main(){for(int a=0;a<4;a++)for(int i=0;i<12;i++)bank[a][i]=64+i*4;
 for(int i=1;i<=11;i++){assert(BtlScene_GetCharPackEntry(3,i)==bank[3]+16+i);borrowing=1;
 assert(BtlScene_GetCharPackEntry(3,i)==bank[0]+16+i);
 assert(BtlScene_GetCharPackEntry(2,i)==bank[2]+16+i);
 assert(BtlScene_GetCharPackEntry(0,i)==bank[0]+16+i);borrowing=0;}
 borrowing=1;assert(BtlScene_GetCharPackEntry(3,0)==bank[3]+16);
 borrowing=0;assert(BtlScene_GetCharPackEntry(3,8)==bank[3]+24);
 puts("PASS actual native effect-pack lookup: borrowed Goku technique bank, Krilin actor retained, scoped restoration and other actors unchanged");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
