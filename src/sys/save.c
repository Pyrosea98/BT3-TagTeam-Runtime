#include "common.h"
#include "sys/save.h"
#include "sys/heap.h"
#include "sys/rand.h"
#include "sys/common.h"

extern void *memset(void *dst, s32 value, u32 size);

extern void Progress_ClearSession(void); /* clears the per-session parts of gProgress */

/* Debug (no callers): unlocks every character, stage, list entry and item, marks about half the items new, and gives 4,850,000 money. */
void Save_UnlockAll(SaveData *opt) {
    SaveSlot *slot = opt->slot;
    s32 i;
    s32 *v;

    for (i = 0; i < SAVE_CHARA_WORDS; i++) {
        opt->charaBits[i] = 0;
    }
    for (i = 0; i < SAVE_CHARA_COUNT; i++) {
        SAVE_CHARA_WORD(gSaveData, i) |= SAVE_CHARA_MASK(i);
    }
    gSaveData->stageBits = 0;
    for (i = 0; i < SAVE_STAGE_COUNT; i++) {
        gSaveData->stageBits |= 1LL << i;
    }
    for (i = 0; i < SAVE_BGM_LIST_COUNT; i++) {
        if (i >= SAVE_BGM_COUNT) {
            break;
        }
        gSaveData->bgmBits |= 1 << i;
    }
    for (i = 0; i < SAVE_FLAG8_COUNT; i++) {
        gSaveData->unlockFlags |= 1 << i;
    }
    for (i = 0; i < SAVE_ITEM_COUNT; i++) {
        opt->item[i] |= SAVE_ITEM_OWNED;
        if (Rand_Range(100) & 1) {
            opt->item[i] |= SAVE_ITEM_NEW;
        }
    }
    opt->money = SAVE_MONEY_UNLOCK_ALL;
    for (i = 0; i < SAVE_REC_COUNT; i++) {
        opt->rec[i].chara = -1;
    }
    opt->unk20C = 20;
    opt->unk208 |= 1;
    for (i = 0; i < SAVE_SLOT_COUNT; i++) {
        slot[i].flags |= 3;
        v = opt->slot[i].val;
        v[0] = 0xFFFF;
        v[1] = 0xFFFF;
        v[2] = 0xFFFF;
    }
}

/* Clears the block and writes a new save: starting characters/stages/items, default key assignment, volumes and flags. */
void Save_SetDefaults(SaveData *opt) {
    s32 i;
    ItemInfo *info;

    info = (ItemInfo *)((u8 *)gCommonRes->data[2] + ((ItemFile *)gCommonRes->data[2])->itemOffset / 4 * 4);
    Progress_ClearSession();
    memset(opt, 0, SAVE_SIZE);
    opt->slot[0].flags |= 3;
    opt->slot[0].val[0] |= 1;
    opt->slot[0].val[2] |= 1;
    opt->unkC = 1;
    Save_ResetRules();
    for (i = 0; i < SAVE_CHARA_COUNT; i++) {
        switch (i) {
        case 7:
        case 8:
        case 9:
        case 10:
        case 0x18:
        case 0x26:
        case 0x36:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x4B:
        case 0x4C:
        case 0x61:
        case 0x6E:
        case 0x78:
        case 0x91:
        case 0x95:
        case 0x96:
        case 0x97:
        case 0x98:
        case 0x99:
        case 0x9A:
        case 0x9B:
        case 0x9C:
        case 0x9D:
        case 0x9E:
        case 0x9F:
        case 0xA0:
            break;
        default:
            SAVE_CHARA_WORD(gSaveData, i) |= SAVE_CHARA_MASK(i);
            break;
        }
    }
    for (i = 0; i < SAVE_STAGE_COUNT; i++) {
        switch (i) {
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x1F:
        case 0x20:
            break;
        default:
            gSaveData->stageBits |= 1LL << i;
            break;
        }
    }
    for (i = 0; i < SAVE_BGM_LIST_COUNT; i++) {
        if (i >= SAVE_BGM_COUNT) {
            break;
        }
        gSaveData->bgmBits |= 1 << i;
    }
    for (i = 0; i < SAVE_ITEM_COUNT; i++) {
        if (info[i].flags & ITEM_INFO_INITIAL) {
            Save_AddItem(i);
        }
    }
    for (i = 0; i < SAVE_REC_COUNT; i++) {
        opt->rec[i].chara = -1;
    }
    opt->unk20C = 5;
    opt->unk77C = 99;
    for (i = 0; i < SAVE_PAD_COUNT; i++) {
        opt->key[i][0] = 2;
        opt->key[i][1] = 1;
        opt->key[i][2] = 0;
        opt->key[i][3] = 3;
        opt->key[i][4] = 4;
        opt->key[i][5] = 5;
        opt->key[i][6] = 6;
        opt->key[i][7] = 7;
        opt->keyEdit[i][0] = 2;
        opt->keyEdit[i][1] = 1;
        opt->keyEdit[i][2] = 0;
        opt->keyEdit[i][3] = 3;
        opt->keyEdit[i][4] = 4;
        opt->keyEdit[i][5] = 5;
        opt->keyEdit[i][6] = 6;
        opt->keyEdit[i][7] = 7;
    }
    opt->flags |= SAVE_FLAG_DEFAULT;
    opt->unk1694 = 0;
    opt->bgmVolume = SAVE_VOLUME_DEFAULT;
    opt->seVolume = SAVE_VOLUME_DEFAULT;
}

/* Empty; called with the block right after the defaults are written. */
void Save_Stub266600(SaveData *opt) {
}

/* Allocates the 0x4000-byte save block and fills it with a new save. */
void Save_Init(void) {
    gSaveData = Heap_Alloc(SAVE_SIZE, 0x20, 0, 2);
    memset(gSaveData, 0, SAVE_SIZE);
    Save_SetDefaults(gSaveData);
    Save_Stub266600(gSaveData);
}

/* Gives item idx (0-based) if it is not owned yet, marking it new. */
void Save_AddItem(s32 idx) {
    if (!(gSaveData->item[idx] & SAVE_ITEM_OWNED)) {
        gSaveData->item[idx] |= SAVE_ITEM_OWNED;
        gSaveData->item[idx] |= SAVE_ITEM_NEW;
    }
}

/* Adds amount (may be negative), without overflowing the existing save field. */
void Save_AddMoney(s32 amount) {
#ifdef PORT
    s64 total = (s64)gSaveData->money + amount;
    gSaveData->money = total > SAVE_MONEY_MAX ? SAVE_MONEY_MAX : total < 0 ? 0 : (s32)total;
#else
    gSaveData->money += amount;
    if (gSaveData->money > SAVE_MONEY_MAX) {
        gSaveData->money = SAVE_MONEY_MAX;
    } else if (gSaveData->money < 0) {
        gSaveData->money = 0;
    }
#endif
}

#ifdef PORT
/* Port Dragon History chapters (port/include/mod_story.h): their progress bits live in bits 24..31 of the first
   sub menu's flags, which the game never sets (MSLOT_* are 0x01..0x40 and every reader masks them), so they are
   kept in the same 0x4000-byte save as the stock Dragon History progress. Two bits per chapter, chapters 0..3. */
s32 Save_GetMoney(void) { return gSaveData != NULL ? gSaveData->money : 0; }

/* Dragon History chapters: puts a found Dragon Ball (1..7) into unlockFlags, as HistResult_BuildRewards does for
   stock story battles. 1 when it was new, 0 when already owned or invalid. */
s32 Save_GrantDragonBall(s32 ball) {
    s32 mask;

    if (gSaveData == NULL || ball < 1 || ball > 7) {
        return 0;
    }
    mask = 1 << (ball - 1);
    if (gSaveData->unlockFlags & mask) {
        return 0;
    }
    gSaveData->unlockFlags |= mask;
    return 1;
}

s32 Save_GetStoryBits(void) {
    return gSaveData != NULL ? (gSaveData->slot[0].flags >> 24) & 0xFF : 0;
}

void Save_OrStoryBits(s32 bits) {
    if (gSaveData != NULL) {
        gSaveData->slot[0].flags |= (bits & 0xFF) << 24;
    }
}

/* Dragon History chapter progress beyond the first four: the same byte (bits 24..31) of slot `group` - stock code
   reads only MSLOT_* (bits 0..6) and the ranks use bits 8..23. Group 0 is Save_GetStoryBits. */
s32 Save_GetStoryBitsGroup(s32 group) {
    return gSaveData != NULL && group >= 0 && group < SAVE_SLOT_COUNT ? ((u32)gSaveData->slot[group].flags >> 24) & 0xFF : 0;
}

void Save_OrStoryBitsGroup(s32 group, s32 bits) {
    if (gSaveData != NULL && group >= 0 && group < SAVE_SLOT_COUNT) {
        gSaveData->slot[group].flags |= (s32)((u32)(bits & 0xFF) << 24);
    }
}

/* Two-bit highest difficulty rank (0 unranked, 1 Easy, 2 Normal, 3 Hard).
   All stock slot readers mask MSLOT_* (bits0..6); bits8..23 are unused.
   Keep the previous outcome bits24..31 of slot0 intact. Eight chapters per
   slot, 72 total, no save layout/version change. Chapter IDs must stay stable. */
s32 Save_GetStoryRank(s32 chapter) {
    if (gSaveData == NULL || chapter < 0 || chapter >= SAVE_SLOT_COUNT * 8) return 0;
    return ((u32)gSaveData->slot[chapter / 8].flags >> (8 + (chapter % 8) * 2)) & 3u;
}

void Save_ClearStoryRank(s32 chapter) {
    if(gSaveData && chapter>=0 && chapter<SAVE_SLOT_COUNT*8)
        gSaveData->slot[chapter/8].flags &= ~(3u << (8+(chapter%8)*2));
}

void Save_RaiseStoryRank(s32 chapter, s32 rank) {
    s32 shift;
    u32 flags, mask;
    if (gSaveData == NULL || chapter < 0 || chapter >= SAVE_SLOT_COUNT * 8 || rank < 1 || rank > 3) return;
    if (rank <= Save_GetStoryRank(chapter)) return;
    shift = 8 + (chapter % 8) * 2;
    flags = (u32)gSaveData->slot[chapter / 8].flags;
    mask = 3u << shift;
    gSaveData->slot[chapter / 8].flags = (s32)((flags & ~mask) | ((u32)rank << shift));
}
#endif

/* Restores the six rule settings at 0xC34 to 3, 2, 2, 0, 0, 0. */
void Save_ResetRules(void) {
    gSaveData->rule[0] = 3;
    gSaveData->rule[1] = 2;
    gSaveData->rule[2] = 2;
    gSaveData->rule[3] = 0;
    gSaveData->rule[4] = 0;
    gSaveData->rule[5] = 0;
}
