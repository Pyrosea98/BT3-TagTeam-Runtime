from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_cam.c').read_text();a=s.index('static Mtx44 sModHudMatrices[');b=s.index('\n#endif',a);body=s[a:b]
head=r"""#include <cassert>
#include <cstdio>
#define NULL nullptr
using s32=int;struct Vec4{float x,y,z,w;};struct Mtx44{int marker;};struct View{Mtx44 world2screen;};struct BtlCamView{View view;};struct BtlCam{BtlCamView views[2];};struct DemoCam{View view;int fixed;void *anim;int flags;};
#define DEMO_CAM_PLAYING 1
#define DEMO_CAM_STOP 2
static View *gBtlCamView;static BtlCam cam;static DemoCam demo;static BtlCam *gBtlCam=&cam;static DemoCam *gDemoCam=&demo;static int quad,split,priority=-1,projected,behind;
int Port_QuadMode(){return quad;}int Port_HudPartCount(){return 4;}int Battle_IsSplitScreen(){return split;}int BtlCam_GetPriorityView(){return priority;}int BtlCam_GetDefaultView(){return 0;}
int Mtx_ProjectPoint(int *out,Mtx44*m,Vec4*p){projected=m->marker;out[0]=0x7000+(int)(p->x+m->marker)*16;out[1]=0x7200+(int)p->y*16;out[3]=behind?0:1;return 0;}
int Port_QuarterProject(int q,Vec4*p,int*x,int*y){projected=100+q;*x=0x7000+(int)p->x*16;*y=0x7200+(int)p->y*16;return !behind;}
"""
foot=r"""int main(){cam.views[0].view.world2screen.marker=10;cam.views[1].view.world2screen.marker=20;demo.view.world2screen.marker=50;int x,y;Vec4 pos={5,8,0,1};assert(Port_ModHudProject(0,&pos,&x,&y)&&x==15&&y==8);split=1;assert(Port_ModHudViewCount()==2);assert(Port_ModHudProject(1,&pos,&x,&y)&&projected==20);quad=1;assert(Port_ModHudViewCount()==4);assert(Port_ModHudProject(3,&pos,&x,&y)&&projected==103);
 demo.anim=&demo;demo.flags=DEMO_CAM_PLAYING;assert(Port_ModHudViewCount()==1);assert(Port_ModHudProject(0,&pos,&x,&y)&&projected==50&&x==55);demo.view.world2screen.marker=60;pos.x=9;assert(Port_ModHudProject(0,&pos,&x,&y)&&x==69);assert(!Port_ModHudProject(1,&pos,&x,&y));behind=1;assert(!Port_ModHudProject(0,&pos,&x,&y));behind=0;demo.flags|=DEMO_CAM_STOP;assert(Port_ModHudViewCount()==4&&demo.flags==3&&demo.anim==&demo);demo.anim=nullptr;demo.fixed=1;assert(Port_ModHudViewCount()==1);gDemoCam=nullptr;assert(Port_ModHudViewCount()==4);gDemoCam=&demo;gBtlCamView=&cam.views[1].view;Port_ModHudCaptureView(0,1);quad=1;split=1;assert(Port_ModHudViewCount()==1);assert(Port_ModHudProject(0,&pos,&x,&y)&&projected==20);cam.views[1].view.world2screen.marker=99;assert(Port_ModHudProject(0,&pos,&x,&y)&&projected==20);Port_ModHudCaptureView(0,1);assert(Port_ModHudProject(0,&pos,&x,&y)&&projected==99);for(int i=0;i<4;i++){cam.views[0].view.world2screen.marker=200+i;gBtlCamView=&cam.views[0].view;Port_ModHudCaptureView(i,4);}assert(Port_ModHudViewCount()==4);for(int i=0;i<4;i++)assert(Port_ModHudProject(i,&pos,&x,&y)&&projected==200+i);Port_ModHudResetViews();assert(Port_ModHudViewCount()==1);gBtlCam=nullptr;assert(!Port_ModHudProject(0,&pos,&x,&y));puts("PASS actual overhead camera selector: moving cinematic/model points, full/split/quad transitions, behind-camera/invalid view, fixed camera, stop has no mutations");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot);subprocess.run(['g++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
