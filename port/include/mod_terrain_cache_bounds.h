#ifndef MOD_TERRAIN_CACHE_BOUNDS_H
#define MOD_TERRAIN_CACHE_BOUNDS_H
#include <string.h>
#include "mod_terrain_dynamic.h"
/* Remeshing only changes height. A strip whose XZ footprint misses all
   edited scars has exactly the same triangles, normals and soil as before. */
static int td_edit_bounds(const TdState *old, const TdState *next, float bounds[4]) {
    int found = 0;
    for (int i = 0; i < old->count || i < next->count; ++i) {
        if (i < old->count && i < next->count &&
            !memcmp(&old->scars[i], &next->scars[i], sizeof(TdScar))) continue;
        const TdState *states[2] = {old, next};
        for (int side = 0; side < 2; ++side) {
            if (i >= states[side]->count) continue;
            float b[4];
            td_bounds(&states[side]->scars[i], b);
            if (!found) { memcpy(bounds, b, sizeof(b)); found = 1; }
            else {
                bounds[0] = td_min(bounds[0],b[0]); bounds[1] = td_min(bounds[1],b[1]);
                bounds[2] = td_max(bounds[2],b[2]); bounds[3] = td_max(bounds[3],b[3]);
            }
        }
    }
    return found;
}
static int td_input_hits_edit(const float *v, unsigned count, const float b[4]) {
    unsigned outside = 15;
    for (unsigned i = 0; i < count && outside; ++i, v += TD_ATTR) {
        unsigned mask = (v[0] < b[0]) | ((v[2] < b[1]) << 1) |
                        ((v[0] > b[2]) << 2) | ((v[2] > b[3]) << 3);
        outside &= mask;
    }
    return outside == 0;
}
#endif
