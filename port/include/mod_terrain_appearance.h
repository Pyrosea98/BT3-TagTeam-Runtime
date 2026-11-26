#ifndef MOD_TERRAIN_APPEARANCE_H
#define MOD_TERRAIN_APPEARANCE_H

#include <math.h>

static float tva_clamp(float value, float lo, float hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

/* Sampled only by the soil pass on the existing shared terrain mesh. */
static float TVA_OctagonalDistance(float x, float y)
{
    float a = fabsf(x), b = fabsf(y);
    return a > b ? a + .375f*b : b + .375f*a;
}

static void TVA_FacetedSoil(int orientation, float dx, float dz, float radius,
                            float *light, float *alpha)
{
    static const float directions[8][2] = {
        {1, 0}, {.707107f, .707107f}, {0, 1}, {-.707107f, .707107f},
        {-1, 0}, {-.707107f, -.707107f}, {0, -1}, {.707107f, -.707107f}
    };
    const float *axis = directions[orientation & 7];
    float u = (dx*axis[0] + dz*axis[1])/radius;
    float v = (dz*axis[0] - dx*axis[1])/radius;
    float a = TVA_OctagonalDistance(u+.255f, v-.135f)/.605f;
    float b = TVA_OctagonalDistance(u-.310f, v+.135f)/.480f;
    float q = a < b ? a : b;
    float x = a < b ? u+.255f : u-.310f;
    float y = a < b ? v-.135f : v+.135f;
    float facets = tva_clamp(.5f + 3.f*(x*.48f - y), 0, 1);
    float inner = tva_clamp((q-.34f)/.36f, 0, 1);
    float bank = tva_clamp(1.f-fabsf(q-.81f)/.19f, 0, 1);
    float seam = tva_clamp(1.f-fabsf(a-b)*6.f, 0, 1);
    float mask = tva_clamp((1.045f-q)/.13f, 0, 1);
    float face = 1.01f + .25f*facets - .58f*inner*(1.f-.48f*facets);
    *light = tva_clamp(face + bank*(.62f+.29f*facets) - .36f*seam, .44f, 1.46f);
    *alpha *= mask;
}

#define TVA_CAPACITY 16
typedef struct {
    float x, y, z, dx, dz, height, size, life;
    int age, seed;
} TvaStone;

typedef struct { TvaStone stones[TVA_CAPACITY]; } TvaStones;

/* Life is measured in battle updates. A positive +Y points into the ground. */
static void TVA_Burst(TvaStones *pool, float x, float y, float z, float radius,
                      float sx, float sz, int serial, int deepened)
{
    int i, made = 0;
    int count = deepened ? 3 : radius >= 80.f ? 8 : radius >= 30.f ? 6 : 4;
    float spread = deepened ? 3.f : tva_clamp(radius * .12f, 3.f, 22.f);
    float start = radius * (deepened ? .14f : .62f);
    if (!(radius >= 3.f && radius <= 600.f)) return;
    for (i = 0; i < TVA_CAPACITY && made < count; ++i) {
        TvaStone *stone = pool->stones + i;
        float angle, px, pz;
        if (stone->life > 0) continue;
        angle = (float)(serial & 31)*.3927f + (float)made*2.39996f;
        px = cosf(angle);
        pz = sinf(angle);
        *stone = (TvaStone){
            x + px*start, y + start*(sx*px + sz*pz) - 1.5f, z + pz*start,
            px*spread, pz*spread,
            tva_clamp(radius*(.17f + .03f*(float)(made % 3)), 2.f, 25.f),
            tva_clamp(radius*(.038f + .012f*(float)(made % 4)), .65f, 13.f),
            (float)(24 + ((serial + 7*made) & 7)), 0,
            (serial & 1023)*17 + made
        };
        ++made;
    }
}

static void TVA_Step(TvaStones *pool)
{
    int i;
    for (i = 0; i < TVA_CAPACITY; ++i) {
        TvaStone *stone = pool->stones + i;
        if (stone->life > 0 && (float)++stone->age >= stone->life)
            stone->life = 0;
    }
}

/* x, y, z, size, alpha. Expire in place after the airborne arc. */
static void TVA_Pose(const TvaStone *stone, float out[5])
{
    float age = (float)stone->age;
    float fly = tva_clamp(age / (stone->life - 8.f), 0, 1);
    out[0] = stone->x + stone->dx*fly;
    out[1] = stone->y - 4.f*stone->height*fly*(1.f-fly);
    out[2] = stone->z + stone->dz*fly;
    out[3] = stone->size;
    out[4] = tva_clamp((age + 1.f)*.5f, 0, 1)
           * tva_clamp((stone->life - age)/10.f, 0, 1);
}

#endif
