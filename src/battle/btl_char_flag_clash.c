#include "common.h"
#include "battle/btl_char_flag.h"

/*
 * Clash sequences between the two fighters: 0x1D8750..0x1D9A20.
 *
 * BtlClash_Update runs once per frame after the per-fighter passes. While roster +0x40 is 0 it looks
 * for both fighters being in a clash action at once and starts the matching sequence:
 *   A (states 1..5)   both in actions 0x130..0x132
 *   B (states 6..8)   both in action 0xFA
 *   C (states 9..11)  both in action 0xFB (then 0xFC)
 * The sequences drive the fighter camera, voices and the flags that tell the action code who won.
 */

extern f32 atan2f(f32 y, f32 x);
extern f32 sinf(f32 x);
extern f32 cosf(f32 x);
extern f32 Mathf_Asin(f32 x);
extern void Vec3_Normalize(Vec4 *dst, Vec4 *src);
extern void Vec4_Lerp(Vec4 *dst, Vec4 *a, Vec4 *b, f32 t); /* a + (b - a) * t */
extern f32 BtlUtil_WrapAngle(f32 a);
extern f32 BtlUtil_ClampF(f32 v, f32 lo, f32 hi);
extern BtlFlagChr *BtlChar_Get(s32 i);
extern BtlFlagPose *BtlChar_GetPos(BtlFlagChr *chr);
extern s32 BtlChar_FrameMod(s32 n);
extern u32 BtlChar_Rand(void);
extern void BtlChar_PlayVoice(BtlFlagChr *chr, s32 kind);
extern s32 Battle_GetMode(void);
extern void ChrCam_SetCut(BtlFlagChr *chr, Vec4 *vecA, Vec4 *vecADelta, Vec4 *vecB, Vec4 *vecBDelta, Vec4 *vecC,
                          Vec4 *vecCDelta, s32 unk88, f32 valA, f32 valADelta, f32 valB, f32 valBDelta, f32 valC,
                          f32 valCDelta, s32 unk8C, s32 unk90, s32 unk94, s32 time, s32 flags);
extern void ChrCam_RequestCut(BtlFlagChr *chr, s32 arg1, s32 arg2);
extern void ChrCam_EndCut(BtlFlagChr *chr);
extern s32 BtlCharApi_IsInClashA(s32 objId);                                  /* action id in 0x130..0x132 */
extern s32 BtlAct_GetCurrent(BtlFlagChr *chr);                            /* action id */
extern s32 BtlAct_GetCurrentClass(BtlFlagChr *chr);                            /* class of the technique in use (0..4), or -1 */
extern void BtlAct_CountAndMarkOpponent(BtlFlagChr *chr);                           /* chr->clashCountB++, opponent +0xD4C = 1 */
extern f32 BtlCharApi_GetHeight(s32 objId);                                  /* height */
extern void BtlCharApi_GetNodePos(s32 objId, s32 node, Vec4 *out);            /* world position of a model node */
extern f32 EftStruggle_GetCamDist(s32 side, s32 type);
extern f32 EftStruggle_GetMidDist(void);
extern void EftStruggle_End(s32 objId);
extern s32 BtlStage_GetPathCount(void);                                       /* number of stage paths */
extern BtlClashPath *BtlStage_GetPath(s32 n);                            /* stage path n */
extern s32 BtlSuper_IsThrow(BtlFlagChr *chr, s32 cls);
extern s32 BtlSuper_GetFlags(BtlFlagChr *chr, s32 cls);                   /* attribute word of the technique */
extern s32 BtlSuper_GetDamage(BtlFlagChr *chr, s32 cls, s32 arg2, s32 arg3); /* damage of the technique */
extern void BtlColl_StartThrow(BtlFlagChr *chr, BtlFlagChr *target, s32 cls, s32 attr);
extern void BtlMember_Damage(BtlFlagChr *chr, s32 damage, s32 flags);    /* applies damage */

extern BtlFlagRoster *gBtlChars;
extern Vec4 D_002EC2A0; /* zero vector */

typedef struct BtlClashProgress {
    /* 0x00 */ u8 unk0[0x3C];
    /* 0x3C */ s32 unk3C; /* difficulty */
} BtlClashProgress;
extern BtlClashProgress *gProgress;

#ifdef PORT
/* The pair of a clash (the third-fighter experiment, and what more fighters would stand on).
 *
 * The game runs a clash for fighters 0 and 1: every function of this file names them by number. Here the two are
 * a PAIR of roster indices, the lower one first (it takes fighter 0's part: the camera's side, the first voice,
 * "the lead is positive when it is ahead"), found from the fighters themselves: two enemies that are in the same
 * clash action at once (Port_ClashFind, when no sequence runs). The code that puts two fighters into a clash
 * (BtlHit_CheckClash for blows, EftDet_ClashPass for beams) names the pair a few frames earlier with
 * Port_ClashPair, so that the two are each other's opponent from the first frame on.
 *
 * The state of a clash (roster +0x40..0x94: the sequence's step, the lead, the camera's point, the path) is ONE:
 * one clash at a time. While one runs or is about to, another pair is refused (Port_ClashBusy), and a fighter
 * found in a clash action without being of the pair is let out of it as the game lets out a fighter left alone.
 * With two fighters the pair is 0 and 1 and none of this changes anything. */
extern s32 BtlChar_GetCount(void);
extern s32 Port_IsEnemy(s32 a, s32 b);                 /* btl_char_flag_opp.c */
extern void Port_TargetHit(s32 attacker, s32 victim);  /* btl_char_flag_opp.c: each is the other's target */
extern void Port_ClashPairLog(const char *what, int a, int b, int countA, int countB, int winner); /* port/src/plat_stub.c */
extern int Port_ForceClash(int *a, int *b);            /* port/src/plat_stub.c: BT3_FORCE_CLASH, a test's tool */
extern void Port_ForceClashDone(void);
extern s32 BtlAct_IsTechniqueId(s32 action);           /* action is 0x105..0x132 */
s32 gPortClash[2] = { 0, 1 };
static s32 sPortClashPending; /* frames for which a named pair waits for its sequence to start */
#define CLASH_P0 gPortClash[0]
#define CLASH_P1 gPortClash[1]
#define CLASH_P(n) gPortClash[n]

/* 1 while a clash runs or a pair has just been named: no other pair may start one. */
s32 Port_ClashBusy(void) {
    return gBtlChars->clashState != 0 || sPortClashPending > 0;
}

/* Names the pair of the clash that is about to start. Returns 0 when another is in the way. */
s32 Port_ClashPair(s32 a, s32 b) {
    if (a == b || (u32)a >= (u32)BtlChar_GetCount() || (u32)b >= (u32)BtlChar_GetCount() || !Port_IsEnemy(a, b)) {
        return 0;
    }
    if (Port_ClashBusy() && !((a == CLASH_P0 && b == CLASH_P1) || (a == CLASH_P1 && b == CLASH_P0))) {
        return 0;
    }
    CLASH_P0 = a < b ? a : b;
    CLASH_P1 = a < b ? b : a;
    sPortClashPending = 10;
    Port_TargetHit(a, b);
    return 1;
}

/* The other fighter of the clash `i` is in or about to be in, or -1. */
s32 Port_ClashPartner(s32 i) {
    if (BtlChar_GetCount() > 2 && Port_ClashBusy()) {
        if (i == CLASH_P0) {
            return CLASH_P1;
        }
        if (i == CLASH_P1) {
            return CLASH_P0;
        }
    }
    return -1;
}

/* What kind of clash action a fighter is in: 1 the struggle (0x130..0x132), 6 the blows (0xFA), 9 the vanish
   (0xFB); the numbers are the sequences' first steps. */
static s32 Port_ClashKindOf(s32 i) {
    s32 action = BtlAct_GetCurrent(BtlChar_Get(i));

    if (BtlCharApi_IsInClashA(i)) {
        return 1;
    }
    return action == 0xFA ? 6 : action == 0xFB ? 9 : 0;
}

/* No sequence runs: looks for two enemies in the same clash action and makes them the pair. */
static void Port_ClashFind(void) {
    s32 count = BtlChar_GetCount();
    s32 i, k;

    if (sPortClashPending > 0) {
        sPortClashPending--;
    }
    if (count < 3) {
        CLASH_P0 = 0;
        CLASH_P1 = 1;
        sPortClashPending = 0;
        return;
    }
    if (sPortClashPending > 0 && Port_ClashKindOf(CLASH_P0) != 0 && Port_ClashKindOf(CLASH_P0) == Port_ClashKindOf(CLASH_P1)) {
        return; /* (the named pair is there) */
    }
    for (i = 0; i < count; i++) {
        for (k = i + 1; k < count; k++) {
            if (Port_ClashKindOf(i) != 0 && Port_ClashKindOf(i) == Port_ClashKindOf(k) && Port_IsEnemy(i, k)) {
                CLASH_P0 = i;
                CLASH_P1 = k;
                Port_TargetHit(i, k);
                return;
            }
        }
    }
}
#else
#define CLASH_P0 0
#define CLASH_P1 1
#define CLASH_P(n) n
#endif

/* 1 while both fighters are in actions 0x130..0x132. */
s32 BtlClash_BothInClashA(void) {
    if (BtlCharApi_IsInClashA(CLASH_P0) && BtlCharApi_IsInClashA(CLASH_P1)) {
        return 1;
    }
    return 0;
}

/* Raises held flag 0xC3 on a fighter that is in actions 0x130..0x132. */
void BtlClash_HoldClashA(s32 player) {
    if (BtlCharApi_IsInClashA(player)) {
        BtlChar_SetHeldFlag(BtlChar_Get(player), 0xC3);
    }
}

/* 1 while both fighters are in action 0xFA. */
s32 BtlClash_BothInActionFA(void) {
    s32 action = BtlAct_GetCurrent(BtlChar_Get(CLASH_P0));

    if (action != 0xFA) {
        return 0;
    }
    return BtlAct_GetCurrent(BtlChar_Get(CLASH_P1)) == action;
}

/* Raises flag 0xBF on a fighter still in action 0xFA (ends it). */
void BtlClash_EndActionFA(s32 player) {
    BtlFlagChr *chr = BtlChar_Get(player);

    if (BtlAct_GetCurrent(chr) == 0xFA) {
        BtlChar_SetFlag(chr, 0xBF);
    }
}

/* 1 while both fighters are in action 0xFB. */
s32 BtlClash_BothInActionFB(void) {
    s32 action = BtlAct_GetCurrent(BtlChar_Get(CLASH_P0));

    if (action != 0xFB) {
        return 0;
    }
    return BtlAct_GetCurrent(BtlChar_Get(CLASH_P1)) == action;
}

/* 1 while both fighters are in action 0xFC. */
s32 BtlClash_BothInActionFC(void) {
    s32 action = BtlAct_GetCurrent(BtlChar_Get(CLASH_P0));

    if (action != 0xFC) {
        return 0;
    }
    return BtlAct_GetCurrent(BtlChar_Get(CLASH_P1)) == action;
}

/* Raises flag 0xC6 on a fighter still in action 0xFC (ends it). */
void BtlClash_EndActionFC(s32 player) {
    BtlFlagChr *chr = BtlChar_Get(player);

    if (BtlAct_GetCurrent(chr) == 0xFC) {
        BtlChar_SetFlag(chr, 0xC6);
    }
}

/* Clash A camera: a one-frame cut orbiting one fighter (type 0 wide, 1 from the side, 2 close on node 0x30). */
void BtlClash_SetOrbitCut(s32 player, s32 type) {
    Vec4 d;
    BtlFlagChr *chr;
    s32 nodeA = 3;
    s32 nodeB = 0x11;
    f32 pitch;
    f32 yaw;
    f32 dist;

    chr = BtlChar_Get(player);
    pitch = 0.0f;
    BtlOpp_GetDelta(chr, &d);
    yaw = 0.0f;
    dist = 0.0f;
    Vec3_Normalize(&d, &d);
    switch (type) {
    case 0:
        if (player == CLASH_P0) {
            dist = 2.4f;
        } else {
            dist = -2.4f;
        }
        pitch = 0.0f;
        yaw = BtlUtil_WrapAngle(BtlChar_GetPos(chr)->yaw - dist);
        dist = BtlCharApi_GetHeight(chr->objId);
        dist += EftStruggle_GetCamDist(player, type);
        break;
    case 1:
        if (player == CLASH_P0) {
            dist = 0.5f;
        } else {
            dist = -0.5f;
        }
        pitch = 0.0f;
        yaw = BtlUtil_WrapAngle(BtlChar_GetPos(chr)->yaw - dist);
        dist = BtlCharApi_GetHeight(chr->objId);
        dist += EftStruggle_GetCamDist(player, type);
        break;
    case 2:
        dist = 3.14159265f;
        nodeA = 0x30;
        nodeB = 0x30;
        yaw = BtlUtil_WrapAngle(BtlChar_GetPos(chr)->yaw - dist);
        pitch = Mathf_Asin(d.y) - 0.2f;
        dist = BtlCharApi_GetHeight(chr->objId) * 0.4f;
        dist += EftStruggle_GetCamDist(player, type);
        break;
    }
    ChrCam_SetCut(chr, &D_002EC2A0, &D_002EC2A0, &D_002EC2A0, &D_002EC2A0, &D_002EC2A0, &D_002EC2A0, nodeA, yaw, 0.0f,
                  pitch, 0.0f, dist, 0.0f, nodeA, nodeB, nodeB, 1, 0x45);
}

/* Clash A camera: a one-frame cut around the point between the fighters, swinging with the balance. */
void BtlClash_SetMidCut(Vec4 *mid, f32 bias) {
    Vec4 d;
    Vec4 look;
    Vec4 a;
    Vec4 b;
    BtlFlagChr *chr = BtlChar_Get(CLASH_P0);
    f32 ang = 1.5707963f;
    f32 half;
    f32 base;
    f32 yaw;
    f32 pitch;

    BtlOpp_GetDelta(chr, &d);
    half = 0.5f;
    ang = bias * ang;
    base = 50.0f;
    Vec3_Normalize(&d, &d);
    BtlCharApi_GetNodePos(CLASH_P0, 0x11, &a);
    ang = ang * half;
    BtlCharApi_GetNodePos(CLASH_P1, 0x11, &b);
    Vec4_Lerp(&look, &b, &a, bias * 0.6f + half);
    ang = ang + -1.5707963f;
    yaw = BtlUtil_WrapAngle(atan2f(d.x, d.z) + ang);
    pitch = -Mathf_Asin(d.y);
    pitch *= bias;
    ChrCam_SetCut(chr, mid, &D_002EC2A0, mid, &D_002EC2A0, &look, &D_002EC2A0, -1, yaw, 0.0f, pitch, 0.0f,
                  EftStruggle_GetMidDist() + base, 0.0f, -1, -1, -1, 1, 0x45);
}

#ifdef PORT
/* C chooses paths1..N-1, then picks a DIFFERENT point/path. Admit the new
   free-battle trigger only when all of those authored camera paths are usable. */
s32 Port_VanishPathsReady(void){s32 i,n=BtlStage_GetPathCount();
 if(n<3)return 0;
 for(i=1;i<n;i++){BtlClashPath*p=BtlStage_GetPath(i);if(!p||!p->points||p->first<0||p->end-p->first<2)return 0;}
 return 1;
}
#endif

/* Clash C camera: a one-frame cut placed at the first point of a stage path, looking along its angles. */
void BtlClash_SetPathCut(s32 n) {
    Vec4 pos;
    Vec4 look;
    BtlClashPath *path = BtlStage_GetPath(n);

    if (path != NULL) {
        f32 *p = path->points;

        pos.x = p[0];
        pos.y = p[1];
        pos.z = p[2];
        pos.w = 1.0f;
        look.x = pos.x + sinf(p[5]);
        look.y = pos.y - sinf(p[4]);
        look.z = pos.z + cosf(p[5]);
        look.w = 1.0f;
        ChrCam_SetCut(BtlChar_Get(CLASH_P0), &pos, &D_002EC2A0, &pos, &D_002EC2A0, &look, &D_002EC2A0, -1, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, -1, -1, -1, 1, 0x45);
    }
}

/* Squashes the clash balance into -0.8..0.8: x / (|x| + 1) with x = lead / 20. */
f32 BtlClash_CalcBias(s32 lead) {
    f32 x = lead * 0.05f;

    return BtlUtil_ClampF(x / (__builtin_fabsf(x) + 1.0f), -0.8f, 0.8f);
}

#ifdef PORT
static s32 sPortBeamAudioSlot=-1;
static void port_beam_audio_end(void){extern void Port_StreamBeamLoop(s32,s32);if(sPortBeamAudioSlot>=0)Port_StreamBeamLoop(sPortBeamAudioSlot,0);sPortBeamAudioSlot=-1;}
static void port_beam_audio_begin(s32 owner){
 extern s32 Port_NetActive(void),BattleReplay_IsActive(void);
 extern void Port_StreamBeamLoop(s32,s32);
 port_beam_audio_end();if(Port_NetActive()||BattleReplay_IsActive())return;
 sPortBeamAudioSlot=(u32)owner<2?owner:0;Port_StreamBeamLoop(sPortBeamAudioSlot,1);
}
#endif

/* Clash A, one frame. Returns the next state (0 when it is over). */
#ifdef PORT
extern int Port_Setting(const char*,int),Port_NetActive(void),BattleReplay_IsActive(void),BtlCharApi_IsInputInjected(int);
static int port_clash_scale(int beam){int x;if(Port_NetActive()||BattleReplay_IsActive())return 1;x=Port_Setting(beam?"beam_struggle_length":"dash_struggle_length",0);return x==1?2:x==2?4:1;}
extern int Port_AssistScore(int,int,int);
static int port_clash_count(BtlFlagChr*c,int n,int beam){int x;if(Port_NetActive()||BattleReplay_IsActive())return n;
 if(BtlCharApi_IsInputInjected(c->objId)){x=Port_Setting(beam?"beam_struggle_cpu_power_percent":"dash_struggle_cpu_power_percent",100);if(x<25)x=25;if(x>200)x=200;n=(int)((long long)n*x/100);}
 return Port_AssistScore(c->player,n,beam);
}
#else
#define port_clash_scale(b) 1
#define port_clash_count(c,n,b) (n)
#endif
s32 BtlClash_UpdateA(s32 state) {
    Vec4 a;
    Vec4 b;
    BtlFlagRoster *roster = gBtlChars;
    BtlFlagChr *chr0 = BtlChar_Get(CLASH_P0);
    BtlFlagChr *chr1 = BtlChar_Get(CLASH_P1);
    s32 count0 = port_clash_count(chr0,chr0->clashCountA,1);
    BtlClashA *c = &roster->clashA;
    s32 count1 = port_clash_count(chr1,chr1->clashCountA,1);

    switch (state) {
    case 1:
        c->timer = 0;
        c->lead = 0;
        state++;
#ifdef PORT
        port_beam_audio_begin(chr0->player);
        if(chr0->player>=2){extern void StreamSe_PlayDefault(s32,s32);StreamSe_PlayDefault(0,0x8D30);}
        else
#endif
        BtlCharSnd_PlayStream(chr0, 0x8D30);
        break;
    case 2:
        if (c->timer == 0) {
            BtlChar_PlayVoice(chr0, 0x14);
        }
        if (c->timer == 0x1E) {
            BtlChar_PlayVoice(chr1, 0x14);
        }
        if (c->timer < 0x1E) {
            BtlClash_SetOrbitCut(CLASH_P0, 0);
        } else if (c->timer < 0x3C) {
            BtlClash_SetOrbitCut(CLASH_P1, 0);
        } else {
            if (count1 < count0) {
                c->lead++;
            }
            if (count0 < count1) {
                c->lead--;
            }
            if (c->bias < -0.64f) {
                BtlClash_SetOrbitCut(CLASH_P0, 1);
            } else if (0.64f < c->bias) {
                BtlClash_SetOrbitCut(CLASH_P1, 1);
            } else {
                BtlClash_SetMidCut(&c->mid, c->bias);
            }
        }
        c->timer++;
        if (c->timer >= 0x6A*port_clash_scale(1)) {
            c->timer = 0;
            state++;
        }
        break;
    case 3:
        if (c->timer == 0) {
            if (c->lead == 0) {
                c->lead = BtlChar_FrameMod(2) ? 1 : -1;
            }
            if (c->lead > 0) {
                BtlChar_PlayVoice(chr0, 0x15);
            } else {
                BtlChar_PlayVoice(chr1, 0x15);
            }
        }
        if (c->lead > 0) {
            BtlClash_SetOrbitCut(CLASH_P0, 2);
        } else {
            BtlClash_SetOrbitCut(CLASH_P1, 2);
        }
        c->timer++;
        if (c->timer >= 0x10) {
            c->timer = 0;
            state++;
        }
        break;
    case 4:
        if (c->lead > 0) {
            c->lead += 4;
        } else {
            c->lead -= 4;
        }
        BtlClash_SetMidCut(&c->mid, c->bias);
        if (__builtin_abs(c->lead) >= 0x47) {
            c->timer = 0;
            state++;
        }
        break;
    case 5:
        if (c->timer == 0) {
            BtlFlagChr *winner;
            BtlFlagChr *loser;
            s32 flags = 0;
            s32 cls;
            s32 loserCls;
            s32 damage;

            winner = chr0;
            loser = chr1;
            if (!(c->lead > 0)) {
                if (c->lead < 0) {
                    winner = chr1;
                    loser = chr0;
                } else if (BtlChar_FrameMod(2) == 0) {
                    winner = chr1;
                    loser = chr0;
                }
            }
            cls = BtlAct_GetCurrentClass(winner);
            loserCls = BtlAct_GetCurrentClass(loser);
            BtlChar_SetHeldFlag(winner, 0xC1);
            BtlChar_SetHeldFlag(winner, 0xC3);
            BtlChar_SetHeldFlag(loser, 0xC2);
            if (BtlSuper_IsThrow(winner, cls)) {
                BtlColl_StartThrow(winner, loser, cls, BtlSuper_GetFlags(winner, cls));
#ifdef PORT
                {extern void Port_AssistClashThrow(int,int);Port_AssistClashThrow(winner->player,loser->player);}
#endif
                BtlChar_SetHeldFlag(winner, 0xA0);
                EftStruggle_End(winner->objId);
            } else {
                damage = BtlSuper_GetDamage(winner, cls, 0, 1);
                damage += BtlSuper_GetDamage(loser, loserCls, 0, 1) / 2;
                switch (cls) {
                case 2:
                    flags = 0x800000;
                    break;
                case 3:
                    flags = 0x1000000;
                    break;
                case 4:
                    flags = 0x2000000;
                    break;
                }
#ifdef PORT
                {extern int Port_AssistBeamClashDamage(int,int);damage=Port_AssistBeamClashDamage(winner->player,damage);}
#endif
                BtlMember_Damage(loser, damage, flags);
                BtlChar_PlayVoice(loser, 0x16);
                BtlCharSnd_PlayStream(loser, 0x8D31);
                EftStruggle_End(-1);
            }
            BtlChar_RaiseFirstClash(winner);
            c->winner = winner->player;
#ifdef PORT
            Port_ClashPairLog("struggle decided", CLASH_P0, CLASH_P1, count0, count1, winner->player);
#endif
        }
        if (c->winner == CLASH_P0) {
            BtlClash_SetOrbitCut(CLASH_P1, 0);
        } else {
            BtlClash_SetOrbitCut(CLASH_P0, 0);
        }
        c->lead = 0;
        state = 0;
        break;
    }
    c->bias = BtlClash_CalcBias(c->lead);
    BtlCharApi_GetNodePos(CLASH_P0, 0x11, &a);
    BtlCharApi_GetNodePos(CLASH_P1, 0x11, &b);
    Vec4_Lerp(&c->mid, &b, &a, c->bias * 0.5f + 0.5f);
    return state;
}

/* Clash B, one frame. Returns the next state (0 when it is over). */
s32 BtlClash_UpdateB(s32 state) {
    BtlClashB *c = &gBtlChars->clashB;
    BtlFlagChr *chr0 = BtlChar_Get(CLASH_P0);
    BtlFlagChr *chr1 = BtlChar_Get(CLASH_P1);

    switch (state) {
    case 6:
        c->timer = 0;
        state = 7;
        ChrCam_RequestCut(chr0, 1, 6);
        break;
    case 7:
        if (c->timer == 0) {
            BtlChar_PlayVoice(chr0, 0x18);
        }
        if (c->timer == 0xF) {
            BtlChar_PlayVoice(chr1, 0x18);
        }
        c->timer++;
        if (c->timer >= 0x4C*port_clash_scale(0)) {
            c->timer = 0;
            state = 8;
        }
        if (!BtlClash_BothInActionFA()) {
            state = 0;
            BtlClash_EndActionFA(CLASH_P0);
            BtlClash_EndActionFA(CLASH_P1);
        }
        break;
    case 8:
        if (c->timer == 0) {
            s32 win;
            s32 lose;
            BtlFlagChr *winner;
            BtlFlagChr *loser;

            if (port_clash_count(chr1,chr1->clashCountB,0) < port_clash_count(chr0,chr0->clashCountB,0)) {
                win = 0;
            } else if (port_clash_count(chr0,chr0->clashCountB,0) < port_clash_count(chr1,chr1->clashCountB,0)) {
                win = 1;
            } else {
                win = BtlChar_FrameMod(2) == 0;
            }
            lose = !win;
            winner = BtlChar_Get(CLASH_P(win));
            loser = BtlChar_Get(CLASH_P(lose));
            BtlChar_SetFlag(winner, 0xBF);
            BtlChar_SetFlag(loser, 0xC0);
#ifdef PORT
            {extern void Port_AssistRushOutcome(int,int);Port_AssistRushOutcome(winner->player,loser->player);}
#endif
            BtlChar_PlayVoice(winner, 0x19);
            BtlChar_RaiseFirstClash(winner);
            ChrCam_EndCut(chr0);
            c->winner = win;
#ifdef PORT
            Port_ClashPairLog("blows decided", CLASH_P0, CLASH_P1, chr0->clashCountB, chr1->clashCountB, winner->player);
#endif
        }
        c->timer++;
        if (!BtlClash_BothInActionFA()) {
            state = 0;
        }
        break;
    }
    return state;
}

/* Clash C, one frame: every exchange moves the pair to a random point of a random stage path. */
s32 BtlClash_UpdateC(s32 state) {
    BtlClashC *c = &gBtlChars->clashC;
    BtlFlagChr *chr0 = BtlChar_Get(CLASH_P0);
    BtlFlagChr *chr1 = BtlChar_Get(CLASH_P1);
    BtlClashPath *path;
    BtlFlagChr *winner;
    BtlFlagChr *loser;

    switch (state) {
    case 9:
        c->count = 0;
        c->path = (BtlChar_Rand() >> 2) % (u32)(BtlStage_GetPathCount() - 1) + 1;
        c->hold = (BtlChar_Rand() >> 2) % 10 < 7 ? 2 : 1;
        path = BtlStage_GetPath(c->path);
        if (path != NULL) {
            c->point = (BtlChar_Rand() >> 2) % (u32)(path->end - path->first) + path->first;
        } else {
            c->point = 0;
        }
        c->prevPick = -1;
        c->pick = (BtlChar_Rand() >> 2) & 3;
        if (Battle_GetMode() == 1) {
            c->level = gProgress->unk3C;
        } else {
            c->level = 1;
        }
        state++;
        break;
    case 10:
        if (BtlClash_BothInActionFC()) {
            BtlClash_SetPathCut(c->path);
        }
        if (BtlChar_TestFlag(chr0, 0xC4) && BtlChar_TestFlag(chr1, 0xC4)) {
            if (chr0->unkE58 >= 0) {
                BtlAct_CountAndMarkOpponent(chr0);
            } else {
                BtlAct_CountAndMarkOpponent(chr1);
            }
            c->count++;
            if (c->count >= 7*port_clash_scale(0) && chr0->clashCountB != chr1->clashCountB) {
                if (port_clash_count(chr1,chr1->clashCountB,0) < port_clash_count(chr0,chr0->clashCountB,0)) {
                    winner = chr0;
                    loser = chr1;
                } else {
                    winner = chr1;
                    loser = chr0;
                }
                BtlChar_SetFlag(winner, 0xC7);
                state++;
                BtlChar_SetFlag(loser, 0xC8);
#ifdef PORT
                {extern void Port_AssistRushOutcome(int,int);Port_AssistRushOutcome(winner->player,loser->player);}
#endif
#ifdef PORT
                Port_ClashPairLog("vanish decided", CLASH_P0, CLASH_P1, chr0->clashCountB, chr1->clashCountB, winner->player);
#endif
            } else {
                c->hold--;
                if (c->hold <= 0) {
                    s32 prev = c->path;
                    s32 n = (BtlChar_Rand() >> 2) % (u32)(BtlStage_GetPathCount() - 2);
                    c->path = n + 1;
                    if (c->path >= prev) {
                        c->path = n + 2;
                    }
                    c->hold = (BtlChar_Rand() >> 2) % 10 < 7 ? 2 : 1;
                    c->point = 0;
                    path = BtlStage_GetPath(c->path);
                    if (path != NULL) {
                        c->point = (BtlChar_Rand() >> 2) % (u32)(path->end - path->first) + path->first;
                    }
                } else {
                    path = BtlStage_GetPath(c->path);
                    if (path != NULL) {
                        s32 prev = c->point;
                        c->point = (BtlChar_Rand() >> 2) % (u32)(path->end - path->first - 1) + path->first;
                        if (c->point >= prev) {
                            c->point++;
                        }
                    } else {
                        c->point = 0;
                    }
                }
                c->prevPick = c->pick;
                c->pick = (BtlChar_Rand() >> 2) & 3;
                BtlChar_SetFlag(chr0, 0xC5);
                BtlChar_SetFlag(chr1, 0xC5);
            }
        }
        if (!BtlClash_BothInActionFB() && !BtlClash_BothInActionFC()) {
            state = 0;
            BtlClash_EndActionFC(CLASH_P0);
            BtlClash_EndActionFC(CLASH_P1);
        }
        break;
    case 11:
        if (!BtlClash_BothInActionFC()) {
            state = 0;
        }
        break;
    }
    return state;
}

/* Runs the clash sequences: starts one when both fighters enter a clash action, then steps it. */
void BtlClash_Update(void) {
    s32 *state = &gBtlChars->clashState;
#ifdef PORT
    s32 was = *state;

    if (*state == 0) {
        s32 a, b;
        s32 kind = Port_ForceClash(&a, &b);

        /* (the test's tool: the two are put into a clash as BtlHit_CheckClash does it, 1 the blows, 2 the vanish;
           3: into the struggle, as EftDet_ClashPass does it, when both happen to be in a technique of a class
           that has one. No beams have met then: the sequence runs without the struggle's effect.) */
        if (kind == 3) {
            if ((u32)a < (u32)BtlChar_GetCount() && (u32)b < (u32)BtlChar_GetCount() &&
                BtlAct_IsTechniqueId(BtlAct_GetCurrent(BtlChar_Get(a))) &&
                BtlAct_IsTechniqueId(BtlAct_GetCurrent(BtlChar_Get(b))) &&
                (u32)(BtlAct_GetCurrentClass(BtlChar_Get(a)) - 2) < 3 &&
                (u32)(BtlAct_GetCurrentClass(BtlChar_Get(b)) - 2) < 3 && !BtlCharApi_IsInClashA(a) &&
                !BtlCharApi_IsInClashA(b) && !Port_ClashBusy() && Port_ClashPair(a, b)) {
                BtlChar_SetHeldFlag(BtlChar_Get(a), 0xAA);
                BtlChar_SetHeldFlag(BtlChar_Get(b), 0xAA);
            }
        } else if (kind != 0 && (u32)a < (u32)BtlChar_GetCount() && (u32)b < (u32)BtlChar_GetCount()) {
            /* (only when the two are near each other, as two that touch are: within 30) */
            Vec4 *pa = &BtlChar_GetPos(BtlChar_Get(a))->pos;
            Vec4 *pb = &BtlChar_GetPos(BtlChar_Get(b))->pos;
            f32 dx = pa->x - pb->x, dy = pa->y - pb->y, dz = pa->z - pb->z;

            if (dx * dx + dy * dy + dz * dz < 30.0f * 30.0f && !Port_ClashBusy() && Port_ClashPair(a, b)) {
                BtlChar_SetFlag(BtlChar_Get(a), kind == 2 ? 0x62 : 0x61);
                BtlChar_SetFlag(BtlChar_Get(b), kind == 2 ? 0x62 : 0x61);
                Port_ForceClashDone();
            }
        }
        Port_ClashFind();
    }
#endif

    if (*state == 0) {
        if (BtlClash_BothInClashA()) {
            *state = 1;
        }
        if (BtlClash_BothInActionFA()) {
            *state = 6;
        }
        if (BtlClash_BothInActionFB()) {
            *state = 9;
        }
    }
    switch (*state) {
    case 0:
#ifdef PORT
        /* (every fighter: one left alone in a clash action is let out of it, whatever its number. The third
           fighter stayed in action 0xFA for ever. The exchange of clash C is only ended here with more than two
           fighters: the game itself does not.) */
        {
            s32 i;

            for (i = 0; i < BtlChar_GetCount(); i++) {
                BtlClash_HoldClashA(i);
            }
            for (i = 0; i < BtlChar_GetCount(); i++) {
                BtlClash_EndActionFA(i);
                if (BtlChar_GetCount() > 2) {
                    BtlClash_EndActionFC(i);
                }
            }
        }
#else
        BtlClash_HoldClashA(0);
        BtlClash_HoldClashA(1);
        BtlClash_EndActionFA(0);
        BtlClash_EndActionFA(1);
#endif
        break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        *state = BtlClash_UpdateA(*state);
        if (!BtlClash_BothInClashA()) {
            BtlClash_HoldClashA(CLASH_P0);
            BtlClash_HoldClashA(CLASH_P1);
            *state = 0;
        }
        break;
    case 6:
    case 7:
    case 8:
        *state = BtlClash_UpdateB(*state);
        break;
    case 9:
    case 10:
    case 11:
        *state = BtlClash_UpdateC(*state);
        break;
    }
#ifdef PORT
    if(sPortBeamAudioSlot>=0&&(*state<1||*state>4))port_beam_audio_end();
    if (BtlChar_GetCount() > 2 && *state != 0) {
        /* (a fighter in a clash action that is not of the running clash: let out, as at step 0) */
        s32 i;

        for (i = 0; i < BtlChar_GetCount(); i++) {
            if (i != CLASH_P0 && i != CLASH_P1) {
                BtlClash_HoldClashA(i);
                BtlClash_EndActionFA(i);
                BtlClash_EndActionFC(i);
            }
        }
    }
    if (was == 0 && *state != 0) {
        Port_ClashPairLog(*state < 6 ? "struggle starts" : *state < 9 ? "blows start" : "vanish starts", CLASH_P0, CLASH_P1,
                          0, 0, -1);
    }
    if (was != 0 && *state == 0) {
        Port_ClashPairLog(was < 6 ? "struggle over" : was < 9 ? "blows over" : "vanish over", CLASH_P0, CLASH_P1, 0, 0, -1);
    }
#endif
}
