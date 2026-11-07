from pathlib import Path
import re,subprocess,tempfile
s=Path('src/battle/btl_demo_cam.c').read_text()
def ex(n):
 a=re.search(r'(?m)^void '+n+r'\([^;]*?\)\s*\{',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code=r"""#include <assert.h>
#include <string.h>
#include <stddef.h>
#define PORT 1
#define DEMO_CAM_PLAYING 1
#define DEMO_CAM_AUTO 2
#define DEMO_CAM_STOP_AT_END 4
typedef struct{int id;}DemoCamAnim;typedef struct{DemoCamAnim*anim;int fixed,flags,unk2F8,unk2FC;void*obj,*chr;int base;}Cam;static Cam camera,*gDemoCam=&camera;static int owned,fixups;int Port_StoryFixedCameraOwned(void){return owned;}void DemoCam_FixupAnim(DemoCamAnim*a){fixups++;}void Mtx_StoreIdentity(int*m){*m=1;}
"""+'\n'.join(ex(n) for n in ['DemoCam_SetAnim','DemoCam_Start','DemoCam_StartAuto','DemoCam_StartAutoOnce'])+r"""
int main(void){DemoCamAnim old={1},next={2};for(int k=0;k<3;k++){void(*start[])(void)={DemoCam_Start,DemoCam_StartAuto,DemoCam_StartAutoOnce};camera=(Cam){.anim=&old,.fixed=1,.flags=32,.unk2F8=7,.unk2FC=9};owned=1;Cam before=camera;DemoCam_SetAnim(&next);start[k]();assert(!memcmp(&before,&camera,sizeof(camera))&&!fixups);owned=0;DemoCam_SetAnim(&next);assert(camera.anim==&next);start[k]();assert(!camera.fixed&&camera.flags==(k==0?1:k==1?3:7));fixups=0;camera.fixed=0;owned=1;DemoCam_SetAnim(&old);assert(camera.anim==&old&&fixups==1);fixups=0;}}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual native camera setters/start variants: Story-owned fixed shot survives animated takeover; ordinary and unowned camera animation remains functional')

# Exercise the actual final camera selection path, not only setter intentions.
t=Path('src/battle/btl_cam.c').read_text();a=t.index('s32 BtlCam_UpdateOverride(void)');i=t.index('{',a)+1;depth=1
while depth:depth+=(t[i]=='{')-(t[i]=='}');i+=1
body=t[a:i]
head=r"""#include <assert.h>
typedef int s32;
#define PORT 1
static int owned,fixed,selected,applied,updates;
int Battle_IsSplitScreen(void){return 0;}int BattleReplay_IsActive(void){return 0;}int BtlCam_GetPriorityView(void){return -1;}int BtlCam_GetDefaultView(void){return 0;}void BtlCam_SelectView(int i){selected++;}void BtlCam_ApplyView(int i){applied++;}void BtlCam_SetLayout(int a,int b){}int Port_QuadMode(void){return 0;}int Port_NetView(void){return -1;}
int Port_StoryApplyFixedCamera(void){if(owned){fixed=1;return 1;}return 0;}int DemoCam_IsActive(void){return fixed;}void DemoCam_Update(void){assert(fixed);updates++;}
"""
foot=r"""int main(void){owned=1;fixed=0;assert(BtlCam_UpdateOverride()==1&&updates==1&&fixed);owned=0;fixed=0;assert(BtlCam_UpdateOverride()==1&&updates==1&&selected==2&&applied==2);}"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'view';p.with_suffix('.c').write_text(head+body+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual final view-selection path reapplies owned Story camera before submitting DemoCam; ordinary gameplay camera retains its selection')

a=t.index('void BtlCam_ApplyView(s32 scissor)');i=t.index('{',a)+1;depth=1
while depth:depth+=(t[i]=='{')-(t[i]=='}');i+=1
body=t[a:i]
head=r"""#include <assert.h>
#define PORT 1
typedef int s32;struct V{int view;};struct C{struct V*cur;};static struct V v;static struct C c={&v},*gBtlCam=&c;static int owned,draws,demos;static struct V demo,*gDemoCam=&demo;static int*gBtlCamView;
int Port_StoryApplyFixedCamera(void){return owned;}void DemoCam_Update(void){demos++;}void View_Apply(int*v,int s){draws++;}
"""
foot=r"""int main(void){owned=1;BtlCam_ApplyView(1);BtlCam_ApplyView(0);assert(demos==2&&!draws&&gBtlCamView==&demo.view);owned=0;BtlCam_ApplyView(1);assert(draws==1&&demos==2);}"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'draw';p.with_suffix('.c').write_text(head+body+foot);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
print('PASS actual draw-pass ApplyView keeps owned Story camera for every draw/cull pass; ordinary player view still applies')
