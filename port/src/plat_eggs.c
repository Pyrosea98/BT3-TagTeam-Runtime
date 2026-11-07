/*
 * Easter-egg absorptions and transformation prices (V05-14). Pure data + logic: nothing here touches
 * game state, so it can be unit-tested on its own and the runtime hook can call it safely.
 *
 * Table source: coordination/docs/versions/0.5/EASTER-EGGS-SPEC.md
 * New-entry ids: coordination/docs/versions/0.5/id-allocation.json
 */
#include <stdlib.h>
#include <string.h>
#include "plat_eggs.h"
#include "mod_scenarios_runtime.inc"

#ifndef EGG_UNUSED
#define EGG_UNUSED 255
#endif

/* Ids -----------------------------------------------------------------------------
   Syn 146, Omega 147, Eis 76, Nuova 145, Rage 275, Naturon 276, Haze 261,
   Oceanus 213 human / 245 dragon
   Cell 105/106/107/108, Android 17 101, 18 102, 13 129, 14 216, 15 218
   Gotenks 48/49/50, Piccolo 22/23/155/167, Super Buu 114
   Baby 140/252/266, Vegeta GT 122/38, Goku GT 7/8/9/10/233, Frieza 91..96, Cellbuzer 254
   ------------------------------------------------------------------------------ */
static const EggRule sEggs[] = {
    { 291, 292, { 29, 31, 34, 122 }, 292, EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },
    { 292, 293, { 0, 1, 3, 7 }, 293, EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 105, 106, { 101, EGG_UNUSED, EGG_UNUSED, EGG_UNUSED }, 106,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 106, 107, { 102, EGG_UNUSED, EGG_UNUSED, EGG_UNUSED }, 107,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 114, 115, { 48, 49, 50, EGG_UNUSED }, 115,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    /* Ultimate Gohan21: only Buutenks becomes Buuhan116. */
    { 115, 116, { 21, EGG_UNUSED, EGG_UNUSED, EGG_UNUSED }, 116,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 114, 114, { 22, 23, 155, 167 }, 114 /* 114 costume 1: filled by the form table */,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 255, 140, { 122, 38, EGG_UNUSED, EGG_UNUSED }, 140,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },
    { 255, 272, { 7, 8, 9, 10 }, 272,
      EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 114, 274, { 91, 92, 93, 94 }, 274,
      EGG_FLAG_ALL_TARGETS | EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },
    { 114, 274, { 105, 106, 107, 108 }, 274,
      EGG_FLAG_ALL_TARGETS | EGG_FLAG_ENEMY_FIRST | EGG_FLAG_CONSUME },

    { 129, 130, { 252, 253, EGG_UNUSED, EGG_UNUSED }, 130,
      EGG_FLAG_ALL_TARGETS | EGG_FLAG_REQUIRE_DEAD | EGG_FLAG_NO_COST | EGG_FLAG_CONSUME },
};

#define EGG_COUNT ((int)(sizeof(sEggs) / sizeof(sEggs[0])))

/* One-Star -> Omega: one stock per 100,000 HP among his siblings; under 100,000 costs nothing.
   Oceanus has two ids (human 213 / dragon 245); either form counts. */
static const EggGate sGates[] = {
    { 146, 147, { 76, 145, 275, 276, 261, 213, 245, EGG_UNUSED }, 100000,
      EGG_GATE_CONSUME_OTHERS },
};

#define EGG_GATE_COUNT ((int)(sizeof(sGates) / sizeof(sGates[0])))

static int sEggsOn = -1;

int Port_EggsEnabled(void) {
    if (sEggsOn < 0) {
        const char *v = getenv("BT3_EGGS");
        sEggsOn = (v != NULL && v[0] == '1') ? 1 : 0;
    }
    return sEggsOn;
}

void Port_EggsSetEnabled(int on) { sEggsOn = on ? 1 : 0; }

int Port_EggsCost(int targetHp) {
    if (targetHp <= 0) {
        return 0;
    }
    return 1 + (targetHp - 1) / 10000;
}

int Port_EggsCostSum(const int *hp, int count) {
    int i, sum = 0;

    if (hp == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        sum += Port_EggsCost(hp[i]);
    }
    return sum;
}

int Port_EggsRuleCount(void) { return EGG_COUNT; }

const EggRule *Port_EggsRuleAt(int index) {
    if (index < 0 || index >= EGG_COUNT) {
        return NULL;
    }
    return &sEggs[index];
}

int Port_EggsResult(int absorber, int targetChara, int trigger, int *ruleIndex) {
    int i, k, hit;

    if (!Port_EggsEnabled() || absorber <= 0 || targetChara <= 0) {
        return 0;
    }
    for (i = 0; i < EGG_COUNT; i++) {
        const EggRule *r = &sEggs[i];
        if (r->absorber != absorber) {
            continue;
        }
        if (r->trigger >= 0 && r->trigger != trigger) {
            continue;
        }
        hit = 0;
        for (k = 0; k < 4; k++) {
            if (r->targets[k] == EGG_UNUSED) {
                continue;
            }
            if (r->targets[k] == targetChara) {
                hit = 1;
                break;
            }
        }
        if (!hit) {
            continue;
        }
        if (ruleIndex != NULL) {
            *ruleIndex = i;
        }
        return r->result > 0 ? r->result : -1;   /* -1: rule matched, result from the form table */
    }
    return 0;
}

int Port_EggsAndroid13Ready(const int *presentTargets, const int *targetHp, int count) {
    int i, need14 = 0, need15 = 0;

    if (presentTargets == NULL || targetHp == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (presentTargets[i] == 252 && targetHp[i] <= 0) {
            need14 = 1;
        }
        if (presentTargets[i] == 253 && targetHp[i] <= 0) {
            need15 = 1;
        }
    }
    return need14 && need15;
}

int Port_EggsGateCount(void) { return EGG_GATE_COUNT; }

const EggGate *Port_EggsGateAt(int index) {
    if (index < 0 || index >= EGG_GATE_COUNT) {
        return NULL;
    }
    return &sGates[index];
}

const EggGate *Port_EggsGate(int source, int target) {
    int i;

    for (i = 0; i < EGG_GATE_COUNT; i++) {
        if (sGates[i].source == source && sGates[i].target == target) {
            return &sGates[i];
        }
    }
    return NULL;
}

int Port_EggsGateCost(int source, int target, const int *presentIds, const int *hp, int count) {
    const EggGate *g = Port_EggsGate(source, target);
    int k, j, cost = 0;

    if (g == NULL || !Port_EggsEnabled()) {
        return -1;                       /* no restriction */
    }
    if (g->hpPerStock <= 0) {
        return 0;                        /* hard gate: the caller only needs "is it allowed" */
    }
    for (k = 0; k < 8; k++) {
        int want = g->others[k];
        if (want == EGG_UNUSED) {
            break;
        }
        for (j = 0; presentIds != NULL && hp != NULL && j < count; j++) {
            if (presentIds[j] == want && hp[j] > 0) {
                cost += hp[j] / g->hpPerStock;   /* floor: under one unit costs nothing */
                break;
            }
        }
    }
    return cost;
}
