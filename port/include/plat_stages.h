/* Stages added from outside the disc: a manifest under the data root (<BT3_DATA>/stages/maps.txt), parsed once
   at start-up. Every line `<file>|<display name>` adds one map as the next stage id (PORT_STAGE_FIRST onward):
   the port serves its model (and a borrowed sound bank) for the game's file ids, and the menus list it. This
   replaces having to spell out BT3_EXTRA_STAGES / BT3_FILE_ALIAS by hand. */
#ifndef PORT_PLAT_STAGES_H
#define PORT_PLAT_STAGES_H

/* The disc's stages are 0x00..0x23 and, in the Beta8 image, 0x27..0x29; the added stages start after those,
   so that an added id never takes the place of one of the disc's own (0x27..0x29 are playable there). */
#define PORT_STAGE_FIRST 0x2A /* first added stage id: 0x2A..0x3F */
#define PORT_STAGE_MAX 22     /* ids 0x2A..0x3F (the stage-list bit field is 64 bits) */

void PortStages_Init(void); /* reads the manifest (or the BT3_EXTRA_STAGES fallback); once */
int PortStages_Count(void);
const char *PortStages_Name(int index); /* the display name of the index-th added map */
/* 1 if `id` is one of the stages added from outside the disc (not one of the disc's own, which in the Beta8
   image include 0x27..0x29). */
int Port_IsAddedStage(int id);
/* 1 if the file the game asks for (its path under the data root, e.g. "pzs3us1/00404.bin") is served from
   somewhere else; `out` gets the replacement path (under the data root, or absolute). */
int PortStages_Alias(const char *rel, char *out, unsigned n);

/* The game reads these (small data, a low address: see the note in menu_c_e.c). */
extern char gPortStageNames[PORT_STAGE_MAX][64];
extern int gPortExtraStageCount;
extern int gPortExtraStages[PORT_STAGE_MAX];

#endif
