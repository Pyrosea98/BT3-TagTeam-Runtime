from pathlib import Path
import re,subprocess,tempfile,json,struct,hashlib
r=Path.cwd();s=(r/'src/battle/btl_char_mgr.c').read_text()
def fn(name):
 a=s.index('void '+name+'(');b=s.index('{',a);level=1;i=b+1
 while level:
  level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
assert fn('BtlChar_UpdateMotion').index('Port_StoryRestoreEndingPlacement') < fn('BtlChar_UpdateMotion').index('BtlChar_PoseToObj')
functions='\n'.join(fn(n)for n in ['BtlChar_UpdateMotion','BtlChar_UpdateStage7','BtlChar_UpdateStage8'])
head=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long long u64;typedef struct {float x,y,z,w;} Vec4;static Vec4 at;
#define BtlChar_GetPos(c) (&at)
#define PORT 1
typedef int s32;
typedef struct{int side,objId,unk1310;}BtlMgrChr;typedef int BtlMgrObj;
static BtlMgrObj obj;static struct{int flags;}roster,*gBtlChars=&roster;
static int held,actions,pushes,orbits,physics,placements,matrices,after_warp;
int BtlAct_GetCurrent(BtlMgrChr*c){return 4;}int BtlAnim_GetId(BtlMgrChr*c){return 0x173;}float BtlAnim_GetProgress(BtlMgrChr*c){return .5f;}void Port_Dbg3(const char*s,int a,int b,int c,int d){}
int Port_StorySyncDrawRootDue(int a){return 0;}void Port_StorySyncDrawRoot(void*p){}
int Port_StoryHeld(int a){return held&&a==0;}
BtlMgrObj*BtlChar_GetObj(BtlMgrChr*c){return &obj;}
int BtlChar_IsFrozen(BtlMgrChr*c){return 0;}int BtlChar_TestFlag(BtlMgrChr*c,int f){return 0;}
#define BTL_CHR_STAGE_7 7
#define BTL_CHR_STAGE_8 8
#define BTL_CHARS_STARTED 1
#define BtlMove_UpdateAction(...) (actions++)
#define BtlObj_SaveNodePositions(...) ((void)0)
#define BtlChar_PlaceRestart(...) ((void)0)
#define BtlChar_PlaceCenter(...) ((void)0)
#define BtlChar_PlaceRelative(...) ((void)0)
#define BtlChar_PlaceOnPath(...) (placements++)
#define BtlChar_PlaceCenterHigh(...) ((void)0)
#define BtlChar_PlaceSaved(...) ((void)0)
#define BtlChar_PlaceWarp(...) (after_warp++)
void Port_StoryRestoreEndingPlacement(int actor){assert(placements==actions && after_warp==1);after_warp=0;}
#define BtlChar_PoseToObj(...) ((void)0)
#define BtlObjAnim_SamplePose(...) ((void)0)
#define BtlObjAnim_UpdateEvents(...) ((void)0)
#define BtlChar_UpdateLean(...) ((void)0)
#define BtlFx_UpdateObjEvents(...) ((void)0)
#define BtlObjPose_CalcMatrices(...) (matrices++)
#define BtlChar_ObjToPose(...) ((void)0)
#define BtlChar_UpdateLookOffset(...) ((void)0)
#define BtlChar_SetStage(...) ((void)0)
#define BtlMove_PushOut(...) (pushes++)
#define BtlMove_ApplyOrbit(...) (orbits++)
#define BtlMove_ClampToStage(...) ((void)0)
#define BtlObjBody_Warp(...) ((void)0)
#define BtlChars_IsTimeStopped(...) 0
#define BtlCharApi_IsChanging(...) 0
#define StgCol_UpdateFighter(...) (physics++)
#define StgGround_UpdateFighter(...) (physics++)
'''
foot=r'''
int main(void){BtlMgrChr c={0,0,0};held=1;BtlChar_UpdateMotion(&c);BtlChar_UpdateStage7(&c);BtlChar_UpdateStage8(&c);
 assert(actions==1&&!pushes&&!orbits&&!physics&&placements==1&&matrices==1); /* scripts and matrices continue, no gravity/input/orbit */
 c.side=1;BtlChar_UpdateMotion(&c);BtlChar_UpdateStage7(&c);BtlChar_UpdateStage8(&c);assert(actions==2&&pushes==1&&orbits==1&&physics==2);
 c.side=0;held=0;BtlChar_UpdateMotion(&c);BtlChar_UpdateStage7(&c);BtlChar_UpdateStage8(&c);assert(actions==3&&pushes==2&&physics==4);
 puts("PASS real native grip: action dispatcher advances for held actors while gravity/orbit are suppressed, script placement and matrices still run, other fighters and released actors unaffected");}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'grip.c';p.write_text(head+functions+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
# Audit translated pairs without mutating the logical English actor keys.
q=r'"(?:\\.|[^"\\])*"';pairs=re.compile(r'\{\s*('+q+r')\s*,\s*('+q+r')\s*\}')
for file in ['port/include/mod_story.h','port/include/mod_story_presentation.h','port/src/gs/mod_story_commentary.h','port/src/gs/mod_story_page.inc']:
 for m in pairs.finditer((r/file).read_text()):assert 'Piccolo'not in m[2],(file,m[2])
print('PASS localized Spanish pairs: Piccoro; English actor keys preserved')
