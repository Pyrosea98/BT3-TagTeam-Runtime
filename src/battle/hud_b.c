#ifdef PORT
#include "coop_selection.h"
#endif
#ifdef PORT
#include "tag_tournament_port.h"
#endif
#include "common.h"
#include "battle/hud_b.h"
#include "battle/battle.h"
#include "sys/randf.h"
#include "sys/heap.h"
#ifdef PORT
/* More than two fighters: the fighter a side's gauge shows (btl_char_flag_opp.c, Port_HudFighter), or, while the
   small copy of the gauge of another fighter is drawn (HudExtra_DrawMini), that fighter. */
extern s32 Port_HudFighter(s32 side);
static s32 sHudMiniWho = -1;
#define HUD_WHO(side) (sHudMiniWho >= 0 ? sHudMiniWho : Port_HudFighter(side))
#else
#define HUD_WHO(side) (side)
#endif

/*
 * Battle HUD: the gauge part (health, ki, blast stock, powered-up timer, status icons, face), 0x21CA60-0x222400.
 * It continues src/battle/hud_a_d.c (the GS mask states and HudGauge_UpdateHp) and is one module with it: the work
 * is gHudGauge, allocated by HudGauge_Init at the end of this file.
 *
 * The part is one tree of 12 nodes over 76 sprites, built once and shown twice a frame: Hud_Draw selects side 0,
 * updates and draws the tree, then does the same for side 1 mirrored (HudGauge_SelectSide). Everything that
 * differs by side is therefore a [2] array indexed by gHudGauge->side.
 * Hud_PreUpdate copies the fighter state into the work once a frame with the setters at the end of the file; the
 * node update callbacks turn it into sprite rectangles and colours, the draw callbacks draw the sprites.
 * Every animation is skipped while the battle is paused (battle flag 0x100).
 *
 * The structs are this file's views; the sprite / node layouts agree with include/battle/hud_a.h.
 */

#define HUDG_PAUSED() (Battle_GetWork()->flags & 0x100)

#define HUDG_HP_BAR 10000      /* health per bar */
#define HUDG_KI_BAR 20000      /* ki per bar, five bars */
#define HUDG_POWER_BAR 6000    /* powered-up timer per bar, five bars */
#define HUDG_BLAST_STOCK 100000

/* One texture entry of a sprite sheet (0x40 bytes). */
typedef struct HudBTex {
    /* 0x00 */ u8 unk0[0x28];
    /* 0x28 */ s32 mark;
    /* 0x2C */ u8 unk2C[0x14];
} HudBTex; /* size 0x40 */

/* A sprite sheet. */
typedef struct HudBRes {
    /* 0x00 */ u8 unk0[0x10];
    /* 0x10 */ HudBTex *tex;
} HudBRes;

/* The side's fighter object as BtlCtrl_GetObj returns it: only what the face drawing reads. */
typedef struct HudBObj {
    /* 0x000 */ u8 unk0[0x50];
    /* 0x050 */ HudBRes *faceRes; /* sheet of the face picture; NULL: no face drawn */
    /* 0x054 */ u8 unk54[0xA40 - 0x54];
    /* 0xA40 */ u32 unkA40;       /* bit 0x40000: the face is tinted (0x80, 0x50, 0x70) */
} HudBObj;

/* Gauge work (gHudGauge, 0x288 bytes). */
typedef struct HudBWork {
    /* 0x000 */ void *res;             /* sprite sheet 0 of the HUD file */
    /* 0x004 */ HudBSprite *spr;       /* 76 */
    /* 0x008 */ HudBGroup *grp;        /* 12 nodes, [0] is the root */
    /* 0x00C */ HudBShake shake[2][3]; /* by side: the panel, the health node, the ki node */
    /* 0x024 */ s32 side;              /* side being updated / drawn */
    /* 0x028 */ s32 hpShown[2];        /* trailing (damage) health */
    /* 0x030 */ s32 hp[2];             /* health now (HudGauge_SetHp) */
    /* 0x038 */ s32 auraAlpha[2];      /* 0..0x80, +-0x10 a frame: the powered-up sparks and the blue flash */
    /* 0x040 */ Ramp lowHpPulse[2];    /* 0 <-> 1 every 0.5 s while health <= one bar: the red flash */
    /* 0x070 */ s32 blast[2];
    /* 0x078 */ s32 ki[2];
    /* 0x080 */ s32 kiShown[2];        /* trailing ki */
    /* 0x088 */ Ramp kiBarFlash[2];    /* 1 -> 0 in 1 s when a ki bar is gained */
    /* 0x0B8 */ Ramp kiFullPulse[2];   /* 0 <-> 1 every 1 s; shown at full ki and by the powered-up lamps */
    /* 0x0E8 */ s32 maxPower[2];
    /* 0x0F0 */ Ramp blastFlash[2];
    /* 0x120 */ u32 statMask[2];       /* status icons shown, bits 0..3 */
    /* 0x128 */ u32 statPos[2];        /* icons drawn bright (positive modifier); the others are dark */
    /* 0x130 */ s32 kiReserve[2];
    /* 0x138 */ u32 statPrev[2];       /* statMask before the last change */
    /* 0x140 */ s32 statTo[2][4];      /* x of each icon: 24 per shown icon before it */
    /* 0x160 */ s32 statFrom[2][4];    /* x before the last change */
    /* 0x180 */ Ramp statRamp[2];
    /* 0x1B0 */ s32 statState[2];      /* 0..5 the change animation, anything else: at rest */
    /* 0x1B8 */ Ramp hpBarFade[2];     /* 1 -> 0 in 0.2 s when the trailing health uses up a bar */
    /* 0x1E8 */ HudBRes *faceRes[2];
    /* 0x1F0 */ HudBObj *obj[2];
    /* 0x1F8 */ s32 switching[2];
    /* 0x200 */ Ramp slide[2];         /* 0 on screen .. 1 away, one side's panel */
    /* 0x230 */ Ramp slideAll;         /* the same for the whole part */
    /* 0x248 */ s32 hpDrain[2];        /* speed of the trailing health, per frame */
    /* 0x250 */ s32 kiDrain[2];
    /* 0x258 */ Ramp kiReservePulse[2];
} HudBWork; /* size 0x288 */

extern HudBWork *gHudGauge;

#define HUDB_MAX(a, b) ((a) < (b) ? (b) : (a))

/* Sprite / node library at 0x224B50.. (not decompiled yet). */
extern void HudSprite_Show(HudBSprite *spr, s32 show);
extern void HudSprite_SetColor(HudBSprite *spr, s32 r, s32 g, s32 b, s32 a);
extern void HudNode_SetPos(HudBGroup *node, s32 x, s32 y);        /* node position */
extern void HudSprite_SetRect(HudBSprite *spr, s32 x0, s32 x1, s32 y0, s32 y1); /* screen rectangle */
extern void HudSprite_SetUv(HudBSprite *spr, s32 u0, s32 u1, s32 v0, s32 v1); /* texel rectangle */
extern void HudSprite_InitPlain(HudBSprite *spr, s32 r, s32 g, s32 b, s32 a);     /* untextured, coloured */
extern void HudSprite_Move(HudBSprite *spr, s32 dx, s32 dy);                 /* moves the rectangle */
extern void HudNode_Show(HudBGroup *node, s32 show);
extern void *memcpy(void *dst, const void *src, u32 n);
extern void *memset(void *dst, s32 c, u32 n);
extern f32 powf(f32 x, f32 y);
extern void HudGfx_CallBegin(void (*fn)(void)); /* run a GS state function (begin) */
extern void HudGfx_CallEnd(void (*fn)(void)); /* run a GS state function (end) */
#ifdef PORT
/* PC build, online play: the player who joined is side 1 of the game, and would have their own gauges, team, prompts
   and counters on the right. With the swap (gs/net.c) the two sides change places on THIS copy's screen only: a
   side's data is the same, where it is drawn is the other side's place. HUD_SCR(side) is the place of a side. */
extern int Port_HudSwap(void);
#define HUD_SCR(side) ((side) ^ Port_HudSwap())
#else
#define HUD_SCR(side) (side)
#endif
extern void HudSprite_SetMirror(HudBSprite *spr, s32 mirror);
extern void HudSprite_InitTex(HudBSprite *spr, void *res, s32 tex, s32 sub);    /* sprite of a sheet's texture */
extern void HudSprite_DrawAlphaClear(HudBSprite *spr);
extern void HudSprite_DrawAt(HudBSprite *spr, void *res, s32 a, s32 b, s32 c);
extern void HudSprite_Draw(HudBSprite *spr, void *res, s32 flag);            /* draw */
extern void HudGauge_GsBeginMask(void);
extern void HudGauge_GsEndMask(void);
extern void HudGauge_GsBeginMask2(void);
extern void HudGauge_GsEndMask2(void);
extern void HudGauge_UpdateHp(void);
extern HudBObj *BtlCtrl_GetObj(s32 side);
extern s32 BtlSide_GetBlastMax(s32 side);
extern s32 BtlSide_IsPoweredUp(s32 side);
extern s32 BtlCtrl_IsActiveDead(s32 side);

/* The six markers of the health bars in reserve (sprites 9..14): one lit per full bar above the bar on show; the
 * one just used up fades out with hpBarFade. Called by HudGauge_DrawHpBarCount, i.e. at draw time. */
void HudGauge_UpdateHpBarCount(void) {
    s32 side = gHudGauge->side;
    s32 bars = gHudGauge->hpShown[side] / 10000;
    s32 i;

    if (gHudGauge->hpShown[side] % 10000 == 0 && bars != 0) {
        bars--;
    }
    for (i = 0; i < 6; i++) {
        HudBSprite *spr = &gHudGauge->spr[9 + i];
        HudSprite_SetColor(spr, 0x80, 0x80, 0x80, 0x80);
        if (i < bars) {
            HudSprite_Show(spr, 1);
        } else if (i == bars) {
            if (gHudGauge->hpBarFade[side].value == 0.0f) {
                HudSprite_Show(spr, 0);
            } else {
                HudSprite_Show(spr, 1);
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)(gHudGauge->hpBarFade[side].value * 128.0f));
            }
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 0 (root): steps the whole-part slide once a frame (on the side 0 pass) and moves the node up to
 * (-256, -224) away. */
void HudGauge_UpdateRoot(HudBGroup *node) {
    if (!HUDG_PAUSED() && gHudGauge->side == 0) {
        Ramp_Step(&gHudGauge->slideAll);
    }
    HudNode_SetPos(node, gHudGauge->slideAll.value * -256.0f, gHudGauge->slideAll.value * -224.0f);
}

/* Update of node 1 (the side's panel): steps the side's slide and moves the node; at rest it sits at (-2, 8). */
void HudGauge_UpdatePanel(HudBGroup *node) {
    Ramp *ramp = &gHudGauge->slide[gHudGauge->side];

    if (!HUDG_PAUSED()) {
        Ramp_Step(ramp);
    }
    HudNode_SetPos(node, (s32)(ramp->value * -256.0f) - 2, (s32)(ramp->value * -224.0f) + 8);
}

/* Update of node 3 (health): applies the three shakes (a random offset of +-amp while the count runs) and lets the
 * trailing health fall towards the real one. Speed: 100 a frame, at least 120 / 150 / 200 once the gap exceeds
 * 5000 / 10000 / 20000; the speed only goes back to 100 when the trail has caught up. */
void HudGauge_UpdateHpTrail(void) {
    s32 side = gHudGauge->side;
    s32 i;

    for (i = 0; i < 3; i++) {
        HudBShake *sh = &gHudGauge->shake[side][i];
        HudBGroup *grp;

        switch (i) {
        case 0:
            grp = &gHudGauge->grp[1];
            break;
        case 1:
            grp = &gHudGauge->grp[3];
            break;
        case 2:
            grp = &gHudGauge->grp[4];
            break;
        default:
            continue;
        }
        if (sh->count != 0) {
            if (HUDG_PAUSED()) {
                continue;
            }
            sh->count--;
            grp->ofsX = Rand_IntRange(-sh->amp, sh->amp);
            grp->ofsY = Rand_IntRange(-sh->amp, sh->amp);
        } else {
            grp->ofsX = 0;
            grp->ofsY = 0;
        }
    }
    if (!HUDG_PAUSED()) {
        Ramp_Step(&gHudGauge->hpBarFade[side]);
        if (gHudGauge->hp[side] < gHudGauge->hpShown[side]) {
            if (gHudGauge->hp[side] < gHudGauge->hpShown[side] - 20000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 200);
            } else if (gHudGauge->hp[side] < gHudGauge->hpShown[side] - 10000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 150);
            } else if (gHudGauge->hp[side] < gHudGauge->hpShown[side] - 5000) {
                gHudGauge->hpDrain[side] = HUDB_MAX(gHudGauge->hpDrain[side], 120);
            }
            if (gHudGauge->hpShown[side] > 10000 && gHudGauge->hpShown[side] % 10000 > 0 &&
                gHudGauge->hpShown[side] % 10000 <= gHudGauge->hpDrain[side]) {
                Ramp_Start(&gHudGauge->hpBarFade[side], 0.2f, 1.0f, 0.0f);
            }
            gHudGauge->hpShown[side] -= gHudGauge->hpDrain[side];
            if (gHudGauge->hpShown[side] < gHudGauge->hp[side]) {
                gHudGauge->hpShown[side] = gHudGauge->hp[side];
            }
        } else {
            gHudGauge->hpShown[side] = gHudGauge->hp[side];
            gHudGauge->hpDrain[side] = 100;
        }
    }
}

/* Update of node 10 (aura). While the fighter is powered up (BtlSide_IsPoweredUp and alive) auraAlpha rises to
 * 0x80, else falls to 0. Ten sparks (sprites 53..62, a 5 x 2 grid 46 x 20 apart) each take a new random frame of
 * the 10-frame table every frame (never the same twice running: redrawn until it differs) and jitter by +-5.
 * Sprite 62 is then re-made into a blue flash of alpha auraAlpha / 128 * 44, sprite 63 into a red one that pulses
 * while health is at most one bar.
 * Random draws (Rand_IntRange, libc rand): per spark 1 or more for the frame, then 2 for the jitter; none while
 * paused. While paused the frame index is the spark's own number.
 *
 * Not matching (behaviour is the same). With the do-while below every register is the original's, but the
 * compiler rotates the loop: the third and fourth compare are placed in front of the Rand_IntRange call and the
 * loop is entered by a jump (351 instructions against 349). Written as `retry: n = Rand_IntRange(0, 9);
 * if (same) goto retry;` the order is the original's, but n * 8 is kept in $a2 (i * 8 in $s1, the sprite in
 * $s0) where the original has one register $s0 for both offsets and the sprite in $s1; `for (;;)` with one
 * compound `if (differs) break;` gives that too. No form tried gave both.
 * Cleanup 4 (RTL dumps; 50 more loop forms, all in one of the two families above: 19 or 41 to 45 differing):
 * - The rotation is made when the loop is expanded (expand_end_loop "roll a loop exit at the top to the end"):
 *   it scans from the loop top for conditional jumps to the loop's exit label and stops 30 RTL instructions
 *   after the top once it has one. The `&&` chain's first two tests (13 instructions each) are inside that
 *   window, so everything behind the second test is moved in front of the loop. `break` statements count as
 *   exits too (`if (a) break; if (b) break; ...` rotates the same way); one compound `if (a || b || c || d)
 *   break;`, `continue` forms, a ternary chain, a flag variable or `goto retry` do not rotate.
 * - In every unrotated form the registers go wrong for ONE reason: gcse (PRE) makes `n * 8` behind the `if`
 *   partially redundant and puts `n8 = i8` on the edge "paused -> join". In the rotated form the if-conversion
 *   pass (15.ce) moves that copy in front of the paused test, so i8 lives in one block, local-alloc gives it
 *   s0 and n8 follows by preference (the original's registers). Unrotated, the copy stays in its own block
 *   behind the loop, i8 becomes a global pseudo crossing the call, n8 (no call crossed) takes a2 first, the
 *   sprite gets s0 and i8 s1. So the original needs: no qualified exit in the first 30 instructions AND the
 *   copy hoisted (or `i * 8` and `n * 8` one pseudo from the start). Not found: `spr[53 + n]` after `n = i`,
 *   `n = i` in front of / behind `spr`, a `same` flag, nested do-while (0). */
void HudGauge_UpdateAura(void);
#if 0
void HudGauge_UpdateAura(void) {
    s32 ofs[2];
    s16 tbl[10][4] = {
        { 0x00, 0x40, 0x00, 0x40 }, { 0x40, 0x80, 0x00, 0x40 }, { 0x80, 0xC0, 0x00, 0x40 }, { 0xC0, 0x100, 0x00, 0x40 },
        { 0x00, 0x40, 0x40, 0x80 }, { 0x40, 0x80, 0x40, 0x80 }, { 0x80, 0xC0, 0x40, 0x80 }, { 0xC0, 0x100, 0x40, 0x80 },
        { 0x00, 0x40, 0x80, 0xC0 }, { 0x40, 0x80, 0x80, 0xC0 },
    };
    s32 n;
    s32 i;
    HudBSprite *spr;
    Ramp *ramp;

    if (BtlSide_IsPoweredUp(HUD_WHO(gHudGauge->side)) && !BtlCtrl_IsActiveDead(HUD_WHO(gHudGauge->side))) {
        gHudGauge->auraAlpha[gHudGauge->side] += 0x10;
        if (gHudGauge->auraAlpha[gHudGauge->side] > 0x80) {
            gHudGauge->auraAlpha[gHudGauge->side] = 0x80;
        }
    } else {
        gHudGauge->auraAlpha[gHudGauge->side] -= 0x10;
        if (gHudGauge->auraAlpha[gHudGauge->side] < 0) {
            gHudGauge->auraAlpha[gHudGauge->side] = 0;
        }
    }
    for (i = 0; i < 10; i++) {
        spr = &gHudGauge->spr[53 + i];
        n = i;
        if (!HUDG_PAUSED()) {
            do {
                n = Rand_IntRange(0, 9);
            } while (tbl[n][0] == spr->uv[0] && tbl[n][1] == spr->uv[1] && tbl[n][2] == spr->uv[2] &&
                     tbl[n][3] == spr->uv[3]);
        }
        spr->uv[0] = tbl[n][0];
        spr->uv[1] = tbl[n][1];
        spr->uv[2] = tbl[n][2];
        spr->uv[3] = tbl[n][3];
        if (HUDG_PAUSED()) {
            ofs[0] = 0;
            ofs[1] = -8;
        } else {
            ofs[0] = Rand_IntRange(-5, 5);
            ofs[1] = Rand_IntRange(-5, 5) - 8;
        }
        HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
        HudSprite_Move(spr, (i % 5) * 46 + ofs[0], (i / 5) * 20 + ofs[1]);
        HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)gHudGauge->auraAlpha[gHudGauge->side]);
    }
    spr = &gHudGauge->spr[62];
    HudSprite_InitPlain(spr, 0, 0, 0xFF, (u8)((f32)gHudGauge->auraAlpha[gHudGauge->side] * 0.0078125f * 44.0f));
    HudSprite_SetRect(spr, 0, 0x180, 0, 0x6C);
    ramp = &gHudGauge->lowHpPulse[gHudGauge->side];
    if (gHudGauge->hp[gHudGauge->side] <= 10000 && !BtlCtrl_IsActiveDead(gHudGauge->side)) {
        if (!HUDG_PAUSED() && Ramp_Step(ramp)) {
            if (ramp->value == 0.0f) {
                Ramp_Start(ramp, 0.5f, 0.0f, 1.0f);
            } else {
                Ramp_Start(ramp, 0.5f, 1.0f, 0.0f);
            }
        }
    } else {
        if (!HUDG_PAUSED() && Ramp_Step(ramp) && ramp->value == 1.0f) {
            Ramp_Start(ramp, 0.5f, 1.0f, 0.0f);
        }
    }
    spr = &gHudGauge->spr[63];
    HudSprite_InitPlain(spr, 0xFF, 0, 0, (u8)(ramp->value * 44.0f));
    HudSprite_SetRect(spr, 0, 0x180, 0, 0x6C);
}
#endif
INCLUDE_RODATA("asm/nonmatchings/battle/hud_b", D_002F1B90);
INCLUDE_ASM("asm/nonmatchings/battle/hud_b", HudGauge_UpdateAura);

/* Update of node 4 (ki): trailing ki (400 a frame, at least 606 / 800 for a gap over 20000 / 40000), then three
 * layers of five bars: the trailing ki (sprites 20..24), the ki (25..29) and the reserve (15..19, pulsing red).
 * A partly filled bar is cut from the top: n = part * 17 / 20000 rows of 17. */
void HudGauge_UpdateKi(void) {
    s32 shown = gHudGauge->kiShown[gHudGauge->side];
    s32 ki = gHudGauge->ki[gHudGauge->side];
    s32 extra = gHudGauge->kiReserve[gHudGauge->side];
    s32 i;
    s32 lo;
    s32 n;
    HudBSprite *spr;

    if (ki < shown - 40000) {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 800);
    } else if (ki < shown - 20000) {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 606);
    } else {
        gHudGauge->kiDrain[gHudGauge->side] = HUDB_MAX(gHudGauge->kiDrain[gHudGauge->side], 400);
    }
    if (!HUDG_PAUSED() && ki < gHudGauge->kiShown[gHudGauge->side]) {
        gHudGauge->kiShown[gHudGauge->side] -= gHudGauge->kiDrain[gHudGauge->side];
    }
    if (ki >= gHudGauge->kiShown[gHudGauge->side]) {
        gHudGauge->kiShown[gHudGauge->side] = ki;
        gHudGauge->kiDrain[gHudGauge->side] = 400;
    }
    shown = gHudGauge->kiShown[gHudGauge->side];
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[20 + i];
        if (shown >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= shown) {
            HudSprite_Show(spr, 0);
        } else {
            n = (shown % 20000) * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[25 + i];
        if (ki >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= ki) {
            HudSprite_Show(spr, 0);
        } else {
            n = ki - i * 20000;
            n = n * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
    if (Ramp_Step(&gHudGauge->kiReservePulse[gHudGauge->side]) && extra != 0) {
        if (gHudGauge->kiReservePulse[gHudGauge->side].value == 0.0f) {
            Ramp_Start(&gHudGauge->kiReservePulse[gHudGauge->side], 0.3f, 0.0f, 1.0f);
        } else {
            Ramp_Start(&gHudGauge->kiReservePulse[gHudGauge->side], 0.3f, 1.0f, 0.0f);
        }
    }
    n = extra / 20000;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[15 + i];
        HudSprite_SetColor(spr, (u8)(gHudGauge->kiReservePulse[gHudGauge->side].value * 192.0f), 0, 0, 0x40);
        if (extra >= i * 20000 + 20000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 20000 >= extra) {
            HudSprite_Show(spr, 0);
        } else {
            n = extra - i * 20000;
            n = n * 17 / 20000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
}

/* Update of node 6: the five bars of the powered-up timer (sprites 30..34), 6000 each, drawn like the ki bars. */
void HudGauge_UpdateMaxPower(void) {
    s32 *p = gHudGauge->maxPower;
    s32 v = p[gHudGauge->side];
    s32 i;
    s32 n;
    HudBSprite *spr;

    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[30 + i];
        if (v >= i * 6000 + 6000) {
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
        } else if (i * 6000 >= v) {
            HudSprite_Show(spr, 0);
        } else {
            n = (v - i * 6000) * 17 / 6000;
            HudSprite_Show(spr, 1);
            HudSprite_SetRect(spr, 0, 0xE, 0x11 - n, 0x11);
            HudSprite_SetUv(spr, 0x10, 0x1E, 0x21 - n, 0x21);
        }
        HudSprite_Move(spr, i * 8, 0);
    }
}

/* Update of node 5: the lamps over the ki bars (sprites 65..69). The lamp of the bar just filled fades with
 * kiBarFlash; with all five bars full (ki > 99999) and that fade over, all five pulse with kiFullPulse. */
void HudGauge_UpdateKiLamps(void) {
    Ramp *a = &gHudGauge->kiBarFlash[gHudGauge->side];
    Ramp *b = &gHudGauge->kiFullPulse[gHudGauge->side];
    s32 ki = gHudGauge->ki[gHudGauge->side];
    s32 bars = ki / 20000;
    s32 i;
    s32 r;
    u8 alpha;
    HudBSprite *spr;

    if (!HUDG_PAUSED()) {
        r = Ramp_Step(a);
    } else {
        r = a->value == a->target;
    }
    if (r) {
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(b);
        } else {
            r = 0;
        }
        if (r) {
            if (b->value == 0.0f) {
                Ramp_Start(b, 1.0f, 0.0f, 1.0f);
            } else {
                Ramp_Start(b, 1.0f, 1.0f, 0.0f);
            }
        }
        if (ki > 99999) {
            alpha = b->value * 128.0f;
            for (i = 0; i < 5; i++) {
                spr = &gHudGauge->spr[65 + i];
                HudSprite_Show(spr, 1);
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
            }
            return;
        }
    }
    alpha = a->value * 128.0f;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[65 + i];
        if (bars == i + 1) {
            HudSprite_Show(spr, 1);
            HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 7: one lamp (sprites 70..74) per full bar of the powered-up timer, at 0.8 * kiFullPulse; a lit
 * lamp hides the ki lamp under it. */
void HudGauge_UpdateMaxPowerLamps(HudBGroup *node) {
    s32 v = gHudGauge->maxPower[gHudGauge->side];
    Ramp *b = &gHudGauge->kiFullPulse[gHudGauge->side];
    s32 bars;
    s32 i;
    u8 alpha;
    HudBSprite *spr;

    HudNode_Show(node, 1);
    bars = v / 6000;
    alpha = b->value * 0.8f * 128.0f;
    for (i = 0; i < 5; i++) {
        spr = &gHudGauge->spr[70 + i];
        if (bars >= i + 1) {
            HudSprite_Show(spr, 1);
            HudSprite_SetColor(spr, 0x80, 0x80, 0x80, alpha);
            HudSprite_Show(&gHudGauge->spr[65 + i], 0);
        } else {
            HudSprite_Show(spr, 0);
        }
    }
}

/* Update of node 8 (blast stock): the digit 0..7 (sprite 35), a copy of it that flashes (75: once when a stock is
 * gained, pulsing while the stock is full), and the fill bar towards the next stock (37, 25 px for 100000),
 * hidden with its frame (36, 38, 48) when the stock is full. */
void HudGauge_UpdateBlast(void) {
    Ramp *ramp = &gHudGauge->blastFlash[gHudGauge->side];
    s32 blast = gHudGauge->blast[gHudGauge->side];
    u16 tbl[8][4] = {
        { 0x00, 0x20, 0x00, 0x20 }, { 0x20, 0x40, 0x00, 0x20 }, { 0x40, 0x60, 0x00, 0x20 }, { 0x60, 0x80, 0x00, 0x20 },
        { 0x00, 0x20, 0x20, 0x40 }, { 0x20, 0x40, 0x20, 0x40 }, { 0x40, 0x60, 0x20, 0x40 }, { 0x60, 0x80, 0x20, 0x40 },
    };
    s32 max = BtlSide_GetBlastMax(HUD_WHO(gHudGauge->side));
    HudBSprite *spr;
    u32 n;
    s32 r;

    n = blast / 100000;
    spr = &gHudGauge->spr[35];
    if (n >= 8) {
        n = 7;
    }
    HudSprite_SetRect(spr, -0x20, 0, 0, 0x20);
    HudSprite_SetUv(spr, tbl[n][0], tbl[n][1], tbl[n][2], tbl[n][3]);
    HudSprite_Move(spr, 10, -16);
    if (!HUDG_PAUSED()) {
        r = Ramp_Step(ramp);
    } else {
        r = 0;
    }
    if (r) {
        if (blast >= max) {
            if (ramp->value == 0.0f) {
                Ramp_Start(ramp, 1.0f, 0.0f, 1.0f);
            } else {
                Ramp_Start(ramp, 1.0f, 1.0f, 0.0f);
            }
        } else if (ramp->value == 1.0f) {
            Ramp_Start(ramp, 1.0f, 1.0f, 0.0f);
        }
    }
    spr = &gHudGauge->spr[75];
    HudSprite_SetRect(spr, -0x20, 0, 0, 0x20);
    HudSprite_SetUv(spr, tbl[n][0], tbl[n][1], tbl[n][2], tbl[n][3]);
    HudSprite_Move(spr, 10, -16);
    if (ramp->value > 0.0f) {
        HudSprite_Show(spr, 1);
    } else {
        HudSprite_Show(spr, 0);
    }
    HudSprite_SetColor(spr, 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
    n = blast % 100000;
    n = n * 25 / 100000;
    spr = &gHudGauge->spr[37];
    HudSprite_SetRect(spr, n, n + 0x1A, 0, 6);
    HudSprite_Move(spr, 5, 3);
    if (blast >= max) {
        HudSprite_Show(&gHudGauge->spr[36], 0);
        HudSprite_Show(&gHudGauge->spr[38], 0);
        HudSprite_Show(&gHudGauge->spr[37], 0);
    } else {
        HudSprite_Show(&gHudGauge->spr[36], 1);
        HudSprite_Show(&gHudGauge->spr[38], 1);
        HudSprite_Show(&gHudGauge->spr[37], 1);
    }
    spr = &gHudGauge->spr[48];
    if (blast >= max) {
        HudSprite_Show(spr, 0);
    } else {
        HudSprite_Show(spr, 1);
    }
}

/* Update of node 9: the four status icons (sprites 43..46, textures 19..22) with their frames (39..42, 49..52).
 * After HudGauge_SetStatIcons changed the set: 0/1 fade out the icons that went (0.2 s), 2/3 slide the ones that
 * stay to their new places (0.3 s, cubic ease-out), 4/5 fade in the new ones (0.2 s); the state then runs past 5
 * (or is -1) and the icons are drawn at rest. An icon is bright if its bit of statPos is set, dark otherwise. */
void HudGauge_UpdateStatIcons(void) {
    s32 side = gHudGauge->side;
    u32 mask = gHudGauge->statMask[side];
    u32 pos = gHudGauge->statPos[side];
    u32 prev = gHudGauge->statPrev[side];
    s32 *state = &gHudGauge->statState[side];
    s32 *to = gHudGauge->statTo[side];
    Ramp *ramp = &gHudGauge->statRamp[side];
    HudBSprite *icon = &gHudGauge->spr[43];
    HudBSprite *frames = &gHudGauge->spr[39];
    HudBSprite *shades = &gHudGauge->spr[49];
    u8 col[2][4] = { { 0x80, 0x80, 0x80, 0x80 }, { 0x40, 0x40, 0x40, 0x80 } };
    s32 *from = gHudGauge->statFrom[side];
    u32 removed = (mask ^ prev) & prev;
    u32 added = (mask ^ prev) & mask;
    u32 kept = mask & prev;
    s32 r;
    s32 i;
    s32 x;
    u8 *c;
    f32 t;

    if (*state == 0) {
        if (removed != 0) {
            *state = 0;
        } else if (added != 0) {
            *state = 2;
        } else {
            *state = -1;
        }
    }
    switch (*state) {
    case 0:
        Ramp_Start(ramp, 0.2f, 1.0f, 0.0f);
        (*state)++;
    case 1:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (removed & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], (u8)((f32)c[3] * ramp->value));
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
                HudSprite_Show(&shades[i], 0);
            } else if (prev & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, from[i], 0);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], from[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], from[i], 0);
            icon++;
        }
        if (r) {
            (*state)++;
        }
        break;
    case 2:
        Ramp_Start(ramp, 0.3f, 0.0f, 1.0f);
        (*state)++;
    case 3:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (kept & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_Show(&frames[i], 1);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            t = 1.0f - powf(1.0f - ramp->value, 3.0f);
            x = (f32)from[i] + (f32)(to[i] - from[i]) * t;
            HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, x, 0);
            HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], x, 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], x, 0);
            icon++;
        }
        if (r) {
            if (added != 0) {
                (*state)++;
            } else {
                *state = -1;
            }
        }
        break;
    case 4:
        Ramp_Start(ramp, 0.2f, 0.0f, 1.0f);
        (*state)++;
    case 5:
        if (!HUDG_PAUSED()) {
            r = Ramp_Step(ramp);
        } else {
            r = 0;
        }
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (added & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], (u8)((f32)c[3] * ramp->value));
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, (u8)(ramp->value * 128.0f));
                HudSprite_Show(&shades[i], 0);
            } else if (mask & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
                HudSprite_Show(&frames[i], 1);
                HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, to[i], 0);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], to[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], to[i], 0);
            icon++;
        }
        if (r) {
            (*state)++;
        }
        break;
    default:
        for (i = 0; i < 4; i++) {
            c = (pos & (1 << i)) ? col[0] : col[1];
            HudSprite_InitTex(icon, gHudGauge->res, i + 0x13, 0);
            if (mask & (1 << i)) {
                HudSprite_Show(icon, 1);
                HudSprite_Show(&frames[i], 1);
                HudSprite_Show(&shades[i], 1);
            } else {
                HudSprite_Show(icon, 0);
                HudSprite_Show(&frames[i], 0);
                HudSprite_Show(&shades[i], 0);
            }
            HudSprite_SetColor(icon, c[0], c[1], c[2], c[3]);
            HudSprite_Move(icon, 5, -5);
            HudSprite_Move(icon, to[i], 0);
            HudSprite_SetColor(&frames[i], 0x80, 0x80, 0x80, 0x80);
            HudSprite_SetRect(&frames[i], 0, 0x2A, 0, 0x16);
            HudSprite_SetUv(&frames[i], 0, 0x2A, 0x16, 0x2C);
            HudSprite_Move(&frames[i], 0x66, 0x18);
            HudSprite_Move(&frames[i], to[i], 0);
            HudSprite_SetRect(&shades[i], 3, 0x27, 1, 0x15);
            HudSprite_SetUv(&shades[i], 0, 0x24, 0x2C, 0x40);
            HudSprite_Move(&shades[i], 0x66, 0x18);
            HudSprite_Move(&shades[i], to[i], 0);
            icon++;
        }
        break;
    }
}

#ifdef PORT
extern int Port_AspectMilli(void);
extern void Port_GsMarker(s32 effect);

/* Marks the side's life bar so the renderer does not take it for a full-screen flash at very wide ratios. */
static void HudGauge_HpAnchor(s32 stretch) {
    Port_GsMarker((stretch ? 0x15 : 0x10) + (HUD_SCR(gHudGauge->side) != 0));
}

/* Adds half the extra screen width before the renderer narrows the panel, bringing the bar to the timer. */
static s32 HudGauge_WideHpExtra(void) {
    extern int Port_HudLayout(void); /* gs_draw.c: 1 = the long bars were asked for in the settings */
    s32 aspect = Port_AspectMilli();
    return Port_HudLayout() == 1 && aspect > 1340 ? (256 * (aspect * 3 - 4000) + 2000) / 4000 : 0;
}

/* Extends the life-bar frame and its flash mask through a plain strip; the portrait and lower gauges stay put. */
static void HudGauge_DrawWideFrame(HudBSprite *spr, s32 clear) {
    s32 extra = HudGauge_WideHpExtra();
    HudBSprite piece;
    s32 i;

    if (!extra) {
        if (clear) { HudSprite_DrawAlphaClear(spr); }
        else { HudSprite_Draw(spr, gHudGauge->res, 0); }
        return;
    }
    HudGauge_HpAnchor(1);
    for (i = 0; i < 4; i++) {
        piece = *spr;
        if (i == 3) {
            piece.pos[2] = piece.uv[2] = 27;
        } else {
            piece.pos[3] = piece.uv[3] = 27;
            if (i == 0) {
                piece.pos[1] = piece.uv[1] = 210;
            } else if (i == 1) {
                piece.pos[0] = piece.uv[0] = 210;
                piece.uv[1] = 220;
                piece.pos[1] = 220 + extra;
            } else {
                piece.uv[0] = 220;
                piece.pos[0] = 220 + extra;
                piece.pos[1] += extra;
            }
        }
        if (clear) { HudSprite_DrawAlphaClear(&piece); }
        else { HudSprite_Draw(&piece, gHudGauge->res, 0); }
    }
    HudGauge_HpAnchor(0);
}
#endif

#ifdef PORT
/* Extends the bar's straight middle, keeping the step and angled tip. The moving mask follows the health
 * fraction over the new width. */
static void HudGauge_DrawHpSprite(s32 index, s32 flag, s32 clear) {
    HudBSprite *spr = &gHudGauge->spr[index];
    s32 extra = HudGauge_WideHpExtra();
    s32 cuts[4] = {0, 64, 144, 160};
    s32 i, origin = spr->pos[0] * (160 + extra) / 160;
    HudBSprite piece;

    if (!extra) {
        if (clear) { HudSprite_DrawAlphaClear(spr); }
        else { HudSprite_Draw(spr, gHudGauge->res, flag); }
        return;
    }
    for (i = 0; i < 3; i++) {
        piece = *spr;
        piece.pos[0] = origin + cuts[i] + (i == 2 ? extra : 0);
        piece.pos[1] = origin + cuts[i + 1] + (i >= 1 ? extra : 0);
        piece.uv[0] = cuts[i];
        piece.uv[1] = cuts[i + 1];
        if (clear) { HudSprite_DrawAlphaClear(&piece); }
        else { HudSprite_Draw(&piece, gHudGauge->res, flag); }
    }
}
#define HUDG_HP_DRAW(i, flag) HudGauge_DrawHpSprite(i, flag, 0)
#define HUDG_HP_CLEAR() HudGauge_DrawHpSprite(8, 0, 1)
#else
#define HUDG_HP_DRAW(i, flag) HudSprite_Draw(&gHudGauge->spr[i], gHudGauge->res, flag)
#define HUDG_HP_CLEAR() HudSprite_DrawAlphaClear(&gHudGauge->spr[8])
#endif

/* Draw of node 3: the health bar is drawn three times (sprites 1..3, 4..6, 7..8), each through the alpha mask of
 * sprite 8; HudGauge_UpdateHp (hud_a_d.c) sets the rectangles first. */
void HudGauge_DrawHp(void) {
#ifdef PORT
    HudGauge_HpAnchor(1);
#endif
    HUDG_HP_CLEAR();
    HudGauge_UpdateHp();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(1, 0);
    HUDG_HP_DRAW(2, 1);
    HUDG_HP_DRAW(3, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HUDG_HP_CLEAR();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(4, 0);
    HUDG_HP_DRAW(5, 1);
    HUDG_HP_DRAW(6, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HUDG_HP_CLEAR();
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HUDG_HP_DRAW(7, 1);
    HUDG_HP_DRAW(8, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
#ifdef PORT
    HudGauge_HpAnchor(0);
#endif
}
#undef HUDG_HP_DRAW
#undef HUDG_HP_CLEAR

/* Draw of node 10: sprite 47 as mask, the panel pieces, the red flash and the node's ten sparks. */
void HudGauge_DrawAura(HudBGroup *node) {
    HudBSprite *spr = &gHudGauge->spr[47];
    u32 i;

#ifdef PORT
    HudGauge_DrawWideFrame(spr, 1);
#else
    HudSprite_DrawAlphaClear(spr);
#endif
    HudGfx_CallBegin(HudGauge_GsBeginMask2);
#ifdef PORT
    HudGauge_DrawWideFrame(spr, 0);
#else
    HudSprite_Draw(spr, gHudGauge->res, 0);
#endif
    HudSprite_Draw(&gHudGauge->spr[48], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[49], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[50], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[51], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[52], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[63], gHudGauge->res, 1);
    for (i = 0; i < node->sprCount; i++) {
        HudSprite_Draw(node->sprList[i], gHudGauge->res, 1);
    }
    HudGfx_CallEnd(HudGauge_GsEndMask2);
#ifdef PORT
    HudGauge_DrawWideFrame(spr, 1);
#else
    HudSprite_DrawAlphaClear(&gHudGauge->spr[47]);
#endif
}

/* Draw of node 2: the bars-in-reserve markers. */
void HudGauge_DrawHpBarCount(HudBGroup *node) {
    u32 i;

    HudGauge_UpdateHpBarCount();
    for (i = 0; i < node->sprCount; i++) {
        HudSprite_Draw(node->sprList[i], gHudGauge->res, 0);
    }
}

/* Draw of node 8: the fill bar through the mask of sprite 38, then the digit and its flash. */
void HudGauge_DrawBlast(void) {
    HudSprite_DrawAlphaClear(&gHudGauge->spr[38]);
    HudGfx_CallBegin(HudGauge_GsBeginMask);
    HudSprite_Draw(&gHudGauge->spr[37], gHudGauge->res, 1);
    HudSprite_Draw(&gHudGauge->spr[38], gHudGauge->res, 0);
    HudGfx_CallEnd(HudGauge_GsEndMask);
    HudSprite_Draw(&gHudGauge->spr[35], gHudGauge->res, 0);
    HudSprite_Draw(&gHudGauge->spr[75], gHudGauge->res, 0);
}

/* Clears the mark of texture i of side i's face sheet. */
void HudGauge_ClearFaceTexMark(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (gHudGauge->faceRes[i] != NULL) {
            HudBTex *tex = gHudGauge->faceRes[i]->tex;

            tex += HUD_SCR(i);
            tex->mark = 0;
        }
    }
}

/* Draw of node 11: the fighter's face. The sheet is taken from the fighter object every frame. */
void HudGauge_DrawFace(HudBGroup *node) {
    u32 i;
    HudBSprite *spr;

    gHudGauge->faceRes[gHudGauge->side] = gHudGauge->obj[gHudGauge->side]->faceRes;
    if (gHudGauge->faceRes[gHudGauge->side] != NULL) {
        HudGauge_ClearFaceTexMark();
        for (i = 0; i < node->sprCount; i++) {
            spr = node->sprList[i];
            /* (PC build: a fighter's face sheet has two pictures, one drawn for the left place and one for the right;
               with the sides swapped the picture is the one of the place, not of the side) */
            HudSprite_InitTex(spr, gHudGauge->faceRes[gHudGauge->side], HUD_SCR(gHudGauge->side), 0);
            if (BtlCtrl_GetObj(HUD_WHO(gHudGauge->side))->unkA40 & 0x40000) {
                HudSprite_SetColor(spr, 0x80, 0x50, 0x70, 0x80);
            } else {
                HudSprite_SetColor(spr, 0x80, 0x80, 0x80, 0x80);
            }
            HudSprite_SetMirror(spr, HUD_SCR(gHudGauge->side));
            HudSprite_DrawAt(spr, gHudGauge->faceRes[gHudGauge->side], 0, 0x2C00, 0x2CD0);
        }
    }
}

/* Draw callback of the plain nodes: every sprite of the node. */
void HudGauge_DrawSprites(HudBGroup *node) {
    u32 i;

    for (i = 0; i < node->sprCount; i++) {
#ifdef PORT
        if (node->sprList[i] == &gHudGauge->spr[0]) {
            HudGauge_DrawWideFrame(node->sprList[i], 0);
            continue;
        }
#endif
        HudSprite_Draw(node->sprList[i], gHudGauge->res, 0);
    }
}

/* Selects the side shown by the next update / draw of the tree; side 1 is mirrored. */
#ifdef PORT
/* Widescreen on PC: tells the renderer where the 2D pieces that follow belong (port/src/gs_marker.c). */
#define PORT_2D_LEFT 0x10
#define PORT_2D_RIGHT 0x11
#define PORT_2D_CENTER 0x12
#define PORT_2D_END 0x13
extern void Port_GsMarker(s32 effect);
#endif

void HudGauge_SelectSide(s32 side) {
    s32 flip = HUD_SCR(side) != 0;
    s32 i;

    gHudGauge->side = side;
#ifdef PORT
    Port_GsMarker(HUD_SCR(side) ? PORT_2D_RIGHT : PORT_2D_LEFT);
#endif
    gHudGauge->grp->flags = (gHudGauge->grp->flags & ~2) | (flip << 1);
    HudSprite_SetMirror(&gHudGauge->spr[35], flip);
    HudSprite_SetMirror(&gHudGauge->spr[75], flip);
    for (i = 0; i < 4; i++) {
        HudSprite_SetMirror(&gHudGauge->spr[43 + i], flip);
    }
}

/* --- Setters fed by Hud_PreUpdate, by side. --- */
void HudGauge_SetHp(s32 side, s32 hp) {
    gHudGauge->hp[side] = hp;
}

/* Gaining a ki bar restarts the bar's lamp; reaching full ki restarts the pulse. */
void HudGauge_SetKi(s32 side, s32 ki) {
    if (gHudGauge->ki[side] / 20000 < ki / 20000) {
        Ramp_Start(&gHudGauge->kiBarFlash[side], 1.0f, 1.0f, 0.0f);
        if (ki > 99999) {
            Ramp_Start(&gHudGauge->kiFullPulse[side], 1.0f, 0.0f, 1.0f);
        }
    }
    gHudGauge->ki[side] = ki;
}

void HudGauge_SetMaxPower(s32 side, s32 value) {
    gHudGauge->maxPower[side] = value;
}

/* Gaining a stock starts the digit's flash. */
void HudGauge_SetBlast(s32 side, s32 blast) {
    if (gHudGauge->blast[side] / 100000 < blast / 100000) {
        Ramp_Start(&gHudGauge->blastFlash[side], 1.0f, 1.0f, 0.0f);
    }
    gHudGauge->blast[side] = blast;
}

/* mask: icons to show, pos: which of them are positive. On a change keeps the old layout in statFrom, lays the
 * shown icons out 24 apart and restarts the animation. An icon keeps its old brightness unless it is in mask. */
void HudGauge_SetStatIcons(s32 side, s32 mask, s32 pos) {
    s32 n = 0;
    s32 i;

    if (mask == gHudGauge->statMask[side] &&
        gHudGauge->statPos[side] ==
            ((mask & pos) | (gHudGauge->statPrev[side] & gHudGauge->statPos[side] & ~mask))) {
        return;
    }
    gHudGauge->statPrev[side] = gHudGauge->statMask[side];
    gHudGauge->statMask[side] = mask;
    gHudGauge->statPos[side] = (mask & pos) | (gHudGauge->statPrev[side] & gHudGauge->statPos[side] & ~mask);
    gHudGauge->statState[side] = 0;
    memcpy(gHudGauge->statFrom[side], gHudGauge->statTo[side], sizeof(gHudGauge->statTo[side]));
    for (i = 0; i < 4; i++) {
        if (gHudGauge->statMask[side] & (1 << i)) {
            gHudGauge->statTo[side][i] = n * 24;
            n++;
        } else {
            gHudGauge->statTo[side][i] = n * 24;
        }
    }
}

void HudGauge_SetKiReserve(s32 side, s32 value) {
    gHudGauge->kiReserve[side] = value;
}

/* Shake the panel and the health node for `count` frames by +-amp (Hud_PreUpdate: the side that takes combo
 * damage, 5 / 10 / 15 frames by the size of the damage). */
void HudGauge_ShakeHp(s32 side, u16 count, u16 amp) {
    gHudGauge->shake[side][0].count = count;
    gHudGauge->shake[side][0].amp = amp;
    gHudGauge->shake[side][1].count = count;
    gHudGauge->shake[side][1].amp = amp;
}

/* Shake the panel and the ki node (Hud_PreUpdate: 8 frames when the fighter powers up). */
void HudGauge_ShakeKi(s32 side, u16 count, u16 amp) {
    gHudGauge->shake[side][0].count = count;
    gHudGauge->shake[side][0].amp = amp;
    gHudGauge->shake[side][2].count = count;
    gHudGauge->shake[side][2].amp = amp;
}

void HudGauge_SetFighter(s32 side, HudBObj *obj) {
    gHudGauge->obj[side] = obj;
}

/* Slide the whole part away / back in `seconds`. */
void HudGauge_SlideOut(f32 seconds) {
    Ramp_Start(&gHudGauge->slideAll, seconds, 0.0f, 1.0f);
}

void HudGauge_SlideIn(f32 seconds) {
    Ramp_Start(&gHudGauge->slideAll, seconds, 1.0f, 0.0f);
}

/* Hud_PreUpdate passes BtlCtrl_IsSwitching(side): the side's panel slides away (0.3 s) while the member switch
 * runs and back after it; on the way back both trailing values restart from 0, so the bars fill up again. */
void HudGauge_SetSwitching(s32 side, s32 switching) {
    if (gHudGauge->switching[side] != switching) {
        if (switching) {
            Ramp_Start(&gHudGauge->slide[side], 0.3f, 0.0f, 1.0f);
        } else {
            Ramp_Start(&gHudGauge->slide[side], 0.3f, 1.0f, 0.0f);
        }
        if (!switching) {
            gHudGauge->hpShown[side] = 0;
            gHudGauge->kiShown[side] = 0;
        }
    }
    gHudGauge->switching[side] = switching;
}

/* Builds the part: the work, 76 sprites, 12 nodes. Tree: 0 root > 1 panel > { 10 aura, 2 bars in reserve,
 * 3 health, 4 ki > 5 ki lamps, 6 powered-up timer > 7 its lamps, 8 blast stock, 9 status icons, 11 face }.
 * `res` is sprite sheet 0 of the HUD file; the root is returned through `out`. */
void HudGauge_Init(HudBGroup **out, void *res) {
    HudBSprite *spr;
    HudBGroup *grp;
    s32 i;

    gHudGauge = Heap_Alloc(sizeof(HudBWork), 0x20, 0, HEAP_ANY);
    memset(gHudGauge, 0, sizeof(HudBWork));
    gHudGauge->spr = Heap_Alloc(76 * sizeof(HudBSprite), 0x20, 0, HEAP_ANY);
    memset(gHudGauge->spr, 0, 76 * sizeof(HudBSprite));
    gHudGauge->grp = Heap_Alloc(12 * sizeof(HudBGroup), 0x20, 0, HEAP_ANY);
    memset(gHudGauge->grp, 0, 12 * sizeof(HudBGroup));
    gHudGauge->res = res;
    for (i = 0; i < 2; i++) {
        gHudGauge->statState[i] = -1;
    }
    for (i = 0; i < 2; i++) {
        gHudGauge->faceRes[i] = NULL;
        gHudGauge->obj[i] = NULL;
    }
    spr = &gHudGauge->spr[0];
    HudSprite_InitTex(spr, res, 6, 0);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[7];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->spr[8];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[5];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->spr[6];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[2];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0x30, 0x40);
    HudSprite_Move(spr, 0xA0, 0);
    spr = &gHudGauge->spr[3];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[1];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[4];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0xA0, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0xA0, 0, 0x10);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[9];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[10];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 9, 0);
    spr = &gHudGauge->spr[11];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x12, 0);
    spr = &gHudGauge->spr[12];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x1B, 0);
    spr = &gHudGauge->spr[13];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x24, 0);
    spr = &gHudGauge->spr[14];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x10);
    HudSprite_SetUv(spr, 0, 0x10, 0x10, 0x20);
    HudSprite_Move(spr, 0x2D, 0);
    spr = &gHudGauge->spr[15];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->spr[16];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->spr[17];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->spr[18];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->spr[19];
    HudSprite_InitTex(spr, res, 8, 3);
    HudSprite_SetRect(spr, 0, 0xE, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x1E, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[20];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->spr[21];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->spr[22];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->spr[23];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->spr[24];
    HudSprite_InitTex(spr, res, 8, 2);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[25];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->spr[26];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->spr[27];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->spr[28];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->spr[29];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[30];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x1C, 0);
    spr = &gHudGauge->spr[31];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0x15, 0);
    spr = &gHudGauge->spr[32];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0xE, 0);
    spr = &gHudGauge->spr[33];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 7, 0);
    spr = &gHudGauge->spr[34];
    HudSprite_InitTex(spr, res, 8, 5);
    HudSprite_SetRect(spr, 0, 0x10, 0, 0x11);
    HudSprite_SetUv(spr, 0x10, 0x20, 0x10, 0x21);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[35];
    HudSprite_InitTex(spr, res, 0xE, 0);
    HudSprite_SetRect(spr, -32, 0, 0, 0x20);
    HudSprite_SetUv(spr, 0, 0x20, 0, 0x20);
    HudSprite_Move(spr, 0xA, -6);
    spr = &gHudGauge->spr[36];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x30, 0, 0xC);
    HudSprite_SetUv(spr, 0, 0x30, 0, 0xC);
    HudSprite_Move(spr, 0x1B, 0x2E);
    spr = &gHudGauge->spr[37];
    HudSprite_InitTex(spr, res, 8, 0);
    HudSprite_SetRect(spr, 0, 0x1A, 0, 6);
    HudSprite_SetUv(spr, 0xC0, 0xDA, 0, 6);
    HudSprite_Move(spr, 5, 3);
    spr = &gHudGauge->spr[38];
    HudSprite_InitTex(spr, res, 8, 1);
    HudSprite_SetRect(spr, 0, 0x1A, 0, 6);
    HudSprite_SetUv(spr, 0xA1, 0xBB, 0, 6);
    HudSprite_Move(spr, 5, 3);
    spr = &gHudGauge->spr[39];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[40];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x18, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[41];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x30, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[42];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x2A, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x2A, 0x16, 0x2C);
    HudSprite_Move(spr, 0x48, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[43];
    HudSprite_InitTex(spr, res, 0x13, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[44];
    HudSprite_InitTex(spr, res, 0x14, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->spr[45];
    HudSprite_InitTex(spr, res, 0x15, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x30, 0);
    spr = &gHudGauge->spr[46];
    HudSprite_InitTex(spr, res, 0x16, 0);
    HudSprite_Move(spr, 5, -5);
    HudSprite_Move(spr, 0x48, 0);
    spr = &gHudGauge->spr[47];
    HudSprite_InitTex(spr, res, 7, 0);
    spr = &gHudGauge->spr[48];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 0, 0x30, 1, 0xB);
    HudSprite_SetUv(spr, 0, 0x30, 0xC, 0x16);
    HudSprite_Move(spr, 0x1B, 0x2E);
    spr = &gHudGauge->spr[49];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[50];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x18, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[51];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x30, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[52];
    HudSprite_InitTex(spr, res, 0xF, 0);
    HudSprite_SetRect(spr, 3, 0x27, 1, 0x15);
    HudSprite_SetUv(spr, 0, 0x24, 0x2C, 0x40);
    HudSprite_Move(spr, 0x48, 0);
    HudSprite_Move(spr, 0x66, 0x18);
    spr = &gHudGauge->spr[53];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0, 0x40, 0, 0x40);
    spr = &gHudGauge->spr[54];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0, 0x40);
    spr = &gHudGauge->spr[55];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0, 0x40);
    spr = &gHudGauge->spr[56];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0xC0, 0x100, 0, 0x40);
    spr = &gHudGauge->spr[57];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0, 0x40, 0x40, 0x80);
    spr = &gHudGauge->spr[58];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0x40, 0x80);
    spr = &gHudGauge->spr[59];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0x40, 0x80);
    spr = &gHudGauge->spr[60];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0xC0, 0x100, 0x40, 0x80);
    spr = &gHudGauge->spr[61];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x40, 0x80, 0, 0x40);
    spr = &gHudGauge->spr[62];
    HudSprite_InitTex(spr, res, 0x10, 0);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    HudSprite_SetUv(spr, 0x80, 0xC0, 0, 0x40);
    spr = &gHudGauge->spr[64];
    HudSprite_Show(spr, 1);
    HudSprite_InitPlain(spr, 0x80, 0x80, 0x80, 0x80);
    HudSprite_SetRect(spr, 0, 0x40, 0, 0x40);
    spr = &gHudGauge->spr[65];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[66];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->spr[67];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->spr[68];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->spr[69];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0, 0x13, 0, 0x16);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->spr[70];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0, 0);
    spr = &gHudGauge->spr[71];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 8, 0);
    spr = &gHudGauge->spr[72];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x10, 0);
    spr = &gHudGauge->spr[73];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x18, 0);
    spr = &gHudGauge->spr[74];
    HudSprite_Show(spr, 0);
    HudSprite_InitTex(spr, res, 0x11, 0);
    HudSprite_SetRect(spr, 0, 0x13, 0, 0x16);
    HudSprite_SetUv(spr, 0x20, 0x33, 0, 0x16);
    HudSprite_Move(spr, 0x20, 0);
    spr = &gHudGauge->spr[75];
    HudSprite_Show(spr, 1);
    HudSprite_InitTex(spr, res, 0x12, 0);
    HudSprite_SetRect(spr, -32, 0, 0, 0x20);
    HudSprite_SetUv(spr, 0, 0x20, 0, 0x20);
    HudSprite_Move(spr, 0xA, -6);
    grp = &gHudGauge->grp[9];
    grp->x = 102;
    grp->y = 24;
    grp->sprCount = 4;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[43];
    grp->sprList[1] = &gHudGauge->spr[44];
    grp->sprList[2] = &gHudGauge->spr[45];
    grp->sprList[3] = &gHudGauge->spr[46];
    grp->update = HudGauge_UpdateStatIcons;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->grp[10];
    grp->sprCount = 10;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[53];
    grp->sprList[1] = &gHudGauge->spr[54];
    grp->sprList[2] = &gHudGauge->spr[55];
    grp->sprList[3] = &gHudGauge->spr[56];
    grp->sprList[4] = &gHudGauge->spr[57];
    grp->sprList[5] = &gHudGauge->spr[58];
    grp->sprList[6] = &gHudGauge->spr[59];
    grp->sprList[7] = &gHudGauge->spr[60];
    grp->sprList[8] = &gHudGauge->spr[61];
    grp->sprList[9] = &gHudGauge->spr[62];
    grp->update = HudGauge_UpdateAura;
    grp->draw = HudGauge_DrawAura;
    grp = &gHudGauge->grp[8];
    grp->x = 29;
    grp->y = 46;
    grp->sprCount = 3;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[35];
    grp->sprList[1] = &gHudGauge->spr[38];
    grp->sprList[2] = &gHudGauge->spr[75];
    grp->update = HudGauge_UpdateBlast;
    grp->draw = HudGauge_DrawBlast;
    grp = &gHudGauge->grp[3];
    grp->x = 69;
    grp->y = 9;
    grp->sprCount = 8;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[1];
    grp->sprList[1] = &gHudGauge->spr[2];
    grp->sprList[2] = &gHudGauge->spr[3];
    grp->sprList[3] = &gHudGauge->spr[4];
    grp->sprList[4] = &gHudGauge->spr[5];
    grp->sprList[5] = &gHudGauge->spr[6];
    grp->sprList[6] = &gHudGauge->spr[7];
    grp->sprList[7] = &gHudGauge->spr[8];
    grp->update = HudGauge_UpdateHpTrail;
    grp->draw = HudGauge_DrawHp;
    grp = &gHudGauge->grp[2];
    grp->x = 73;
    grp->y = 2;
    grp->sprCount = 6;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[9];
    grp->sprList[1] = &gHudGauge->spr[10];
    grp->sprList[2] = &gHudGauge->spr[11];
    grp->sprList[3] = &gHudGauge->spr[12];
    grp->sprList[4] = &gHudGauge->spr[13];
    grp->sprList[5] = &gHudGauge->spr[14];
    grp->draw = HudGauge_DrawHpBarCount;
    grp->update = NULL;
    grp = &gHudGauge->grp[5];
    grp->x = -3;
    grp->y = -3;
    grp->sprCount = 5;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[65];
    grp->sprList[1] = &gHudGauge->spr[66];
    grp->sprList[2] = &gHudGauge->spr[67];
    grp->sprList[3] = &gHudGauge->spr[68];
    grp->sprList[4] = &gHudGauge->spr[69];
    grp->update = HudGauge_UpdateKiLamps;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->grp[7];
    grp->x = -3;
    grp->y = -3;
    grp->sprCount = 5;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[70];
    grp->sprList[1] = &gHudGauge->spr[71];
    grp->sprList[2] = &gHudGauge->spr[72];
    grp->sprList[3] = &gHudGauge->spr[73];
    grp->sprList[4] = &gHudGauge->spr[74];
    grp->update = HudGauge_UpdateMaxPowerLamps;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->grp[4];
    grp->sprCount = 15;
    grp->x = 64;
    grp->y = 27;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[15];
    grp->sprList[1] = &gHudGauge->spr[16];
    grp->sprList[2] = &gHudGauge->spr[17];
    grp->sprList[3] = &gHudGauge->spr[18];
    grp->sprList[4] = &gHudGauge->spr[19];
    grp->sprList[5] = &gHudGauge->spr[20];
    grp->sprList[6] = &gHudGauge->spr[21];
    grp->sprList[7] = &gHudGauge->spr[22];
    grp->sprList[8] = &gHudGauge->spr[23];
    grp->sprList[9] = &gHudGauge->spr[24];
    grp->sprList[10] = &gHudGauge->spr[25];
    grp->sprList[11] = &gHudGauge->spr[26];
    grp->sprList[12] = &gHudGauge->spr[27];
    grp->sprList[13] = &gHudGauge->spr[28];
    grp->sprList[14] = &gHudGauge->spr[29];
    grp->childCount = 1;
    grp->childList = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->childList, 0, grp->childCount * sizeof(void *));
    grp->childList[0] = &gHudGauge->grp[5];
    grp->draw = HudGauge_DrawSprites;
    grp->update = HudGauge_UpdateKi;
    grp = &gHudGauge->grp[6];
    grp->x = 64;
    grp->y = 27;
    grp->sprCount = 5;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[30];
    grp->sprList[1] = &gHudGauge->spr[31];
    grp->sprList[2] = &gHudGauge->spr[32];
    grp->sprList[3] = &gHudGauge->spr[33];
    grp->sprList[4] = &gHudGauge->spr[34];
    grp->childCount = 1;
    grp->childList = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->childList, 0, grp->childCount * sizeof(void *));
    grp->childList[0] = &gHudGauge->grp[7];
    grp->draw = HudGauge_DrawSprites;
    grp->update = HudGauge_UpdateMaxPower;
    grp = &gHudGauge->grp[11];
    grp->x = 16;
    grp->y = 3;
    grp->sprCount = 1;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[64];
    grp->draw = HudGauge_DrawFace;
    grp->update = NULL;
    grp = &gHudGauge->grp[1];
    grp->x = -2;
    grp->sprCount = 6;
    grp->y = 8;
    grp->sprList = Heap_Alloc(grp->sprCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->sprList, 0, grp->sprCount * sizeof(void *));
    grp->sprList[0] = &gHudGauge->spr[0];
    grp->sprList[1] = &gHudGauge->spr[36];
    grp->sprList[2] = &gHudGauge->spr[39];
    grp->sprList[3] = &gHudGauge->spr[40];
    grp->sprList[4] = &gHudGauge->spr[41];
    grp->sprList[5] = &gHudGauge->spr[42];
    grp->childCount = 8;
    grp->childList = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->childList, 0, grp->childCount * sizeof(void *));
    grp->childList[0] = &gHudGauge->grp[10];
    grp->childList[1] = &gHudGauge->grp[2];
    grp->childList[2] = &gHudGauge->grp[3];
    grp->childList[3] = &gHudGauge->grp[4];
    grp->childList[4] = &gHudGauge->grp[6];
    grp->childList[5] = &gHudGauge->grp[8];
    grp->childList[6] = &gHudGauge->grp[9];
    grp->childList[7] = &gHudGauge->grp[11];
    grp->update = HudGauge_UpdatePanel;
    grp->draw = HudGauge_DrawSprites;
    grp = &gHudGauge->grp[0];
    grp->childCount = 1;
    grp->x = 0;
    grp->y = 0;
    grp->childList = Heap_Alloc(grp->childCount * sizeof(void *), 0x20, 0, HEAP_ANY);
    memset(grp->childList, 0, grp->childCount * sizeof(void *));
    grp->childList[0] = &gHudGauge->grp[1];
    grp->update = HudGauge_UpdateRoot;
    grp->draw = NULL;
    *out = gHudGauge->grp;
}

#ifdef PORT
/* The gauges of the fighters past the two sides (the third-fighter experiment; teams: btl_char_flag_opp.c).
 *
 * The part above is one tree shown twice and everything it keeps is a [2] array by side, so a fighter with roste
 * index 2 or more has no place in it. Each of them gets a small row of its own instead, drawn from the same sheet
 * with the same sprite library, under the gauge of the side it is teamed with (a fighter on a team of its own:
 * under player 2's):
 *
 *     face (14 x 14)   health bar (80 x 8: the side's bar at half size)
 *                      ki (five lamps of 20000 each)   one marker per health bar in reserve
 *
 * Health reads as the side's does: one bar is 10000, the colour says how many bars are under it, the trailing
 * (damage) bar falls towards the health at the side's speeds. At no health the row is drawn dark and the bar is
 * empty. Rows stack downwards, four to a column and two columns to a side, so eight fit under one side.
 * Nothing here is drawn, and nothing is added to the frame, while the roster has two fighters. */
#define PORT_FIGHTER_MAX 10 /* fighters a fight may have one day; the two sides are the first two */
#define HUDX_ROWS 4         /* rows of a column */
#define HUDX_SLOTS 8        /* rows under one side */
#define HUDX_BAR_W 80      /* the health bar: half the side's 160 */
#define HUDX_X 18          /* where the bars start: behind the face */

extern s32 BtlChar_GetCount(void);
extern s32 Port_Team(s32 i); /* btl_char_flag_opp.c */
extern void *BtlChar_FindBySide(s32 side); /* (a number past the two sides is the roster index: btl_char_get.c; */
extern s32 BtlSide_GetHp(s32 side);        /* so the by-side queries answer for any fighter) */
extern s32 BtlSide_GetKi(s32 side);
extern void HudNode_Draw(HudBGroup *node, s32 mirror);
extern int Port_HudTestRows(void); /* port/src/plat_stub.c: 1 unless BT3_HUD_ROWS is set */

static s32 sHudExtraShown[PORT_FIGHTER_MAX]; /* trailing health, by roster index */
static s32 sHudExtraDrain[PORT_FIGHTER_MAX];
static s32 sHudExtraFighter;                 /* the fighter whose row is being drawn */
static s32 sHudExtraPlace;                   /* and the side of the screen it is on */
static HudBGroup sHudExtraNode;

/* One piece of the sheet: texture 8 with the palette of entry 8 + sub. */
static void HudExtra_Tex(s32 sub, s32 x0, s32 x1, s32 y0, s32 y1, s32 u0, s32 u1, s32 v0, s32 v1, s32 lum) {
    HudBSprite piece;

    memset(&piece, 0, sizeof(piece));
    HudSprite_InitTex(&piece, gHudGauge->res, 8, sub);
    HudSprite_SetRect(&piece, x0, x1, y0, y1);
    HudSprite_SetUv(&piece, u0, u1, v0, v1);
    HudSprite_SetColor(&piece, lum, lum, lum, 0x80);
    HudSprite_Draw(&piece, gHudGauge->res, 0);
}

/* An untextured rectangle. */
static void HudExtra_Plain(s32 x0, s32 x1, s32 y0, s32 y1, s32 r, s32 g, s32 b, s32 a) {
    HudBSprite piece;

    memset(&piece, 0, sizeof(piece));
    HudSprite_InitPlain(&piece, r, g, b, a);
    HudSprite_SetRect(&piece, x0, x1, y0, y1);
    HudSprite_Draw(&piece, gHudGauge->res, 0);
}

/* The palette of a health bar with `bars` full bars under it (the side's rule, HudGauge_UpdateHp). */
static s32 HudExtra_HpSub(s32 bars) {
    return bars >= 7 ? 5 : bars >= 2 ? 1 : bars > 0 ? 4 : 3;
}

/* Width of `rem` of a bar's 10000; health that is not zero shows at least two pixels. */
static s32 HudExtra_HpWidth(s32 rem) {
    s32 w = rem * HUDX_BAR_W / HUDG_HP_BAR;
    return w < 2 && rem != 0 ? 2 : w;
}

/* Draw callback of the row's node. */
static void HudExtra_DrawRow(void) {
    s32 i = sHudExtraFighter;
    s32 hp = BtlSide_GetHp(i);

    s32 ki = BtlSide_GetKi(i);
    s32 dead = hp <= 0 || BtlCtrl_IsActiveDead(i);
    s32 lum = dead ? 0x30 : 0x80;
    s32 shown, rem, bars, rem2, bars2, w, k;
    HudBObj *obj = BtlCtrl_GetObj(i);

    if (hp < 0) {
        hp = 0;
    }
    /* (the trailing health: HudGauge_UpdateHpTrail's rule) */
    if (!HUDG_PAUSED()) {
        if (hp < sHudExtraShown[i]) {
            s32 gap = sHudExtraShown[i] - hp;
            s32 drain = gap > 20000 ? 200 : gap > 10000 ? 150 : gap > 5000 ? 120 : 100;
            sHudExtraDrain[i] = HUDB_MAX(sHudExtraDrain[i], drain);
            sHudExtraShown[i] -= sHudExtraDrain[i];
            if (sHudExtraShown[i] < hp) {
                sHudExtraShown[i] = hp;
            }
        } else {
            sHudExtraShown[i] = hp;
            sHudExtraDrain[i] = 100;
        }
    }
    shown = sHudExtraShown[i] < hp ? hp : sHudExtraShown[i];
    rem = shown % HUDG_HP_BAR;
    bars = shown / HUDG_HP_BAR;
    if (rem == 0 && bars != 0) {
        rem = HUDG_HP_BAR;
        bars--;
    }
    rem2 = hp % HUDG_HP_BAR;
    bars2 = hp / HUDG_HP_BAR;
    if (rem2 == 0 && bars2 != 0) {
        rem2 = HUDG_HP_BAR;
        bars2--;
    }

    /* the plate behind the row */
    HudExtra_Plain(0, HUDX_X + HUDX_BAR_W + 2, 0, 16, 0x08, 0x0C, 0x20, 0x50);
    /* the face: the picture of the fighter's own sheet that is drawn for this side of the screen */
    if (obj != NULL && obj->faceRes != NULL) {
        HudBSprite face;

        memset(&face, 0, sizeof(face));
        obj->faceRes->tex[sHudExtraPlace].mark = 0; /* (sent again: the side's face has used the same blocks) */
        HudSprite_InitTex(&face, obj->faceRes, sHudExtraPlace, 0);
        HudSprite_SetRect(&face, 1, 15, 1, 15);
        HudSprite_SetColor(&face, lum, lum, lum, 0x80);
        HudSprite_SetMirror(&face, sHudExtraPlace);
        HudSprite_DrawAt(&face, obj->faceRes, 0, 0x2C00, 0x2CD0);
    }
    /* health: the bar's shape in the dark (an empty bar), the bar under the one on show, the trailing bar, the
       bar */
    HudExtra_Tex(1, HUDX_X, HUDX_X + HUDX_BAR_W, 1, 9, 0, 160, 0, 16, 0x14);
    if (bars > 0) {
        HudExtra_Tex(HudExtra_HpSub(bars - 1), HUDX_X, HUDX_X + HUDX_BAR_W, 1, 9, 0, 160, 0, 16, lum);
    }
    if (shown != hp) {
        w = HudExtra_HpWidth(rem);
        /* (that palette is one flat colour over the whole rectangle, which the side's bar cuts to shape with a
           mask; here it is laid in two pieces: the bar is its lower half as far as the step at 64 of 160) */
        HudExtra_Tex(2, HUDX_X, HUDX_X + (w < 32 ? w : 32), 5, 9, 0, 2, 8, 16, lum);
        if (w > 32) {
            HudExtra_Tex(2, HUDX_X + 32, HUDX_X + w, 1, 9, 64, w * 2, 0, 16, lum);
        }
    }
    w = bars2 < bars ? 0 : HudExtra_HpWidth(rem2);
    if (w != 0) {
        HudExtra_Tex(HudExtra_HpSub(bars), HUDX_X, HUDX_X + w, 1, 9, 0, w * 2, 0, 16, lum);
    }
    /* ki: five lamps, the one being filled grows from its foot (HudGauge_UpdateKi) */
    for (k = 0; k < 5; k++) {
        s32 n = ki >= (k + 1) * HUDG_KI_BAR ? 14 : ki <= k * HUDG_KI_BAR ? 0 : (ki - k * HUDG_KI_BAR) * 14 / HUDG_KI_BAR;

        HudExtra_Tex(0, HUDX_X + k * 5, HUDX_X + 7 + k * 5, 9, 16, 0x10, 0x1E, 0x10, 0x1E, 0x28);
        if (n != 0) {
            HudExtra_Tex(1, HUDX_X + k * 5, HUDX_X + 7 + k * 5, 16 - n / 2, 16, 0x10, 0x1E, 0x1E - n, 0x1E, lum);
        }
    }
    /* the health bars in reserve (HudGauge_UpdateHpBarCount shows six at most) */
    for (k = 0; k < bars && k < 6; k++) {
        HudExtra_Tex(1, HUDX_X + 32 + k * 5, HUDX_X + 40 + k * 5, 9, 16, 0, 0x10, 0x11, 0x1F, lum);
    }
}

static void HudExtra_DrawTargetMark(void); /* (below) */

/* ---- the small gauges (asked for by the user: "a miniaturized version of the player HUD") ----
 * Every fighter the two gauges do not show gets the game's own gauge again, at half its size, under the gauge of
 * its team's side: the same tree of nodes and sprites (frame, face, health, ki, blast stock, the powered-up timer,
 * status icons), drawn by the same code. The tree keeps its state per side (the arrays of two in HudBWork:
 * trailing health and ki, flashes, the face, ...): each small gauge has a copy of one side's of its own
 * (sHudMini), swapped into that side while it is updated and drawn and swapped out again, so that its animations
 * go on from frame to frame and the side's own gauge is left as it was. The half size is a matrix around the
 * corner of the screen the side's gauge stands in, under which the tree is drawn as always. */
typedef f32 HudXMtx[4][4];
extern void Mtx_StoreIdentity(HudXMtx m);
extern void Vu0Cur_Push(void);
extern void Vu0Cur_Pop(void);
extern void Vu0Cur_MulMtx(HudXMtx m);
extern void HudGfx_ClearAlpha(void);
extern void HudNode_Update(HudBGroup *node);
extern s32 BtlSide_GetBlast(s32 side);
extern s32 BtlSide_GetMaxPower(s32 side);
extern s32 BtlSide_GetParamUnk2C(s32 side);
extern s32 BtlSide_TestFlagBE(s32 side);
#define HUDM_SCALE 0.5f
#define HUDM_TOP 74  /* the first small gauge's top, in the screen's 448 lines (a gap under the side's own: the user) */
#define HUDM_STEP 30 /* from one to the next */
typedef struct HudMiniSlot {
    HudBShake shake[3];
    s32 hpShown, hp, auraAlpha;
    Ramp lowHpPulse;
    s32 blast, ki, kiShown;
    Ramp kiBarFlash, kiFullPulse;
    s32 maxPower;
    Ramp blastFlash;
    u32 statMask, statPos;
    s32 kiReserve;
    u32 statPrev;
    s32 statTo[4], statFrom[4];
    Ramp statRamp;
    s32 statState;
    Ramp hpBarFade;
    HudBRes *faceRes;
    HudBObj *obj;
    s32 switching;
    Ramp slide;
    s32 hpDrain, kiDrain;
    Ramp kiReservePulse;
} HudMiniSlot;
static HudMiniSlot sHudMini[PORT_FIGHTER_MAX];
static s32 sHudMiniUsed[PORT_FIGHTER_MAX]; /* (0: not drawn yet this fight: started from the side's own) */

static void HudMini_Swap(void *a, void *b, u32 size) {
    u8 t[64];
    u32 n;
    for (n = 0; n < size; n += sizeof(t)) {
        u32 c = size - n < sizeof(t) ? size - n : sizeof(t);
        memcpy(t, (u8 *)a + n, c);
        memcpy((u8 *)a + n, (u8 *)b + n, c);
        memcpy((u8 *)b + n, t, c);
    }
}

/* Does op (swap, or copy into m) on every value small gauge m keeps, with side k's. */
static void HudMini_Copy(void *a, void *b, u32 size) {
    memcpy(a, b, size);
}
static void HudMini_Each(HudMiniSlot *m, s32 k, void (*op)(void *, void *, u32)) {
    HudBWork *w = gHudGauge;
#define HUDM_X(f) op(&m->f, &w->f[k], sizeof(m->f))
    op(m->shake, w->shake[k], sizeof(m->shake));
    HUDM_X(hpShown); HUDM_X(hp); HUDM_X(auraAlpha); HUDM_X(lowHpPulse); HUDM_X(blast); HUDM_X(ki); HUDM_X(kiShown);
    HUDM_X(kiBarFlash); HUDM_X(kiFullPulse); HUDM_X(maxPower); HUDM_X(blastFlash); HUDM_X(statMask); HUDM_X(statPos);
    HUDM_X(kiReserve); HUDM_X(statPrev); op(m->statTo, w->statTo[k], sizeof(m->statTo));
    op(m->statFrom, w->statFrom[k], sizeof(m->statFrom)); HUDM_X(statRamp); HUDM_X(statState);
    HUDM_X(hpBarFade); HUDM_X(faceRes); HUDM_X(obj); HUDM_X(switching); HUDM_X(slide); HUDM_X(hpDrain);
    HUDM_X(kiDrain); HUDM_X(kiReservePulse);
#undef HUDM_X
}

/* Fighter i's gauge on screen side `place` (0 left, 1 right), at `scale` of its size, its top `top` lines down
   (with `dx` to the side): a small one of a row, or a bottom quarter's (four local players). */
/* (m: the gauge's own state, *used 0 until it has been drawn once; anchor: PORT_2D_ placement in a wide picture) */
static void HudExtra_DrawGaugeIn(HudMiniSlot *m, s32 *used, s32 i, s32 place, f32 scale, f32 top, f32 dx, s32 anchor) {
    s32 k = HUD_SCR(0) == place ? 0 : 1; /* (the side whose gauge stands on that side of the screen: its mirroring) */
    s32 was = gHudGauge->side;
    Ramp slideAll = gHudGauge->slideAll;
    HudXMtx mtx;
    s32 hp = BtlSide_GetHp(i);
    HudBSprite savedSprites[76];HudBGroup savedGroups[12];
    memcpy(savedSprites,gHudGauge->spr,sizeof(savedSprites));memcpy(savedGroups,gHudGauge->grp,sizeof(savedGroups));

    if (!*used) {
        *used = 1;
        HudMini_Each(m, k, HudMini_Copy); /* (a copy of the side's own to start from) */
        m->hpShown = hp > 0 ? hp : 0;
        m->hp = m->hpShown;
        m->kiShown = BtlSide_GetKi(i);
        m->switching = 0;
        m->slide.value = 0.0f;
    }
    HudMini_Each(m, k, HudMini_Swap);
    sHudMiniWho = i;
    HudGauge_SetHp(k, hp);
    HudGauge_SetKi(k, BtlSide_GetKi(i));
    HudGauge_SetKiReserve(k, BtlSide_TestFlagBE(i) ? BtlSide_GetParamUnk2C(i) : 0);
    HudGauge_SetBlast(k, BtlSide_GetBlast(i));
    HudGauge_SetMaxPower(k, BtlSide_GetMaxPower(i));
    HudGauge_SetFighter(k, BtlCtrl_GetObj(i));
    gHudGauge->switching[k] = 0;
    Vu0Cur_Push();
    Mtx_StoreIdentity(mtx);
    mtx[0][0] = scale;
    mtx[1][1] = scale;
    /* (about the screen's top left corner; a right side's gauge is mirrored about the screen after it) */
    mtx[3][0] = dx; /* (the right side's mirroring is applied after this: the same corner on both) */
    mtx[3][1] = top;
    Vu0Cur_MulMtx(mtx);
    HudGfx_ClearAlpha();
    HudGauge_SelectSide(k);
    Port_GsMarker(anchor); /* (after the side's own, which HudGauge_SelectSide sets) */
    {
        /* (placed from the middle: so are its life bar, ki and blast stock, which give their side's marker) */
        extern void Port_GsMarkerForce(int effect);
        Port_GsMarkerForce(anchor == PORT_2D_CENTER ? PORT_2D_CENTER : 0);
        HudNode_Update(gHudGauge->grp);
        HudNode_Draw(gHudGauge->grp, 0);
        Port_GsMarkerForce(0);
    }
    Vu0Cur_Pop();
    sHudMiniWho = -1;
    HudMini_Each(m, k, HudMini_Swap);
    gHudGauge->slideAll = slideAll;
    HudGauge_SelectSide(was);
    memcpy(gHudGauge->spr,savedSprites,sizeof(savedSprites));memcpy(gHudGauge->grp,savedGroups,sizeof(savedGroups));
}
static void HudExtra_DrawGauge(s32 i, s32 place, f32 scale, f32 top, f32 dx) {
    HudExtra_DrawGaugeIn(&sHudMini[i], &sHudMiniUsed[i], i, place, scale, top, dx, place ? PORT_2D_RIGHT : PORT_2D_LEFT);
}

/* ---- each player's own HUD (asked for by the user: "for each screen when not in 1p mode ... the player and the
 * target", "the target on the right ... like the actual game") ----
 * A split screen with more than two fighters (two halves, or the four quarters of four local players): every part
 * of the screen has its player's gauge in its top left corner and its target's in its top right one, mirrored, as
 * the game's own fight of one player has them, at half size; the game's two big gauges are not drawn
 * (Port_MultiHud, hud_a.c). The ones next to the middle of the screen keep a gap for the timer and are placed from
 * the middle in a wide picture. Each place has its own gauge state, started again when its fighter changes. */
#define HUDP_SCALE 0.5f
#define HUDP_GAP 24.0f
static HudMiniSlot sHudPart[8];
static s32 sHudPartUsed[8], sHudPartWho[8];

s32 Port_MultiHud(void) {
    extern s32 Battle_IsSplitScreen(void);
    return BtlChar_GetCount() > 2 && Battle_IsSplitScreen();
}

/* How many parts the screen has (4 quarters, else 2 halves) and part r's fighter (its player's, or the one it
   watches). */
s32 Port_HudPartCount(void) {
    {extern int Port_FusionViews(int);int n=Port_FusionViews(-1);if(n>0)return n;}
    extern s32 Port_QuadMode(void);
    extern s32 Port_QuadThree(void);
    return Port_QuadMode() ? (Port_QuadThree() ? 3 : 4) : 2;
}
s32 Port_HudPartFighter(s32 r) {
    {extern int Port_FusionViews(int);if(Port_FusionViews(-1)>0)return Port_FusionViews(r);}
    extern s32 Port_QuadMode(void);
    extern s32 Port_QuarterFighter(s32 q);
    extern s32 Port_Spectated(s32 p);
    return Port_Spectated((Port_QuadMode()||(((Port_TagTourActive()&&Port_TagTourViews()==2)||Port_CoopPlayerViews()==2))) ? Port_QuarterFighter(r) : r);
}

static void HudExtra_DrawParts(void) {
    extern s32 Port_QuadMode(void);
    extern s32 Port_QuarterFighter(s32 q);
    extern s32 Port_Spectated(s32 p);
    extern s32 Port_CamTarget(s32 i);
    extern s32 gPortTarget[];
    extern s32 Port_QuadThree(void);
    s32 quad = Port_QuadMode(), n = Port_HudPartCount(), r, role, three = Port_QuadThree();

    for (r = 0; r < n; r++) {
        s32 me = quad ? Port_QuarterFighter(r) : r;
        s32 w = Port_Spectated(me);
        s32 t = w == me ? Port_CamTarget(me) : gPortTarget[w];
        f32 top = quad && r >= 2 ? 228.0f : 2.0f;
        s32 right = r & 1; /* (a part on the right half of the screen) */
        s32 wide = three && r == 2; /* (the whole bottom half: its target's gauge at the screen's right edge) */

        for (role = 0; role < 2; role++) {
            s32 slot = r * 2 + role, who = role == 0 ? w : t;
            if ((u32)who >= (u32)BtlChar_GetCount() || (role == 1 && who == w)) {
                continue;
            }
            if (sHudPartWho[slot] != who + 1) {
                sHudPartWho[slot] = who + 1;
                sHudPartUsed[slot] = 0;
            }
            if (role == 0) { /* (the player's: left, at the screen's edge or from the middle) */
                HudExtra_DrawGaugeIn(&sHudPart[slot], &sHudPartUsed[slot], who, HUD_SCR(0), HUDP_SCALE, top,
                                     right ? 256.0f + HUDP_GAP : 0.0f, right ? PORT_2D_CENTER : PORT_2D_LEFT);
            } else { /* (the target's: right, mirrored) */
                HudExtra_DrawGaugeIn(&sHudPart[slot], &sHudPartUsed[slot], who, HUD_SCR(1), HUDP_SCALE, top,
                                     right || wide ? 0.0f : 255.0f + HUDP_GAP, right || wide ? PORT_2D_RIGHT : PORT_2D_CENTER);
            }
        }
    }
}

static void HudExtra_DrawMini(s32 i, s32 place, s32 slot) {
    HudExtra_DrawGauge(i, place, HUDM_SCALE, (f32)(HUDM_TOP + slot * HUDM_STEP), 2.0f);
}

/* Draws the rows; called by Hud_Draw behind the two passes of the gauge tree. */
void HudGauge_DrawExtras(void) {
    s32 used[2] = { 0, 0 }; /* rows taken, by side of the screen */
    s32 count;
    s32 i;

    {extern int Port_Setting(const char*,int);int style=Port_Setting("mod_hud_style",1);if(style!=0){/* Overhead layouts draw per-view inward chevrons beside the target gauge. */if(style==3)HudExtra_DrawTargetMark();return;}}
    if (BtlChar_FindBySide(0) == NULL) {
        return; /* (no roster yet) */
    }
    count = BtlChar_GetCount();
    {
        /* Four quarters (four local players): every quarter has its fighter's gauge in its own top corner, as the
           game's two have theirs: the top quarters' are those two (Port_HudFighter), the bottom ones' the same gauge
           half the screen lower. No rows of small gauges: a quarter has no room for them. */
        extern s32 Port_QuadMode(void);
        extern s32 Port_QuarterFighter(s32 q);
        extern s32 Port_Spectated(s32 p);
        if (Port_MultiHud()) {
            HudExtra_DrawParts();
            HudExtra_DrawTargetMark();
            Port_GsMarker(HUD_SCR(1) ? PORT_2D_RIGHT : PORT_2D_LEFT);
            return;
        }
    }
    for (i = 0; i < count && i < PORT_FIGHTER_MAX; i++) {
        /* (everybody but the two the gauges show: the player's and its target, Port_HudFighter; player 1's team
           under player 1's gauge, player 2's under player 2's; a fighter of another team, which is everybody in a
           free-for-all, goes to the side that has fewer rows, the right one first) */
        s32 team = Port_Team(i);
        if (count < 3 || i == HUD_WHO(0) || i == HUD_WHO(1)) {
            continue;
        }
        if (i >= 2) {
            extern s32 Port_FightConsumed(s32 k); /* port/src/plat_fighters.c */
            if (Port_FightConsumed(i - 2)) {
                continue; /* (a fusion merged this ally into its partner: no row) */
            }
        }
        s32 place = team == 0 ? HUD_SCR(0) : team == 1 ? HUD_SCR(1) : used[HUD_SCR(1)] <= used[HUD_SCR(0)] ? HUD_SCR(1) : HUD_SCR(0);
        s32 n;

        /* (one row a fighter; BT3_HUD_ROWS=<n>, testing, draws it n times to show a full stack) */
        for (n = Port_HudTestRows(); n > 0 && used[place] < HUDX_SLOTS; n--) {
            s32 slot = used[place]++;

            HudExtra_DrawMini(i, place, slot);
        }
    }
    if (count > 2) {
        HudExtra_DrawTargetMark();
        Port_GsMarker(HUD_SCR(1) ? PORT_2D_RIGHT : PORT_2D_LEFT); /* (as the second pass of the tree left it) */
    }
}

/* The marker over the player's target (btl_char_flag_opp.c, Port_TargetMark): a small triangle pointing down
   above its head, yellow while the game chooses the target (the nearest enemy) and red when the player has
   chosen it. It is drawn straight in the picture's own coordinates (no 2D anchoring: it has to stay over the
   fighter in any picture shape), its size from how tall the fighter is on the screen. */
typedef struct { f32 x, y, z, w; } __attribute__((aligned(16))) HudXPoint; /* (a Vec4) */
extern s32 Port_TargetMark(s32 p, HudXPoint *head, HudXPoint *feet, s32 *locked); /* btl_char_flag_opp.c */
extern s32 Port_ProjectPoint(HudXPoint *pos, s32 *x, s32 *y);                   /* btl_cam.c */
extern u64 *Dma_BeginDirect(void);
extern void Dma_EndDirect(u64 *end);

static void HudExtra_Triangle(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgba) {
    u64 *p = Dma_BeginDirect();
    u64 *tag = p;

    p += 2;
    p[0] = rgba;
    p[1] = 0x43; /* triangle, alpha blended, context 1 */
    p += 2;
    p[0] = 0;
    p[1] = (u64)(u16)x0 | ((u64)(u16)y0 << 16);
    p += 2;
    p[0] = 0;
    p[1] = (u64)(u16)x1 | ((u64)(u16)y1 << 16);
    p += 2;
    p[0] = 0;
    p[1] = (u64)(u16)x2 | ((u64)(u16)y2 << 16);
    p += 2;
    tag[0] = ((u64)8 << 60) | ((u64)1 << 58) | ((u64)1 << 15) | 1; /* 8 registers, REGLIST, EOP, 1 loop */
    tag[1] = 0x53535301;                                            /* RGBAQ, PRIM, 3 x (UV, XYZ2) */
    Dma_EndDirect(p);
}

static void HudExtra_DrawTargetMark(void) {
    extern s32 Port_QuadMode(void);
    extern s32 Port_QuarterFighter(s32 q);
    extern s32 Port_QuarterProject(s32 q, HudXPoint *pos, s32 *x, s32 *y); /* btl_cam.c */
    HudXPoint head, feet;
    s32 p, locked, ax, ay, fx, fy, h, s, tip;
    s32 quad = Port_QuadMode(); /* (four quarters: each player's marker in its own quarter, through its camera) */

    for (p = 0; p < (quad ? Port_HudPartCount() : 2); p++) {
        if (quad) {
            if (!Port_TargetMark(Port_QuarterFighter(p), &head, &feet, &locked) || !Port_QuarterProject(p, &head, &ax, &ay) ||
                !Port_QuarterProject(p, &feet, &fx, &fy)) {
                continue;
            }
        } else if (!Port_TargetMark(p, &head, &feet, &locked) || !Port_ProjectPoint(&head, &ax, &ay) ||
                   !Port_ProjectPoint(&feet, &fx, &fy)) {
            continue;
        }
        h = fy > ay ? fy - ay : ay - fy;     /* the fighter's height on the screen (head to feet) */
        s = h / 8;
        s = s < 4 * 16 ? 4 * 16 : s > 9 * 16 ? 9 * 16 : s;
        tip = ay - h / 6 - s;                 /* above the head and its hair */
        Port_GsMarker(PORT_2D_END);
        HudExtra_Triangle(ax - s - 24, tip - s * 2 - 16, ax + s + 24, tip - s * 2 - 16, ax, tip + 24, 0x80000000);
        HudExtra_Triangle(ax - s, tip - s * 2, ax + s, tip - s * 2, ax, tip,
                          locked ? 0x80202CFF : 0x8030E0FF); /* (A B G R) */
    }
}

/* Round reset: no trailing health is left over from the fight before. */
void HudGauge_ResetExtras(void) {
    memset(sHudExtraShown, 0, sizeof(sHudExtraShown));
    memset(sHudExtraDrain, 0, sizeof(sHudExtraDrain));
    memset(sHudMiniUsed, 0, sizeof(sHudMiniUsed)); /* (each small gauge starts again from its side's) */
    memset(sHudPartUsed, 0, sizeof(sHudPartUsed));
    memset(sHudPartWho, 0, sizeof(sHudPartWho));
}
#endif
