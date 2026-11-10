#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "battle/eft_j.h"
static int spawned,requests,resets,deferred,diagnostics,alt;
void Port_Dbg3(const char *s,int actor,int group,int mask,int unused){assert(actor==9&&group==3);diagnostics++;}
s32 EftEmit_GetFlagsFromReq(EftJSet *s,void *e,s32 actor,s32 group,s32 part,s32 f1,s32 f4){assert(group==2&&part<2);requests++;return 1;}
s32 EftEmit_GetResetFlags(EftJSet *s,void *e,s32 group,s32 part){assert(group==2);resets++;return 2;}
s32 EftEmit_IsPartDeferred(EftJSet *s,s32 group,s32 part){deferred++;return part==0;}
u8 EftEmit_GetHead26(void *s){return alt;}
void EftEmit_SpawnOwn(EftJSet *s,void *e,void *n,void *pos,void *dir,s32 group,s32 part,s32 res,f32 scale){assert(group==2&&part<2);spawned++;}
extern void EftShotTech_UpdateParts(s32,EftJTask*,EftJSet*);
extern void EftMulti_UpdateParts(s32,EftJTask*,EftJSet*,s32);
extern void EftPropShot_UpdateParts(s32,EftJTask*,EftJSet*,s32);
extern void EftBlast_UpdateParts(s32,EftJTask*,EftJSet*,s32);
int main(void){
 u32 mask=(1u<<2)|(1u<<3);u8 info[2]={2,2};EftJPart parts[2]={0};
 EftJSet set={0};EftJTask task={0};void *work=calloc(1,8192);
 set.mask=&mask;set.parts=parts;set.grp[2].info=info;task.work=work;
 /* Exact fault: advertised group3 with NULL descriptor; group2 still renders. */
 EftShotTech_UpdateParts(9,&task,&set);assert(spawned==2&&requests==2&&diagnostics==1);
 EftMulti_UpdateParts(9,&task,&set,0);EftPropShot_UpdateParts(9,&task,&set,0);EftBlast_UpdateParts(9,&task,&set,0);
 assert(spawned==8&&requests==8&&diagnostics==4);
 EftShotTech_UpdateParts(9,&task,&set);assert(diagnostics==4); /* bounded logs */
 EftMulti_UpdateParts(9,&task,&set,1);EftPropShot_UpdateParts(9,&task,&set,1);EftBlast_UpdateParts(9,&task,&set,1);assert(resets==6);
 /* Present group in normal packs must keep both emitters and muzzle route. */
 mask=1u<<2;parts[0].type=4;alt=1;spawned=0;EftShotTech_UpdateParts(9,&task,&set);assert(spawned==2);
 /* Deferred shot parts retain original scheduling. */
 ((EftJShotTech*)work)->flags=0x20000;spawned=0;EftShotTech_UpdateParts(9,&task,&set);assert(deferred==2&&spawned==1);
 free(work);puts("PASS actual four native emitter walkers: missing group, valid parts, reset, muzzle, deferred and bounded diagnostics");return 0;
}
