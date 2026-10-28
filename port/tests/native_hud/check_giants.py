from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd()
def fn(s,name):
 m=re.search(r'^(?:s32|void) '+name+r'\([^;{}]*\)\s*\{',s,re.M);assert m,name;i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
head=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "battle/bobj_a.h"
static int enabled=1,percent=400,camera=150,net,replay;
int Port_Setting(const char*k,int d){if(!strcmp(k,"canonical_giants"))return enabled;if(!strcmp(k,"giant_size_percent"))return percent;if(!strcmp(k,"giant_camera_distance_percent"))return camera;return d;}
int Port_NetActive(void){return net;}int BattleReplay_IsActive(void){return replay;}
#include "mod_giants_native.inc"
static int eq(float a,float b){return fabsf(a-b)<.0001f;}
'''
tail=r'''int main(void){BobjObj o={0};BobjModelHdr model={0};BobjBodyData data={0};BobjParam p={0};BobjWork work={0};Vec4 out,anchor={10,20,30,1};o.mdl.param=&p;o.mdl.model=&model;o.mdl.body=&data;o.work=&work;((u8*)&p)[2]=4;
for(int base=1;base<=3;base++){for(int j=0;j<4;j++)model.size[j]=base*(j+1);p.radius=base*3;data.unk04=base*2;
for(int scale=100;scale<=400;scale+=25){percent=scale;float f=scale*.01f;assert(eq(Port_GiantScale(&o),f));
for(int frame=0;frame<600;frame++){Port_GiantBodyDimensions(&o);for(int j=0;j<4;j++)assert(eq(o.body.size[j],model.size[j]*f));assert(eq(o.body.radius,p.radius*f));assert(eq(o.body.unkA0,data.unk04*f));}
o.xf.rootPos=(Vec4){anchor.x+3*f,anchor.y-2*f,anchor.z+5*f,1};Port_GiantRootPosition(&o,&out,&anchor);assert(eq(out.x,13)&&eq(out.y,18)&&eq(out.z,35));
}}
percent=400;p.radius=0;Port_GiantBodyDimensions(&o);assert(eq(o.body.radius,model.size[3]*8));
BobjBodyPart part={0};part.flags=1;part.box.local[0]=(Vec4){2,3,4,1};for(int j=0;j<3;j++){part.box.axis[j]=(Vec4){0,0,0,0};((float*)&part.box.axis[j])[j]=4;part.box.mtx.m[j][j]=4;}part.box.world[0]=(Vec4){8,12,16,1};o.body.parts=&part;Port_GiantBodyBoxes(&o);assert(eq(part.box.unk10.x,8)&&eq(part.box.unk10.y,12)&&eq(part.box.unk10.z,16));for(int j=0;j<3;j++){assert(eq(((float*)&part.box.axis[j])[j],1));assert(eq(part.box.mtx.m[j][j],1));}assert(part.box.world[0].x==8&&part.box.local[0].x==2);
assert(Port_GiantCameraScale(&o)==1.5f);camera=999;assert(Port_GiantCameraScale(&o)==3);camera=-1;assert(Port_GiantCameraScale(&o)==1);
for(int mode=0;mode<3;mode++){enabled=mode!=0;net=mode==1;replay=mode==2;assert(Port_GiantScale(&o)==1);Port_GiantBodyDimensions(&o);for(int j=0;j<4;j++)assert(eq(o.body.size[j],model.size[j]));assert(Port_GiantCameraScale(&o)==1);o.xf.rootPos=(Vec4){.1f,.2f,.3f,1};Port_GiantRootPosition(&o,&out,&anchor);assert(!memcmp(&out,&o.xf.rootPos,sizeof(out)));}
enabled=1;net=replay=0;percent=-9;assert(Port_GiantScale(&o)==1);percent=999;assert(Port_GiantScale(&o)==4);((u8*)&p)[2]=2;assert(Port_GiantScale(&o)==1);((u8*)&p)[2]=4;o.type=1;assert(Port_GiantScale(&o)==1);o.type=0;o.work=0;assert(Port_GiantScale(&o)==1);o.work=&work;o.mdl.param=0;assert(Port_GiantScale(&o)==1);assert(Port_GiantScale(0)==1);
puts("PASS native giant helpers: original per-body proportions x13 multipliers x600frames, no compounding, radius fallback, OBB unit axes/scaled extents, unchanged corners, native root motion, off/online/replay/reset/nonfighter guards and camera limits");}
'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'test.c').write_text(head+tail);subprocess.run(['gcc','-std=c99','-Iinclude','-Iport/include','-fsanitize=address,undefined',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
# Exercise the production pose writer/reader, not a duplicate of their maths.
src=(r/'src/battle/btl_char_ctl_d.c').read_text()
head=r'''#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
using s32=int;struct Vec4{float x,y,z,w;};struct Quat{float x,y,z,w;};struct Mtx44{float m[4][4];};
struct BtlCtlObj{Vec4 pos,rot,outPos;float scale;};struct BtlCtlPose{Vec4 pos,move,rot,rootPos,rootRot;};struct BtlCtlNode{Vec4 pos,rot;};struct BtlCtlChr{float scale;};
BtlCtlObj obj;BtlCtlPose pose;BtlCtlNode node;float factor=4;int flag;
BtlCtlObj*BtlChar_GetObj(BtlCtlChr*){return &obj;}BtlCtlPose*BtlChar_GetPos(BtlCtlChr*){return &pose;}BtlCtlNode*BtlObj_GetNode(BtlCtlObj*,int){return &node;}
void Vec4_Copy(Vec4*d,Vec4*s){*d=*s;}void Vec4_Add(Vec4*d,Vec4*a,Vec4*b){*d={a->x+b->x,a->y+b->y,a->z+b->z,a->w};}void Vec4_Sub(Vec4*d,Vec4*a,Vec4*b){*d={a->x-b->x,a->y-b->y,a->z-b->z,a->w};}void Vec4_Scale(Vec4*d,Vec4*s,float f){*d={s->x*f,s->y*f,s->z*f,s->w};}
int BtlChar_TestFlag(BtlCtlChr*,int){return flag;}void Mtx_StoreIdentity(Mtx44*m){*m={};}void Mtx_RotateX(Mtx44*,Mtx44*,float){}void Mtx_RotateY(Mtx44*,Mtx44*,float){}void Mtx_RotateZ(Mtx44*,Mtx44*,float){}void Mtx_MulVec4(Vec4*d,Mtx44*,Vec4*s){*d=*s;}float Vec3_Length(Vec4*v){return sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);}void BtlObjXf_Update(BtlCtlObj*){}
void Quat_ToEuler(Vec4*d,Quat*){*d={};}void Vec4_SetZeroW1(Vec4*d){*d={0,0,0,1};}void Vec4_SetZero(Vec4*d){*d={};}
float Port_GiantScale(const void*){return factor;}void Port_GiantRootPosition(const void*,Vec4*out,const Vec4*anchor){*out={anchor->x+(obj.outPos.x-anchor->x)/factor,anchor->y+(obj.outPos.y-anchor->y)/factor,anchor->z+(obj.outPos.z-anchor->z)/factor,1};}
#define PORT 1
'''
tail=r'''int main(){BtlCtlChr c={1.5f};for(int f=1;f<=4;f++){factor=f;for(int k=0;k<2;k++){flag=k;pose={};pose.pos={10,20,30,1};pose.move={2,3,4,0};pose.rootPos={1,2,3,1};obj.pos={0,0,0,1};BtlChar_PoseToObj(&c,0);assert(obj.scale==1.5f*f);assert(obj.pos.x==12-1.5f*f);assert(obj.pos.y==23-3.f*f);obj.outPos={12+f*6.f,23+f*8.f,34+f*10.f,1};BtlChar_ObjToPose(&c);assert(pose.pos.x==16&&pose.pos.y==28&&pose.pos.z==40);}}
puts("PASS actual giant pose consumers: native scripted scale retained, root subtraction scales once, returned motion unchanged for x1..x4 and held-root flag");}
'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'pose.cpp').write_text(head+fn(src,'BtlChar_ObjToPose')+fn(src,'BtlChar_PoseToObj')+tail);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'pose.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
# Actual attack-volume producer: enlargement must affect extents once, while
# the OBB basis remains orthonormal for the narrow-phase collision solver.
src=(r/'src/battle/bobj_a.c').read_text()
head=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "battle/bobj_a.h"
static float factor=4;static BobjPart node;u16 gPortBodyTouch[12];
float Port_GiantScale(const void*o){return factor;}
int BtlObjBody_Update(BobjObj*o,int n){return 0;}
BobjPart*BtlObj_GetNodeByCode(BobjObj*o,int n){return &node;}
void StgAabb_SetEmpty(BobjAabb*b){memset(b,0,sizeof(*b));}void ColBox_AddPointKeepW(BobjAabb*b,float*p){}
void Mtx_Copy(Mtx44*d,Mtx44*s){*d=*s;}void Vec4_Copy(Vec4*d,Vec4*s){*d=*s;}
void Mtx_MulVec4(Vec4*d,Mtx44*m,Vec4*v){d->x=v->x*m->m[0][0]+m->m[3][0];d->y=v->y*m->m[1][1]+m->m[3][1];d->z=v->z*m->m[2][2]+m->m[3][2];d->w=1;}
void ColSphere_Set(BobjSphere*s,Vec4*c,float radius){s->center=*c;s->radius=radius;}
void ColBounds_OfSphere(BobjAabb*b,BobjSphere*s){}
void ColObb_Init(BobjObb*b,Vec4*c,float x,float y,float z){b->center=*c;b->unk10=(Vec4){x,y,z,1};}
void ColObb_Update(BobjObb*b,Mtx44*m){for(int i=0;i<3;i++)assert(fabsf(m->m[i][i]-1)<.0001f);b->mtx=*m;}
void ColBounds_OfObb(BobjAabb*b,BobjObb*o){}
#define PORT 1
'''
tail=r'''int main(){static BobjObj o;static BobjWork w;o.work=&w;o.hit.hitIndex=0;o.hit.nodeMask=1;o.hit.r0=2;o.hit.r1=3;o.hit.r2=4;o.hit.x=1;o.hit.y=2;o.hit.z=3;
for(int f=1;f<=4;f++){factor=f;memset(&node,0,sizeof(node));for(int i=0;i<3;i++)node.mtx.m[i][i]=f;node.mtx.m[3][0]=10;
o.hit.sphere=1;BtlObjHit_BuildVolumes(&o);assert(w.atkSphereCount==1&&w.atkSphere[0].radius==2*f&&w.atkSphere[0].center.x==10+f);
o.hit.sphere=0;BtlObjHit_BuildVolumes(&o);assert(w.atkObbCount==1&&w.atkObb[0].unk10.x==2*f&&w.atkObb[0].unk10.y==3*f&&w.atkObb[0].unk10.z==4*f);assert(w.atkObb[0].center.x==10+f);assert(o.hit.r0==2&&node.mtx.m[0][0]==f);}
o.hit.hitIndex=-1;BtlObjHit_BuildVolumes(&o);assert(!w.atkObbCount&&!w.atkSphereCount);
puts("PASS actual giant attack volumes: sphere and OBB x1..x4, scaled limb offsets, unit query basis, unchanged source radii/node matrices, closed hit window");}
'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'hit.c').write_text(head+fn(src,'BtlObjHit_BuildVolumes')+tail);subprocess.run(['gcc','-std=c99','-Iinclude','-fsanitize=address,undefined',str(p/'hit.c'),'-lm','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
src=(r/'src/battle/btl_char_cam_modes.c').read_text()
head=r'''#include <cassert>
#include <cstdio>
#include <cmath>
using s32=int;using f32=float;struct Vec4{float x,y,z,w;};struct ChrCam{int distMode;float yaw,side,yawOfs;};struct ChrCamChr{ChrCam cam;int objId;};struct ChrCamBody{Vec4 pos;};ChrCamBody body={{10,20,30,1}};float zoom=1;int split;
#define PI 3.14159265f
#define HALF_PI (PI*.5f)
void*BtlChar_GetObj(ChrCamChr*c){return c;}float Port_GiantCameraScale(const void*){return zoom;}ChrCamBody*BtlChar_GetPos(ChrCamChr*){return &body;}
float BtlCharApi_GetHeight(int){return 40;}float BtlStage_GetBottom(){return 10000;}void BtlOpp_GetTargetPos(ChrCamChr*,Vec4*v){*v={50,20,200,1};}float BtlOpp_GetHalfHeightDiff(ChrCamChr*){return 0;}
void Vec4_Copy(Vec4*d,Vec4*s){*d=*s;}void Vec4_Sub(Vec4*d,Vec4*a,Vec4*b){*d={a->x-b->x,a->y-b->y,a->z-b->z,0};}void Vec4_Set(Vec4*d,float x,float y,float z,float w){*d={x,y,z,w};}void Vec4_Scale(Vec4*d,Vec4*s,float f){*d={s->x*f,s->y*f,s->z*f,s->w};}void Vec4_RotateEuler(Vec4*d,Vec4*,Vec4*s){*d=*s;}
float Vec3_Length(Vec4*v){return sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);}float BtlUtil_LengthXZ(Vec4*v){return sqrtf(v->x*v->x+v->z*v->z);}float BtlUtil_WrapAngle(float f){return f;}int BtlChar_TestFlag(ChrCamChr*,int){return 0;}int Battle_IsSplitScreen(){return split;}
float Mathf_Sin(float f){return sinf(f);}float Mathf_Cos(float f){return cosf(f);}float Mathf_Tan(float f){return tanf(f);}float Mathf_Asin(float f){return asinf(f);}float Mathf_Atan(float f){return atanf(f);}
float ChrCam_GetOffset(ChrCamChr*,Vec4*v,int){*v={0,-50,-90,1};return -.05f;}float ChrCam_GetSideLimit(ChrCamChr*){return 1;}float ChrCam_GetBob(ChrCamChr*){return .5f;}
#define PORT 1
'''
tail=r'''int main(){ChrCamChr c={};for(int layout=0;layout<2;layout++){split=layout;for(int mode=0;mode<2;mode++){Vec4 a,b,ta,tb,ra,rb;zoom=1;if(mode)ChrCam_CalcFree(&c,&a,&ra,&ta);else ChrCam_CalcLockOn(&c,&a,&ra,&ta);zoom=1.5f;if(mode)ChrCam_CalcFree(&c,&b,&rb,&tb);else ChrCam_CalcLockOn(&c,&b,&rb,&tb);assert(ta.x==tb.x&&ta.y==tb.y&&ta.z==tb.z);for(int i=0;i<3;i++)assert(fabsf((((float*)&b)[i]-((float*)&tb)[i])-1.5f*(((float*)&a)[i]-((float*)&ta)[i]))<.001f);assert(ra.x==rb.x&&ra.y==rb.y);}}
puts("PASS actual giant normal cameras: free/lock-on full/split, scaled eye-to-target distance with unchanged target/rotation; authored cut/fixed solvers untouched");}
'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'cam.cpp').write_text(head+fn(src,'ChrCam_CalcLockOn')+fn(src,'ChrCam_CalcFree')+tail);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'cam.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
