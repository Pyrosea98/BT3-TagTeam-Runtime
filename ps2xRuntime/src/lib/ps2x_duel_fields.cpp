// [duelfields] Implementation. See include/runtime/ps2x_duel_fields.h for the map.

#include "runtime/ps2x_duel_fields.h"

#include <cstdio>
#include <cstring>

namespace
{
    inline uint32_t rd32(const uint8_t *rd, uint32_t addr)
    { uint32_t v = 0; std::memcpy(&v, rd + (addr & 0x1FFFFFFFu), 4); return v; }
    inline void wr32(uint8_t *rd, uint32_t addr, uint32_t v)
    { std::memcpy(rd + (addr & 0x1FFFFFFFu), &v, 4); }
    inline uint32_t objAt(const uint8_t *rd, uint32_t slot)
    { return rd32(rd, slot) & 0x1FFFFFFFu; }
}

namespace ps2x_duelfields
{
    Fields read(const uint8_t *rdram)
    {
        Fields f;
        if (!rdram) return f;
        const uint32_t d = objAt(rdram, kDuelObjSlot);
        const uint32_t s = objAt(rdram, kStatePtrSlot);
        if (d)
        {
            f.vs         = rd32(rdram, d + kOffVs);
            f.battleType = rd32(rdram, d + kOffType);
            f.dpVariant  = rd32(rdram, d + kOffDp);
            f.timeLimit  = rd32(rdram, d + kOffTime);
        }
        else
        {
            f.vs = f.battleType = f.dpVariant = f.timeLimit = 0xFFFFFFFFu;
        }
        (void)s;   // committed copies are read through readCommitted() when a caller needs them
        return f;
    }

    bool writeDuelObject(uint8_t *rdram, const Fields &f)
    {
        if (!rdram) return false;
        const uint32_t d = objAt(rdram, kDuelObjSlot);
        if (!d) return false;
        wr32(rdram, d + kOffVs, f.vs);
        wr32(rdram, d + kOffType, f.battleType);
        wr32(rdram, d + kOffDp, f.dpVariant);
        if (f.timeLimit != 0xFFFFFFFFu) wr32(rdram, d + kOffTime, f.timeLimit);
        return true;
    }

    bool writeCommitted(uint8_t *rdram, const Fields &f)
    {
        if (!rdram) return false;
        const uint32_t s = objAt(rdram, kStatePtrSlot);
        if (!s) return false;
        wr32(rdram, s + kOffCommitVs, f.vs);
        wr32(rdram, s + kOffCommitType, f.battleType);
        wr32(rdram, s + kOffCommitDp, f.dpVariant);
        return true;
    }

    bool apply(uint8_t *rdram, const Fields &f)
    {
        const bool a = writeDuelObject(rdram, f);
        const bool b = writeCommitted(rdram, f);
        return a || b;
    }

    Fields subtype(VsMode vs, BattleType type, DpVariant dp)
    {
        Fields f;
        f.vs = (uint32_t)vs;
        f.battleType = (uint32_t)type;
        f.dpVariant = (uint32_t)dp;
        return f;
    }

    void format(const Fields &f, char *out, unsigned n)
    {
        if (!out || !n) return;
        const char *v = (f.vs < 4u) ? kVsNames[f.vs] : "?";
        const char *t = (f.battleType < 3u) ? kTypeNames[f.battleType] : "?";
        const char *d = (f.dpVariant < 3u) ? kDpNames[f.dpVariant] : "?";
        char tb[24];
        if (f.timeLimit == 0xFFFFFFFFu) std::snprintf(tb, sizeof tb, "keep");
        else                            std::snprintf(tb, sizeof tb, "%u", f.timeLimit);
        std::snprintf(out, n, "%s %s dp=%s(%u) time=%s", v, t, d, f.dpVariant, tb);
    }
}
