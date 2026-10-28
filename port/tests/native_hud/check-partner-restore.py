import subprocess,tempfile,re
from pathlib import Path
r=Path.cwd();s=(r/'port/src/plat_fighters.c').read_text()
def extract(name,ret):
 a=re.search(r'\n'+ret+' '+name+r'\([^;{}]*\)\s*\{',s).start()+1;i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
head=r'''#include <stdio.h>
#include <cmath>
#include <assert.h>
#include <string.h>
struct FightExtra {int member,consumed;};static FightExtra sFight[1];static int sFightCount=1;
static int sFusionMirror[12][5],sFusionOwner[12],sFusionLink[12][5];
static unsigned char actors[3][0x1600];static int shown,requested,watchReset;
int BtlChar_GetCount(){return 3;}int Port_Team(int i){return i==1?1:0;}
void *BtlChar_Get(int i){return i>=0&&i<3?actors[i]:NULL;}
void *BtlMember_GetActive(void *p){return (char*)p+0x9a4;}
void *BtlChar_GetPos(void *p){return (char*)p+0x10;}
int Port_FighterObjId(int i){return i;}void *BtlObj_Get(int i){return actors[i];}
void BtlObj_SetPartVisible(void*,unsigned part,int on){assert(part==0);shown=on;}
void BtlAct_Request(void*,int action){requested=action;}
void Port_ReviveSpectator(int who){watchReset=who+1;}

float BtlCharApi_GetRadius(int){return 4;}
int Port_AssistSafePoint(int,float*){return 1;}
int Port_AssistCommitPosition(int who,const float*point,float yaw){float*p=(float*)BtlChar_GetPos(BtlChar_Get(who));memcpy(p,point,4*sizeof(float));p[5]=yaw;p[0x98/4]=p[0x9c/4]=0;return 1;}
void BtlChar_ClearFlag(void*,int){}void Port_DefusionBurst(int){}void BtlObjBody_Warp(void*,void*){}
'''
test=r'''
int main(){sFight[0]={1,0};*(int*)actors[0]=0;*(int*)(actors[0]+0x998)=1;
unsigned char *ally=actors[2]+0x9a4;memset(ally,0x31,0xa4);*(int*)ally=166;*(int*)(ally+4)=2;*(int*)(ally+0x40)=73000;
Port_TeamFillFusion(actors[0]);assert(*(int*)(actors[0]+0x998)==2);assert(!memcmp(actors[0]+0x9a4+0xa4,ally,0xa4));
unsigned char saved[0xa4];memcpy(saved,ally,0xa4);memset(ally,0,0xa4);sFight[0].consumed=1;sFusionOwner[2]=1;
float *p=(float*)BtlChar_GetPos(actors[2]),*l=(float*)BtlChar_GetPos(actors[0]);l[0]=100;l[1]=200;l[2]=300;l[3]=1;l[5]=.3f;p[0x98/4]=123;p[0x9c/4]=567;
assert(!Port_ExtraFusionRelease(1,1,saved,actors[0]));assert(sFight[0].consumed);
assert(Port_ExtraFusionRelease(0,1,saved,actors[0]));assert(!sFight[0].consumed&&!memcmp(saved,ally,0xa4));assert(shown==1&&requested==0xB&&watchReset==3);
assert(fabsf(p[0]-(100+cosf(.3f)*10))<.001f&&p[1]==200&&fabsf(p[2]-(300-sinf(.3f)*10))<.001f&&p[5]==.3f&&p[0x98/4]==0&&p[0x9c/4]==0);
assert(!Port_ExtraFusionRelease(0,1,saved,actors[0]));sFight[0].consumed=1;Port_ExtraRoundReset();assert(!sFight[0].consumed);sFight[0].consumed=1;Port_ExtraRoundReset();assert(!sFight[0].consumed);puts("PASS actual fusion partner consumers: real transformed costume/gauges, scoped release, restored controls/position/inertia, one-shot ownership and repeated round consumption reset");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(head+extract('Port_FusionPartnerActor','int')+extract('Port_TeamFillFusion','void')+extract('Port_ExtraFusionRelease','int')+extract('Port_ExtraRoundReset','void')+test)
 subprocess.run(['c++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
