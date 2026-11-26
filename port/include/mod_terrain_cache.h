/* Bounded render-thread-only cache. Original input is retained solely to verify content identity;
   only touched triangles are baked. View matrices, texture handles and draw state are never cached. */
#ifndef MOD_TERRAIN_CACHE_H
#define MOD_TERRAIN_CACHE_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mod_terrain_cache_bounds.h"
#ifdef __ANDROID__
#define TC_SLOTS 512
#define TC_BUDGET (6u*1024u*1024u)
#define TC_SIDE_BUDGET (3u*1024u*1024u)
#else
#define TC_SLOTS 2048
#define TC_BUDGET (128u*1024u*1024u)
#define TC_SIDE_BUDGET (6u*1024u*1024u)
#endif
#define TC_WAYS 4
typedef struct {float*v;size_t n,cap;int failed;} TcTris;
typedef struct {uint64_t hash,used;unsigned count;float*input;unsigned char*changed;TcTris base,soil;size_t bytes;} TcEntry;
typedef struct {TdState state;float rgb[3];int valid;TcEntry e[TC_SLOTS];size_t bytes;uint64_t tick,hits,builds,failures;} TdCache;
static void tc_free(TcEntry*e){free(e->input);free(e->changed);free(e->base.v);free(e->soil.v);memset(e,0,sizeof(*e));}
static void TC_Clear(TdCache*c){int i;for(i=0;i<TC_SLOTS;i++)tc_free(c->e+i);c->bytes=0;c->valid=0;}
static void tc_emit(const float*v,void*u){TcTris*a=u;if(a->failed)return;if(a->n==a->cap){size_t cap=a->cap?a->cap*2:32;float*p;if(cap>TC_SIDE_BUDGET/144u){a->failed=1;return;}p=realloc(a->v,cap*144);if(!p){a->failed=1;return;}a->v=p;a->cap=cap;}memcpy(a->v+a->n++*36,v,144);}
static uint64_t tc_hash(const void*p,size_t n){const unsigned char*b=p;uint64_t h=1469598103934665603ULL;while(n--)h=(h^*b++)*1099511628211ULL;return h;}
static TcEntry* TC_Get(TdCache*c,const TdState*t,const float*v,unsigned count,const float*rgb){
 TcEntry tmp;uint64_t hash;size_t input;int i,slot=-1;
 if(count<3||count>4096)return NULL;input=(size_t)count*48;
 if (!c->valid || c->state.stage != t->stage || c->state.enabled != t->enabled || memcmp(c->rgb,rgb,12)) {
     TC_Clear(c); c->state=*t; memcpy(c->rgb,rgb,12); c->valid=1;
 } else if (memcmp(&c->state,t,sizeof(*t))) {
     float bounds[4];
     if (td_edit_bounds(&c->state,t,bounds)) for (int k=0;k<TC_SLOTS;++k) {
         TcEntry *entry=&c->e[k];
         if (entry->input && td_input_hits_edit(entry->input,entry->count,bounds)) {
             c->bytes-=entry->bytes; tc_free(entry);
         }
     }
     c->state=*t;
 }
 hash=tc_hash(v,input);c->tick++;
 unsigned bucket=(unsigned)(hash^(hash>>32))&(TC_SLOTS/TC_WAYS-1);bucket*=TC_WAYS;
 for(i=(int)bucket;i<(int)bucket+TC_WAYS;i++)if(c->e[i].input&&c->e[i].hash==hash&&c->e[i].count==count&&!memcmp(c->e[i].input,v,input)){c->e[i].used=c->tick;c->hits++;return c->e+i;}
 memset(&tmp,0,sizeof(tmp));tmp.input=malloc(input);tmp.changed=calloc(count-2,1);if(!tmp.input||!tmp.changed)goto failed;
 memcpy(tmp.input,v,input);tmp.count=count;tmp.hash=hash;tmp.used=c->tick;
 for(i=0;i+2<(int)count;i++)if(TD_Remesh(t,v+i*12,tc_emit,&tmp.base)){tmp.changed[i]=1;TD_Soil(t,v+i*12,rgb,tc_emit,&tmp.soil);if(tmp.base.failed||tmp.soil.failed)goto failed;}
 tmp.bytes=input+count-2+(tmp.base.cap+tmp.soil.cap)*144;if(tmp.bytes>TC_BUDGET)goto failed;
 {uint64_t oldest=UINT64_MAX;
  for(i=(int)bucket;i<(int)bucket+TC_WAYS;i++){
   if(!c->e[i].input){slot=i;oldest=0;}
   else if(oldest&&c->e[i].used<oldest){oldest=c->e[i].used;slot=i;}
  }
  if(slot<0)goto failed;
  c->bytes-=c->e[slot].bytes;tc_free(c->e+slot);
 }
 while(c->bytes+tmp.bytes>TC_BUDGET){uint64_t oldest=UINT64_MAX;int evict=-1;
  for(i=0;i<TC_SLOTS;i++)if(c->e[i].input&&c->e[i].used<oldest){oldest=c->e[i].used;evict=i;}
  if(evict<0)goto failed;c->bytes-=c->e[evict].bytes;tc_free(c->e+evict);
 }
 c->e[slot]=tmp;c->bytes+=tmp.bytes;c->builds++;return c->e+slot;
failed:tc_free(&tmp);c->failures++;return NULL;
}
#endif
