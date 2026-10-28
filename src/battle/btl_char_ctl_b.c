#include "common.h"
#include "battle/btl_char_ctl.h"

/*
 * Character change requests and the "time stopped" word: 0x1D60A0..0x1D6438.
 *
 * The roster holds a ring of up to 7 requests (roster + 0x138, 0x140 bytes). A fighter that transforms, fuses,
 * switches member or needs an extra object pushes a request (BtlChange_RequestChara / BtlChange_RequestObject,
 * called from the fighter state machine); the battle loader (src/battle/battle_load.c) serves one at a time:
 *
 *   state 0  idle
 *   state 1  BtlChange_Update (end of every frame, also while paused) popped the request: it is now `cur`
 *   state 2  BtlChange_SetTaken   the loader created its job (through BtlChange_NotifyTaken)
 *   state 3  BtlChange_SetLoaded  the files are in memory (through BtlChange_NotifyLoaded); `loaded` = 1
 *   state 4  BtlChange_SetReady   the owning fighter allows the swap (the loader waits for this, BtlChange_IsReady)
 *   state 5  BtlChange_SetDone    the owning fighter is finished; the next BtlChange_Update clears `cur`
 *
 * roster + 0x274 (`timeStop`, BtlChars_IsTimeStopped) is written only by BtlChange_Update and BtlChange_Reset:
 * it is 1 on every frame that ends with an active request and 0 otherwise. A request that finishes and a queued
 * one that starts in the same BtlChange_Update keep it at 1 without a gap.
 */

extern void *memset(void *dst, s32 c, u32 n);

extern BtlCtlRoster *gBtlChars;

#ifdef PORT
/* More than two fighters. The ring holds six requests, and a request that finds it full is dropped without a
   word: the fighter that made it then waits for its model for ever, holding everybody else still. Two fighters
   never have six waiting; ten can (each may ask for a transformation, a partner's body, a damaged costume).
   What does not fit waits here, in its order, and moves into the ring as the ring gets room. */
#define PORT_CHANGE_MORE 32
static BtlChangeReq sPortChangeMore[PORT_CHANGE_MORE];
static s32 sPortChangeMoreCount;

static void Port_ChangeRefill(void) {
    BtlChangeQueue *q = &gBtlChars->change;
    s32 i;

    while (sPortChangeMoreCount > 0 && (q->write + 1) % BTL_CHANGE_RING != q->read) {
        q->req[q->write] = sPortChangeMore[0];
        q->write = (q->write + 1) % BTL_CHANGE_RING;
        sPortChangeMoreCount--;
        for (i = 0; i < sPortChangeMoreCount; i++) {
            sPortChangeMore[i] = sPortChangeMore[i + 1];
        }
    }
}

/* The fighter whose request is the active one, -1 when none is. BtlChars_UpdateFreeze asks: while a request is
   active nobody but its owner holds the others still. The owner has to act for the queue to move on (it says
   when the swap may happen and when it is done), and a fighter that starts a transformation meanwhile stands in
   its action with the hit-stop up, waiting for a turn that never came while the owner was frozen by it (seen
   with ten fighters: one transformed during two others' rush sequence, whose attacker had asked for its
   partner's body; the fight stood still for good). */
s32 Port_ChangeOwner(void) {
    BtlChangeQueue *q = &gBtlChars->change;

    return q->cur != NULL ? q->cur->player : -1;
}
#endif

/* Reserves the next slot of the request ring, or returns NULL when it is full. */
BtlChangeReq *BtlChange_Alloc(void) {
    BtlChangeQueue *q = &gBtlChars->change;
    s32 w = q->write;

#ifdef PORT
    if (sPortChangeMoreCount > 0 || (w + 1) % BTL_CHANGE_RING == q->read) {
        return sPortChangeMoreCount < PORT_CHANGE_MORE ? &sPortChangeMore[sPortChangeMoreCount++] : NULL;
    }
#endif
    if ((w + 1) % BTL_CHANGE_RING == q->read) {
        return NULL;
    }
    q->write = (w + 1) % BTL_CHANGE_RING;
    return &q->req[w];
}

/* Takes the oldest request out of the ring, or returns NULL when it is empty. */
BtlChangeReq *BtlChange_Pop(void) {
    BtlChangeQueue *q = &gBtlChars->change;
    s32 r = q->read;

    if (q->write == r) {
        return NULL;
    }
    q->read = (r + 1) % BTL_CHANGE_RING;
    return &q->req[r];
}

/* Empties the request queue and clears the time-stop word. */
void BtlChange_Reset(void) {
    memset(&gBtlChars->change, 0, sizeof(BtlChangeQueue));
#ifdef PORT
    sPortChangeMoreCount = 0;
#endif
}

/* Once per frame: retires a finished request, starts the next one, and sets the time-stop word while one is active. */
void BtlChange_Update(void) {
    BtlChangeQueue *q = &gBtlChars->change;

#ifdef PORT
    {extern void Port_FusionChangeLog(int,int,int,int);
     static int state=-1,player=-1,kind=-1,id=-1;
     if(q->cur){if(q->state!=state||q->cur->player!=player||q->cur->kind!=kind||q->cur->id!=id){
      state=q->state;player=q->cur->player;kind=q->cur->kind;id=q->cur->id;Port_FusionChangeLog(player,state,kind,id);
     }}else state=-1;}
#endif
#ifdef PORT
    {
        /* (the experiment's tool, BT3_HEAPCHK: the request in progress, every five seconds) */
        extern void Port_Dbg3(const char *what, int a, int b, int c, int d);
        static s32 n;
        if (++n % 150 == 0 && q->cur != NULL) {
            Port_Dbg3("change request: state / player / kind / id", q->state, q->cur->player, q->cur->kind, q->cur->id);
        }
    }
#endif

#ifdef PORT
    Port_ChangeRefill();
#endif
    if (q->state == BTL_CHANGE_DONE) {
        q->state = BTL_CHANGE_IDLE;
        q->loaded = 0;
        q->cur = NULL;
    }
    if (q->cur == NULL) {
        q->cur = BtlChange_Pop();
        if (q->cur == NULL) {
            q->timeStop = 0;
        } else {
            q->state = BTL_CHANGE_STARTED;
            q->loaded = 0;
            q->timeStop = 1;
        }
    } else {
        q->timeStop = 1;
    }
}

#ifdef PORT
/* Frame at which each player's last model change was requested; BtlChar_OnModelLoaded logs the wait of the
   presentation loops toward it (Juan 2026-10-26: the white screen of the damaged clothes swap "lasts too long"). */
static s32 sPortChangeReq[12] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
int Port_ChangeWaited(s32 player){extern s32 BtlChar_FrameMod(s32);return player>=0&&player<12&&sPortChangeReq[player]>=0?BtlChar_FrameMod(0x7fffffff)-sPortChangeReq[player]:-1;}
#endif

/* Queues a character model change (transformation, fusion, member switch) for a player. */
void BtlChange_RequestChara(s32 player, s32 chara, s32 costume, s32 variant, s32 animChara, s32 unk18, s32 voiceChara) {
    BtlChangeReq *req = BtlChange_Alloc();
#ifdef PORT
    if (player >= 0 && player < 12) { extern s32 BtlChar_FrameMod(s32); sPortChangeReq[player] = BtlChar_FrameMod(0x7fffffff); }
#endif

    if (req != NULL) {
        req->player = player;
        req->chara = chara;
        req->costume = costume;
        req->variant = variant;
        req->animChara = animChara;
        req->unk18 = unk18;
        req->voiceChara = voiceChara;
        req->kind = BTL_CHANGE_KIND_CHARA;
        req->slot = 0;
    }
}

/* Queues the load of an extra object for a player. */
void BtlChange_RequestObject(s32 player, s32 id, s32 costume, s32 variant, s32 slot) {
    BtlChangeReq *req = BtlChange_Alloc();

    if (req != NULL) {
        req->player = player;
        req->kind = BTL_CHANGE_KIND_OBJECT;
        req->id = id;
        req->costume = costume;
        req->variant = variant;
        req->animChara = -1;
        req->unk18 = -1;
        req->voiceChara = -1;
        req->slot = slot;
    }
}

/* The loader has picked up the active request. */
void BtlChange_SetTaken(void) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur != NULL) {
        q->state = BTL_CHANGE_TAKEN;
    }
}

/* The files of the active request are in memory. */
void BtlChange_SetLoaded(void) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur != NULL) {
        q->state = BTL_CHANGE_LOADED;
        q->loaded = 1;
    }
}

/* Tells a player whether the files of its own active request are in memory. */
s32 BtlChange_IsLoadedFor(s32 player) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur == NULL) {
        return 0;
    }
    if (q->cur->player != player) {
        return 0;
    }
    return q->loaded;
}

/* Tells whether the files of the active request are in memory. */
s32 BtlChange_IsLoaded(void) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur != NULL) {
        return q->loaded;
    }
    return 0;
}

/* Returns the time-stop word: non-zero while a change request is active. */
s32 BtlChars_IsTimeStopped(void) {
    return gBtlChars->change.timeStop;
}

/* Returns the player that owns the active request, or -1. */
s32 BtlChange_GetPlayer(void) {
    BtlChangeReq *cur = gBtlChars->change.cur;

    if (cur != NULL) {
        return cur->player;
    }
    return -1;
}

/* The owning fighter is ready for the new model to be swapped in. */
void BtlChange_SetReady(s32 player) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur != NULL && q->cur->player == player) {
        q->state = BTL_CHANGE_READY;
    }
}

/* The owning fighter has finished with the request; BtlChange_Update retires it. */
void BtlChange_SetDone(s32 player) {
    BtlChangeQueue *q = &gBtlChars->change;

    if (q->cur != NULL && q->cur->player == player) {
        q->state = BTL_CHANGE_DONE;
    }
}
