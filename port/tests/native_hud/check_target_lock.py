from pathlib import Path
import subprocess,tempfile
r=Path.cwd();src=(r/'src/battle/btl_char_flag_opp.c').read_text()
def extract(s,name):
 start=s.rfind('\n',0,s.index(name+'('))+1;a=s.index('{',start);i=a+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
head=r"""#include <cassert>
#include <cmath>
#include <cstring>
using s32=int;using f32=float;
#define PORT_FIGHTERS 10
struct Vec4{float x,y,z,w;};struct Pos{Vec4 pos;};struct BtlFlagChr{int player,action,dead;Pos p;};
static BtlFlagChr actors[10];static int count=4,gPortTarget[10],sPortTargetHold[10],sPortLock[10],sPortLockNew[10],human[10],seq[10],clash[10],gPortCamOf=-1;
int BtlChar_GetCount(){return count;}BtlFlagChr*BtlChar_Get(int i){assert(i>=0&&i<count);return &actors[i];}
int Port_TargetHuman(int i){return human[i];}int Port_IsEnemy(int i,int k){return k>=0&&k<count&&i!=k;}
int Port_LockValid(int i){return human[i]&&sPortLock[i]>0&&sPortLock[i]<=count;}
int Port_InSequence(int i){return seq[i]>=0;}int Port_SequencePartner(int i){return seq[i];}int Port_ClashPartner(int i){return clash[i];}
int BtlAct_GetCurrent(BtlFlagChr*c){return c->action;}int BtlAct_IsTechniqueId(int a){return a>=0x105&&a<=0x132;}
int BtlChar_IsDead(BtlFlagChr*c){return c->dead;}Pos*BtlChar_GetPos(BtlFlagChr*c){return &c->p;}
void Vec4_Sub(Vec4*d,Vec4*a,Vec4*b){d->x=a->x-b->x;d->y=a->y-b->y;d->z=a->z-b->z;}
float Vec3_Length(Vec4*v){return sqrtf(v->x*v->x+v->y*v->y+v->z*v->z);}
void Port_UpdateSpectate(){}void Port_TargetChoose(int){}void Port_CamTargetLog(int){}
void BtlChar_GetSnapPos(BtlFlagChr*c,Vec4*out,int){*out=c->p.pos;}
"""
aim=extract(src,'BtlOpp_GetTargetPos')
code=head+extract(src,'Port_CamTarget')+extract(src,'Port_UpdateTarget')+'\n#define OPPONENT(chr) BtlChar_Get((chr)->player==gPortCamOf?Port_CamTarget((chr)->player):gPortTarget[(chr)->player])\n'+aim
foot=r"""
void init(){count=4;for(int i=0;i<10;i++){actors[i]={i,7,0,{{float(i*100),0,0,1}}};seq[i]=clash[i]=-1;human[i]=0;sPortLock[i]=sPortLockNew[i]=sPortTargetHold[i]=0;gPortTarget[i]=i?0:1;}human[0]=1;sPortLock[0]=3;}
int main(){Vec4 aim;
 init();actors[1].action=0x140;Port_UpdateTarget();assert(gPortTarget[0]==2&&Port_CamTarget(0)==2);BtlOpp_GetTargetPos(&actors[0],&aim);assert(aim.x==200);
 init();sPortTargetHold[0]=60;Port_UpdateTarget();assert(gPortTarget[0]==2);BtlOpp_GetTargetPos(&actors[0],&aim);assert(aim.x==200);
 init();actors[0].action=0x105;Port_UpdateTarget();assert(gPortTarget[0]==1&&Port_CamTarget(0)==1);
 init();seq[0]=3;Port_UpdateTarget();assert(gPortTarget[0]==3&&Port_CamTarget(0)==3);
 init();clash[0]=1;Port_UpdateTarget();assert(gPortTarget[0]==1&&Port_CamTarget(0)==1);
 init();actors[0].action=0x140;Port_UpdateTarget();assert(gPortTarget[0]==1);actors[0].action=7;Port_UpdateTarget();assert(gPortTarget[0]==2);
 init();human[0]=0;sPortTargetHold[0]=60;Port_UpdateTarget();assert(gPortTarget[0]==1&&sPortTargetHold[0]==59);
 init();human[0]=0;actors[1].dead=1;sPortTargetHold[0]=120;Port_UpdateTarget();assert(gPortTarget[0]!=1&&!actors[gPortTarget[0]].dead);
 init();count=2;Port_UpdateTarget();assert(gPortTarget[0]==1&&gPortTarget[1]==0);
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS actual target update/camera/beam aim: unrelated old-target action, recovered hit hold, committed beam/sequence/clash/reaction, CPU hold and 1v1')
