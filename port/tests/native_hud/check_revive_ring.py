from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/src/fight_panels.c').read_text();a=s.index('static void read_revive_rings(');b=s.index('\nstatic void read_actors',a);body=s[a:b]
head=r"""#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "fight_panels.h"
struct FpPoint{float x,y,z,w;};static int chars[2][0x1600/4],member[2][48],net,replay,shown=1,consumed,frame;static float pos[2][4]={{10,20,30,1},{100,50,60,1}},lastY;
int Port_Setting(const char*k,int d){if(!strcmp(k,"show_revive_ring"))return shown;return d;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlChar_FrameMod(int){return frame;}
void*BtlChar_Get(int i){return chars[i];}int BtlChar_GetCount(){return 2;}int index(void*p){return (int*)p==chars[0]?0:1;}void*BtlMember_GetActive(void*p){return member[index(p)];}void*BtlChar_GetPos(void*p){return pos[index(p)];}float BtlCharApi_GetGroundY(int){return 5;}int rd(void*p,int o){return *(int*)((char*)p+o);}int Port_FightConsumed(int){return consumed;}int Port_Team(int i){return i;}
int Port_ModHudProject(int q,FpPoint*p,int*x,int*y){assert(q<4);*x=(int)p->x;*y=(int)p->z;lastY=p->y;return p->w==1;}
"""
body=body.replace('p=BtlChar_GetPos(c)','p=(float*)BtlChar_GetPos(c)')
foot=r"""int main(){FpState st={};st.viewCount=4;member[0][2]=member[1][2]=1;member[0][0x44/4]=member[1][0x44/4]=10000;member[1][0x40/4]=1;read_revive_rings(&st);assert(st.reviveRing[0].active&&!st.reviveRing[1].active&&st.reviveRing[0].x[0][0]==90&&st.reviveRing[0].x[0][12]==-70&&st.reviveRing[0].visible[3][24]);assert(lastY<5);float y=lastY;frame=20;read_revive_rings(&st);assert(lastY!=y);memset(&st.reviveRing,0,sizeof(st.reviveRing));shown=0;read_revive_rings(&st);assert(!st.reviveRing[0].active);shown=1;net=1;read_revive_rings(&st);assert(!st.reviveRing[0].active);net=0;replay=1;read_revive_rings(&st);assert(!st.reviveRing[0].active);puts("PASS actual revival ring snapshot: exact range, ground height/wave,4view projection, living/disabled/online/replay exclusions; ASan/UBSan");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot)
 subprocess.run(['g++','-std=c++17','-I'+str(r/'port/src/gs'),'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
