/* The fight panels: what the side bars show of a fight (gs/ui.cpp draws them). port/src/fight_panels.c reads the
   fight once per blank on the game's thread; the render thread takes a copy. C and C++. */
#ifndef PORT_FIGHT_PANELS_H
#define PORT_FIGHT_PANELS_H
#ifdef __cplusplus
extern "C" {
#endif

#define FP_MEMBERS 5 /* a side's member slots (include/battle/btl_char_member.h: BTL_MEMBER_SLOTS) */
#define FP_PATH 260

typedef struct FpMember {
    int present;  /* the slot holds a member */
    int chara;    /* character id (the active member's: the one on screen, after a transformation) */
    int costume;  /* costume code (8 and up: a skin of the mod grid) */
    int hp, hpMax; /* 10000 per bar */
    char path[FP_PATH]; /* host path of its large portrait ("" = none found) */
} FpMember;

typedef struct FpSide {
    int found;        /* the side has a fighter */
    int active;       /* the member that is fighting */
    int count;        /* member slots in use */
    int next;         /* the member a switch would bring in, -1 none */
    int ki, kiMax;    /* the active member's: 20000 per bar */
    int blast, blastMax; /* 100000 per stock */
    int maxPower;     /* 0..30000: the powered-up mode's timer */
    int poweredUp;
    int comboHits, comboDamage, comboTimer; /* the combo this side is landing on the other */
    FpMember m[FP_MEMBERS];
} FpSide;

#define FP_ACTORS 12
#define FP_VIEWS 4
typedef struct FpActor {
 int assistKind,assistState,assistHelpers;
 int fusionReadyMask,fusionAccept,fusionProgress;
 int reviveTarget,reviveProgress,reviveRecovering,reviveInsufficient;
 int present,chara,costume,hp,hpMax,ki,kiMax,blast,blastMax,poweredUp,player,team,fusionSeconds,fusionMove,fusionAttack,fusionSwap;
 int visible[4],x[4],y[4];char name[96];
} FpActor;
typedef struct FpView {int x,y,w,h,me,target,takeover;} FpView;
typedef struct FpReviveRing {int active,team;int visible[4][25],x[4][25],y[4][25];} FpReviveRing;
/* the defuse cinematic's crack on a fighter, per view: segments in 1/16 HUD units, alpha 0..255, width in 1/16 HUD
   units (port/include/mod_defuse_cinematic.inc: Port_DefuseCrack). Two at once at most. */
#define FP_CRACK_SEGS 48
#define FP_CRACKS 2
typedef struct FpCrackSeg {short x0,y0,x1,y1;unsigned short width;unsigned char alpha,pad;} FpCrackSeg;
typedef struct FpDefuseLight {int active,count[FP_VIEWS];FpCrackSeg seg[FP_VIEWS][FP_CRACK_SEGS];} FpDefuseLight;
typedef struct FpState {
    FpReviveRing reviveRing[FP_ACTORS];
    int battle;  /* a battle's fighters are there */
    int gauges;  /* the fight is on with its gauges shown */
    FpSide side[2];
    int paused,actorCount,viewCount;FpActor actor[FP_ACTORS];FpView view[FP_VIEWS];
    FpDefuseLight defuse[FP_CRACKS];
} FpState;

void PortPanels_GameTick(void);      /* the game's thread, once per blank */
int PortPanels_Read(FpState *out);   /* any thread: a consistent copy; 0 = nothing read yet */

#ifdef __cplusplus
}
#endif
#endif
