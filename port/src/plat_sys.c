/*
 * Sony kernel, SIF and CD/DVD calls of the game on PC. There is no second processor, no modules to load and no
 * drive: these report success. (Files are served by plat_file.c.)
 */
#include <stdint.h>

int GetThreadId(void) { return 1; }
int ChangeThreadPriority(int tid, int prio) { (void)tid; (void)prio; return 0; }
void FlushCache(int mode) { (void)mode; }

void sceSifInitRpc(uint32_t mode) { (void)mode; }
int sceSifRebootIop(char *img) { (void)img; return 1; }
int sceSifSyncIop(void) { return 1; }
int sceSifLoadModule(char *name, int argLen, char *args) { (void)name; (void)argLen; (void)args; return 0; }
int sceFsReset(void) { return 0; }

int sceCdInit(int mode) { (void)mode; return 1; }
int sceCdMmode(int media) { (void)media; return 1; }

/* [beta8-fusion] Result UI costume for a fusion. The game writes 0 (UI costume 1); our
   mod reserves costume 3 (internal 2) for the fusion-of-fusions pair (52+53 -> 54/59) and
   for Goku (Fin) 3 + Gohan Definitivo 21 -> Vegetto 51. Source/partner are member chara
   ids; result is the BtlParam +0xB4 value. Keep this identical to the MIPS runtime rule. */
int Port_FusionCostume(int source, int partner, int result) {
    if (((source == 52 && partner == 53) || (source == 53 && partner == 52)) &&
        (result == 54 || result == 59))
        return 2;
    if (((source == 3 && partner == 21) || (source == 21 && partner == 3)) && result == 51)
        return 2;
    if (((source == 7 && partner == 122) || (source == 122 && partner == 7)) && result == 51)
        return 1;
    return 0;
}

/* [fight-60] Experimental fight cadence. 2 = the native 30 fps (two vblanks per fight frame);
   1 = one vblank, so the fight loop runs at 60. BT3_FIGHT_60=1 selects it. Timer scaling for
   the 60 case is added separately; without it the fight plays at double speed on purpose. */
int Port_FightVsyncs(void) {
    static int value = -1;
    if (value < 0) {
        const char *s = getenv("BT3_FIGHT_60");
        value = (s != NULL && s[0] != '0') ? 1 : 2;
    }
    return value;
}

/* func_002BB390: the library call whose result seeds libc rand at start-up (Rand_Init; a clock or time value on
   the PS2, not identified). Fixed here so that a run is repeatable; BT3_SEED overrides it. */
#include <stdlib.h>
long func_002BB390(void) {
    const char *s = getenv("BT3_SEED");
    return s != NULL ? strtol(s, NULL, 0) : 0x12345678;
}

/*
 * The game's C library random generator, as in the PS2 executable (rand 0x2A9C78, srand 0x2A9C60: newlib's
 * 64-bit generator). Part of the simulation state. include/port_libm.h renames the game's rand / srand to these.
 */
unsigned long long gPortRandNext = 1;
void Port_Srand(unsigned seed) { gPortRandNext = seed; }
int Port_Rand(void) {
    gPortRandNext = gPortRandNext * 6364136223846793005ull + 1;
    return (int)((gPortRandNext >> 32) & 0x7FFFFFFF);
}
