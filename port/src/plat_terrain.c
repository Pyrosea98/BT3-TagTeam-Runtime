#include <stdio.h>
#include <string.h>
#include "mod_terrain_bridge.h"
#include "mod_terrain_walls.h"
#include "mod_terrain_charge.h"
#include "mod_terrain_collision_cache.h"
static TccCache sCollisionCache;
static TwMesh sWalls;
extern void Port_TerrainCollectStatic(void*,void*);
extern void Port_TerrainApplyStructures(void*);
extern int Port_TerrainProfOn(void);extern unsigned Port_TerrainProfNow(void);extern void Port_TerrainProfAdd(int,unsigned);extern void Port_TerrainProfCount(int);
static void td_apply_structures(void*s){unsigned t0=Port_TerrainProfNow();Port_TerrainApplyStructures(s);Port_TerrainProfAdd(5,t0);}
void Port_TerrainCollectTriangle(void*i,void*t,const void*v){TW_Collect(i,t,v);}
static int sChargeSlot[16],sTerrainMaterial=-1;
static TvaStones sImpactStones;
static TdState sTerrain;static unsigned char sTerrainLock;static unsigned sLogs;
static void td_lock(void){if(__atomic_test_and_set(&sTerrainLock,__ATOMIC_ACQUIRE)){unsigned t0=Port_TerrainProfNow();while(__atomic_test_and_set(&sTerrainLock,__ATOMIC_ACQUIRE)){}Port_TerrainProfAdd(6,t0);} }
static void td_unlock(void){__atomic_clear(&sTerrainLock,__ATOMIC_RELEASE);}
void Port_TerrainReset(void){td_lock();memset(&sImpactStones,0,sizeof(sImpactStones));TCC_Clear(&sCollisionCache);TW_Clear(&sWalls);memset(&sTerrain,0,sizeof(sTerrain));memset(sChargeSlot,0,sizeof(sChargeSlot));sTerrainMaterial=-1;sLogs=0;td_unlock();}
void Port_TerrainConfigure(int stage,int enabled){td_lock();if(sTerrain.stage!=stage||!enabled){memset(&sImpactStones,0,sizeof(sImpactStones));TCC_Clear(&sCollisionCache);TW_Clear(&sWalls);memset(&sTerrain,0,sizeof(sTerrain));memset(sChargeSlot,0,sizeof(sChargeSlot));sTerrainMaterial=-1;sTerrain.stage=stage;}sTerrain.enabled=enabled;td_unlock();}
void Port_TerrainSetMaterial(int stage,int kind){td_lock();if(stage==sTerrain.stage)sTerrainMaterial=kind>=0&&kind<=8?kind:-1;td_unlock();}
int Port_TerrainGetMaterial(int stage){int kind;td_lock();kind=stage==sTerrain.stage?sTerrainMaterial:-1;td_unlock();return kind;}
int Port_TerrainSnapshot(void*out){int n;td_lock();memcpy(out,&sTerrain,sizeof(sTerrain));n=sTerrain.enabled?sTerrain.count:0;td_unlock();return n;}
/* Cache footprint bounds at commit. Queries use the same locked generation as
   the snapshot and only copy it when the original mesh must be replaced. */
static float sQueryBounds[TD_MAX][4];
int Port_TerrainSnapshotNearBox(void *out, const float *lo, const float *hi, float *depth) {
    float deepest = 0;
    td_lock();
    if (sTerrain.enabled) for (int i=0; i<sTerrain.count; ++i) {
        const float *b = sQueryBounds[i];
        if (hi[0]<b[0] || lo[0]>b[2] || hi[2]<b[1] || lo[2]>b[3]) continue;
        deepest = td_max(deepest, sTerrain.scars[i].depth);
    }
    if (deepest > 0) memcpy(out, &sTerrain, sizeof(sTerrain));
    td_unlock();
    *depth = deepest;
    return deepest > 0;
}
int Port_TerrainPartOverlap(const void *state,const float bounds[][4],const float *xyz,const float *radius) {
    const TdState *t=state;
    float r=*radius,x=xyz[0],y=xyz[1],z=xyz[2];
    float x0=x-r,x1=x+r,z0=z-r,z1=z+r;
    int result=0;
    for (int k=0;k<t->count;++k) {
        const float *b=bounds[k];
        if (x1<b[0] || x0>b[2] || z1<b[1] || z0>b[3]) continue;
        const TdScar *scar=t->scars+k;
        if (fabsf(y-scar->ay)<r+scar->r*1.2f+scar->depth+8) return 2;
        result=1;
    }
    return result;
}
static int td_commit_locked(TdState*next){TwInput in={0};TwMesh walls={0};int ok;unsigned t0=Port_TerrainProfNow();Port_TerrainCollectStatic(next,&in);ok=TW_Build(&in,next,&walls);if(ok){TW_Clear(&sWalls);sWalls=walls;sTerrain=*next;for(int i=0;i<next->count;++i)td_bounds(&next->scars[i],sQueryBounds[i]);}else{TW_Clear(&walls);fprintf(stderr,"bt3: terrain boundary build rejected (capacity/allocation)\n");}TW_InputClear(&in);Port_TerrainProfAdd(4,t0);return ok;}
static int td_add(void*event,int visual){TdEvent*e=event;int ret,count,stage;td_lock();TdState next=sTerrain;ret=TD_Add(&next,e);if(ret&&!td_commit_locked(&next))ret=0;
 if(ret&&visual&&!e->kind)TVA_Burst(&sImpactStones,e->ax,e->ay,e->az,e->r,e->sx,e->sz,next.serial,ret==2);
 count=sTerrain.count;stage=sTerrain.stage;td_unlock();if(ret)td_apply_structures(&next);if(sLogs++<128)fprintf(stderr,"bt3: terrain-dynamic stage=%d event=%d owner=%d result=%d count=%d xyz100=%d,%d,%d radius100=%d depth100=%d\n",stage,e->kind,e->owner,ret,count,(int)(e->ax*100),(int)(e->ay*100),(int)(e->az*100),(int)(e->r*100),(int)(e->depth*100));return ret;}
/* Fitted impacts keep the non-overlapping height-field invariant. */
static int td_impact(void*event,int visual){TdEvent e=*(TdEvent*)event;TdState t;int i;
 if(!Port_TerrainSnapshot(&t))return td_add(&e,visual);
 if(!e.kind){
  /* A contact on the bottom of an existing pit is still the same terrain layer.
   * Deepen it in-place instead of creating a second floor at the pit's Y. */
  for(i=0;i<t.count;i++){TdScar*c=t.scars+i;float ground=c->ay+c->sx*(e.ax-c->ax)+c->sz*(e.az-c->az);
   if(td_dist2(c,e.ax,e.az)<c->r*c->r*.8f&&e.ay>=ground-4&&e.ay<=ground+c->depth+8){
    int ret=0;td_lock();if(sTerrain.serial==t.serial&&c->level<3){TdState next=sTerrain;next.scars[i].depth=td_min(240,c->depth+e.depth*.35f);next.scars[i].level++;next.serial++;if(td_commit_locked(&next)){t=next;ret=2;if(visual)TVA_Burst(&sImpactStones,e.ax,e.ay,e.az,e.r,e.sx,e.sz,next.serial,1);}}td_unlock();if(ret)td_apply_structures(&t);return ret;
   }
  }
  int fit=TQ_Fit(&t,&e);if(fit==1)return td_add(&e,visual);return 0;
 }

 {float dx=e.bx-e.ax,dz=e.bz-e.az,len=sqrtf(dx*dx+dz*dz);int k,start=0,run=0,best=0,lo=0,hi=0;
  for(k=0;k<=128;k++){float u=(float)k/128.f,x=e.ax+dx*u,z=e.az+dz*u;int free=1;
   for(i=0;i<t.count;i++){TdScar*c=t.scars+i;if(fabsf(c->ay-e.ay)<140&&td_dist2(c,x,z)<(c->r+e.r+3)*(c->r+e.r+3)){free=0;break;}}
   if(free){if(!run)start=k;run++;if(run>best){best=run;lo=start;hi=k;}}else run=0;
  }
  /* One extra sample of clearance accounts for gaps between the fit samples. */
  if(lo>0)lo++;if(hi<128)hi--;if(hi<=lo||(hi-lo)*len/128.f<10)return 0;
  float u=(float)lo/128.f,v=(float)hi/128.f;e.bx=e.ax+dx*v;e.bz=e.az+dz*v;e.ax+=dx*u;e.az+=dz*u;e.ay+=e.sx*dx*u+e.sz*dz*u;
 }
 return td_add(&e,visual);
}
int Port_TerrainAdd(void *event){return td_add(event,0);}
int Port_TerrainImpact(void *event){return td_impact(event,0);}
int Port_TerrainImpactVisual(void *event){return td_impact(event,1);}

void Port_TerrainStonesTick(int paused){
 if(paused)return;
 td_lock();TVA_Step(&sImpactStones);td_unlock();
}
/* out is NULL for a count, otherwise an array of TVA_CAPACITY stones. */
int Port_TerrainStones(void *out){
 TvaStone *dst=out;int i,n=0;
 td_lock();
 for(i=0;i<TVA_CAPACITY;i++)if(sImpactStones.stones[i].life>0){
  if(dst)dst[n]=sImpactStones.stones[i];
  n++;
 }
 td_unlock();return n;
}
/* A charge owns one persistent pressure scar. Grow it in bounded steps, replacing
 * its own slot only. Never erase another attack's crater or heal a deeper scar. */
int Port_TerrainCharge(void*event,int step){
 TdEvent e=*(TdEvent*)event;TdState next,without;int ret=0,slot,owner=e.owner,i;float scale;
 if(owner<0||owner>=16||step<1||step>12)return 0;
 td_lock();next=sTerrain;slot=sChargeSlot[owner]-1;
 if(slot>=next.count||slot<0||td_dist2(next.scars+slot,e.ax,e.az)>4)slot=-1;
 without=next;
 if(slot>=0){for(i=slot;i+1<without.count;i++)without.scars[i]=without.scars[i+1];without.count--;}
 scale=(float)(step-1)/11.f;scale=scale*scale*(3.f-2.f*scale);
 e.r*=.15f+.85f*scale;e.depth*=.02f+.98f*scale;
 if(TQ_Fit(&without,&e)==1){
  if(slot>=0){TdScar old=next.scars[slot];if(e.r<old.r)e.r=old.r;if(e.depth<old.depth)e.depth=old.depth;}
  if(TD_Add(&without,&e)==1){
   if (slot >= 0) {
       const TdScar *updated = &without.scars[without.count - 1];
       if (!memcmp(&next.scars[slot], updated, sizeof(*updated))) {
           td_unlock();
           return 1;
       }
       next.scars[slot] = *updated;
   }
   else {slot=next.count;next.scars[next.count++]=without.scars[without.count-1];}
   next.serial++;
   if (td_commit_locked(&next)) {
       sChargeSlot[owner] = slot + 1;
       ret = 1;
   }
  }
 }
 td_unlock();if(ret)td_apply_structures(&next);return ret;
}
int Port_TerrainRemesh(void*s,const void*v,void(*cb)(const float*,void*),void*u){return TD_Remesh((const TdState*)s,v,cb,u);}

int Port_TerrainHasScar(void*point){float*p=point;TdState t;int i;if(!Port_TerrainSnapshot(&t))return 0;for(i=0;i<t.count;i++){TdScar*c=t.scars+i;if(td_dist2(c,p[0],p[2])<c->r*c->r&&fabsf(p[1]-c->ay)<c->depth+5)return 1;}return 0;}

int Port_TerrainActive(void){int n;td_lock();n=sTerrain.enabled?sTerrain.count:0;td_unlock();return n;}

int Port_TerrainEnabled(void){int on;td_lock();on=sTerrain.enabled;td_unlock();return on;}

int Port_TerrainRemeshBox(void*s,const void*v,const void*b,void(*cb)(const float*,void*),void*u){return TCC_Remesh(&sCollisionCache,(const TdState*)s,v,b,cb,u);}

/* Immutable skirt list, built only when the scar pool changes. Pointer/int ABI. */
int Port_TerrainWalls(void*state,const void*box,void(*cb)(const float*,void*),void*u){
 const TdState*t=state;const float*b=box;int i,n=0;td_lock();
 if(t->enabled&&t->serial==sTerrain.serial&&t->stage==sTerrain.stage)for(i=0;i<sWalls.n;i++){
 const float*v=sWalls.v+i*36;if(b&&(td_min(v[0],td_min(v[12],v[24]))>b[2]||td_max(v[0],td_max(v[12],v[24]))<b[0]||td_min(v[2],td_min(v[14],v[26]))>b[3]||td_max(v[2],td_max(v[14],v[26]))<b[1]))continue;cb(v,u);n++;}
 td_unlock();return n;
}
int Port_TerrainProfInfo(int which){int i,r=0;switch(which){case 0:return (int)sCollisionCache.builds;case 1:return (int)sCollisionCache.hits;case 2:return sTerrain.enabled?sTerrain.count:0;case 3:for(i=0;i<sTerrain.count;i++)if(sTerrain.scars[i].r>r)r=(int)sTerrain.scars[i].r;return r;case 4:return (int)sWalls.n;}return 0;}
