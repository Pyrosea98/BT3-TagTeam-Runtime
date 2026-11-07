#ifndef PORT_PLAT_EGGS_H
#define PORT_PLAT_EGGS_H

/* Easter-egg absorptions and transformation gates (v0.5, V05-14). Data + pure decision logic only: no
   game pointers, so the module compiles on its own and the runtime hook can be added without touching
   this file.

   Spec: coordination/docs/versions/0.5/EASTER-EGGS-SPEC.md
   Ids:  coordination/docs/versions/0.5/id-allocation.json (new entries) + stock roster ids. */

#define EGG_FLAG_ENEMY_FIRST   0x01   /* prefer an enemy target over an ally */
#define EGG_FLAG_REQUIRE_DEAD  0x02   /* target must already be defeated (HP <= 0) */
#define EGG_FLAG_CONSUME       0x04   /* target is removed for the rest of the match */
#define EGG_FLAG_ALL_TARGETS   0x08   /* every id in targets[] must be present (triple absorption) */
#define EGG_FLAG_NO_COST       0x10   /* always 0 stocks (chips of defeated androids) */

typedef struct EggRule {
    int absorber;        /* character id that performs the absorption */
    int trigger;         /* form/transform slot it answers to; -1 = any transform input */
    int targets[4];      /* acceptable target character ids, 255 = unused */
    int result;          /* character/form id the absorber becomes */
    int flags;
} EggRule;

/* A transformation price. The source pays stocks for the "others" that are still up, at
   `hpPerStock` HP each (floor), and - optionally - removes them when the transform fires.
   Used by the One-Star dragon: becoming Omega costs one stock per 100,000 HP among his siblings, and
   a sibling under 100,000 HP costs nothing. */
typedef struct EggGate {
    int source;
    int target;
    int others[8];       /* ids that are priced; 255 = unused */
    int hpPerStock;      /* 0 = hard gate (no price, just a condition) */
    int flags;           /* EGG_GATE_CONSUME_OTHERS */
} EggGate;

#define EGG_GATE_CONSUME_OTHERS 0x01

/* Master switch. Until the settings screen wires it, the env var BT3_EGGS=1 turns the feature on. */
int Port_EggsEnabled(void);
void Port_EggsSetEnabled(int on);

/* Stock cost from the target's current HP: ceil(hp / 10000), 0 when hp <= 0. */
int Port_EggsCost(int targetHp);
int Port_EggsCostSum(const int *hp, int count);

/* Pure lookup: the result id for `absorber` absorbing `targetChara`, or 0 when there is no rule.
   `trigger` is the form the absorber asked for (-1 when unknown). Returns the rule index + 1 through
   `ruleIndex` when not NULL. No side effects. */
int Port_EggsResult(int absorber, int targetChara, int trigger, int *ruleIndex);
int Port_EggsRuleCount(void);
const EggRule *Port_EggsRuleAt(int index);

/* Android 13 special: needs both 14 and 15 defeated; returns 1 when the rule may fire. */
int Port_EggsAndroid13Ready(const int *presentTargets, const int *targetHp, int count);

/* Transformation price for `source -> target`, in blast stocks.
   Returns -1 when the pair has no gate (no restriction).
   Sums floor(hp / hpPerStock) over the gate's "others" that are present and alive.
   `presentIds`/`hp` describe every fighter in the match, any side. */
int Port_EggsGateCost(int source, int target, const int *presentIds, const int *hp, int count);
const EggGate *Port_EggsGate(int source, int target);
int Port_EggsGateCount(void);
const EggGate *Port_EggsGateAt(int index);

#endif
