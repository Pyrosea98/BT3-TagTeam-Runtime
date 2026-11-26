/* A charge starts only on available terrain. Nearby scars constrain the new
 * footprint instead of making the entire pressure mark vanish. */
#ifndef MOD_TERRAIN_CHARGE_H
#define MOD_TERRAIN_CHARGE_H
#include "mod_terrain_dynamic.h"
/* 1 fitted, 2 already inside an existing crater, 0 insufficient free room. */
static int TQ_Fit(const TdState*t,TdEvent*e){int i;float room=e->r,original=e->r;
 for(i=0;i<t->count;i++){const TdScar*s=t->scars+i;float d;if(fabsf(s->ay-e->ay)>=140)continue;d=sqrtf(td_dist2(s,e->ax,e->az))-s->r;if(d<=0)return 2;if(d-2<room)room=d-2;}
 if(room<3)return 0;e->r=room;e->depth*=room/original;return 1;
}
#endif
