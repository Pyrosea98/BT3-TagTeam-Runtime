#include <assert.h>
#include <stdio.h>
#include "battle/eft_i.h"
static int killed;
#define PARTICLE(N) void N##_Kill(void*h){if(h)killed++;} s32 N##_IsAlive(void*h){return h!=0;}
PARTICLE(EftRay) PARTICLE(EftRays) PARTICLE(EftBill) PARTICLE(EftRibbon) PARTICLE(EftChain) PARTICLE(EftAnimPart) PARTICLE(EftPtcl) PARTICLE(EftQuad) PARTICLE(EftPart10) PARTICLE(EftLink) PARTICLE(EftZap)
extern s32 EftEmit_UpdateAlive(EftEmitSet*,EftEmitState*);
extern void EftEmit_KillAll(EftEmitSet*,EftEmitState*);
extern void EftEmit_MarkKind6(EftEmitSet*,EftEmitState*);
int main(void){
 EftEmitSet set={0};EftEmitHdr hdr={0};EftEmitGroupDef group={0};EftEmitDef def={0};EftEmitState state={0};
 set.hdr=&hdr;set.defs=&def;hdr.mask=(1u<<5)|(1u<<17);set.grp[5].def=&group;group.count=1;
 state.handle[0].h[0]=(void*)42;state.handle[1].h[0]=(void*)43;def.kind=6;
 EftEmit_MarkKind6(&set,&state);assert(state.flags[0]&EFT_EMIT_KIND6);
 assert(EftEmit_UpdateAlive(&set,&state)==(1u<<5));
 EftEmit_KillAll(&set,&state);assert(killed==2&&!state.handle[0].h[0]&&!state.handle[1].h[0]);
 assert(!EftEmit_UpdateAlive(&set,&state));
 puts("PASS actual native emitter lifecycle: absent group ignored, valid particles marked/tracked/killed in both halves");return 0;
}
