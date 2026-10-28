#ifdef PORT
#include "coop_selection.h"
#endif
#ifdef PORT
#include "tag_tournament_port.h"
#include "sys/pad.h"
#endif
#include "common.h"
#include "battle/btl_char_flag.h"

/*
 * Opponent helpers: 0x1DB048..0x1DBF20. Each takes a fighter and answers about the other one
 * (fighter 1 for player 0, fighter 0 otherwise).
 */

extern f32 atan2f(f32 y, f32 x);
extern f32 Mathf_Asin(f32 x);
extern void Vec4_Copy(Vec4 *dst, Vec4 *src);
extern void Vec4_Sub(Vec4 *dst, Vec4 *a, Vec4 *b);
extern void Vec4_Scale(Vec4 *dst, Vec4 *src, f32 scale);
extern f32 Vec3_Dot(Vec4 *a, Vec4 *b);
extern f32 Vec3_Length(Vec4 *v);
extern f32 BtlUtil_WrapAngle(f32 a);
extern f32 BtlUtil_LengthXZ(Vec4 *v);
extern f32 BtlUtil_MaxF(f32 a, f32 b);
extern BtlFlagChr *BtlChar_Get(s32 i);
extern BtlFlagPose *BtlChar_GetPos(BtlFlagChr *chr);
extern void *BtlChar_GetObj(BtlFlagChr *chr);
extern s32 BtlChar_IsDead(BtlFlagChr *chr);
extern void *BtlObj_Get(s32 id);
extern void ChrCam_EndCut(BtlFlagChr *chr);
extern void BtlChar_GetSnapPos(BtlFlagChr *chr, Vec4 *out, s32 slot); /* override position `slot` when its bit of +0x3C0 is set, else the position */
extern void BtlChar_GetSnapRot(BtlFlagChr *chr, Vec4 *out, s32 slot); /* the same for the rotation */
extern s32 BtlMember_HasAbility(BtlFlagChr *chr, s32 arg);
extern s32 BtlAct_GetCurrent(BtlFlagChr *chr);                        /* action id */
extern s32 BtlAct_IsTechniqueId(s32 action);                             /* action is 0x105..0x132 */
extern f32 BtlCharApi_GetHeight(s32 objId);                              /* object +0xFF4: height (10 without object) */
extern f32 BtlCharApi_GetBodyUnkFF8(s32 objId);                              /* object +0xFF8 (10 without object) */
extern f32 BtlCharApi_GetCenterHeight(s32 objId);                              /* object +0xFFC (5 without object) */
extern void BtlCharApi_GetNodePos(s32 objId, s32 node, Vec4 *out);        /* world position of a model node */
extern f32 BtlCharApi_GetRadius(s32 objId);                              /* body radius */
extern s32 BtlCharApi_IsInSkill(s32 objId);                              /* action is 0xFD..0x102 */
extern s32 BtlParam_GetUnk0(BtlFlagChr *chr);                        /* u16 at +0 of the object's parameter block */
extern s32 BtlParam_GetUnk2(BtlFlagChr *chr);                        /* s8 at +2 of the object's parameter block */
extern f32 BtlParam_GetUnkC7Scale(BtlFlagChr *chr);

#ifdef PORT
/* Teams (the third-fighter experiment, and what a team battle or a free-for-all would stand on).
 *
 * The game has two fighters and "the opponent" is the other one (230 BtlOpp_* calls, the AI's 48 "other
 * fighter"s, the shots' and the punches' victim). Here every fighter has a TEAM, an enemy is a fighter of another
 * team, and each fighter has a TARGET: the enemy everything it does "towards the opponent" is about.
 *
 *   Port_Team(i)          the team of roster index i: 0 for player 1, 1 for player 2, and an extra fighter's is
 *                         what the fight's set-up gave it (port/src/plat_fighters.c): 1 (with player 2), 0 (with
 *                         player 1: an ally) or a further team, 2 on (alone, or with other extra fighters)
 *   Port_IsEnemy(a, b)    fighters of different teams
 *   gPortTarget[i]        the fighter's target, chosen once a frame (Port_UpdateTarget, from BtlChars_UpdateMain):
 *                         the nearest enemy that still stands, kept until another is clearly nearer (three
 *                         quarters of the distance) so that it does not flip from frame to frame
 *   Port_TargetHit(a, v)  a hit: v is a's target from here on, held for two seconds
 *
 * There is no friendly fire: a shot or a punch is only tried on enemies. With two fighters all of this gives
 * the game's own answer (each one's target is the other). */
extern s32 BtlChar_GetCount(void);
extern int Port_FightTeam(int k); /* port/src/plat_fighters.c: extra fighter k's, decided when the fight loads */
#define PORT_FIGHTERS 10 /* (PORT_FIGHTER_MAX, port/include/port_fighters.h) */
s32 gPortTarget[PORT_FIGHTERS] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static s32 sPortTargetHold[PORT_FIGHTERS];
extern s32 Port_ClashPartner(s32 i); /* btl_char_flag_clash.c: the other fighter of the clash i is in, or -1 */
s32 Port_Team(s32 i) {
    return i < 2 ? i : Port_FightTeam(i - 2);
}
s32 Port_IsEnemy(s32 a, s32 b) {
    return a != b && Port_Team(a) != Port_Team(b);
}
/* While a fighter is busy with one of its enemies the target stays: a rush or a throw is a sequence between the
   attacker and the fighter it hit, and the effects of it ask "the opponent" all the way through (a rush on the
   third fighter stopped the game in EftShotTech_UpdateSubs when the target changed under it). */
/* 1 while fighter i is one of the two of a rush technique's scripted sequence (the attacker's actions 0x12D..0x12F,
   the victim's 0x133 up) and has its partner as its target. The two are placed against each other on every step
   ("the opponent"), and nothing from outside may come between: a third fighter's shot that only touched the
   victim made that fighter the victim's opponent, the victim was placed against somebody who was not in the
   sequence, far off the map, and its hair's swing angle grew past what the angle wrap can bring back (the game
   stood still in Mathf_WrapAngle: seen with ten fighters). */
extern s32 Port_SequencePartner(s32 i); /* btl_act_f.c: from the sequence's own record of the two */
static s32 Port_InSequence(s32 i) {
    s32 action = BtlAct_GetCurrent(BtlChar_Get(i));
    s32 cur = gPortTarget[i];

    if (Port_SequencePartner(i) >= 0) {
        return 1;
    }
    if (!((action >= 0x12D && action <= 0x12F) || action >= 0x133)) {
        return 0;
    }
    return (u32)cur < (u32)BtlChar_GetCount() && cur != i;
}
void Port_TargetHit(s32 attacker, s32 victim) {
    /* (Both must be fighters: a shot's owner can be another object, a partner's, with an id past the roster.
       Player 2's target became 3 through here and its AI read a controller that is not there: a crash in
       BtlAiSense_IsBehindOpponent, seen by the user.) */
    if ((u32)attacker < (u32)BtlChar_GetCount() && (u32)victim < (u32)BtlChar_GetCount() &&
        Port_IsEnemy(attacker, victim)) {
        /* (Two in a clash keep each other: a hit from outside the pair does not turn either away.) */
        if ((Port_ClashPartner(attacker) >= 0 && Port_ClashPartner(attacker) != victim) ||
            (Port_ClashPartner(victim) >= 0 && Port_ClashPartner(victim) != attacker)) {
            return;
        }
        if (!Port_InSequence(attacker)) {
            gPortTarget[attacker] = victim;
            sPortTargetHold[attacker] = 120;
        }
        if (Port_InSequence(victim)) {
            return;
        }
        /* The victim's too: what a hit does to it is worked out against "its opponent" (which way it is thrown,
           the pair of a rush or an ultimate), and that has to be the one who hit it. With an ally in the fight an
           ultimate of player 1's on player 2 never ended: player 2's opponent was the ally. */
        gPortTarget[victim] = attacker;
        sPortTargetHold[victim] = 60;
    }
}
/* A fight is being loaded: nobody has a target from the fight before. (They are asked before the first frame
   chooses them, by the HUD for one: after a fight of three, a fight of two read player 1's target, the third
   fighter, which is not there: a crash in BtlSide_IsComboHitNew.) */
/* The player's own choice (asked for by the user: "targeting with flicking R3 to switch between them and have an
   indicator on the highlighted target"). A flick of the right stick to the right or the left makes the next enemy
   on the screen that way the player's target (past the last, round to the far side), and it stays the target
   until it is down; the marker over its head is drawn by hud_b.c. What the fight needs still comes first: a rush,
   a throw, a clash or a technique keeps the target it is about until it is over, and a hit from somebody else
   turns the player to them for the second the game needs; then it is the chosen one again. Without a choice,
   the nearest enemy, as before. The right stick's other use in a fight, moving the camera to one side while it
   is held, is not there with more than two fighters (btl_char_cam_modes.c). A player is fighter 0 or 1 on a side
   that is not computer-controlled (BT3_TARGET_FLICK, testing: fighter 0 too, flicking by itself). */
static s32 sPortLock[PORT_FIGHTERS];  /* 1 + the chosen enemy, or 0 */
static s32 sPortLockAuto[PORT_FIGHTERS]; /* the choice was the game's (the nearest, at the start or when the one
                                            before went down), not a flick */
static s32 sPortLockNew[PORT_FIGHTERS]; /* chosen and not the target yet: no hold keeps the old one */
static s32 sPortWatch[PORT_FIGHTERS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }; /* spectating (below): the
                                         fighter the player of fighter p watches, or -1 */
static s32 sPortWatchWait[PORT_FIGHTERS]; /* frames since player p's fighter went down */
extern s32 BattleSide_IsCpu(s32 side);                  /* battle_load.c */
extern s32 BtlInput_IsPressed(BtlFlagChr *chr, u32 mask); /* btl_input.c */
extern s32 Port_ProjectPoint(Vec4 *pos, s32 *x, s32 *y); /* btl_cam.c */
extern int Port_TargetTestFlick(int i);                 /* port/src/plat_stub.c: BT3_TARGET_FLICK */
extern void Port_TargetLog(int i, int target, int x, int locked);
static int sControlOwner[PORT_FIGHTERS],sControlTarget[PORT_FIGHTERS],sControlNotice[PORT_FIGHTERS];
void Port_TakeoverReset(void){int i;for(i=0;i<PORT_FIGHTERS;i++)sControlOwner[i]=sControlTarget[i]=sControlNotice[i]=0;}
void Port_TakeoverApply(void*actor){int i=*(int*)actor;if((u32)i>=PORT_FIGHTERS)return;
 if(sControlOwner[i]){*(int*)((u8*)actor+0x1278)=sControlOwner[i]==1;if(sControlOwner[i]>=2)*(int*)((u8*)actor+4)=sControlOwner[i]-2;}
}
s32 Port_TargetHuman(s32 i) {
    if((u32)i<PORT_FIGHTERS&&sControlOwner[i])return sControlOwner[i]>=2;
    extern int Port_FightPad(int k); /* port/src/plat_fighters.c: an extra fighter's controller, -1 the computer */
    if (i == 0 || i == 1) {
        return !BattleSide_IsCpu(i) || (i == 0 && Port_TargetTestFlick(-1));
    }
    return i >= 2 && i < BtlChar_GetCount() && Port_FightPad(i - 2) >= 0; /* (players 3 and 4, and a teammate) */
}
/* Input bridge: controller number is not necessarily a fighter's roster/object id. */
s32 Port_ModClashActionForPad(s32 pad) {
    extern BtlFlagRoster *gBtlChars;
    s32 i;
    if (gBtlChars == NULL || (u32)pad >= 4) return -1;
    for (i = 0; i < BtlChar_GetCount(); i++) {
        BtlFlagChr *chr = BtlChar_Get(i);
        if(chr!=NULL&&Port_TargetHuman(i)){
            extern int Port_FusionControlPads(int,int*,int*);int move,attack;
            if(Port_FusionControlPads(i,&move,&attack)){if(attack==pad)return BtlAct_GetCurrent(chr);}
            else if(chr->pad==pad)return BtlAct_GetCurrent(chr);
        }
    }
    return -1;
}

s32 Port_TargetLocked(s32 i) {
    return (u32)i < PORT_FIGHTERS && sPortLock[i] != 0;
}
static s32 Port_LockValid(s32 i) {
    s32 l = sPortLock[i] - 1;

    return l >= 0 && l < BtlChar_GetCount() && Port_IsEnemy(i, l) && !BtlChar_IsDead(BtlChar_Get(l));
}
/* The next enemy on the screen from the target, to the right (dir 1) or the left (-1); -1 when there is none. */
static s32 Port_NextOnScreen(s32 i, s32 dir) {
    s32 count = BtlChar_GetCount(), k, best = -1, wrap = -1, cx = 0x8000, cy, x, y;
    s32 cur = Port_LockValid(i) ? sPortLock[i] - 1 : gPortTarget[i]; /* (from the one the player sees) */
    s32 bestD = 0, wrapD = 0, d;
    Vec4 pos;

    if ((u32)cur < (u32)count) {
        BtlChar_GetSnapPos(BtlChar_Get(cur), &pos, 6);
        if (!Port_ProjectPoint(&pos, &cx, &cy)) {
            cx = 0x8000;
        }
    }
    for (k = 0; k < count; k++) {
        if (k == cur || !Port_IsEnemy(i, k) || BtlChar_IsDead(BtlChar_Get(k))) {
            continue;
        }
        BtlChar_GetSnapPos(BtlChar_Get(k), &pos, 6);
        if (!Port_ProjectPoint(&pos, &x, &y)) {
            x = cx + dir * 0x100000; /* (behind the camera: after everything on the screen that way) */
        }
        d = (x - cx) * dir;
        if (d >= 0 && (best < 0 || d < bestD)) {
            best = k;
            bestD = d;
        }
        if (wrap < 0 || d < wrapD) {
            wrap = k;
            wrapD = d;
        }
    }
    return best >= 0 ? best : wrap;
}
static void Port_TargetChoose(s32 i) {
    BtlFlagChr *me = BtlChar_Get(i);
    s32 dir, n, x = 0, y;
    Vec4 pos;

    if (!Port_TargetHuman(i)) {
        return;
    }
    dir = BtlInput_IsPressed(me, 0x400) ? 1 : BtlInput_IsPressed(me, 0x100) ? -1 : Port_TargetTestFlick(i);
    if (dir != 0) {
        n = Port_NextOnScreen(i, dir);
        if (n >= 0) {
            sPortLock[i] = n + 1;
            sPortLockNew[i] = 1;
            sPortLockAuto[i] = 0;
            BtlChar_GetSnapPos(BtlChar_Get(n), &pos, 6);
            Port_ProjectPoint(&pos, &x, &y);
            Port_TargetLog(i, n, x, 1);
        }
    }
    if (sPortLock[i] != 0 && !Port_LockValid(i)) {
        Port_TargetLog(i, sPortLock[i] - 1, 0, 0);
        sPortLock[i] = 0;
        sPortLockNew[i] = 0;
    }
    /* (no choice: the nearest enemy that stands becomes it, and stays it; the target does not move to whoever
       is nearer later on: the user asked for no switching of the camera by itself) */
    if (sPortLock[i] == 0) {
        s32 count = BtlChar_GetCount(), k, best = -1;
        f32 bestDist = 0.0f, dist;
        Vec4 d;

        for (k = 0; k < count; k++) {
            if (!Port_IsEnemy(i, k) || BtlChar_IsDead(BtlChar_Get(k))) {
                continue;
            }
            Vec4_Sub(&d, &BtlChar_GetPos(BtlChar_Get(k))->pos, &BtlChar_GetPos(me)->pos);
            dist = Vec3_Length(&d);
            if (best < 0 || dist < bestDist) {
                best = k;
                bestDist = dist;
            }
        }
        if (best >= 0) {
            sPortLock[i] = best + 1;
            sPortLockNew[i] = 1;
            sPortLockAuto[i] = 1;
        }
    }
}
/* The enemy the player's camera looks at: the chosen one, except while a rush, a throw, a clash or a technique
   of the player's is on, which the camera has to show between the two of it. A hit from somebody else turns the
   player to them for what the hit does (Port_TargetHit), not the camera: it stays on the chosen enemy. Anyone
   else, and two fighters: the target. */
static s32 Port_InSequence(s32 i);
s32 Port_CamTarget(s32 i) {
    if (BtlChar_GetCount() < 3 || !Port_TargetHuman(i) || !Port_LockValid(i) || Port_InSequence(i) ||
        Port_ClashPartner(i) >= 0 || BtlAct_IsTechniqueId(BtlAct_GetCurrent(BtlChar_Get(i)))) {
        return gPortTarget[i];
    }
    return sPortLock[i] - 1;
}
/* Set while a fighter's own camera is worked out (btl_char_mgr.c): "the opponent" is then Port_CamTarget's. */
s32 gPortCamOf = -1;
/* (BT3_CAM_LOG: each change of what player i's camera looks at, and of the game's target, with the action) */
static void Port_CamTargetLog(s32 i) {
    static s32 lastCam[2] = { -1, -1 }, lastGame[2] = { -1, -1 };
    extern void Port_CamFollowLog(int i, int cam, int game, int action);

    if (i < 2 && Port_TargetHuman(i) && (Port_CamTarget(i) != lastCam[i] || gPortTarget[i] != lastGame[i])) {
        lastCam[i] = Port_CamTarget(i);
        lastGame[i] = gPortTarget[i];
        Port_CamFollowLog(i, lastCam[i], lastGame[i], BtlAct_GetCurrent(BtlChar_Get(i)));
    }
}
/* The marker's two points for player p: its target's head (model node 0x30, which the camera keeps behind) and
   its feet; 0 when the marker is not drawn (two fighters, no player, a target that is down). The third answer: 1
   when chosen. */
extern void BtlCharApi_GetNodePos(s32 objId, s32 node, Vec4 *out); /* btl_char_api.c */
s32 Port_TargetMark(s32 p, Vec4 *head, Vec4 *feet, s32 *locked) {
    s32 t = Port_CamTarget(p);

    if (BtlChar_GetCount() < 3 || !Port_TargetHuman(p) || (u32)t >= (u32)BtlChar_GetCount() ||
        !Port_IsEnemy(p, t) || BtlChar_IsDead(BtlChar_Get(t))) {
        return 0;
    }
    BtlCharApi_GetNodePos(BtlChar_Get(t)->objId, 0x30, head);
    *feet = BtlChar_GetPos(BtlChar_Get(t))->pos;
    *locked = sPortLock[p] == t + 1 && !sPortLockAuto[p];
    return 1;
}

void Port_TargetReset(void) {
    s32 i;

    for (i = 0; i < PORT_FIGHTERS; i++) {
        gPortTarget[i] = i == 0 ? 1 : 0;
        sPortWatch[i] = -1;
        sPortWatchWait[i] = 0;
        sPortTargetHold[i] = 0;
        sPortLock[i] = 0;
        sPortLockNew[i] = 0;
        sPortLockAuto[i] = 0;
    }
    gPortCamOf = -1;
}
/* Spectating (asked for by the user: "when I die I should be able to spectate a character and with the R3 I can
   swap between them"). With more than two fighters a player whose fighter is down while the fight goes on (its team
   still has fighters) watches another one: a second after the knock-out, the first of its team that stands; R3
   moves to the next that stands, the team first and then the enemies, round to the first; when the one watched
   goes down, the next. The player's view then shows that fighter's own camera (btl_cam.c, BtlCam_UpdateView),
   which follows it and looks at its target, and what is about that fighter counts as about a player for the
   cut-ins (btl_char_api.c). Nothing of the fight changes: who is watched only chooses a picture. */
extern s32 Pad_IsPressed(s32 pad, u32 mask); /* sys/pad.c: the game buttons pressed this frame */
extern s32 BattleSide_GetPad(s32 side); /* battle_load.c */
/* Fighter k as a spectator of player p would have it: on p's team before the enemies, standing, not p itself. */
static s32 Port_WatchOk(s32 p, s32 k) {
    return k != p && (u32)k < (u32)BtlChar_GetCount() && !BtlChar_IsDead(BtlChar_Get(k));
}
/* The next one to watch after `cur` (-1: from the start): p's team in roster order, then everyone else. */
static s32 Port_WatchNext(s32 p, s32 cur) {
    s32 count = BtlChar_GetCount(), order[PORT_FIGHTERS], n = 0, k, at = -1;

    for (k = 0; k < count && n < PORT_FIGHTERS; k++) {
        if (k != p && !Port_IsEnemy(p, k)) {
            order[n++] = k;
        }
    }
    for (k = 0; k < count && n < PORT_FIGHTERS; k++) {
        if (k != p && Port_IsEnemy(p, k)) {
            order[n++] = k;
        }
    }
    for (k = 0; k < n; k++) {
        if (order[k] == cur) {
            at = k;
        }
    }
    for (k = 1; k <= n; k++) {
        s32 c = order[(at + k + n) % n];
        if (Port_WatchOk(p, c)) {
            return c;
        }
    }
    return -1;
}
extern void Port_SpectateLog(int p, int fighter, int why); /* port/src/plat_stub.c: BT3_SPECTATE_LOG */
extern int Port_NetActive(void),BattleReplay_IsActive(void),BtlSeq_GetState(void),Port_Setting(const char*,int);
extern Pad *Port_Pad(s32);
static int Port_TakeoverCandidate(int p){int t;BtlFlagChr*c;
 if(Port_NetActive()||BattleReplay_IsActive()||BtlSeq_GetState()!=3||!Port_Setting("spectator_takeover_enabled",1)||(u32)p>=PORT_FIGHTERS||!Port_TargetHuman(p)||!BtlChar_IsDead(BtlChar_Get(p)))return -1;
 t=sPortWatch[p];if(t<0||t>=BtlChar_GetCount()||Port_TargetHuman(t)||Port_IsEnemy(p,t)||BtlChar_IsDead(BtlChar_Get(t)))return -1;
 c=BtlChar_Get(t);{extern int BtlChar_IsFrozen(void*),ChrCam_IsCutActive(void*);if(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))return -1;}
 return t;
}
int Port_TakeoverHud(int who){int p;if(Port_NetActive()||BattleReplay_IsActive())return 0;
 if((u32)who<PORT_FIGHTERS&&sControlNotice[who]>0&&Port_Setting("show_takeover_confirmation",1))return 2;
 if(!Port_Setting("show_takeover_hints",1))return 0;
 for(p=0;p<BtlChar_GetCount()&&p<PORT_FIGHTERS;p++)if(sPortWatch[p]==who&&Port_TakeoverCandidate(p)==who)return 1;
 return 0;
}
static void Port_UpdateSpectate(void) {
    extern u32 Port_PadPressed(s32 pad); /* sys/pad.c: the pad's own buttons pressed this frame, any of the four */
    s32 p;

    for (p = 0; p < BtlChar_GetCount() && p < PORT_FIGHTERS; p++) {
        s32 was = sPortWatch[p];
        if(sControlNotice[p]>0)sControlNotice[p]--;
        if(!Port_Setting("spectate_fallen_fighters",1)){sPortWatch[p]=-1;sPortWatchWait[p]=0;continue;}
        if (BtlChar_GetCount() < 3 || !Port_TargetHuman(p) || !BtlChar_IsDead(BtlChar_Get(p))) {
            sPortWatch[p] = -1;
            sPortWatchWait[p] = 0;
            continue;
        }
        if (sPortWatchWait[p] < 60) { /* (a second on the fighter that went down first) */
            sPortWatchWait[p]++;
            continue;
        }
        if (sPortWatch[p] < 0 || !Port_WatchOk(p, sPortWatch[p])) {
            sPortWatch[p] = Port_WatchNext(p, sPortWatch[p]);
            if (sPortWatch[p] != was) {
                Port_SpectateLog(p, sPortWatch[p], was < 0 ? 0 : 1);
            }
        } else {
            s32 pad = BtlChar_Get(p)->pad;
            int t=Port_TakeoverCandidate(p);
            if(t>=0&&(u32)pad<4&&(Port_Pad(pad)->held&PAD_L2)&&(Port_PadPressed(pad)&PAD_SQUARE)){
             int seconds=Port_Setting("takeover_confirmation_seconds",3);if(seconds<1)seconds=1;if(seconds>10)seconds=10;
             sControlOwner[p]=1;sControlOwner[t]=pad+2;sControlTarget[p]=t+1;sControlNotice[t]=seconds*30;
             Port_TakeoverApply(BtlChar_Get(p));Port_TakeoverApply(BtlChar_Get(t));
             {extern void BtlInput_Init(void*),BtlAct_ClearQueue(void*);BtlInput_Init(BtlChar_Get(t));BtlAct_ClearQueue(BtlChar_Get(t));}
             Port_SpectateLog(p,t,3);continue;
            }
            if ((u32)pad < 4 && (Port_PadPressed(pad) & 0x0004)) { /* (R3 in the pad's own button bits: sys/pad.h, gs_input.c B_R3) */
                sPortWatch[p] = Port_WatchNext(p, sPortWatch[p]);
                Port_SpectateLog(p, sPortWatch[p], 2);
            }
        }
    }
}
/* The fighter whose camera player p's view shows: p, or the one it watches. */
s32 Port_Spectated(s32 p) {
    {extern int Port_FusionViewOwner(int);p=Port_FusionViewOwner(p);}

    extern int Port_AssistView(int);s32 subject=p,assist;
    if((u32)p<PORT_FIGHTERS&&sControlTarget[p]>0&&sControlTarget[p]<=BtlChar_GetCount())subject=sControlTarget[p]-1;
    else if((u32)p<PORT_FIGHTERS&&sPortWatch[p]>=0&&sPortWatch[p]<BtlChar_GetCount())subject=sPortWatch[p];
    assist=Port_AssistView(subject);return assist>=0?assist:subject;
}
/* The fighter the gauge of HUD side `side` (0 left, 1 right) shows (hud_a.c, hud_b.c; asked for by the user: "the
   person I'm focusing on has the main opponent HUD, and the old main swaps to one of their teammates'"). With
   more than two fighters the player's side shows the player's fighter, or the one it watches once it is down,
   and the other side that fighter's target: the enemy the player chose (Port_CamTarget), or the watched
   fighter's own. Everybody else is in the small rows (hud_b.c, HudGauge_DrawExtras). Two players: each side its
   own player (or the one it watches). With two fighters: the side. Only the HUD asks: the values the computer
   reads through the same functions are asked for by side, as always. */
s32 Port_HudFighter(s32 side) {
    {extern int Port_FusionViews(int);int n=Port_FusionViews(-1);if(n>0){int who=Port_FusionViews(side);return n==1&&side==1?Port_CamTarget(who):who;}}
    s32 me, w, t;

    if(((Port_TagTourActive()&&Port_TagTourViews()==2)||Port_CoopPlayerViews()==2)){extern s32 Port_QuarterFighter(s32 q);return Port_Spectated(Port_QuarterFighter(side));}
    if (BtlChar_GetCount() < 3 || (side != 0 && side != 1)) {
        return side;
    }
    {
        /* (four quarters: the two gauges are the top quarters' fighters; the bottom ones', hud_b.c) */
        extern s32 Port_QuadMode(void);
        if (Port_QuadMode()) {
            return Port_Spectated(Port_QuarterFighter(side));
        }
    }
    if (Port_TargetHuman(0) && Port_TargetHuman(1)) {
        return Port_Spectated(side);
    }
    me = !Port_TargetHuman(0) && Port_TargetHuman(1) ? 1 : 0; /* (a demo: fighter 0, as the camera) */
    w = Port_Spectated(me);
    if (side == me) {
        return w;
    }
    t = w == me ? Port_CamTarget(me) : gPortTarget[w];
    return (u32)t < (u32)BtlChar_GetCount() && t != w ? t : side;
}
/* Four local players (asked for by the user: "up to four local players", each with a quarter of the screen). The
   picture is split in four when a split-screen fight (the game's two-player one) has extra fighters played from
   controllers. Quarter q (0 top left, 1 top right, 2 bottom left, 3 bottom right) is controller q+1's: the fighter
   played on it, else (a quarter nobody plays: three players, or a test) fighter 0 / 1 for the top ones and the
   first fighter no quarter shows for the bottom ones. */
s32 Port_QuadMode(void) {
    {extern int Port_FusionViews(int);int n=Port_FusionViews(-1);if(n>0)return n>2;}
    extern s32 Battle_IsSplitScreen(void);
    extern int Port_FightPad(int k);
    s32 k;

    if (BtlChar_GetCount() < 3 || !Battle_IsSplitScreen() || Port_CoopPlayerViews()==2 || (Port_TagTourActive()&&Port_TagTourViews()<3)) {
        return 0;
    }
    for (k = 2; k < BtlChar_GetCount(); k++) {
        if (Port_FightPad(k - 2) >= 0) {
            return 1;
        }
    }
    return 0;
}
/* Three local players (asked for by the user: "two top, one wide bottom"): the quarters with nobody on controller
   4; player 3 has the whole bottom half (btl_cam.c, layout 4). */
s32 Port_QuadThree(void) {
    {extern int Port_FusionViews(int);int n=Port_FusionViews(-1);if(n>0)return n==3;}
    s32 k;

    if (!Port_QuadMode()) {
        return 0;
    }
    for (k = 0; k < BtlChar_GetCount(); k++) {
        if (Port_TargetHuman(k) && BtlChar_Get(k)->pad == 3) {
            return 0;
        }
    }
    return 1;
}
s32 Port_QuarterFighter(s32 q) {
    {extern int Port_FusionViews(int);if(Port_FusionViews(-1)>0)return Port_FusionViews(q);}
    s32 slot[4] = { -1, -1, -1, -1 }, k, n;

    for (k = 0; k < BtlChar_GetCount(); k++) {
        s32 pad = BtlChar_Get(k)->pad;
        if (Port_TargetHuman(k) && (u32)pad < 4 && slot[pad] < 0) {
            slot[pad] = k;
        }
    }
    for (n = 0; n < 4; n++) {
        if (slot[n] >= 0) {
            continue;
        }
        if (n < 2 && slot[0] != n && slot[1] != n && slot[2] != n && slot[3] != n) {
            slot[n] = n;
            continue;
        }
        for (k = 0; k < BtlChar_GetCount() && slot[n] < 0; k++) {
            if (slot[0] != k && slot[1] != k && slot[2] != k && slot[3] != k) {
                slot[n] = k;
            }
        }
    }
    return (u32)q < 4 && slot[q] >= 0 ? slot[q] : 0;
}

/* 1 when fighter i is watched by a player whose fighter is down. */
s32 Port_SpectateOf(s32 i) {
    s32 p;

    for (p = 0; i >= 0 && p < BtlChar_GetCount() && p < PORT_FIGHTERS; p++) {
        if (sPortWatch[p] == i) {
            return 1;
        }
    }
    return 0;
}

void Port_UpdateTarget(void) {
    s32 count = BtlChar_GetCount();
    s32 i, k;

    Port_UpdateSpectate();
    if (count < 3) {
        gPortTarget[0] = 1;
        gPortTarget[1] = 0;
        return;
    }
    for (i = 0; i < count && i < PORT_FIGHTERS; i++) {
        BtlFlagChr *me = BtlChar_Get(i);
        s32 cur = gPortTarget[i];
        s32 best = -1;
        f32 bestDist = 0.0f, curDist = -1.0f;
        Vec4 d;

        if (cur >= count || !Port_IsEnemy(i, cur) || (cur >= 0 && BtlChar_IsDead(BtlChar_Get(cur)))) {
            cur = -1;
        }
        Port_TargetChoose(i);
        /* The two of a rush technique's sequence are each other's target, whatever happened before it began
           (the hit that starts one does not always pass Port_TargetHit). */
        if (Port_SequencePartner(i) >= 0) {
            gPortTarget[i] = Port_SequencePartner(i);
            sPortTargetHold[i] = 120;
            continue;
        }
        /* A clash is between two: each is the other's target for as long as it lasts, and two seconds on (the
           loser's damage is "the opponent's count", asked after the sequence is over). */
        if (Port_ClashPartner(i) >= 0) {
            gPortTarget[i] = Port_ClashPartner(i);
            sPortTargetHold[i] = 120;
            continue;
        }
        /* A technique (a beam, a rush, an ultimate: actions 0x105..0x132) is between the fighter and its victim
           for as long as it lasts, which can be far longer than the two seconds a hit holds the target: a Spirit
           Bomb on the third fighter went back to fighter 2 half way (seen by the user; reproduced from a
           recording). The target stays for the whole of it, and two seconds on. */
        /* And a victim's reaction is ABOUT its attacker ("the opponent" all through it): a fighter in a sequence
           as the victim (actions from 0x133 up) keeps the target the hit gave it. */
        if (cur >= 0 && (BtlAct_IsTechniqueId(BtlAct_GetCurrent(me)) ||
                         BtlAct_GetCurrent(me) >= 0x133)) {
            sPortTargetHold[i] = 120;
            continue;
        }
        if (Port_LockValid(i)) {
            gPortTarget[i] = sPortLock[i] - 1;
            sPortTargetHold[i] = 0;
            sPortLockNew[i] = 0;
            continue;
        }
        /* A free human aims at the same selected enemy as their camera. Hit hold
           remains useful for CPUs/unlocked fighters, but cannot override that choice. */
        if (cur >= 0 && sPortTargetHold[i] > 0) {
            sPortTargetHold[i]--;
            continue;
        }
        for (k = 0; k < count; k++) {
            f32 dist;

            if (!Port_IsEnemy(i, k) || BtlChar_IsDead(BtlChar_Get(k))) {
                continue;
            }
            Vec4_Sub(&d, &BtlChar_GetPos(BtlChar_Get(k))->pos, &BtlChar_GetPos(me)->pos);
            dist = Vec3_Length(&d);
            if (k == cur) {
                curDist = dist;
            }
            if (best < 0 || dist < bestDist) {
                best = k;
                bestDist = dist;
            }
        }
        if (best < 0) {
            /* No enemy stands: the one it had, or any enemy, or (a team alone) anyone else; never itself. */
            if (cur < 0) {
                for (k = 0; k < count; k++) {
                    if (Port_IsEnemy(i, k)) {
                        cur = k;
                        break;
                    }
                }
                gPortTarget[i] = cur >= 0 ? cur : (i == 0 ? 1 : 0);
            }
            continue;
        }
        if (curDist < 0.0f || bestDist < curDist * 0.75f) {
            gPortTarget[i] = best;
        }
    }
    for (i = 0; i < 2 && i < count; i++) {
        Port_CamTargetLog(i);
    }
}
#define OPPONENT(chr) BtlChar_Get((chr)->player == gPortCamOf ? Port_CamTarget((chr)->player) : gPortTarget[(chr)->player])
#else
#define OPPONENT(chr) ((chr)->player == 0 ? BtlChar_Get(1) : BtlChar_Get(0))
#endif

/* Where to aim at the opponent: its override position 6 when set, else its position. */
void BtlOpp_GetTargetPos(BtlFlagChr *chr, Vec4 *out) {
    BtlChar_GetSnapPos(OPPONENT(chr), out, 6);
}

/* The opponent's override position `slot` when set, else its position. */
void BtlOpp_GetSnapPos(BtlFlagChr *chr, Vec4 *out, s32 slot) {
    BtlChar_GetSnapPos(OPPONENT(chr), out, slot);
}

/* The opponent's override rotation 6 when set, else its rotation. */
void BtlOpp_GetTargetRot(BtlFlagChr *chr, Vec4 *out) {
    BtlChar_GetSnapRot(OPPONENT(chr), out, 6);
}

/* The opponent's override rotation `slot` when set, else its rotation. */
void BtlOpp_GetSnapRot(BtlFlagChr *chr, Vec4 *out, s32 slot) {
    BtlChar_GetSnapRot(OPPONENT(chr), out, slot);
}

/* Y angle of BtlOpp_GetTargetRot. */
f32 BtlOpp_GetTargetYaw(BtlFlagChr *chr) {
    Vec4 rot;

    BtlChar_GetSnapRot(OPPONENT(chr), &rot, 6);
    return rot.y;
}

/* Vector from the fighter to the opponent's target position. */
void BtlOpp_GetDelta(BtlFlagChr *chr, Vec4 *out) {
    Vec4 pos;

    BtlOpp_GetTargetPos(chr, &pos);
    Vec4_Sub(out, &pos, &BtlChar_GetPos(chr)->pos);
}

/* Copies the vector at +0x30 of the opponent's pose. */
void BtlOpp_GetPoseVec30(BtlFlagChr *chr, Vec4 *out) {
    Vec4_Copy(out, &BtlChar_GetPos(OPPONENT(chr))->unk30);
}

/* Copies the opponent's velocity (pose +0x40). */
void BtlOpp_GetVelocity(BtlFlagChr *chr, Vec4 *out) {
    Vec4_Copy(out, &BtlChar_GetPos(OPPONENT(chr))->vel);
}

/* Distance to the opponent. */
f32 BtlOpp_GetDistance(BtlFlagChr *chr) {
    Vec4 d;

    BtlOpp_GetDelta(chr, &d);
    return Vec3_Length(&d);
}

/* Horizontal distance to the opponent. */
f32 BtlOpp_GetDistanceXZ(BtlFlagChr *chr) {
    Vec4 d;

    BtlOpp_GetDelta(chr, &d);
    return BtlUtil_LengthXZ(&d);
}

/* Horizontal distance between the two bodies: distance minus both radii, never negative. */
f32 BtlOpp_GetGapXZ(BtlFlagChr *chr) {
    f32 d = BtlOpp_GetDistanceXZ(chr);

    d -= BtlCharApi_GetRadius(chr->objId);
    d -= BtlOpp_GetRadius(chr);
    return BtlUtil_MaxF(d, 0.0f);
}

/* Yaw of the direction to the opponent; 0 when it is straight above or below. */
f32 BtlOpp_GetYaw(BtlFlagChr *chr) {
    Vec4 d;

    BtlOpp_GetDelta(chr, &d);
    if (__builtin_fabsf(d.x) < 0.0001f && __builtin_fabsf(d.z) < 0.0001f) {
        return 0.0f;
    }
    return atan2f(d.x, d.z);
}

/* Yaw to the opponent relative to the fighter's own rotation. */
f32 BtlOpp_GetYawFromFacing(BtlFlagChr *chr) {
    f32 yaw = BtlOpp_GetYaw(chr);
    BtlFlagPose *pose = BtlChar_GetPos(chr);

    return BtlUtil_WrapAngle(yaw - BtlUtil_WrapAngle(pose->rot.y + BtlChar_GetPos(chr)->unk64));
}

/* Yaw to the opponent relative to the opponent's rotation. */
f32 BtlOpp_GetYawFromItsFacing(BtlFlagChr *chr) {
    f32 yaw = BtlOpp_GetYaw(chr);

    return BtlUtil_WrapAngle(yaw - BtlOpp_GetTargetYaw(chr));
}

/* Pitch of the direction to the opponent (positive when it is higher: +Y is down). */
f32 BtlOpp_GetPitch(BtlFlagChr *chr) {
    Vec4 d;
    f32 len;
    f32 pitch;

    BtlOpp_GetDelta(chr, &d);
    len = Vec3_Length(&d);
    pitch = 0.0f;
    if (0.001f < len) {
        Vec4_Scale(&d, &d, 1.0f / len);
        pitch = -Mathf_Asin(d.y);
    }
    return pitch;
}

/* The same after adding half the height difference to the vertical offset. */
f32 BtlOpp_GetPitchAdjusted(BtlFlagChr *chr) {
    Vec4 d;
    f32 len;
    f32 pitch;

    BtlOpp_GetDelta(chr, &d);
    d.y += BtlOpp_GetHalfHeightDiff(chr);
    len = Vec3_Length(&d);
    pitch = 0.0f;
    if (0.001f < len) {
        Vec4_Scale(&d, &d, 1.0f / len);
        pitch = -Mathf_Asin(d.y);
    }
    return pitch;
}

/* Half the fighter's height minus half the opponent's. */
f32 BtlOpp_GetHalfHeightDiff(BtlFlagChr *chr) {
    BtlFlagChr *opp = OPPONENT(chr);
    f32 own = BtlCharApi_GetHeight(chr->objId);

    return own * 0.5f - BtlCharApi_GetHeight(opp->objId) * 0.5f;
}

/* The opponent's body radius. */
f32 BtlOpp_GetRadius(BtlFlagChr *chr) {
    return BtlCharApi_GetRadius(OPPONENT(chr)->objId);
}

/* Signed byte +2 of the opponent's parameter block. */
s32 BtlOpp_GetParamByte2(BtlFlagChr *chr) {
    return BtlParam_GetUnk2(OPPONENT(chr));
}

/* The opponent's height (object +0xFF4). */
f32 BtlOpp_GetHeight(BtlFlagChr *chr) {
    return BtlCharApi_GetHeight(OPPONENT(chr)->objId);
}

/* The opponent's object +0xFFC. */
f32 BtlOpp_GetObjUnkFFC(BtlFlagChr *chr) {
    return BtlCharApi_GetCenterHeight(OPPONENT(chr)->objId);
}

/* Word +0 of the opponent's parameter block. */
s32 BtlOpp_GetParamWord0(BtlFlagChr *chr) {
    return BtlParam_GetUnk0(OPPONENT(chr));
}

/* The opponent's player index. */
s32 BtlOpp_GetPlayer(BtlFlagChr *chr) {
    return OPPONENT(chr)->player;
}

/* The opponent's battle object id. */
s32 BtlOpp_GetObjId(BtlFlagChr *chr) {
    return OPPONENT(chr)->objId;
}

/* The opponent's battle object. */
void *BtlOpp_GetObj(BtlFlagChr *chr) {
    return BtlChar_GetObj(OPPONENT(chr));
}

/* The opponent's action as this frame should see it: last frame's until both fighters have reached stage 6. */
s32 BtlOpp_GetSeenAction(BtlFlagChr *chr) {
    BtlFlagChr *opp = OPPONENT(chr);

    if (BtlChar_GetStage(chr) < 6 || BtlChar_GetStage(opp) < 6) {
        return opp->prevAction;
    }
    return BtlAct_GetCurrent(opp);
}

/* The same choice for the opponent's +0x964 / +0x968. */
s32 BtlOpp_GetSeenUnk964(BtlFlagChr *chr) {
    BtlFlagChr *opp = OPPONENT(chr);

    if (BtlChar_GetStage(chr) < 6 || BtlChar_GetStage(opp) < 6) {
        return opp->unk968;
    }
    return opp->unk964;
}

/* The opponent's object +0xFF8. */
f32 BtlOpp_GetObjUnkFF8(BtlFlagChr *chr) {
    return BtlCharApi_GetBodyUnkFF8(OPPONENT(chr)->objId);
}

/* Copies the opponent's vector at +0x15A0. */
void BtlOpp_GetUnk15A0(BtlFlagChr *chr, Vec4 *out) {
    BtlFlagChr *opp = OPPONENT(chr);

    Vec4_Copy(out, &opp->unk15A0);
}

/* World position of a node of the opponent's model. */
void BtlOpp_GetNodePos(BtlFlagChr *chr, s32 node, Vec4 *out) {
    BtlCharApi_GetNodePos(OPPONENT(chr)->objId, node, out);
}

/* Copies the vector the opponent's object points at with +0xFA0. */
void BtlOpp_GetObjVecFA0(BtlFlagChr *chr, Vec4 *out) {
    Vec4_Copy(out, *(Vec4 **)((u8 *)BtlObj_Get(OPPONENT(chr)->objId) + 0xFA0));
}

/* The opponent's +0xD88. */
s32 BtlOpp_GetUnkD88(BtlFlagChr *chr) {
    return OPPONENT(chr)->unkD88;
}

/* Seconds until the two fighters meet at their current velocities; 10000000 when they are not closing. */
f32 BtlOpp_GetClosingTime(BtlFlagChr *chr) {
    Vec4 pos;
    Vec4 vel;
    Vec4 oppPos;
    Vec4 oppVel;
    Vec4 d;
    Vec4 dir;
    f32 dist;
    f32 speed;
    f32 rel;
    f32 eps;

    Vec4_Copy(&pos, &BtlChar_GetPos(chr)->pos);
    eps = 0.001f;
    BtlOpp_GetTargetPos(chr, &oppPos);
    Vec4_Sub(&d, &oppPos, &pos);
    dist = Vec3_Length(&d);
    if (dist < eps) {
        return 0.0f;
    }
    Vec4_Scale(&dir, &d, 1.0f / dist);
    Vec4_Copy(&vel, &BtlChar_GetPos(chr)->vel);
    BtlOpp_GetVelocity(chr, &oppVel);
    speed = Vec3_Dot(&vel, &dir);
    if (speed < 0.0f) {
        return 10000000.0f;
    }
    rel = speed - Vec3_Dot(&oppVel, &dir);
    if (rel < eps) {
        return 10000000.0f;
    }
    return dist / rel;
}

/* BtlMember_HasAbility on the opponent. */
s32 BtlOpp_HasAbility(BtlFlagChr *chr, s32 arg) {
    return BtlMember_HasAbility(OPPONENT(chr), arg);
}

/* BtlParam_GetUnkC7Scale on the opponent. */
f32 BtlOpp_GetUnk20F318(BtlFlagChr *chr) {
    return BtlParam_GetUnkC7Scale(OPPONENT(chr));
}

/* The opponent's clash counter (+0xE50). */
s32 BtlOpp_GetClashCountB(BtlFlagChr *chr) {
    return OPPONENT(chr)->clashCountB;
}

#ifdef PORT
/* 1 while fighter i has an enemy that stands. */
static s32 Port_AnyEnemyStands(s32 i) {
    s32 k;
    for (k = 0; k < BtlChar_GetCount(); k++) {
        if (Port_IsEnemy(i, k) && !BtlChar_IsDead(BtlChar_Get(k))) {
            return 1;
        }
    }
    return 0;
}
#endif

/* Copies what the opponent is doing into this fighter's flags 0xAC..0xB9 and 0xD6. */
void BtlOpp_MirrorFlags(BtlFlagChr *chr) {
    BtlFlagChr *opp;

#ifdef PORT
    /* More than two fighters: "the opponent" here was fighter 1 for fighter 0 and fighter 0 for everybody else,
       whoever they fought. Its state is copied into the fighter's flags every frame, and one of them (0xB1, "the
       opponent is down") makes BtlInput_Fetch drop all of the fighter's input: once fighter 1 was down, fighter 0
       stood still for the rest of the fight, and once fighter 0 was, every extra fighter did (the user: "an enemy
       did a cutscene attack and it broke my inputs"). The opponent is the fighter's target, as everywhere else
       (during a catch or a rush, the attacker), and 0xB1 means that no enemy stands, as it does with two. */
    if (BtlChar_GetCount() > 2) {
        s32 t = gPortTarget[chr->player];
        opp = BtlChar_Get((u32)t < (u32)BtlChar_GetCount() && t != chr->player ? t : chr->player == 0 ? 1 : 0);
    } else
#endif
    if (chr->player == 0) {
        opp = BtlChar_Get(1);
    } else {
        opp = BtlChar_Get(0);
    }
    BtlChar_ClearFlagRange(chr, 0xAC, 0xB9);
    if (BtlChar_TestFlag(opp, 0xB)) {
        BtlChar_SetHeldFlag(chr, 0xAC);
    }
    if (BtlChar_TestFlag(opp, 0x1A)) {
        BtlChar_SetHeldFlag(chr, 0xAD);
    }
    if (BtlChar_TestFlag(opp, 0x3F)) {
        BtlChar_SetHeldFlag(chr, 0xAE);
    }
    if (BtlChar_TestFlag(opp, 0x85)) {
        BtlChar_SetHeldFlag(chr, 0xB3);
    }
    if (BtlAct_IsTechniqueId(BtlAct_GetCurrent(opp))) {
        BtlChar_SetHeldFlag(chr, 0xAF);
    }
    if (BtlCharApi_IsInSkill(opp->objId)) {
        BtlChar_SetHeldFlag(chr, 0xB0);
    }
#ifdef PORT
    if (BtlChar_GetCount() > 2 ? !Port_AnyEnemyStands(chr->player) : BtlChar_IsDead(opp)) {
#else
    if (BtlChar_IsDead(opp)) {
#endif
        BtlChar_SetHeldFlag(chr, 0xB1);
    }
    if (BtlChar_TestFlag(opp, 0xCC)) {
        BtlChar_SetHeldFlag(chr, 0xB4);
    }
    if (BtlChar_TestFlag(opp, 0x26)) {
        BtlChar_SetHeldFlag(chr, 0xB2);
    }
    if (BtlChar_TestFlag(opp, 0x27)) {
        ChrCam_EndCut(chr);
    }
    if (BtlChar_TestFlag(opp, 0x90)) {
        BtlChar_SetFlag(chr, 0xB5);
    }
    if (BtlChar_TestFlag(opp, 0x16)) {
        BtlChar_SetFlag(chr, 0xB6);
    }
    if (BtlChar_TestFlag(opp, 0x17)) {
        BtlChar_SetFlag(chr, 0xB7);
    }
    if (BtlChar_TestFlag(opp, 0x28)) {
        BtlChar_SetFlag(chr, 0xD6);
    }
    if (BtlChar_TestFlag(opp, 0xDC)) {
        BtlChar_SetFlag(chr, 0xB8);
    }
    if (BtlChar_TestFlag(opp, 0x92)) {
        BtlChar_SetFlag(chr, 0xB9);
    }
}

#ifdef PORT
void Port_ReviveSpectator(s32 who){
 if((u32)who<PORT_FIGHTERS){sPortWatch[who]=-1;sPortWatchWait[who]=0;sPortTargetHold[who]=0;}
}
#endif
