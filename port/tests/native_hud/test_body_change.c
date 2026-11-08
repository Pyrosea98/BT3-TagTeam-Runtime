#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "battle/btl_char_member.h"
#include "battle/btl_char_ctl.h"
#include "battle/btl_char_action.h"
static unsigned char actors[4][0x1600];
static BtlCtlRoster roster;
BtlCtlRoster *gBtlChars=&roster;
static int online,replay,requested[4],flags[4];
extern int Port_BodyChangeForm(void*);
extern void Port_BodyChangeLoaded(void*),Port_BodyChangeReset(void);
void *BtlChar_Get(int i){return i>=0&&i<4?actors[i]:0;}
int BtlChar_GetCount(void){return 4;}
int Port_NetActive(void){return online;}int BattleReplay_IsActive(void){return replay;}
int Port_IsEnemy(int a,int b){return a!=b&&a%2!=b%2;}
BtlMember *BtlMember_GetActive(BtlMemberChr*p){return &p->members[p->active];}
void BtlChar_SetFlag(void*p,int a){flags[*(int*)p]|=a==0x55;}
int Port_StoryRetired(int i){(void)i;return 0;}int Port_StorySceneParked(int i){(void)i;return 0;}
void Port_FusionChangeLog(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
void Port_Dbg3(const char*w,int a,int b,int c,int d){(void)w;(void)a;(void)b;(void)c;(void)d;}
int BtlChar_FrameMod(int n){return n?0:0;}
void BtlChar_ClearFlag(void*p,int f){(void)p;(void)f;}
int BtlSeq_GetState(void){return 0;}int BtlCtrl_UseTechnique(int p,int s){return 0;}
int BtlAct_GetTechniqueAction(void*p,int s){return 0;}
static void w(void*p,int o,int x){*(int*)((char*)p+o)=x;}
static int v(void*p,int o){return *(int*)((char*)p+o);}
static BtlMember *m(int i){return (BtlMember*)(actors[i]+0x9a4);}
static void setup(void){int i;memset(actors,0,sizeof(actors));memset(&roster,0,sizeof(roster));memset(requested,0,sizeof(requested));memset(flags,0,sizeof(flags));online=replay=0;BtlChange_Reset();
 for(i=0;i<4;i++){w(actors[i],0,i);w(actors[i],4,i);w(actors[i],8,i%2);w(actors[i],12,i+8);w(actors[i],0x998,1);m(i)->present=1;m(i)->chara=i?10:86;m(i)->costume=i?2:0;m(i)->gauge.health=12345+i*20000;m(i)->gauge.healthMax=100000+i*10000;m(i)->gauge.ki=23456+i;m(i)->gauge.kiMax=100000;m(i)->gauge.blast=34567+i;m(i)->gauge.blastMax=400000;m(i)->bonus[1]=i*3;m(i)->cpuLevel=i*4;}
 w(actors[0],0x948,301);w(actors[1],0x948,313);for(i=0;i<2;i++){w(actors[i],0xe90,0x280);w(actors[i],0xe94,0);w(actors[i],0xe98,1);}
}
static void load(int side){/* The real loader has bound the new body before this callback. */
 m(side)->chara=999;m(side)->gauge.health=m(side)->gauge.healthMax=999999;Port_BodyChangeLoaded(actors[side]);
}
int main(void){BtlMember a,b;int i;_Static_assert(sizeof(BtlMember)==0xa4,"native member ABI");_Static_assert(offsetof(BtlActChr,form)==0x12cc,"native form ABI");
 setup();a=*m(0);b=*m(1);
 /* Actual native picker entry; a separate current lock-on could point at 3. */
 BtlAct_SetFormRandom((BtlActChr*)actors[0]);assert(v(actors[0],0x12d0)==b.chara&&v(actors[0],0x12d4)==b.costume);
 BtlChange_RequestChara(0,b.chara,b.costume,b.gauge.variant,-1,-1,-1);BtlChange_Update();assert(roster.change.cur->player==0&&roster.change.timeStop);
 BtlChange_SetTaken();BtlChange_SetLoaded();BtlChange_SetReady(0);load(0);assert(m(0)->chara==b.chara&&m(0)->gauge.health==b.gauge.health&&m(0)->gauge.unk30==1);
 BtlChange_SetDone(0);BtlChange_Update();assert(roster.change.cur->player==1&&roster.change.timeStop);assert(roster.change.cur->chara==86);
 BtlChange_SetTaken();BtlChange_SetLoaded();BtlChange_Update();assert(roster.change.state==4);load(1);assert(roster.change.state==5);BtlChange_Update();assert(!roster.change.cur&&!roster.change.timeStop);
 assert(m(1)->chara==86&&m(1)->gauge.health==a.gauge.health&&m(1)->gauge.ki==a.gauge.ki&&m(1)->gauge.blast==a.gauge.blast&&m(1)->gauge.unk30==0);
 b.gauge.unk30=1;assert(!memcmp(m(0),&b,sizeof(b)));assert(!memcmp(m(1),&a,sizeof(a)));
 for(i=0;i<4;i++){assert(v(actors[i],0)==i&&v(actors[i],4)==i&&v(actors[i],8)==i%2&&v(actors[i],12)==i+8);}assert(v(actors[0],0x94c)==0xb&&v(actors[1],0x94c)==0xb&&flags[0]&&flags[1]);
 /* Receiving Ginyu's body does not grant repeat body swaps. Denied swaps do not heal. */
 w(actors[1],0x948,301);w(actors[0],0x948,313);for(i=0;i<2;i++){w(actors[i],0xe94,1);w(actors[i],0xe98,0);}a=*m(1);assert(Port_BodyChangeForm(actors[1]));assert(v(actors[1],0x12d0)==86);load(1);assert(!memcmp(&a,m(1),sizeof(a)));
 /* Extra physical fighters use the same transaction, retaining P3/P4 ownership. */
 setup();m(2)->chara=86;a=*m(2);b=*m(3);w(actors[2],0x948,301);w(actors[3],0x948,313);for(i=2;i<4;i++){w(actors[i],0xe90,0x280);w(actors[i],0xe94,2);w(actors[i],0xe98,3);}
 BtlAct_SetFormRandom((BtlActChr*)actors[2]);BtlChange_RequestChara(2,b.chara,b.costume,0,-1,-1,-1);BtlChange_Update();BtlChange_SetLoaded();BtlChange_SetReady(2);load(2);BtlChange_SetDone(2);BtlChange_Update();assert(roster.change.cur->player==3);BtlChange_SetLoaded();BtlChange_Update();assert(roster.change.state==4);load(3);BtlChange_Update();assert(!roster.change.cur);b.gauge.unk30=1;assert(!memcmp(m(2),&b,sizeof(b))&&!memcmp(m(3),&a,sizeof(a)));assert(v(actors[2],4)==2&&v(actors[3],4)==3);
 /* Wrong reciprocal victim, allied/self/dead bodies and active fusion receipts: no stolen body. */
 setup();w(actors[1],0xe94,3);assert(Port_BodyChangeForm(actors[0]));assert(v(actors[0],0x12d0)==86);
 setup();m(1)->gauge.health=0;assert(Port_BodyChangeForm(actors[0]));assert(v(actors[0],0x12d0)==86);
 setup();m(1)->gauge.fused=1;assert(Port_BodyChangeForm(actors[0]));assert(v(actors[0],0x12d0)==86);
 setup();online=1;assert(!Port_BodyChangeForm(actors[0]));online=0;replay=1;assert(!Port_BodyChangeForm(actors[0]));
 /* Reset cancels every receipt. Repeated rounds always begin with original bodies. */
 setup();assert(Port_BodyChangeForm(actors[0]));BtlChange_Reset();a=*m(0);Port_BodyChangeLoaded(actors[0]);assert(!memcmp(&a,m(0),sizeof(a)));
 puts("PASS native Ginyu picker/serialized queue: actual pair, reciprocal body gauges/stats/costumes, owner/team retention, no heal on denial, restricted stolen skills, reset/rematch, online/replay isolation");return 0;}
