#include "common.h"
#include "battle/btl_pool.h"
#include "sys/heap.h"

extern void *memset(void *dst, s32 c, u32 n);

extern BtlPool *gBtlPool;
/* Written by BtlPool_Init and BtlPool_SetInitArg, never read. */
extern s32 gBtlPoolInitArg;

/* The arena block is reached through a pointer to the field, not as a plain member: after a store to it
 * the compiler reloads gBtlPool, which a direct `gBtlPool->arenaMem = x` does not reproduce. */
#define ARENA_MEM (*&gBtlPool->arenaMem)

s32 BtlPool_CalcArenaSize(void);
void BtlPool_CarveArenas(void);
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem);

#ifdef PORT
/* The experiment of more than two fighters. The arenas of the first extra fighter (9 and 10) are part of the
   game's own block, whether the fight has one or not (as it has been since the third fighter). Those of the
   further ones are made only when the fight has such fighters, in one block of the port's memory where there is
   such (200 KB a fighter, and the heap is sized for two), else of the heap. */
extern int gPortExtraObjs;                   /* port/src/plat_stub.c: the extra fighters of this fight */
extern unsigned Port_GameBigAlloc(int size); /* port/src/plat_mem.c */
static u8 *sPortArenaMem;
/* An arena's size is never checked by the game: what does not fit is written into the next arena. With two
   fighters everything fits; with more, the arenas that hold something for each fighter fill up (the shot
   layer's 5 KB held three fighters' records and not a fourth: the wall effect behind it was written over, a
   crash when it was drawn). The most each arena has held is kept, an arena that overflows says so once, and
   BT3_HEAPCHK prints the figures when the scene ends. */
extern void Port_Dbg3(const char *what, int a, int b, int c, int d); /* port/src/plat_stub.c */
extern void Port_PoolFull(int slot, int size, int used);
static s32 sPortPoolPeak[BTL_POOL_SLOT_COUNT];
static void Port_PoolWatch(s32 slot, BtlPoolSlot *s) {
    if (s->used > sPortPoolPeak[slot]) {
        if (s->used > s->size && sPortPoolPeak[slot] <= s->size) {
            Port_PoolFull(slot, s->size, s->used);
        }
        sPortPoolPeak[slot] = s->used;
    }
}
static void Port_PoolExtraArenas(void) {
    s32 each = BtlPool_AlignUp32(0x19000);
    s32 size = (gPortExtraObjs - 1) * 2 * each;
    u8 *mem;
    s32 k;

    if (gPortExtraObjs < 2) {
        return;
    }
    sPortArenaMem = (u8 *)Port_GameBigAlloc(size);
    if (sPortArenaMem == NULL) {
        sPortArenaMem = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
    }
    memset(sPortArenaMem, 0, size);
    mem = sPortArenaMem;
    for (k = 1; k < gPortExtraObjs; k++) {
        gBtlPool->arenaMask |= BTL_POOL_ARENA_BIT(BTL_POOL_EXTRA_SHOT(k)) | BTL_POOL_ARENA_BIT(BTL_POOL_EXTRA_EFFECT(k));
        mem = BtlPool_InitSlot(&gBtlPool->slots[BTL_POOL_EXTRA_SHOT(k)], each, mem);
        mem = BtlPool_InitSlot(&gBtlPool->slots[BTL_POOL_EXTRA_EFFECT(k)], each, mem);
    }
}
#endif

/* The size of arena `slot`. */
#ifdef PORT
/* More than three fighters: four of the game's arenas hold something for every fighter of the scene (measured:
   arena 1 has 39 KB a fighter and 2 KB left with three; the shot layer's arena 3 has 1.5 KB a fighter and holds
   three; arenas 0 and 6 a few bytes). They grow by this for each fighter past the third. With up to three
   fighters every size is the game's own. */
static s32 Port_PoolGrow(s32 slot) {
    static const s32 kEach[9] = { 0x100, 0xC000, 0, 0x800, 0, 0, 0x100, 0, 0 };

    return gPortExtraObjs > 1 ? (gPortExtraObjs - 1) * kEach[slot] : 0;
}
#define POOL_SIZE(slot, size) BtlPool_AlignUp32((size) + Port_PoolGrow(slot))
#else
#define POOL_SIZE(slot, size) BtlPool_AlignUp32(size)
#endif

/* Allocates the pool manager and the memory behind every arena slot. */
void BtlPool_Init(s32 arg) {
    s32 size;

    gBtlPoolInitArg = arg;
    gBtlPool = Heap_Alloc(sizeof(BtlPool), 0x20, 0, HEAP_ANY);
    memset(gBtlPool, 0, sizeof(BtlPool));
    gBtlPool->arenaMask = 0x3FE;
#ifdef PORT
    gBtlPool->arenaMask |= BTL_POOL_ARENA_BIT(9) | BTL_POOL_ARENA_BIT(10);
#endif
    size = BtlPool_CalcArenaSize();
    if (size > 0) {
#ifdef PORT
        /* (a block larger than the game's is not asked of its heap, which is sized for two fighters) */
        ARENA_MEM = gPortExtraObjs > 1 ? (u8 *)Port_GameBigAlloc(size) : NULL;
        if (ARENA_MEM == NULL)
#endif
        ARENA_MEM = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
        memset(ARENA_MEM, 0, size);
        BtlPool_CarveArenas();
    }
#ifdef PORT
    Port_PoolExtraArenas();
#endif
}

/* Frees the arena memory and the pool manager. */
void BtlPool_Term(void) {
#ifdef PORT
    if (gBtlPool != NULL) {
        s32 i;

        for (i = 0; i < BTL_POOL_SLOT_COUNT; i++) {
            if (sPortPoolPeak[i] != 0) {
                Port_Dbg3("effect arena: number / size / most used / fighters", i, gBtlPool->slots[i].size, sPortPoolPeak[i], 2 + gPortExtraObjs);
            }
            sPortPoolPeak[i] = 0;
        }
    }
    if (sPortArenaMem != NULL) {
        Heap_Free(sPortArenaMem);
        sPortArenaMem = NULL;
    }
#endif
    if (ARENA_MEM != NULL) {
        Heap_Free(ARENA_MEM);
        ARENA_MEM = NULL;
    }
    if (gBtlPool != NULL) {
        Heap_Free(gBtlPool);
        gBtlPool = NULL;
    }
}

/* Allocates size bytes (rounded up to 32) from a slot: bump allocation for an arena, the heap otherwise. */
void *BtlPool_Alloc(s32 slot, s32 size) {
    s32 aligned = BtlPool_AlignUp32(size);
    BtlPoolSlot *s = &gBtlPool->slots[slot];
    void *ptr;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
        ptr = s->cur;
        s->cur += aligned;
        s->used += aligned;
#ifdef PORT
        Port_PoolWatch(slot, s);
#endif
    } else {
        s->used += aligned;
        ptr = Heap_Alloc(size, 0x20, 0, HEAP_ANY);
    }
    return ptr;
}

/* Frees a pointer from a slot; a no-op for arena slots, which are only released by BtlPool_Reset. */
void BtlPool_Free(s32 slot, void *ptr) {
    if (!(gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot))) {
        if (ptr != NULL) {
            Heap_Free(ptr);
        }
    }
}

/* Rewinds an arena slot to empty. */
void BtlPool_Reset(s32 slot) {
    BtlPoolSlot *s = &gBtlPool->slots[slot];

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(slot)) {
        s->cur = s->base;
        s->used = 0;
    }
}

/* Selects the slot that BtlPool_GetCurrent reports. */
void BtlPool_SetCurrent(s32 slot) {
    gBtlPool->current = slot;
}

/* Returns the slot selected by BtlPool_SetCurrent. */
s32 BtlPool_GetCurrent(void) {
    return gBtlPool->current;
}

/* Stores the value BtlPool_Init also records (never read back). */
void BtlPool_SetInitArg(s32 arg) {
    gBtlPoolInitArg = arg;
}

/* Returns the total size of all enabled arena slots. */
s32 BtlPool_CalcArenaSize(void) {
    s32 total = 0;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        total = POOL_SIZE(0, 0xA000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        total += POOL_SIZE(1, 0x113000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        total += POOL_SIZE(2, 0x1E000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        total += POOL_SIZE(3, 0x1400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        total += POOL_SIZE(4, 0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(5)) {
        total += POOL_SIZE(5, 0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        total += POOL_SIZE(6, 0x400);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        total += POOL_SIZE(7, 0x19000);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(8)) {
        total += POOL_SIZE(8, 0x19000);
    }
#ifdef PORT
    total += 2 * BtlPool_AlignUp32(0x19000);
#endif
    return total;
}

/* Hands each enabled arena slot its piece of arenaMem. Slots 5 and 8 test the bit of slots 4 and 7 (original bug). */
void BtlPool_CarveArenas(void) {
    u8 *mem = gBtlPool->arenaMem;

    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(0)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[0], POOL_SIZE(0, 0xA000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(1)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[1], POOL_SIZE(1, 0x113000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(2)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[2], POOL_SIZE(2, 0x1E000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(3)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[3], POOL_SIZE(3, 0x1400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[4], POOL_SIZE(4, 0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(4)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[5], POOL_SIZE(5, 0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(6)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[6], POOL_SIZE(6, 0x400), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[7], POOL_SIZE(7, 0x19000), mem);
    }
    if (gBtlPool->arenaMask & BTL_POOL_ARENA_BIT(7)) {
        mem = BtlPool_InitSlot(&gBtlPool->slots[8], POOL_SIZE(8, 0x19000), mem);
    }
#ifdef PORT
    mem = BtlPool_InitSlot(&gBtlPool->slots[9], BtlPool_AlignUp32(0x19000), mem);
    mem = BtlPool_InitSlot(&gBtlPool->slots[10], BtlPool_AlignUp32(0x19000), mem);
#endif
}

/* Points a slot at its arena memory and returns the address just past it. */
u8 *BtlPool_InitSlot(BtlPoolSlot *slot, s32 size, u8 *mem) {
    slot->size = size;
    slot->base = mem;
    slot->cur = mem;
    return mem + size;
}

/* Rounds a size up to a multiple of 32. */
s32 BtlPool_AlignUp32(s32 size) {
    if (size % 32) {
        size = size / 32 * 32 + 32;
    }
    return size;
}
