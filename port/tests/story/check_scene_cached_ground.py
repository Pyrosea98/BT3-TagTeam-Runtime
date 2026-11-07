from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/btl_char_mgr.c').read_text();i=s.index('void BtlChar_UpdateStage9(');b=s.index('{',i);e=b+1;n=1
while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
head=r"""
#include <assert.h>
#include <stdio.h>
#define PORT 1
#define BTL_CHR_STAGE_9 9
typedef struct{int side;}BtlMgrChr;typedef int BtlMgrObj;
static int owned,queries;static float y,draw;
BtlMgrObj*BtlChar_GetObj(BtlMgrChr*c){static int o;return &o;}int BtlChar_IsFrozen(BtlMgrChr*c){return 0;}
void BtlChar_SetStage(BtlMgrChr*c,int n){}int Port_StoryScenePlacementOwned(int a){return owned;}
void Port_StoryRestoreEndingPlacement(int a){}
void BtlColl_UpdateGround(BtlMgrChr*c){queries++;y=-64;}void BtlChar_PoseToObj(BtlMgrChr*c,int n){draw=y;}void BtlObjPose_CalcMatrices(BtlMgrObj*o){}
"""
foot=r"""
int main(){BtlMgrChr c={8};owned=1;y=0;BtlChar_UpdateStage9(&c);assert(!queries&&y==0&&draw==0);
 y=-20;BtlChar_UpdateStage9(&c);assert(!queries&&draw==-20);
 owned=0;BtlChar_UpdateStage9(&c);assert(queries==1&&draw==-64);
 puts("PASS actual native Stage9: held scene ignores cached old floor, kick height survives, free gameplay resumes ordinary ground collision");}
"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'ground.c';p.write_text(head+s[i:e]+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
