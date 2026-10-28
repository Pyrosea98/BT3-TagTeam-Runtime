from pathlib import Path
import subprocess,tempfile
s=Path('src/battle/eft_a.c').read_text();start=s.index('void EftCam_Start(');i=s.index('{',start)+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[start:i]
head=r'''#include <cassert>
#include <cstddef>
#define PORT 1
using s32=int;
struct EftCamArg{void*anim;int objId,unk8;};struct Cam{EftCamArg arg;int objId,active,hold;};
Cam cam;Cam*gEftCam=&cam;int calls,shown=1;
int Port_CutShownObj(int){return shown;}
void DemoCam_SetAnim(void*p){assert(p);++calls;}void DemoCam_SetChr(void*){}
void*BtlObj_Get(int){return &cam;}float BtlCharApi_GetChargedUnkC78(int){return 0;}
void DemoCam_SetTime(float){}void DemoCam_Start(){++calls;}
'''
test=r'''int main(){EftCamArg a={nullptr,0,0};EftCam_Start(nullptr);EftCam_Start(&a);assert(!calls&&!cam.active);
a.anim=&a;shown=0;EftCam_Start(&a);assert(!calls&&!cam.active);
shown=1;EftCam_Start(&a);assert(calls==2&&cam.active&&cam.objId==0);
cam.active=0;a.objId=-1;EftCam_Start(&a);assert(calls==4&&cam.active&&cam.objId==-1);}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+body+test);subprocess.run(['c++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual EftCam_Start: missing camera preserves view; valid and owner-admitted cameras still start')
