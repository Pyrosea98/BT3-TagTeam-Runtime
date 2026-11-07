from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fun(file,sig):
 s=(r/file).read_text();a=s.index(sig);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]

warp='#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n#define PORT 1\ntypedef int s32;typedef struct{float x,y,z,w;}Vec4;typedef struct{Vec4 pos,rot;unsigned char unk20[16];}BtlCapiBPose;typedef struct{BtlCapiBPose pose;Vec4 warpPos,warpRot;int flags[512];}BtlCapiBChr;static BtlCapiBChr chr;static int active=1,placed,chapter=6;int Port_StoryChapter(void){return chapter;}BtlCapiBChr*BtlChar_Get(int p){return p==0?&chr:0;}BtlCapiBPose*BtlChar_GetPos(BtlCapiBChr*c){return &c->pose;}void Vec4_Copy(Vec4*a,Vec4*b){*a=*b;}void BtlChar_SetHeldFlag(BtlCapiBChr*c,int f){c->flags[f]=1;}void BtlChar_ClearFlag(BtlCapiBChr*c,int f){c->flags[f]=0;}int Port_StoryActive(){return active;}int BtlStage_FindZoneNear(int n,Vec4*p){return 3;}void BtlChar_Place(void*ptr,Vec4*p,Vec4*rot,int zone){BtlCapiBChr*c=ptr;Vec4 a=*p,b=*rot;assert(zone==3);c->pose.pos=a;c->pose.rot=b;placed++;}\n'+"\n".join([fun('src/battle/btl_capi_b.c','void BtlCtrl_SetPos('),fun('src/battle/btl_capi_b.c','void BtlCtrl_SetRot(')])+'int main(){Vec4 p={20,30,40,1},rot={0,1.5,0,1};memset(chr.pose.unk20,0x3F,16);BtlCtrl_SetPos(0,&p);for(int i=0;i<16;i++)assert(chr.pose.unk20[i]==0);assert(placed==1&&chr.pose.pos.x==20&&!chr.flags[0xfc]);BtlCtrl_SetRot(0,&rot);assert(placed==2&&chr.pose.rot.y==1.5&&!chr.flags[0xfd]);chapter=11;memset(chr.pose.unk20,0x3F,16);BtlCtrl_SetPos(0,&p);assert(placed==3);for(int i=0;i<16;i++)assert(chr.pose.unk20[i]==0x3F);active=0;p.x=99;BtlCtrl_SetPos(0,&p);BtlCtrl_SetRot(0,&rot);assert(placed==3&&chr.pose.pos.x==20&&chr.flags[0xfc]&&chr.flags[0xfd]);BtlCtrl_SetPos(-1,&p);puts("PASS actual native Story placement: position/rotation committed before technique dispatch; ordinary deferred warp unchanged");}\n'

effect='#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n#define PORT 1\ntypedef int s32;typedef struct {void*model;int size;}EftObjRes;static int calls,ret;int BtlObj_Create(int t,EftObjRes*r,int a){calls++;assert(t==1&&a==1&&r->model&&r->size==0x20);return ret;}\n'+"\n".join([fun('src/battle/eft_ad_b.c','s32 EftObj_Create(')])+'int main(){EftObjRes r;int model;ret=7;assert(EftObj_Create(&r,&model)==7&&calls==1);assert(EftObj_Create(&r,0)==-1&&calls==1);assert(EftObj_Create(0,&model)==-1&&calls==1);ret=-3;assert(EftObj_Create(&r,&model)==-1&&calls==2);puts("PASS actual EftObj_Create: absent models fail safely; valid resources and pool-failure unchanged");}\n'

with tempfile.TemporaryDirectory() as d:
 for n,c in [('warp',warp),('effect',effect)]:
  p=Path(d)/n;p.with_suffix('.c').write_text(c);subprocess.run(['cc','-std=gnu99','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
