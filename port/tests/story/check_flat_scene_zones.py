from pathlib import Path
import tempfile,subprocess
s=Path('port/include/mod_story_scene.inc').read_text()
def body(signature):
 a=s.index(signature);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef unsigned u32;typedef struct{float x,y,z,w;}Vec4;typedef struct{float y;}StgGroundHit;static Vec4 pos[2];
static int chapter=6,ready=1;enum{ST_SET_CANON=1};static struct{int active,set;}sStRun={1,1};static struct{u32 flags;}work;
void*dummy;typeof(work)*Battle_GetWork(void){return &work;}
int st_chapter(void){return chapter;}int BtlStage_IsReady(void){return ready&&!(work.flags&0x2000);}int st_obj(int a){return a;}float st_height(int a){return 10;}
void BtlCharApi_GetPos(int a,Vec4*p){*p=pos[a];}void BtlCtrl_SetPos(int a,Vec4*p){assert(!(work.flags&0x2000));pos[a]=*p;}
int BtlStage_ProbeGround(int z,Vec4*p,StgGroundHit*h){assert(BtlStage_IsReady());h->y=p->z>10 && p->y>-500?-64:0;return 0;}
void st_scene_ground_actor(int a){StgGroundHit h;Vec4 probe=pos[a];probe.y-=220;BtlStage_ProbeGround(-1,&probe,&h);pos[a].y=h.y;}
void st_face(int a,int b){}void Port_Dbg3(const char*s,int a,int b,int c,int d){}
'''+body('static u32 st_scene_geometry_open(')+body('static void st_scene_geometry_close(')+body('static int st_scene_flat_pair_query(')+body('static int st_scene_flat_pair(')+r'''
int main(void){pos[0]=(Vec4){0,0,0,1};pos[1]=(Vec4){0,0,13,1};work.flags=0x2000|0x40;assert(!BtlStage_IsReady());assert(st_scene_flat_pair(0,1,1.3f));assert(work.flags==(0x2000|0x40));work.flags&=~0x2000;st_scene_ground_actor(0);st_scene_ground_actor(1);assert(fabsf(pos[0].y-pos[1].y)<=1.5f);assert(pos[1].z<=10);work.flags|=0x2000;chapter=11;assert(!st_scene_flat_pair(0,1,1.3f)&&work.flags==(0x2000|0x40));chapter=6;ready=0;assert(!st_scene_flat_pair(0,1,1.3f)&&work.flags==(0x2000|0x40));puts("PASS actual ending geometry scope + flat-pair search: loaded WIN-stage queried synchronously, flags restored; other chapters/unloaded stages untouched; height-dependent64-unit cliff rejected after real-height resampling");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'zone';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-lm','-o',str(p)],check=True);subprocess.run([str(p)],check=True)
