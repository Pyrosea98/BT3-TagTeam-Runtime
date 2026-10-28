#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gs/mod_clash_input.h"

static void press_beam(ModClashInput *state, unsigned *buttons, unsigned char *lx, unsigned char *ly) {
    assert(ModClash_Apply(state, 0x130, buttons, lx, ly) == 1);
    assert((*buttons & 0x8000u) == 0);
}

static void release_beam(ModClashInput *state, unsigned *buttons, unsigned char *lx, unsigned char *ly) {
    *buttons &= ~0x8000u;
    assert(ModClash_Apply(state, 0x130, buttons, lx, ly) == 1);
}

int main(void) {
    ModClashInput state = { -1, 0, 0, 0 };
    unsigned buttons = 0;
    unsigned char lx = 128, ly = 128;

    /* A button held at clash admission is consumed but cannot award a free point. */
    buttons = 0x8000u;
    press_beam(&state, &buttons, &lx, &ly);
    assert(state.dir == 0 && state.blocked && state.held);
    release_beam(&state, &buttons, &lx, &ly);
    assert(!state.blocked && !state.held);

    /* Fresh presses, not holding, rotate through all four directions. */
    buttons = 0x8000u;
    press_beam(&state, &buttons, &lx, &ly);
    assert(state.dir == 1 && lx == 255);
    release_beam(&state, &buttons, &lx, &ly);
    buttons = 0x8000u;
    press_beam(&state, &buttons, &lx, &ly);
    assert(state.dir == 2 && ly == 255);
    release_beam(&state, &buttons, &lx, &ly);
    buttons = 0x8000u;
    press_beam(&state, &buttons, &lx, &ly);
    assert(state.dir == 3 && lx == 0);
    release_beam(&state, &buttons, &lx, &ly);
    buttons = 0x8000u;
    press_beam(&state, &buttons, &lx, &ly);
    assert(state.dir == 0 && ly == 0);

    /* Holding the same press cannot score again. */
    lx = ly = 128;
    assert(ModClash_Apply(&state, 0x130, &buttons, &lx, &ly) == 1);
    assert(state.dir == 0 && lx == 128 && ly == 128);
    release_beam(&state, &buttons, &lx, &ly);

    /* Rush uses Triangle, and changing action cannot inherit a held attack. */
    buttons = 0x1000u;
    assert(ModClash_Apply(&state, 0xFA, &buttons, &lx, &ly) == 1);
    assert((buttons & 0x1000u) == 0 && state.dir == 0 && state.blocked);
    buttons = 0x8000u;
    assert(ModClash_Apply(&state, 0x130, &buttons, &lx, &ly) == 1);
    assert(state.dir == 0 && state.blocked);

    /* Outside a clash the helper is inert and leaves the pad alone. */
    buttons = 0x8000u | 0x1000u;
    assert(ModClash_Apply(&state, 11, &buttons, &lx, &ly) == 0);
    assert(buttons == (0x8000u | 0x1000u));
    assert(state.action == -1 && !state.blocked && !state.held && state.dir == 0);

    /* Vanish QTE keeps ALL face buttons and the stick unchanged, even when
       the preceding struggle had a held Triangle or remapped direction. */
    for(int action=0xFB;action<=0xFC;action++)for(unsigned mask=0;mask<16;mask++){
        buttons=mask<<12;unsigned before=buttons;lx=37;ly=219;
        assert(ModClash_Apply(&state,action,&buttons,&lx,&ly)==0);
        assert(buttons==before&&lx==37&&ly==219);
    }
    puts("PASS clash input: admission held suppression, tap-only four-direction scoring, rush/beam buttons, action-change and idle isolation");
    return 0;
}
