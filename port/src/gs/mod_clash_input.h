/* Our local struggle controls. Pure state machine; no game memory or RNG writes. */
#ifndef MOD_CLASH_INPUT_H
#define MOD_CLASH_INPUT_H
typedef struct { int action, dir, held, blocked; } ModClashInput;
static unsigned ModClash_Button(int action) {
    if (action >= 0x130 && action <= 0x132) return 0x8000; /* Square: beam */
    if (action == 0xFA) return 0x1000; /* Triangle: rush */
    return 0;
}
static int ModClash_Apply(ModClashInput *s, int action, unsigned *btn,
                          unsigned char *lx, unsigned char *ly) {
    unsigned bit = ModClash_Button(action);
    int down = bit != 0 && (*btn & bit) != 0;
    if (!bit) { s->action = -1; s->held = 0; s->dir = 0; s->blocked = 0; return 0; }
    if (s->action != action) { /* A held attack at admission must not score a free point. */
        s->action = action; s->held = down; s->dir = 0; s->blocked = down;
    }
    if (s->blocked) {
        *btn &= ~bit;
        if (!down) { s->blocked = 0; s->held = 0; }
        return 1;
    }
    if (down && !s->held) s->dir = (s->dir + 1) & 3;
    *btn &= ~bit; /* only the struggle button, and only while actually in a struggle */
    if (down) {
        switch (s->dir) {
        case 0: *ly = 0; break;
        case 1: *lx = 255; break;
        case 2: *ly = 255; break;
        default: *lx = 0; break;
        }
    }
    s->held = down;
    return 1;
}
#endif
