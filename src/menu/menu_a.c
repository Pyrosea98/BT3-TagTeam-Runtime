#ifdef PORT
#include "coop_selection.h"
#endif
#include "common.h"
#include "menu/menu_a.h"
#ifdef PORT
extern void Port_NetMenuOpen(void);
extern void Port_ModMenuDrawScene(MFlash *flash);
extern int Port_ModMenuActive(void);
extern void Port_ModMenuReset(void);
extern void Port_ModMenuEnterMain(void);
extern int Port_ModMenuStep(unsigned pressed, unsigned repeat, int *players, int *sound);
extern int Port_NetMenuListed(void);
extern int Port_NetSession(void);
extern void Port_NetLeave(void);
extern int Port_ModesMenu(void); /* port/src/plat_modes.c: the extra modes, this menu's second set of entries */
extern void Port_ModesSheets(int off, int on);
extern void Port_ModesShow(const int *rows);
extern void Port_ModesCursor(int entry);
extern int Port_ModesChoose(int entry);
extern int Port_ModesLeave(void);
extern void Port_ModesShowPlayers(const int *rows);
extern int Port_ModesChoosePlayers(int entry, int n);
#include "battle/col_c.h" /* FontStyle / FONT_ALIGN_* / FONT_SHADOW_*, for the extra modes line in the message window */
#endif
#include "sys/pad.h"
#include "sys/save.h"

/* The screen's work (overlay .data, 0x3B0E80). */
MainMenu *gMainMenu = NULL;

/*
 * MainMenu: the main menu of the game (progress mode 4), 0x334C00..0x336A90. First object of the menu
 * overlay DBZP.BIN. A ring of up to 11 items, six plates visible, the cursor on rows 0..3; four guide
 * characters (Gohan, Videl, Trunks, Goten) of which one talks; the seven dragon balls of the save.
 */

#define MAINMENU_VOICE_BASE 0x86F9

/* The item under the cursor. */
#define MAINMENU_CUR(m) ((m)->items[((m)->top + (m)->cursor + 1) % (m)->itemCount])
#ifdef PORT

/* The extra modes (fights with more than two fighters: Team Battle, Free-for-All, Training) are a second set of
   entries of this menu; SELECT switches to it and back. The set is this menu with a list of three: the plates,
   the cursor, the sounds and the movie are the menu's own. Each mode sits on the plate of one of the menu's
   entries, whose icon it shows, and the port draws the mode's name where that entry's name is
   (port/src/gs/menu_text.c). */
#define PORTMODES_NUM 3
static const s32 sPortModesItem[PORTMODES_NUM] = { 3, 2, 6 }; /* the plates of Duel, Dragon World Tour, Ultimate Training */
static char *const sPortModesHelp[PORTMODES_NUM] = {
    "Teams of up to five fighters,\nall in the fight at the same time.",
    "Fill both lists with fighters.\nEvery one fights for itself.",
    "Training, with the extra fighter\nset up in the settings (F1).",
};
/* After Team Battle or Free-for-All, the same menu lists the number of players (asked for by the user: "1 Player,
   2 Players and so on"): 1 against the computer, 2 the game's two-player split screen, 3 and 4 that with players
   3 and 4 on the teams' second members in quarters (port/src/plat_fighters.c, Local players). Four rows: the
   fourth row's plate is shown for it. */
#define PORTPLAYERS_NUM 4
static const s32 sPortPlayersItem[PORTPLAYERS_NUM] = { 3, 2, 6, 1 };
static char *const sPortPlayersHelp[PORTPLAYERS_NUM] = {
    "One player against the computer.",
    "Two players, the screen split in two.",
    "Three players, the screen in quarters:\ncontroller 3 plays team 1's second fighter.",
    "Four players, the screen in quarters:\ncontrollers 3 and 4 play the second fighters.",
};
static s32 sPortModesPick; /* the mode the number of players is asked for (0 Team Battle, 1 Free-for-All) */
static s32 sPortModes; /* 0 the menu's own entries, 1 the extra modes, 2 its own again from this frame on, 3 the
                          number of players */
/* the menu's own list while the extra modes are showing */
static struct {
    s32 items[MAINMENU_ITEM_MAX];
    s32 itemCount;
    s32 top;
    s32 cursor;
    s32 extra;
} sPortModesOwn;
static s32 sPortModesDuel[2]; /* the versus menu's two answers as the player left them, while Team Battle or Free-for-All is on */
#endif

#ifdef PORT
/*
 * PC only: "Tenkaichi Budokai Online" (item 11), listed after Dragon Net Battle (after Duel when that one is
 * hidden). Its label is written into the free fourth row of the second label sheet of each plate state (label 11,
 * the item's own number), put together from the letters of the other labels of the player's disc
 * (port/src/gs/menu_text.c, Port_MainItemLabel); without them (another edition of the game) from port_tbo_label.h. Its
 * icon is Dragon Net Battle's. Choosing it opens the port's screen mode (menu/port_screen.c) with the screens of
 * menu/port_tbo.c. It has no voiced line of the guides.
 */
#include "port_tbo_label.h"

#define MAINMENU_TBO 11
#define MM_ICON(item) ((item) == MAINMENU_TBO ? 4 : (item)) /* whose icon an item shows */

/* A label sheet's pixels are stored the way the GS keeps 8-bit textures (in blocks): where pixel (x, y) is. */
static s32 PortMainMenu_PixelAt(s32 x, s32 y) {
    s32 block = (y & ~0xF) * 512 + (x & ~0xF) * 2;
    s32 swap = (((y + 2) >> 2) & 1) * 4;
    s32 line = (((y & ~3) >> 1) + (y & 1)) & 7;

    return block + line * 512 * 2 + ((x + swap) & 7) * 4 + ((y >> 1) & 1) + ((x >> 2) & 2);
}

/* Writes runs of palette indices into label row `row` of a sheet. */
static void PortMainMenu_WriteRuns(u8 *tex, s32 row, const PortLabelRun *run, s32 count) {
    u8 *px = *(u8 **)(tex + 0x38) + 0x60;
    s32 i;
    s32 k;

    /* The bundled fallback clears 256 pixels; ISO sheets are 512 wide.
       Clear the whole row so its old label cannot show through on the right. */
    for (i = 0; i < 0x20; i++) {
        for (k = 0; k < 512; k++) px[PortMainMenu_PixelAt(k, row * 0x20 + i)] = 0;
    }
    for (i = 0; i < count; i++) {
        for (k = 0; k < run[i].len; k++) {
            px[PortMainMenu_PixelAt(run[i].x + k, row * 0x20 + run[i].y)] = run[i].v;
        }
    }
}

/* The label in the disc's lettering into row `row` of a sheet; 0 when it could not be made. */
static s32 PortMainMenu_WriteDisc(u8 *tex, s32 row, s32 lit) {
    extern s32 Port_MainItemLabel(s32 entry, s32 lit, s32 out);
    static u8 index[512 * 0x20];
    u8 *px = *(u8 **)(tex + 0x38) + 0x60;
    s32 x;
    s32 y;

    if (!Port_MainItemLabel((s32)(u32)tex, lit, (s32)(u32)index)) {
        return 0;
    }
    for (y = 0; y < 0x20; y++) {
        for (x = 0; x < 512; x++) {
            px[PortMainMenu_PixelAt(x, row * 0x20 + y)] = index[y * 512 + x];
        }
    }
    return 1;
}
#define MAINMENU_VOICED(item) ((item) < MAINMENU_TBO)
#else
#define MM_ICON(item) (item)
#define MAINMENU_VOICED(item) 1
#endif

/* Starts the idle loop of the seven dragon balls (all seven collected). */
void MainMenu_StartBallLoops(void) {
    MFlashRef ref;
    char name[64];
    s32 i;
    MFlash *flash = &gMainMenu->flash[0];

    for (i = 0; i < 7; i++) {
        sprintf(name, "mc_ch_a_db_%d", i + 1);
        Flash_FindLabel(flash, NULL, name, &ref);
        Flash_ClipGotoLabel(flash, &ref, "fl_loop");
    }
}

#define MM_RES(n) \
    res = (MTexRes *)MPACK_AT(gMainMenu->res, n); \
    Res_RelocateOffsets(&res, res, res)

/* Loads and unpacks the screen (section `section` of archive 1), builds the item list and the windows. */
void MainMenu_Init(s32 section) {
    MTexRes *res = NULL;
    s32 i;

    if (gMenuArc1 == NULL) {
        gMenuArc1 = File_LoadSync(gProgress->baseFile + 1, NULL, 0);
    }
#ifdef PORT
    gMainMenu = Heap_Alloc(sizeof(MainMenu), 0x20, 0, 2); /* (its item list is longer on the PC: MAINMENU_ITEM_MAX) */
    memset(gMainMenu, 0, sizeof(MainMenu));
#else
    gMainMenu = Heap_Alloc(0x188, 0x20, 0, 2);
    memset(gMainMenu, 0, 0x188);
#endif
    gMainMenu->pack = (u32 *)MPACK_AT(gMenuArc1, section);
    gMainMenu->res = Sprite_Unpack(gMainMenu->pack, NULL, NULL);
    MM_RES(1);
    gMainMenu->bg = res;
    MM_RES(2);
    gMainMenu->tex[0] = MTEX(res, 0);
    gMainMenu->tex[1] = MTEX(res, 1);
    MM_RES(3);
    gMainMenu->tex[5] = MTEX(res, 0);
    gMainMenu->tex[4] = MTEX(res, 1);
    MM_RES(4);
    gMainMenu->tex[2] = MTEX(res, 0);
    gMainMenu->tex[3] = MTEX(res, 1);
    MM_RES(5);
    gMainMenu->tex[8] = MTEX(res, 0);
    MM_RES(6);
    gMainMenu->tex[12] = MTEX(res, 0);
    MM_RES(7);
    gMainMenu->tex[7] = MTEX(res, 0);
    gMainMenu->tex[11] = MTEX(res, 1);
    MM_RES(8);
    gMainMenu->tex[6] = MTEX(res, 0);
    gMainMenu->tex[10] = MTEX(res, 1);
    gMainMenu->tex[9] = MTEX(res, 2);
    MM_RES(9);
    gMainMenu->tex[41] = MTEX(res, 0);
    MM_RES(10);
    gMainMenu->tex[43] = MTEX(res, 0);
    MM_RES(11);
    gMainMenu->tex[42] = MTEX(res, 0);
    MM_RES(12);
    gMainMenu->tex[44] = MTEX(res, 0);
    gMainMenu->tex[45] = MTEX(res, 1);
    gMainMenu->tex[46] = MTEX(res, 2);
    gMainMenu->tex[47] = MTEX(res, 3);
    gMainMenu->tex[48] = MTEX(res, 4);
    gMainMenu->tex[49] = MTEX(res, 5);
    MM_RES(13);
    gMainMenu->tex[22] = MTEX(res, 0);
    MM_RES(26);
    gMainMenu->tex[27] = MTEX(res, 0);
    gMainMenu->tex[28] = MTEX(res, 1);
    gMainMenu->tex[29] = MTEX(res, 2);
    MM_RES(15);
    gMainMenu->tex[30] = MTEX(res, 0);
    gMainMenu->tex[13] = MTEX(res, 1);
    gMainMenu->tex[14] = MTEX(res, 2);
    gMainMenu->tex[15] = MTEX(res, 3);
    MM_RES(14);
    gMainMenu->tex[31] = MTEX(res, 0);
    gMainMenu->tex[32] = MTEX(res, 1);
    gMainMenu->tex[33] = MTEX(res, 2);
    MM_RES(24);
    gMainMenu->tex[38] = MTEX(res, 0);
    gMainMenu->tex[39] = MTEX(res, 1);
    gMainMenu->tex[40] = MTEX(res, 2);
    MM_RES(25);
    gMainMenu->tex[19] = MTEX(res, 0);
    gMainMenu->tex[20] = MTEX(res, 1);
    gMainMenu->tex[21] = MTEX(res, 2);
    gMainMenu->tex[16] = MTEX(res, 3);
    gMainMenu->tex[17] = MTEX(res, 4);
    gMainMenu->tex[18] = MTEX(res, 5);
    MM_RES(16);
    gMainMenu->tex[23] = MTEX(res, 0);
    gMainMenu->tex[24] = MTEX(res, 2);
    MM_RES(17);
    gMainMenu->tex[25] = MTEX(res, 0);
    gMainMenu->tex[26] = MTEX(res, 2);
    MM_RES(18);
    gMainMenu->tex[35] = MTEX(res, 0);
    gMainMenu->tex[34] = MTEX(res, 2);
    MM_RES(19);
    gMainMenu->tex[37] = MTEX(res, 0);
    gMainMenu->tex[36] = MTEX(res, 2);
    Flash_Create(&gMainMenu->flash[0], MPACK_AT(gMainMenu->res, 20), gMainMenu->tex);
    Flash_Play(&gMainMenu->flash[0], 1);
    MM_RES(23);
    IconWin_Init(MPACK_AT(gMainMenu->res, 22), res);
    IconWin_Open();
    gMainMenu->msgText = MPACK_AT(gMainMenu->res, 28);
    gMainMenu->subtitles = MPACK_AT(gMainMenu->res, 27);
    MsgWin_Init(MPACK_AT(gMainMenu->res, 21), gMainMenu->msgText, 1, 0);
    MsgWin_Open();

    for (i = 0; i < MAINMENU_ITEM_MAX; i++) {
#ifdef PORT
        /* Item 4, "Dragon Net Battle", is the one entry the PS2 game never lists: it has its plate, label, icon and
           the guides' voiced lines on the disc, and no mode behind it (the Wii version's online mode). The port
           lists it: it is the way into online play. BT3_MENU_ITEM4=0 hides it again (the pad recordings made
           before it was listed count on the old menu). */
        if (i == 4) { /* The separate TBO screen below is the single online entry. */
            continue;
        }
        if (i == MAINMENU_TBO) { /* (listed after Dragon Net Battle, below: not again at the end) */
            continue;
        }
#else
        if (i == 4) {
            continue;
        }
#endif
        if (i == 10 && (gSaveData->unlockFlags & 0x7F) != 0x7F) {
            continue;
        }
        gMainMenu->items[gMainMenu->itemCount++] = i;
#ifdef PORT
        if (i == 3) {
            gMainMenu->items[gMainMenu->itemCount++] = MAINMENU_TBO;
        }
#endif
    }
#ifdef PORT
    /* the second sheet of each state (slot 8 not chosen, 12 chosen) is the entry behind the first */
    if (!PortMainMenu_WriteDisc(gMainMenu->tex[8] + 0x40, 3, 0) || !PortMainMenu_WriteDisc(gMainMenu->tex[12] + 0x40, 3, 1)) {
        PortMainMenu_WriteRuns(gMainMenu->tex[8] + 0x40, 3, sPortTboOff, sizeof(sPortTboOff) / sizeof(sPortTboOff[0]));
        PortMainMenu_WriteRuns(gMainMenu->tex[12] + 0x40, 3, sPortTboOn, sizeof(sPortTboOn) / sizeof(sPortTboOn[0]));
    }
#endif
    gMainMenu->top = gMainMenu->itemCount - 1;
    for (i = 0; i < gMainMenu->itemCount; i++) {
        if (gMainMenu->items[i] == gProgress->mainMenuItem) {
            gMainMenu->top = i - 1;
            if (gMainMenu->top < 0) {
                gMainMenu->top += gMainMenu->itemCount;
            }
            break;
        }
    }
    gMainMenu->voiceLine = -1;
    for (i = 0; i < 4; i++) {
        gMainMenu->blink[i] = Rand_Range(0x20);
    }
#ifdef PORT
    /* the sheets with the first eight entries' names: tex[8] as the plates not under the cursor have them
       ("mc_menu_text_off"), tex[12] as the plate under it has ("mc_menu_text_on") */
    sPortModes = 0;
    Port_ModesSheets((s32)(u32)gMainMenu->tex[8], (s32)(u32)gMainMenu->tex[12]);
#endif
}

/* Frees the screen.
   The movies of a screen are an array (MainMenu_PlateGoto takes the index) of ONE, and Term / Update / Draw
   walk it with a loop. The loop compiles away, but it is what makes the original load the work pointer for the
   Flash call separately (`lw a0,%lo(gMainMenu)(s0)`) instead of sharing the address register with the code
   after it: without the loop the function does not match. */
void MainMenu_Term(void) {
    s32 i;

    MsgWin_Term();
    IconWin_Term();
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
        Flash_Destroy(&gMainMenu->flash[i]);
    }
    if (gMainMenu->res != NULL) {
        Heap_Free(gMainMenu->res);
        gMainMenu->res = NULL;
    }
    if (gMainMenu != NULL) {
        Heap_Free(gMainMenu);
        gMainMenu = NULL;
    }
}

#ifdef PORT
/* The extra modes' list is three long and sits on the first three rows. The plates around them (the one above,
   the fourth row, the one below, the one that scrolls in) and the two scroll arrows are taken out of the picture
   while it is up, and put back when the menu's own list is. */
static void MainMenu_PortModesPlates(s32 show) {
    static const s32 plates[4] = { 1, 5, 6, 7 };
    MFlashRef ref;
    char name[64];
    s32 i;
    MFlash *flash = &gMainMenu->flash[0];

    for (i = 0; i < 4; i++) {
        sprintf(name, "mc_menu_plate_%d", plates[i]);
        Flash_FindLabel(flash, NULL, name, &ref);
        Flash_ClipSetFlags(flash, &ref, 2, show || (sPortModes == 3 && plates[i] == 5)); /* (the fourth row: 4 Players) */
    }
    for (i = 0; i < 2; i++) {
        Flash_FindLabel(flash, NULL, i ? "mc_menu_yajirusi_down" : "mc_menu_yajirusi_up", &ref);
        Flash_ClipSetFlags(flash, &ref, 2, show);
    }
}

/* The message window's line while the extra modes are up: what the mode under the cursor is, printed with the
   game's font where the window has the guides' lines (which are not spoken then). */
static void MainMenu_PortModesHelp(void) {
    extern void Font_PrintAsciiAt(s32 x, s32 y, char *str);
    extern FontStyle gFontStyle;
    FontStyle saved = gFontStyle;

    gFontStyle.align = FONT_ALIGN_LEFT;
    gFontStyle.color = 0x80FFFFFF;
    gFontStyle.shadowMode = FONT_SHADOW_DROP;
    gFontStyle.shadowColor = 0x80000000;
    gFontStyle.shadowDx = 1;
    gFontStyle.shadowDy = 1;
    Font_PrintAsciiAt(25, 369, sPortModes == 3 ? sPortPlayersHelp[gMainMenu->cursor] : sPortModesHelp[gMainMenu->cursor]);
    gFontStyle = saved;
}

#endif
/* Draws the background, sets up every clip of the movie for this frame and draws it and the two windows. */
void MainMenu_Draw(void) {
    MFlashRef ref;
    MFlashUv uv;
    char name[64];
    MFlashRef eye;
    MFlashRef mouth;
    s32 i;
    s32 item;
    MFlash *flash;

    Sprite_DrawPicture(gMainMenu->bg, 0, 0, 0x80);
    flash = &gMainMenu->flash[0];
    for (i = 0; i < 4; i++) {
        switch (i) {
        case 0:
            Flash_FindLabel(flash, NULL, "mc_guide_gohan_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_gohan_mouth", &mouth);
            break;
        case 1:
            Flash_FindLabel(flash, NULL, "mc_guide_bidel_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_bidel_mouth", &mouth);
            break;
        case 3:
            Flash_FindLabel(flash, NULL, "mc_guide_torankusu_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_torankusu_mouth", &mouth);
            break;
        case 2:
            Flash_FindLabel(flash, NULL, "mc_guide_goten_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_goten_mouth", &mouth);
            break;
        }
        FlashAnim_Blink(flash, &eye, &gMainMenu->blink[i], 0);
        if (gMainMenu->guide == i) {
            FlashAnim_Talk(flash, &mouth, &gMainMenu->talk[i], 0);
        } else {
            FlashAnim_ShowNext2(flash, &mouth, 0);
        }
    }

    {
        /* columns of each item's animated icon sheet */
        s32 cols[MAINMENU_ITEM_MAX] = { 4, 4, 4, 5, 5, 5, 5, 4, 5, 5, 4 };

        for (i = 0; i < 6; i++) {
            item = gMainMenu->items[(gMainMenu->top + i) % gMainMenu->itemCount];
            uv.x0 = 0;
            uv.x1 = 0x200;
            uv.y0 = (item % 8) * 0x20;
            uv.y1 = uv.y0 + 0x20;
            sprintf(name, "mc_menu_plate_%d", i + 1);
            Flash_FindLabel(flash, name, "mc_menu_text_off", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, item / 8);
            Flash_FindLabel(flash, name, "mc_menu_text_on", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, item / 8);
            uv.x0 = (MM_ICON(item) % 4) * 0x40;
            uv.y0 = (MM_ICON(item) / 4) * 0x40;
            uv.x1 = uv.x0 + 0x40;
            uv.y1 = uv.y0 + 0x40;
            Flash_FindLabel(flash, name, "mc_icon_off", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            uv.x0 = 0;
            uv.y0 = (MM_ICON(item) % 10) * 0x30;
            uv.x1 = 0x30;
            uv.y1 = uv.y0 + 0x30;
            Flash_FindLabel(flash, name, "mc_icon_play", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, MM_ICON(item) / 10);
            if (item == MAINMENU_CUR(gMainMenu)) {
                FlashAnim_Sheet(flash, &ref, &gMainMenu->iconTimer, &gMainMenu->iconFrame, &uv,
                                cols[MM_ICON(MAINMENU_CUR(gMainMenu))], 1, MM_ICON(MAINMENU_CUR(gMainMenu)) == 3 ? 3 : 6);
            }
        }
    }

    {
        item = gMainMenu->items[gMainMenu->extra];
        uv.x1 = 0x200;
        uv.x0 = 0;
        uv.y0 = (item % 8) * 0x20;
        uv.y1 = uv.y0 + 0x20;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_menu_text_off", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, item / 8);
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_menu_text_on", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, item / 8);
        uv.x0 = (MM_ICON(item) % 4) * 0x40;
        uv.y0 = (MM_ICON(item) / 4) * 0x40;
        uv.x1 = uv.x0 + 0x40;
        uv.y1 = uv.y0 + 0x40;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_icon_off", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        uv.x0 = 0;
        uv.y0 = (MM_ICON(item) % 10) * 0x30;
        uv.x1 = 0x30;
        uv.y1 = uv.y0 + 0x30;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_icon_play", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, MM_ICON(item) / 10);
    }

    for (i = 0; i < 2; i++) {
        uv.x0 = i * 0x20;
        uv.y0 = 0x20;
        uv.x1 = uv.x0 + 0x20;
        uv.y1 = 0x40;
        Flash_FindLabel(flash, NULL, i ? "mc_menu_yajirusi_down" : "mc_menu_yajirusi_up", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
    }

    for (i = 0; i < 7; i++) {
        sprintf(name, "mc_ch_a_db_%d", i + 1);
        if (gSaveData->unlockFlags & (s32)(1U << i)) {
            Flash_FindLabel(flash, NULL, name, &ref);
            Flash_ClipSetFlags(flash, &ref, 2, 1);
            uv.x0 = (i % 4) * 0x40;
            uv.y0 = (i / 4) * 0x40;
            uv.x1 = uv.x0 + 0x40;
            uv.y1 = uv.y0 + 0x40;
            Flash_FindLabel(flash, name, "mc_dragonball", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
        } else {
            Flash_FindLabel(flash, NULL, name, &ref);
            Flash_ClipSetFlags(flash, &ref, 2, 0);
        }
    }

    for (i = 0; i < 2; i++) {
        uv.y0 = 0;
        uv.y1 = 0x100;
        uv.x0 = 0;
        uv.x1 = 0x200;
        Flash_FindLabel(flash, NULL, i ? "mc_bg_cloud" : "mc_bg_cloud_a", &ref);
        FlashAnim_Scroll(flash, &ref, &uv, &gMainMenu->cloud[i], NULL, i ? -0.18962963f : -0.35555556f, 0.0f);
    }

#ifdef PORT
    if (sPortModes != 0) {
        MainMenu_PortModesPlates(sPortModes == 2);
        if (sPortModes == 2) {
            sPortModes = 0;
        }
    }
#endif
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
#ifdef PORT
        if (Port_ModMenuActive()) Port_ModMenuDrawScene(&gMainMenu->flash[i]);
        else
#endif
        Flash_Draw(&gMainMenu->flash[i]);
    }
#ifdef PORT
    if (Port_ModMenuActive()) return;
#endif
    IconWin_Draw();
#ifdef PORT
    if ((sPortModes == 1 || sPortModes == 3) && (gMainMenu->flash[0].flags & MFLASH_PAD)) {
        MainMenu_PortModesHelp(); /* (drawn with the window's own text: MsgWin_Draw ends with the font's queue) */
    }
#endif
    MsgWin_Draw(0, 0, gMainMenu->voiceLine);
}

/* Sends the plate under the cursor of movie `idx` to a label. */
void MainMenu_PlateGoto(s32 idx, char *label) {
    MFlashRef ref;
    char name[64];
    MFlash *flash = &gMainMenu->flash[idx];

    sprintf(name, "mc_menu_plate_%d", gMainMenu->cursor + 2);
    Flash_FindLabel(flash, NULL, name, &ref);
    Flash_ClipGotoLabel(flash, &ref, label);
}

/* Per frame: when the guide's greeting has ended, lets a random guide read out the current item; steps the movie. */
void MainMenu_Update(void) {
    s32 i;

    if (gMainMenu->flags & MAINMENU_GREETED) {
        if (gMainMenu->voiceLine == gMainMenu->guide + 0xC || gMainMenu->voiceLine == gMainMenu->guide + 0x3C) {
            if (Voice_GetStat(0) == 5) {
                gMainMenu->guide = Rand_Range(4);
                if (MAINMENU_VOICED(MAINMENU_CUR(gMainMenu))) {
                    gMainMenu->voiceLine = MAINMENU_CUR(gMainMenu) * 4 + gMainMenu->guide + 0x10;
                    Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
                } else {
                    gMainMenu->voiceLine = -1;
                }
            }
        }
    }
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
        Flash_Advance(&gMainMenu->flash[i]);
    }
}

/* What a choice does: marks the screen as leaving, sets the next mode and plays the movie out. */
#define MAINMENU_CHOOSE(next) \
    gMainMenu->flags |= MAINMENU_CHOSEN; \
    gMainMenu->flags |= MAINMENU_LEAVING; \
    gMainMenu->timer = 15; \
    gProgress->mode = (next); \
    Flash_GotoLabel(&gMainMenu->flash[0], "fl_out", 1)

/* Counts the choices made on the main menu in the save, 0..23. */
#define MAINMENU_COUNT() \
    gSaveData->unkA0C++; \
    if (gSaveData->unkA0C >= 24) { \
        gSaveData->unkA0C = 0; \
    }

/* Pad 0: up / down move the cursor (the ring scrolls at the ends), confirm enters a mode, cancel leaves. */
void MainMenu_Input(s32 *result) {
    if (!(gMainMenu->flash[0].flags & MFLASH_PAD)) {
        return;
    }
    if (!(gMainMenu->flags & MAINMENU_STARTED)) {
        MainMenu_PlateGoto(0, "fl_on_start");
        if ((gSaveData->unlockFlags & 0x7F) == 0x7F) {
            MainMenu_StartBallLoops();
        }
        gMainMenu->flags |= MAINMENU_STARTED;
    }
    if (gPad[0].gameRepeat & 8) {
        MainMenu_PlateGoto(0, "fl_off_start");
        gMainMenu->iconTimer = 0;
        gMainMenu->iconFrame = 0;
        gMainMenu->cursor--;
        if (gMainMenu->cursor < 0) {
            gMainMenu->cursor = 0;
            Flash_GotoLabel(&gMainMenu->flash[0], "fl_menu_down", 1);
            gMainMenu->top--;
            if (gMainMenu->top < 0) {
                gMainMenu->top += gMainMenu->itemCount;
            }
            gMainMenu->extra = (gMainMenu->top + 6) % gMainMenu->itemCount;
        }
        goto moved;
    } else if (gPad[0].gameRepeat & 4) {
        MainMenu_PlateGoto(0, "fl_off_start");
        gMainMenu->iconTimer = 0;
        gMainMenu->iconFrame = 0;
        gMainMenu->cursor++;
        if (gMainMenu->cursor >= MAINMENU_ROWS) {
            gMainMenu->cursor = MAINMENU_ROWS - 1;
            Flash_GotoLabel(&gMainMenu->flash[0], "fl_menu_up", 1);
            gMainMenu->extra = gMainMenu->top;
            gMainMenu->top++;
            if (gMainMenu->top >= gMainMenu->itemCount) {
                gMainMenu->top -= gMainMenu->itemCount;
            }
        }
    moved:
        MainMenu_PlateGoto(0, "fl_on_start");
        gMainMenu->guide = Rand_Range(4);
        if (MAINMENU_VOICED(MAINMENU_CUR(gMainMenu))) {
            gMainMenu->voiceLine = MAINMENU_CUR(gMainMenu) * 4 + gMainMenu->guide + 0x10;
            Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
        } else {
            gMainMenu->voiceLine = -1;
        }
        Snd_PlaySe(1, 0);
    } else if (gPad[0].gamePressed & 0x200) {
        switch (MAINMENU_CUR(gMainMenu)) {
        case 0:
            MAINMENU_CHOOSE(6);
            MAINMENU_COUNT();
            break;
        case 1:
            MAINMENU_CHOOSE(13);
            MAINMENU_COUNT();
            break;
        case 2:
            MAINMENU_CHOOSE(33);
            MAINMENU_COUNT();
            break;
        case 3:
            MAINMENU_CHOOSE(38);
            MAINMENU_COUNT();
            break;
        case 4:
#ifdef PORT
            Port_NetMenuOpen(); /* the port's own screen for online play (port/src/gs/ui.cpp); the menu stays where it is */
#endif
            break;
        case 5:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(48);
            break;
        case 6:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(44);
            break;
        case 7:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(53);
            break;
        case 8:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(60);
            break;
        case 9:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(62);
            break;
        case 10:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(70);
            break;
#ifdef PORT
        case MAINMENU_TBO: /* "Tenkaichi Budokai Online": the port's screen mode runs its screens (menu/port_tbo.c) */
            gPortScreen = PORT_SCREEN_TBO;
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(MPROG_MODE_PORT_SCREEN);
            break;
#endif
        }
        MainMenu_PlateGoto(0, "fl_ok");
        Snd_PlaySe(1, 1);
    } else if (gPad[0].gamePressed & 0x400) {
        Flash_GotoLabel(&gMainMenu->flash[0], "fl_cancel", 1);
        IconWin_Close();
        MsgWin_Close();
        ColorFade_StartOut(0, 0, 0, 0x14);
        *result = 0;
        Snd_PlaySe(1, 2);
    }
}

#ifdef PORT
/* Switches the list: to the extra modes (`on`), or back to the menu's own entries where they were left. The plate
   under the cursor goes dark and the one under the new cursor lights up, as when the cursor moves. */
static void MainMenu_PortModesSwitch(s32 on) {
    extern void Voice_StopWithLip(void);
    s32 i;

    MainMenu_PlateGoto(0, "fl_off_start");
    gMainMenu->iconTimer = 0;
    gMainMenu->iconFrame = 0;
    if (on) {
        for (i = 0; i < MAINMENU_ITEM_MAX; i++) {
            sPortModesOwn.items[i] = gMainMenu->items[i];
        }
        sPortModesOwn.itemCount = gMainMenu->itemCount;
        sPortModesOwn.top = gMainMenu->top;
        sPortModesOwn.cursor = gMainMenu->cursor;
        sPortModesOwn.extra = gMainMenu->extra;
        for (i = 0; i < PORTMODES_NUM; i++) {
            gMainMenu->items[i] = sPortModesItem[i];
        }
        gMainMenu->itemCount = PORTMODES_NUM;
        gMainMenu->top = PORTMODES_NUM - 1; /* (the first of the list on the first row) */
        gMainMenu->cursor = 0;
        gMainMenu->extra = 0;
        /* the guides have no lines for these entries: the one being spoken stops, and its subtitle goes */
        Voice_StopWithLip();
        gMainMenu->voiceLine = -1;
        sPortModes = 1;
        Port_ModesShow(sPortModesItem);
        Snd_PlaySe(1, 1);
    } else {
        for (i = 0; i < MAINMENU_ITEM_MAX; i++) {
            gMainMenu->items[i] = sPortModesOwn.items[i];
        }
        gMainMenu->itemCount = sPortModesOwn.itemCount;
        gMainMenu->top = sPortModesOwn.top;
        gMainMenu->cursor = sPortModesOwn.cursor;
        gMainMenu->extra = sPortModesOwn.extra;
        /* a guide reads out the entry the cursor is back on, as after a move */
        gMainMenu->guide = Rand_Range(4);
        gMainMenu->voiceLine = MAINMENU_CUR(gMainMenu) * 4 + gMainMenu->guide + 0x10;
        Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
        sPortModes = 2;
        Port_ModesShow(NULL);
        Snd_PlaySe(1, 2);
    }
    MainMenu_PlateGoto(0, "fl_on_start");
}

/* Between the extra modes (players 0) and the number of players for mode entry `pick` (players 1). */
static void MainMenu_PortPlayersSwitch(s32 players, s32 pick) {
    s32 i;

    MainMenu_PlateGoto(0, "fl_off_start");
    gMainMenu->iconTimer = 0;
    gMainMenu->iconFrame = 0;
    if (players) {
        sPortModesPick = pick;
        for (i = 0; i < PORTPLAYERS_NUM; i++) {
            gMainMenu->items[i] = sPortPlayersItem[i];
        }
        gMainMenu->itemCount = PORTPLAYERS_NUM;
        gMainMenu->top = PORTPLAYERS_NUM - 1;
        gMainMenu->cursor = 0;
        sPortModes = 3;
        Port_ModesShowPlayers(sPortPlayersItem);
        Snd_PlaySe(1, 1);
    } else {
        for (i = 0; i < PORTMODES_NUM; i++) {
            gMainMenu->items[i] = sPortModesItem[i];
        }
        gMainMenu->itemCount = PORTMODES_NUM;
        gMainMenu->top = PORTMODES_NUM - 1;
        gMainMenu->cursor = sPortModesPick;
        sPortModes = 1;
        Port_ModesShow(sPortModesItem);
        Port_ModesCursor(sPortModesPick);
        Snd_PlaySe(1, 2);
    }

    MainMenu_PlateGoto(0, "fl_on_start");
}

/* The pad on the frames the menu takes input, before MainMenu_Input. SELECT switches to the extra modes; while
   they are up the pad is theirs: up / down move the cursor over the three (no scrolling), confirm chooses, cancel,
   Circle or SELECT goes back to the menu's own entries. A choice is made the way an entry's is (MAINMENU_CHOOSE), but for
   the character select behind the versus menu (40, the team select) or behind the training menu (45), and what
   that menu would have left in gProgress is set here. Returns 1 when this frame's input was taken. */
static s32 MainMenu_PortModes(void) {
    s32 next, players = 1, sound = -1, result, was;
    if (!(gMainMenu->flash[0].flags & MFLASH_PAD) || !(gMainMenu->flags & MAINMENU_STARTED)) return 0;
    if (Port_NetSession()) { Port_ModMenuReset(); return 0; }
    Port_ModesMenu(); /* renew hint; custom UI does not depend on relabelled sheets */
    was = Port_ModMenuActive();
    result = Port_ModMenuStep(gPad[0].gamePressed, gPad[0].gameRepeat, &players, &sound);
    if (sound >= 0) Snd_PlaySe(1, sound);
    if (!was && Port_ModMenuActive()) {
        extern void Voice_StopWithLip(void);
        Voice_StopWithLip(); gMainMenu->voiceLine = -1;
    }
    if (result == 30) { MAINMENU_CHOOSE(33); return 1; }
    if (result > 0) {
        next = Port_ModesChoosePlayers(result - 1, players);
        if (next == 40) {
            sPortModesDuel[0] = *(s32 *)((u8 *)gProgress + 0x620);
            sPortModesDuel[1] = *(s32 *)((u8 *)gProgress + 0x624);
            *(s32 *)((u8 *)gProgress + 0x620) = Port_CoopActive() ? (Port_CoopLeader(0)>=0 && Port_CoopLeader(1)>=0 ? 1 : 0) : (players >= 2 ? 1 : 0);
            *(s32 *)((u8 *)gProgress + 0x624) = 1;
        } else if (next == 45) {
            *(s32 *)((u8 *)gProgress + 0x7D4) |= 0x10;
        }
        if (next > 0) { MAINMENU_CHOOSE(next); }
        return 1;
    }
    return was || Port_ModMenuActive() || result < 0;
}

#endif
/* The main menu's own frame loop. Returns 1 when a mode was chosen (gProgress->mode is set), 0 on cancel. */
s32 MainMenu_Run(s32 section) {
    s32 result = 1;

#ifdef PORT
    if (Port_NetSession()) {
        Port_NetLeave(); /* an online session lives in the versus mode: back at the main menu it is over (no return) */
    }
    /* a mode chosen from the extra modes ends here too: Duel is the game's own again, and its menu opens on what
       the player chose there last, not on the team battle the mode was entered with */
    if (Port_ModesLeave() == 40) {
        *(s32 *)((u8 *)gProgress + 0x620) = sPortModesDuel[0];
        *(s32 *)((u8 *)gProgress + 0x624) = sPortModesDuel[1];
    }
#endif
#ifdef PORT
    Port_ModMenuEnterMain();
#endif
    MainMenu_Init(section);
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        Gfx_BeginFrame();
        Pad_Update();
        Snd_Update();
        ColorFade_Update();
        if (!(gProgress->flags & MPROG_FREEZE)) {
            MainMenu_Update();
        }
        MainMenu_Draw();
        ColorFade_Draw();
        Gfx_EndFrame(1);
        Dma_Flush();
        File_Stub264D90();
        if (gProgress->flags & MPROG_FREEZE) {
            continue;
        }
        if (ColorFade_IsInDone()) {
            if (!(gMainMenu->flags & MAINMENU_GREETED) && (gMainMenu->flash[0].flags & MFLASH_PAD)) {
                gMainMenu->flags |= MAINMENU_GREETED;
                gMainMenu->guide = Rand_Range(4);
                if ((gSaveData->unlockFlags & 0x7F) == 0x7F) {
                    gMainMenu->voiceLine = gMainMenu->guide + 0x3C;
                } else {
                    gMainMenu->voiceLine = gMainMenu->guide + 0xC;
                }
                Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
            }
        }
        if (ColorFade_IsFadingOut()) {
            Voice_FadeOutStep(0);
            Bgm_FadeOutStep();
            continue;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        if (gMainMenu->flags & MAINMENU_LEAVING) {
            if (--gMainMenu->timer == -1) {
                ColorFade_StartOut(0, 0, 0, 0x14);
                gProgress->mainMenuItem = MAINMENU_CUR(gMainMenu);
            }
        } else {
#ifdef PORT
            if (MainMenu_PortModes()) {
                continue;
            }
#endif
            MainMenu_Input(&result);
        }
    }
    sceGsSyncPath(0, 0);
#ifdef PORT
    if (sPortModes == 1) {
        /* left through an extra mode: the cursor comes back where it was among the menu's own entries */
        gProgress->mainMenuItem = sPortModesOwn.items[(sPortModesOwn.top + sPortModesOwn.cursor + 1) % sPortModesOwn.itemCount];
        Port_ModesShow(NULL);
    }
    sPortModes = 0;
    Port_ModMenuReset();
#endif
    MainMenu_Term();
    Dma_ResetBuffers();
    return result;
}
