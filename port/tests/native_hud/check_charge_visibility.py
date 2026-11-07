# v0.4.2 B1/B2: with more than two fighters a technique's start (flag 0x12A) hides only its target, never the
# caster's helpers, a fighter that is changing, or one starting its own technique. Compiles the actual
# BtlChars_UpdateObjFlag2 from src/battle/btl_char_mgr.c (PORT) against a small fighter table; ASan/UBSan.
from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
mgr=(r/'src/battle/btl_char_mgr.c').read_text()
fn=mgr[mgr.index('void BtlChars_UpdateObjFlag2(void)'):mgr.index('/* Clears bit 1')]
fn=fn.replace('chr = BtlChar_Get(','chr = (BtlMgrChr*)BtlChar_Get(').replace('chr=BtlChar_Get(','chr=(BtlMgrChr*)BtlChar_Get(')
head=r'''#include <cassert>
#include <cstdio>
#include <cstring>
typedef int s32;typedef unsigned u32;typedef unsigned char u8;
struct Obj{int flags;};struct BtlMgrChr{int objId;};
static BtlMgrChr actors[6];static Obj objs[6];static int count=4,f12A[6],f30[6],fB[6],action[6],about=1,owner=-1;
int gPortTarget[10];
void*BtlChar_Get(int i){assert(i>=0&&i<count);return &actors[i];}int BtlChar_GetCount(){return count;}
static int idx(void*p){return (BtlMgrChr*)p-actors;}
int BtlChar_TestFlag(void*p,int f){int i=idx(p);return f==0x12A?f12A[i]:f==0x30?f30[i]:f==0xB?fB[i]:0;}
Obj*BtlChar_GetObj(void*p){return &objs[idx(p)];}int BtlAct_GetCurrent(void*p){return action[idx(p)];}
int Port_AboutPlayer(int){return about;}int Port_FormPresentationOwner(){return owner;}
int Port_AssistQteLeader(int){return -1;}
static int storyScene;int Port_StorySceneActorShown(int side){return storyScene&&side<2;}
static int hiddenHelper=-1;int Port_StoryHiddenAttackHelper(int actor){return actor==hiddenHelper;}
#define PORT 1
'''
foot=r'''
static int shown(int i){return (objs[i].flags&2)!=0;}
static void reset(){memset(f12A,0,sizeof f12A);memset(f30,0,sizeof f30);memset(fB,0,sizeof fB);memset(action,0,sizeof action);
 for(int i=0;i<6;i++){actors[i].objId=i;objs[i].flags=0;action[i]=11;}about=1;owner=-1;count=4;
 gPortTarget[0]=1;gPortTarget[1]=0;gPortTarget[2]=1;gPortTarget[3]=0;gPortTarget[4]=5;gPortTarget[5]=4;}
int main(){
 reset();hiddenHelper=2;BtlChars_UpdateObjFlag2();assert(shown(0)&&shown(1)&&!shown(2)&&shown(3));hiddenHelper=-1;BtlChars_UpdateObjFlag2();assert(shown(2));
 reset();storyScene=1;fB[1]=fB[2]=1;BtlChars_UpdateObjFlag2();assert(shown(1)&&!shown(2));
 storyScene=0;BtlChars_UpdateObjFlag2();assert(!shown(1)&&!shown(2));
 reset();BtlChars_UpdateObjFlag2();for(int i=0;i<4;i++)assert(shown(i));
 /* caster 0 charges at 1: only 1 hidden; its helper 2 and the other enemy 3 stay drawn (B1/B2) */
 reset();f12A[0]=1;action[2]=7;BtlChars_UpdateObjFlag2();assert(shown(0)&&!shown(1)&&shown(2)&&shown(3));
 /* someone transforming during the charge stays drawn, also when it is the target (B1 "empty" transform) */
 reset();f12A[0]=1;action[3]=0xF0;BtlChars_UpdateObjFlag2();assert(shown(3));
 reset();f12A[0]=1;action[1]=0xEC;BtlChars_UpdateObjFlag2();assert(shown(1));
 reset();f12A[0]=1;owner=1;BtlChars_UpdateObjFlag2();assert(shown(1));
 /* two starts aimed at each other: both drawn */
 reset();f12A[0]=f12A[1]=1;BtlChars_UpdateObjFlag2();assert(shown(0)&&shown(1)&&shown(2)&&shown(3));
 /* a start not about a player hides nobody (as before) */
 reset();f12A[0]=1;about=0;BtlChars_UpdateObjFlag2();for(int i=0;i<4;i++)assert(shown(i));
 /* native 0x30 / 0xB hiding is kept, for the caster too */
 reset();f12A[0]=1;fB[0]=1;f30[2]=1;BtlChars_UpdateObjFlag2();assert(!shown(0)&&!shown(1)&&!shown(2)&&shown(3));
 /* bad targets are ignored */
 reset();f12A[0]=1;gPortTarget[0]=0;BtlChars_UpdateObjFlag2();for(int i=0;i<4;i++)assert(shown(i));
 reset();f12A[0]=1;gPortTarget[0]=-1;BtlChars_UpdateObjFlag2();for(int i=0;i<4;i++)assert(shown(i));
 reset();f12A[0]=1;gPortTarget[0]=9;BtlChars_UpdateObjFlag2();for(int i=0;i<4;i++)assert(shown(i));
 /* six fighters, two charges at once */
 reset();count=6;f12A[0]=f12A[4]=1;BtlChars_UpdateObjFlag2();assert(shown(0)&&!shown(1)&&shown(2)&&shown(3)&&shown(4)&&!shown(5));
 /* two fighters: the game's own rule (only the caster drawn) */
 reset();count=2;f12A[0]=1;BtlChars_UpdateObjFlag2();assert(shown(0)&&!shown(1));
 puts("PASS charge visibility: >2 fighters hide only the start's target; helpers, changing fighters, form owner, mutual starts kept; non-player starts, 0x30/0xB, bad targets, 6 fighters, 2-fighter native rule; ASan/UBSan");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(head+fn+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
