#ifndef PORT_COSTUME_NUMBERS_H
#define PORT_COSTUME_NUMBERS_H
/* Call on the costume movie AFTER timeline updates. The Beta8 number sheet
   already has all eight glyphs (2 columns x4 rows); added clips reuse1 until
   their on/off LEAVES receive their own cell. Leave stock four-plate movies alone. */
static void Port_CostumeNumberUvs(MFlash *movie) {
    s32 n, state;
    char plate[32];
    MFlashRef ref;
    for (n = 0; n < 8; n++) {
        MFlashUv uv = {0};
        sprintf(plate, "mc_color_plate_%d", n + 1);
        Flash_FindLabel(movie, NULL, plate, &ref);
        if (ref.id < 0) continue;
        uv.x0 = (n % 2) * 64; uv.y0 = (n / 2) * 32;
        uv.x1 = uv.x0 + 64; uv.y1 = uv.y0 + 32;
        for (state = 0; state < 2; state++) {
            Flash_FindLabel(movie, plate, state ? "mc_color_text_on" : "mc_color_text_off", &ref);
            if (ref.id >= 0) Flash_ClipSetUv(movie, &ref, &uv);
        }
    }
}
#endif
