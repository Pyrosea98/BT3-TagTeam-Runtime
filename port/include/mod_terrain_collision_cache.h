/* Simulation-thread cache of remeshed collision tiles. Never caches a callback,
 * fighter state, flags or a camera. Full state/content identity prevents stale hits. */
#ifndef MOD_TERRAIN_COLLISION_CACHE_H
#define MOD_TERRAIN_COLLISION_CACHE_H
#include <stdlib.h>
#include <string.h>
#include "mod_terrain_cache_bounds.h"
#ifdef __ANDROID__
#define TCC_ENTRIES 256
#define TCC_BUDGET (1024u*1024u)
#define TCC_ENTRY_BUDGET (256u*1024u)
#else
#define TCC_ENTRIES 2048
#define TCC_BUDGET (16u*1024u*1024u)
#define TCC_ENTRY_BUDGET (512u*1024u)
#endif
#define TCC_WAYS 4
#define TCC_TILE 128.f
typedef struct {float input[36],box[4],*v;unsigned n,bytes,stamp;int used,changed;} TccEntry;
typedef struct {TdState state;TccEntry e[TCC_ENTRIES];unsigned bytes,stamp,builds,hits;int valid;} TccCache;
typedef struct {float*v;unsigned n,cap;int failed;} TccBuild;
static void TCC_Clear(TccCache*c){int i;for(i=0;i<TCC_ENTRIES;i++)free(c->e[i].v);memset(c,0,sizeof(*c));}
static void tcc_collect(const float*v,void*user){TccBuild*b=user;if(b->failed)return;if(b->n==b->cap){unsigned cap=b->cap?b->cap*2:16;void*p;if(cap*144u>TCC_ENTRY_BUDGET){b->failed=1;return;}p=realloc(b->v,cap*144u);if(!p){b->failed=1;return;}b->v=p;b->cap=cap;}memcpy(b->v+b->n++*36,v,144);}
static int tcc_box_hit(const float*v,const float*b){return td_min(v[0],td_min(v[12],v[24]))<=b[2]&&td_max(v[0],td_max(v[12],v[24]))>=b[0]&&td_min(v[2],td_min(v[14],v[26]))<=b[3]&&td_max(v[2],td_max(v[14],v[26]))>=b[1];}
/* Four-way set associative lookup: bounded key work, not a full pool scan
   for every leaf. Keep exact key equality and the same tile/geometry semantics. */
/* Positions and tile coordinates select the bucket. The lookup below still
   compares every vertex attribute and tile bound before reusing geometry. */
static unsigned tcc_key(const float *triangle, const float *tile) {
    unsigned hash = 2166136261u;
    for (int i = 0; i < 3; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            unsigned word;
            memcpy(&word, triangle + i * TD_ATTR + axis, sizeof(word));
            hash = (hash ^ word) * 16777619u;
        }
    }
    for (int i = 0; i < 4; ++i) {
        unsigned word;
        memcpy(&word, tile + i, sizeof(word));
        hash = (hash ^ word) * 16777619u;
    }
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    return (hash ^ (hash >> 13)) & (TCC_ENTRIES / TCC_WAYS - 1);
}
static int TCC_Remesh(TccCache*c,const TdState*t,const float*v,const float*b,TdEmit cb,void*u){
 float tile[4];int i,slot=-1;unsigned oldest=~0u;TccEntry*e;TccBuild build={0};
 if(!t||!t->enabled||!t->count)return 0;
 if(!b)return TD_Remesh(t,v,cb,u);
 if (!c->valid || c->state.stage != t->stage || c->state.enabled != t->enabled) {
     TCC_Clear(c); c->state=*t; c->valid=1;
 } else if (memcmp(&c->state,t,sizeof(*t))) {
     float bounds[4];
     if (td_edit_bounds(&c->state,t,bounds)) for (int k=0;k<TCC_ENTRIES;++k) {
         TccEntry *entry=&c->e[k];
         if (entry->used && td_input_hits_edit(entry->input,3,bounds)) {
             c->bytes-=entry->bytes; free(entry->v); memset(entry,0,sizeof(*entry));
         }
     }
     c->state=*t;
 }
 tile[0]=floorf(b[0]/TCC_TILE)*TCC_TILE;tile[1]=floorf(b[1]/TCC_TILE)*TCC_TILE;tile[2]=(floorf(b[2]/TCC_TILE)+1)*TCC_TILE;tile[3]=(floorf(b[3]/TCC_TILE)+1)*TCC_TILE;
 unsigned bucket=tcc_key(v,tile)*TCC_WAYS;
 for(i=(int)bucket;i<(int)bucket+TCC_WAYS;i++){
 e=c->e+i;if(e->used&&!memcmp(e->input,v,144)&&!memcmp(e->box,tile,16)){c->hits++;e->stamp=++c->stamp;goto emit;}
 if(!e->used){slot=i;oldest=0;}else if(oldest&&e->stamp<oldest){slot=i;oldest=e->stamp;}}
 c->builds++;i=TD_RemeshBox(t,v,tile,tcc_collect,&build);
 if(build.failed){free(build.v);return TD_RemeshBox(t,v,b,cb,u);}
 e=c->e+slot;c->bytes-=e->bytes;free(e->v);memset(e,0,sizeof(*e));
 while(c->bytes+build.cap*144u>TCC_BUDGET){unsigned stamp=~0u;int victim=-1,j;for(j=0;j<TCC_ENTRIES;j++)if(c->e[j].used&&c->e[j].stamp<stamp){stamp=c->e[j].stamp;victim=j;}if(victim<0)break;TccEntry*p=c->e+victim;c->bytes-=p->bytes;free(p->v);memset(p,0,sizeof(*p));}
 memcpy(e->input,v,144);memcpy(e->box,tile,16);e->v=build.v;e->n=build.n;e->bytes=build.cap*144u;e->changed=i;e->used=1;e->stamp=++c->stamp;c->bytes+=e->bytes;
emit:
 if(e->changed)for(i=0;i<(int)e->n;i++){const float*p=e->v+i*36;if(tcc_box_hit(p,b))cb(p,u);}return e->changed;
}
#endif
