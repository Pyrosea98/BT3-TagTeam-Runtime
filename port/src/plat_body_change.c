/* Actual-hit Ginyu Body Change. Native model jobs, not a controller/texture swap.
 * The original cinematic owns the first job; the second job is acknowledged here
 * because its victim cannot be relied upon to dispatch while the loader stops time.
 * Requests remain serialized by BtlChange. No participant resumes between commits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "battle/btl_char_member.h"
extern void *BtlChar_Get(s32);
extern s32 BtlChar_GetCount(void),Port_IsEnemy(s32,s32),Port_NetActive(void),BattleReplay_IsActive(void);
extern void BtlChange_RequestChara(s32,s32,s32,s32,s32,s32,s32),BtlChange_SetReady(s32),BtlChange_SetDone(s32);
extern void BtlAct_Request(void*,s32),BtlChar_SetFlag(void*,s32);
#define BC_MAX 16
static struct {
 int active,source,target,sourceSlot,targetSlot,loaded,queued,ownersReady;
 unsigned owners;
 BtlMember body[2];
} bc;
static BtlMember denied[BC_MAX];
static int deniedSlot[BC_MAX],deniedValid[BC_MAX];
static int rd(void*p,int off){return *(s32*)((u8*)p+off);}
static void wr(void*p,int off,int v){*(s32*)((u8*)p+off)=v;}
static BtlMember *member(void *p){int slot=rd(p,0x994),count=rd(p,0x998);return slot>=0&&slot<count&&count<=5?(BtlMember*)((u8*)p+0x9a4+slot*sizeof(BtlMember)):NULL;}
static void form(void*p,const BtlMember*m){
 int *f=(int*)((u8*)p+0x12cc);int i;for(i=0;i<12;i++)f[i]=-1;
 f[1]=m->chara;f[2]=m->costume;f[3]=f[4]=0;f[5]=m->gauge.variant;f[10]=0;
}
void Port_BodyChangeReset(void){memset(&bc,0,sizeof(bc));memset(deniedValid,0,sizeof(deniedValid));}
/* 0: untouched original action (online/replays or not Body Change).
 * 1: form request supplied, including a no-change request for denied local swaps. */
int Port_BodyChangeForm(void *actor){
 int s,t,i,n=BtlChar_GetCount(),action;void *victim;BtlMember *a,*b;
 if(Port_NetActive()||BattleReplay_IsActive()||!actor||rd(actor,0xe90)!=0x280)return 0;
 s=rd(actor,0);if(s<0||s>=n||s>=BC_MAX||(a=member(actor))==NULL||a->chara!=86)return 0;
 if(!bc.ownersReady){for(i=0;i<n&&i<BC_MAX;i++){void*p=BtlChar_Get(i);BtlMember*m=p?member(p):NULL;if(m&&m->chara==86)bc.owners|=1u<<i;}bc.ownersReady=1;}
 /* Native rejection must not turn into a random body or a free health refill. */
 denied[s]=*a;deniedSlot[s]=rd(actor,0x994);deniedValid[s]=1;form(actor,a);
 t=rd(actor,0xe98);action=rd(actor,0x948);
 if(bc.active||!(bc.owners&(1u<<s))||rd(actor,0xe94)!=s||action<301||action>303||t<0||t>=n||t>=BC_MAX||!Port_IsEnemy(s,t))return 1;
 victim=BtlChar_Get(t);b=victim?member(victim):NULL;
 if(!b||!a->present||!b->present||a->gauge.health<=0||b->gauge.health<=0||a->gauge.healthMax<=0||b->gauge.healthMax<=0||a->gauge.health>a->gauge.healthMax||b->gauge.health>b->gauge.healthMax)return 1;
 if(rd(victim,0xe90)!=0x280||rd(victim,0xe94)!=s||rd(victim,0xe98)!=t||rd(victim,0x948)<313||rd(victim,0x948)>315)return 1;
 /* An in-fight fusion has a separate partner/timer lifecycle. Never duplicate it. */
 if(a->gauge.fused||b->gauge.fused)return 1;
 bc.active=1;bc.source=s;bc.target=t;bc.sourceSlot=rd(actor,0x994);bc.targetSlot=rd(victim,0x994);bc.body[0]=*a;bc.body[1]=*b;bc.loaded=bc.queued=0;
 deniedValid[s]=0;form(actor,b);
 fprintf(stderr,"bt3: body change captured actual pair %d/%d bodies %d:%d <-> %d:%d\n",s,t,a->chara,a->costume,b->chara,b->costume);
 return 1;
}
/* Called before the change queue retires DONE. State values are the native ABI. */
static void probe(void){
 static int frames,started;void*c,*target;BtlMember*m;
 extern void BtlChar_ClearFlag(void*,int);extern int BtlSeq_GetState(void),BtlCtrl_UseTechnique(int,int);extern void BtlAct_SetQueue(void*,unsigned,int);extern int BtlAct_GetTechniqueAction(void*,int);
 if(!getenv("BT3_BODY_CHANGE_PROBE")||started||BtlSeq_GetState()!=3)return;
 c=BtlChar_Get(0);target=BtlChar_Get(1);m=c?member(c):NULL;if(!m||m->chara!=86||!target||rd(c,0x948)>0x37||rd(target,0x948)>0x37)return;
 if(++frames<60)return;
 if(BtlCtrl_UseTechnique(0,4)){int f,action=BtlAct_GetTechniqueAction(c,4);BtlChar_ClearFlag(c,0x11e);for(f=0x120;f<=0x124;f++)BtlChar_ClearFlag(c,f);BtlAct_SetQueue(c,0,0x105);BtlAct_SetQueue(c,1,action);BtlAct_Request(c,0x105);started=1;fprintf(stderr,"bt3: owned body change probe requested native ultimate action %x\n",action);}
}
void Port_BodyChangeQueue(int owner,int state){
 void *victim;probe();if(!bc.active)return;
 if(!bc.queued&&(bc.loaded&1)&&owner==bc.source&&state==5){
  victim=BtlChar_Get(bc.target);if(!victim||rd(victim,0x994)!=bc.targetSlot)return;
  form(victim,&bc.body[0]);bc.queued=1;
  BtlChange_RequestChara(bc.target,bc.body[0].chara,bc.body[0].costume,bc.body[0].gauge.variant,-1,-1,-1);
 }
 if(bc.queued&&owner==bc.target&&state==3)BtlChange_SetReady(bc.target);
}
static void publish(int side,int slot,const BtlMember*m,int restrictAbilities){
 void*p=BtlChar_Get(side);BtlMember*dst;if(!p||rd(p,0x994)!=slot)return;dst=member(p);if(!dst)return;
 *dst=*m;dst->gauge.unk30=restrictAbilities;
}
/* Native skill/parameter binding must see the incoming identity even when the
 * victim finished its cinematic before its queued model job was serviced. */
void Port_BodyChangeBeforeLoaded(void *actor){
 int side=rd(actor,0);BtlMember*m;
 if(!bc.active||(m=member(actor))==NULL)return;
 if(side==bc.source&&rd(actor,0x994)==bc.sourceSlot)*m=bc.body[1];
 if(side==bc.target&&bc.queued&&rd(actor,0x994)==bc.targetSlot)*m=bc.body[0];
}
/* After native model/parameter/skill binding, restore the body's actual gauges,
 * bonuses and abilities. The native attacker already bound restricted common
 * technique tables (flag A6); retain that intended stolen-body limitation. */
void Port_BodyChangeLoaded(void *actor){
 int side=rd(actor,0);if(side<0||side>=BC_MAX)return;
 if(deniedValid[side]&&rd(actor,0x994)==deniedSlot[side]){publish(side,deniedSlot[side],&denied[side],denied[side].gauge.unk30);deniedValid[side]=0;return;}
 if(!bc.active)return;
 if(side==bc.source){publish(side,bc.sourceSlot,&bc.body[1],1);bc.loaded|=1;}
 if(side==bc.target&&bc.queued){
  publish(bc.source,bc.sourceSlot,&bc.body[1],1);publish(side,bc.targetSlot,&bc.body[0],0);bc.loaded|=2;
  BtlChange_SetDone(side);
  BtlAct_Request(BtlChar_Get(bc.source),0xB);BtlAct_Request(actor,0xB);
  BtlChar_SetFlag(BtlChar_Get(bc.source),0x55);BtlChar_SetFlag(actor,0x55);
  fprintf(stderr,"bt3: body change committed actual pair %d/%d; controllers/teams retained, body gauges preserved\n",bc.source,bc.target);
  bc.active=0;
 }
}
