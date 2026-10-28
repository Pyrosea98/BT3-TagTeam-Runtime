from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd();s=(r/'port/src/fight_panels.c').read_text();a=s.index('static void read_fusion_partners(');b=s.index('static void read_revive_rings(',a);body=s[a:b]
head=r"""#include <cassert>
#include <cstdio>
#include <cstring>
#include "fight_panels.h"
struct FpPoint{float x,y,z,w;};static FpActor sHudLastAlive[FP_ACTORS];static unsigned char actors[3][0x1600],memberRow[0xa4];static int active=1,source=2,temp=101,projectX=42;
void *BtlChar_Get(int i){return i>=0&&i<3?actors[i]:nullptr;}int Port_FusionPartnerInfo(void*c,int*s,int*o,int*m){if(c!=actors[0]||!active)return 0;*s=source;*o=temp;*m=1;return 1;}void *BtlMember_Get(void*,int){return memberRow;}int rd(void*p,int o){return *(int*)((char*)p+o);}int Port_Team(int){return 0;}const char *Port_CharaName(int){return "Vegeta";}
void BtlCharApi_GetNodePos(int obj,int,FpPoint *p){assert(obj==101);p->x=projectX;p->y=55;}int Port_ModHudProject(int,FpPoint*p,int*x,int*y){*x=p->x;*y=p->y;return 1;}
"""
foot=r"""int main(){FpState st={};st.actorCount=3;st.viewCount=1;*(int*)memberRow=34;sHudLastAlive[2].present=1;sHudLastAlive[2].chara=34;sHudLastAlive[2].player=2;sHudLastAlive[2].hp=70000;read_fusion_partners(&st);assert(st.actorCount==3&&st.actor[2].present&&st.actor[2].player==2&&st.actor[2].hp==70000&&st.actor[2].x[0]==42);projectX=75;read_fusion_partners(&st);assert(st.actor[2].x[0]==75);active=0;memset(&st.actor[2],0,sizeof(FpActor));read_fusion_partners(&st);assert(!st.actor[2].present);active=1;source=-1;*(int*)(memberRow+0x40)=40000;read_fusion_partners(&st);assert(st.actorCount==4&&st.actor[3].present&&st.actor[3].hp==40000&&st.actor[3].player==0);st.actorCount=FP_ACTORS;read_fusion_partners(&st);assert(st.actorCount==FP_ACTORS);puts("PASS actual fusion partner snapshot: temporary-model projection follows motion, P2 identity/HP retained after consumption, no post-cinematic ghost, serial partner bounded allocation");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot);subprocess.run(['g++','-std=c++17','-I'+str(r/'port/src/gs'),str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
