from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fun(path,sig):
 s=(r/path).read_text();a=s.index(sig);end=s.index('{',a)+1;n=1
 while n:n+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
code=r"""
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef int s32;typedef struct{int player,kind,id;}BtlChangeReq;
typedef struct{int state,loaded,timeStop;BtlChangeReq*cur;}BtlChangeQueue;
typedef struct{BtlChangeQueue change;}Roster;static Roster roster,*gBtlChars=&roster;
#define BTL_CHANGE_IDLE 0
#define BTL_CHANGE_STARTED 1
#define BTL_CHANGE_TAKEN 2
#define BTL_CHANGE_LOADED 3
#define BTL_CHANGE_READY 4
#define BTL_CHANGE_DONE 5
static int active=1,parked[16],retired[16],binds,doneBeforeBind;static BtlChangeReq *next;
int Port_StoryRetired(int i){return active&&i>=0&&i<16&&retired[i];}
int Port_StorySceneParked(int i){return active&&i>=0&&i<16&&parked[i];}
void Port_FusionChangeLog(int a,int b,int c,int d){}void Port_Dbg3(const char*s,int a,int b,int c,int d){}
void Port_BodyChangeQueue(int a,int b){}
void Port_ChangeRefill(void){}BtlChangeReq*BtlChange_Pop(void){BtlChangeReq*p=next;next=0;return p;}
typedef struct{int id;}BtlMgrChr;static BtlMgrChr chars[16];
BtlMgrChr*BtlChar_Get(int i){return i>=0&&i<16?chars+i:0;}
void BtlChar_OnModelLoaded(BtlMgrChr*c){assert(gBtlChars->change.state==BTL_CHANGE_READY);binds++;}
"""+fun('src/battle/btl_char_ctl_b.c','void BtlChange_Update(')+fun('src/battle/btl_char_ctl_b.c','void BtlChange_SetDone(')+fun('src/battle/btl_char_mgr.c','void BtlChars_OnModelLoaded(')+r"""
int main(void){BtlChangeReq dead={1},live={13};BtlChangeQueue*q=&roster.change;q->cur=&dead;q->loaded=1;q->state=3;
 BtlChange_Update();assert(q->state==3&&q->cur==&dead&&!binds);
 retired[1]=1;q->state=2;BtlChange_Update();assert(q->state==2&&q->cur==&dead);
 q->state=3;BtlChange_Update();assert(q->state==4&&q->cur==&dead&&q->loaded&&!binds);
 BtlChars_OnModelLoaded(1);assert(binds==1&&q->state==5);
 next=&live;BtlChange_Update();assert(q->cur==&live&&q->state==1&&!q->loaded&&q->timeStop);
 q->state=3;BtlChange_Update();assert(q->state==3&&q->cur==&live);
 q->state=4;BtlChars_OnModelLoaded(13);assert(binds==2&&q->state==4); /* live owner keeps its native handshake */
 parked[13]=1;q->state=3;BtlChange_Update();assert(q->state==4);BtlChars_OnModelLoaded(13);assert(q->state==5&&binds==3);
 active=0;q->cur=&live;q->state=3;BtlChange_Update();assert(q->state==3);active=1;
 q->cur=0;q->state=0;BtlChange_Update();assert(!q->cur&&!q->timeStop);
 puts("PASS actual retired/parked Story requests: waits for real load, READY -> native bind -> DONE, next fighter admitted; live owners untouched");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'load.c';p.write_text(code);subprocess.run(['cc','-DPORT','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
