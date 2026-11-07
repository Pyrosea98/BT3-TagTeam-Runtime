#ifndef BATTLE_EFT_OWNER_H
#define BATTLE_EFT_OWNER_H
/* Hit feedback occupies bits0..18. Extra-fighter ownership is an encoded field,
   never one bit per actor: 14 extras would overflow the 32-bit task tag. */
#ifdef PORT
#define EFT_OWNER_MASK 0x01f00000u
#define EFT_OWNER_TAG(n) ((n)==0 ? 0x800u : ((unsigned)(n)-2<14) ? ((unsigned)((n)-1)<<20) : 0x1000u)
#define EFT_OWNER_RESET(n) ((n)==0 ? 2 : ((unsigned)(n)-2<14) ? (n)+3 : 3)
#else
#define EFT_OWNER_TAG(n) ((n)==0 ? 0x800 : 0x1000)
#define EFT_OWNER_RESET(n) ((n)==0 ? 2 : 3)
#endif
#endif
