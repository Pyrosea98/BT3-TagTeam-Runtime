/*
 * Markers for the PC renderer inside the game's display list. The game builds each frame's list first and the
 * renderer executes it afterwards, so "a native effect goes HERE" has to travel in the list: a one-register GIF
 * packet that writes the effect number to GS register 0x7F, which the real GS does not have. The front end
 * (port/src/gs/gs_core.c) hands it to the GPU back end; the software reference ignores it and keeps drawing the
 * PS2 passes. Called from the game's effect functions under `#ifdef PORT`.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern void Dma_AddData(void *src, int size);

/* Object/part identity must travel with the DMA list, not in a global set
 * while the game is merely constructing that list. Reserved PC register7E. */
void Port_GsBloodPart(int type,int actor,int node) {
    const char *enabled=getenv("BT3_BLOOD_SURFACE_REVIEW");uint32_t value=0;
    static uint32_t previous;
    uint32_t pkt[12]={0x10000002u,0,0x10000000u,0x50000002u,
                     0x8001u,0x10000000u,0xE,0,0,0x424C4F44u,0x7E,0};
    if(enabled&&strcmp(enabled,"1"))return;
    if(type==0&&actor>=0&&actor<16&&node>=0&&node<256)value=(actor+1)|((node+1)<<8);
    if(value==previous)return;previous=value;
    pkt[8]=value;Dma_AddData(pkt,sizeof(pkt));
}

extern void Dma_AddData(void *src, int size);

/* A HUD piece that is placed from the middle of the screen whatever side it would be on (the gauges of split
   screen's parts next to the middle, src/battle/hud_b.c): while this is set (an effect number, 0: none), a side
   panel's marker (left / right 0x10 / 0x11, the life bar's 0x15 / 0x16) given by the HUD code inside the piece is
   this one instead. */
static int sForce;
void Port_GsMarkerForce(int effect) {
    sForce = effect;
}

void Port_GsMarker(int effect) {
    if (sForce != 0 && (effect == 0x10 || effect == 0x11 || effect == 0x15 || effect == 0x16)) {
        effect = sForce;
    }
    {
    uint32_t pkt[12] = {
        0x10000000u | 2, 0, 0x10000000u, 0x50000000u | 2, /* DMA CNT, 2 quadwords; VIF FLUSHE, DIRECT 2 */
        0x8000u | 1, 0x10000000u, 0xE, 0,                  /* GIF tag: 1 loop, EOP, one register: A+D */
        (uint32_t)effect, 0, 0x7F, 0,                      /* value, register number */
    };

    Dma_AddData(pkt, sizeof(pkt));
    }
}
