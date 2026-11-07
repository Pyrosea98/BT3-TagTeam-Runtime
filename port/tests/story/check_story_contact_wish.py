from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/tests/story/test_saga_chapters.c').read_text();s=s[:s.index('int main(')]
s=s.replace('static int sReady; s32 BtlStage_IsReady(void) { return sReady; }','static int sReady; s32 BtlStage_IsReady(void) { return 1; }')
s=s.replace('h->y = 0; return 0;', 'h->y = sTestFloor; return 0;')
s=s.replace('s32 BtlStage_ProbeGround(', 'static float sTestFloor;\ns32 BtlStage_ProbeGround(',1)
s=s.replace('void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { assert(isfinite(p->x) && isfinite(a->y)); }','static Vec4 sTestEye,sTestAt;void BtlFacade_SetFixedCamera(Vec4 *p, Vec4 *a) { sTestEye=*p;sTestAt=*a;assert(isfinite(p->x) && isfinite(a->y)); }')
s=s.replace('*p = sPos[a]; *q = (Vec4){0, 0, 0, 1}; return 0;','*p = (Vec4){0,sTestFloor,0,1}; *q = (Vec4){0, 0, 0, 1}; return 0;')
s+=r'''
static void start_case(int ch){MsView v={0};Port_ScenarioClear();Port_StoryChoose(ch);assert(Port_ScenarioDraft(&v));for(int i=0;i<N;i++)sTeam[i]=i<sScN?sScTeams[i]:0;fight_start();}
int main(void){Vec4 head,chest;int kid,rival;float dx,dz;
 start_case(2);st_event_start(2,0);kid=(int)sStEventTo.w;rival=sStRun.rival;
 sStRun.index=1;for(int f=0;f<ST_GOHAN_FLIGHT;f++)st_event_frame(2);
 assert(sStEventHit);head=st_scene_face(kid);chest=st_scene_face(rival);chest.y+=st_height(rival)*.22f;
 assert(fabsf(head.x-chest.x)<.001f&&fabsf(head.y-chest.y)<.001f&&fabsf(head.z-chest.z)<.001f);
 st_scene_end();start_case(6);sStAtkEvent=1;sStAtkBy=actor(13);sStAtkAt=actor(1);sStAtkFired=0;
 sPos[sStAtkAt]=(Vec4){0,0,0,1};sRot[sStAtkAt]=(Vec4){0,1.2f,0,1};
 assert(st_choreo_update(6,&stEvents[6].event[1],6));dx=sPos[sStAtkAt].x-sPos[sStAtkBy].x;dz=sPos[sStAtkAt].z-sPos[sStAtkBy].z;
 assert(dx*sinf(sRot[sStAtkBy].y)+dz*cosf(sRot[sStAtkBy].y)>0);
 assert(stChapters[19].stage==12);Port_StoryChoose(19);assert(Port_StoryStage()==12);
 puts("PASS animated Gohan head reaches Raditz chest; clinging attacker aims at victim before native technique; Roshi uses Kame House");return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'camera';p.with_suffix('.c').write_text(s);subprocess.run(['cc','-std=gnu99','-w','-DPORT','-fsanitize=address,undefined','-Iinclude','-Iport/include','-Iport/src',str(p.with_suffix('.c')),'-lm','-o',str(p)],check=True);subprocess.run([str(p)],check=True)

# The actual ambient-water callback must be safe in the DemoCam-only wish viewer.
src=(r/'src/battle/eft_b.c').read_text();start=src.index('void EftBubble_UpdateAmbient(void) {');end=src.index('/* Returns a cleared free bubble',start)
code=r'''
#include <assert.h>
#include <stddef.h>
typedef float f32;typedef int s32;typedef struct{float y;}V;typedef struct{V camPos;}View;typedef struct{View view;}Cam;static Cam *gBtlCam;
typedef struct{int objId;}EftBubbleChr;typedef struct{int trail;}Flags;static Flags flags;
typedef struct{EftBubbleChr chr[2];float camTimer[2];}W;static W *gEftBubble;
typedef struct{int flags;}B;static B work;static int called;
#define EFTB_BATTLE_PAUSE 1
#define EFTB_CAM_AT(i) (&gBtlCam[i])
#define EFT_BUBBLE_CHR_FLAGS(c) (&flags)
B *Battle_GetWork(void){return &work;}int Battle_IsSplitScreen(void){return 0;}
void EftWater_GetSurfaceY(float*p){called++;*p=0;}int rand(void){return 0;}
void EftBubble_EmitCamera(int i){(void)i;}int BtlCharApi_IsInWater(int i){(void)i;return 0;}
void EftBubble_EmitBody(int a,int b,int c){(void)a;(void)b;(void)c;}
'''+src[start:end]+r'''
int main(void){EftBubble_UpdateAmbient();assert(!called);Cam cam={{{-1}}};W w={0};gBtlCam=&cam;gEftBubble=&w;EftBubble_UpdateAmbient();assert(called==1);}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'water.c';p.write_text(code);subprocess.run(['cc','-DPORT','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
print('PASS actual ambient water callback: no battle camera is safe; normal fight callback still runs')
