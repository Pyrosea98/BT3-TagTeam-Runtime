// The settings window of the PC build: Dear ImGui over the finished picture. F1 opens and closes it, or START + SELECT
// of a controller, or the key and button of its Controls list (gs_input.c: PortInput_Hotkey).
// Everything it shows lives elsewhere (ui.h): the renderer's settings in gs_gpu.c, the bindings in gs_input.c.
// It runs on the render thread, which owns the window and its events.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <atomic>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_opengl3.h"
#include "ui.h"
#include "namefont.h"
#include "../port_release.h"
#include "../plat_fighters.h"
#include "menu_text.h"
#include "../quit_dialog.h"
#include "net_chat.h"

static int sMeter = -1; // the meter (frame rate, connection): -1 not read yet
static bool meter_on(void);
static int sGL; // 1: the OpenGL back end draws the window (ImGui's OpenGL3 backend, no SDL GPU device)
static char sGpuName[256];

extern "C" {
extern volatile int gPortNetMenuRequest;
void Port_NetOptions(int rollback, int delay); // gs/net.c
void Port_NetRulesSet(int type, int dp, int time); // gs/net.c: the host's battle type, DP limit row, time limit
void Port_NetSpeedSet(int percent);                // ... and the match's speed, 100..200 (set before the rules)
int Port_NetSpeedStandard(void);                   // what a quick match runs at
void Port_NetNameSet(const char *name);         // gs/net.c: this player's name for the match
int Port_IsWide(void);                         // plat_stub.c: the picture is wider than 4:3
int Port_AspectMilli(void);
extern "C" int gPortTboTyping;                     // gs_input.c: a keypad of Tenkaichi Budokai Online takes text
extern "C" int PortInput_TboEvent(const SDL_Event *ev);
extern "C" void PortInput_TboClipboard(void);      // ... and its "Copy Code": onto the clipboard, from this thread
void Match_UpdateAsk(void);                    // net_match.c: asks GitHub for the newest release, once
int Match_UpdateLatest(void);                  // ... its number (0.1.33 is 133), 0 while unknown
int Port_Menus43(void);                       // plat_stub.c: the menus are shown at 4:3 in a wider picture
int Port_LateInput(void);                     // plat_stub.c: "Low input lag", the blanks are waited for before the pad read
void Port_LateInputSet(int on);
void Port_Menus43Set(int on);
int Port_HudLayout(void);                      // gs_draw.c: 2 = the HUD is about the middle of the screen
int GsDraw_HudShown(void);                     // ... and 1 while the game is drawing the HUD's side panels
const char *Port_NetName(int player);          // the name of the player of side 0 or 1 ("" outside a match)
int Port_HudSwap(void);                        // the two sides of the HUD are in each other's places on this copy
int Port_NetActive(void);
int Port_NetMe(void);                          // gs/net.c: this copy's side, 0 or 1
void *BtlChar_FindByObjId(int objId);          // the game: the fighter of a side, NULL outside a fight
int Port_NetStats(int *out);                   // gs/net.c: the meter's numbers of an online match
int Port_LobbyOther(void);
extern unsigned gPortLiveBlanks;               // plat_stub.c: vertical blanks the game has gone through
int Port_RollCan(void);                        // gs/state.c
const char *Port_FileRoot(void);               // plat_file.c: the game's data folder
unsigned GsGl_StripTexture(const void *rgba, int w, int h); // gs_gl.c
void GsGl_StripTextureFree(unsigned id);
const char *PortStages_Dir(void);                          // plat_stages.c
const char *PortStages_Name(int index);
int PortStages_Count(void);
const char *PortSongs_Dir(void);                           // plat_songs.c
const char *PortSongs_Name(int index);
int PortSongs_Count(void);
int Port_Setting(const char *name, int def);   // plat_settings.c
void Port_SettingSave(const char *name, int value);
void Port_SettingText(const char *name, const char *def, char *out, int size);
void Port_SettingTextSave(const char *name, const char *value);
void Port_SettingsWrite(void);
int Port_SettingsFlush(void);
int Port_ClashSquare(void);                    // gs_input.c: Square counts as a direction in clashes
void Port_ClashSquareSet(int on);
int Port_ClashSquareX(void);                   // what a press is worth, in quarters of a point (4, 6, 7, 8)
void Port_ClashSquareXSet(int quarters);
extern int gsInterp;                           // gs_draw.c: in-between pictures on (1) / off (0); -1 before the first frame
void Port_SettingsWrite(void);
int Port_SettingsFlush(void);
int Port_LobbyStart(int host, const char *address, int port); // gs/net.c
int Port_LobbyStartCode(int host, const char *code);         // the same with a room code (the matchmaking service)
int Port_LobbyStartQuick(int queue);                         // a quick match: 0 single, 1 team, 2..4 DP battle of 10 / 15 / 20
void Port_LobbyQueuesAsk(void);                              // asks the service how many wait in each queue
void Port_LobbyHelloAsk(void);                               // asks it whether this release is new enough
int Port_LobbyRelease(int *min, int *latest);                // this release's number; the oldest let in and the newest (0: not known)
int Port_LobbyQueueWaiting(int queue);                       // ... as last heard, -1 if not known
const char *Port_LobbyCode(void);                            // the host's room code, "" until it is known
const char *Port_LobbyError(void);                           // why the matchmaking failed (state -3)
int Port_LobbyRelayed(void);                                 // the other player was reached through the relay
int Port_LobbyPoll(void);
void Port_LobbyCancel(void);
void Port_LobbyLaunch(void);
}
static std::atomic<int> sStartupCreditsFrame{0};
extern "C" void Port_ModStartupLog(int section) {
    fprintf(stderr, "bt3: startup ISO card section=%d\n", section);
}
extern "C" void Port_StartupCreditsFrame(int frame) {
    if (frame == 1) fprintf(stderr, "bt3: startup native thank-you card begin\n");
    if (frame == 0) fprintf(stderr, "bt3: startup native thank-you card complete\n");
    sStartupCreditsFrame.store(frame, std::memory_order_release);
}
static bool sReady, sOpen;
static bool sNet;                              // the open window is the online screen (Dragon Net Battle), not the settings
static int sForceTab = -1;                     // BT3_UI_OPEN=<tab>: open at start on that tab (testing)
static int sCapKind, sCapPlayer, sCapAction;   // waiting for a key (1) or a controller button (2) to bind
static Uint64 sCapUntil;                       // ... until then: a controller has no button that is not a binding, so the wait ends by itself
static bool sQuit;                             // the question before the game closes is up
static bool sQuitNo;                           // ... and Esc answered it: no
static bool sPadHold;                          // the controller does not work the window: a binding is waited for, or its button is still down
static int sTab;                               // the settings tab that is open (the shoulder buttons go to its neighbours)
static bool sPadTab;                           // the shoulder buttons changed the tab this frame
static SDL_GPUDevice *sDevice;
// The online chat (net_chat.c): its key opens a line to type, Enter sends it, Esc puts it away.
static bool sChat;                             // a line is being typed
static bool sChatFocus;                        // ... and its field takes the keyboard next frame
static bool sChatKeyHeld;                      // ... and the key that opened it is still down (its letter is not typed)
static bool sChatSoon;                         // the last Enter came within a second of the line before: not sent
static char sChatBuf[CHAT_TEXT + 1];
static void chat_close(void);

/* Name strips (gamedata/stages/names.rgba and gamedata/songs/names.rgba): the names of the stages and tracks added
   from outside the disc, drawn by the port over the menu in the game's own lettering. Raw RGBA, a 12-byte header
   (w, h, count) and then `count` images of w x h stacked. */
static SDL_GPUTexture *sNameTex, *sSongTex;
static uint32_t sNameW, sNameH, sNameCount, sSongW, sSongH, sSongCount;

static uint8_t *read_strip(const char *path, size_t *bytes, uint32_t *w, uint32_t *h, uint32_t *count) {
    FILE *fp = fopen(path, "rb");
    uint32_t hdr[3];
    uint8_t *pix;

    if (fp == NULL) {
        return NULL;
    }
    if (fread(hdr, sizeof(hdr), 1, fp) != 1 || hdr[0] == 0 || hdr[1] == 0 || hdr[2] == 0) {
        fclose(fp);
        return NULL;
    }
    *w = hdr[0];
    *h = hdr[1];
    *count = hdr[2];
    *bytes = (size_t)hdr[0] * hdr[1] * hdr[2] * 4;
    pix = (uint8_t *)malloc(*bytes);
    if (pix == NULL || fread(pix, 1, *bytes, fp) != *bytes) {
        free(pix);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return pix;
}

static SDL_GPUTexture *make_strip(uint8_t *pix, size_t bytes, uint32_t w, uint32_t h, uint32_t count) {
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTransferBufferCreateInfo tbi;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_GPUTexture *tex;
    void *map;

    if (sGL) { // the OpenGL back end: a GL texture, its name carried in the pointer (ImGui's OpenGL3 backend takes it so)
        unsigned id = GsGl_StripTexture(pix, (int)w, (int)(h * count));
        free(pix);
        return (SDL_GPUTexture *)(intptr_t)id;
    }
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.width = w;
    ci.height = h * count;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tex = SDL_CreateGPUTexture(sDevice, &ci);
    if (tex == NULL) {
        free(pix);
        return NULL;
    }
    SDL_zero(tbi);
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = (Uint32)bytes;
    tb = SDL_CreateGPUTransferBuffer(sDevice, &tbi);
    map = SDL_MapGPUTransferBuffer(sDevice, tb, false);
    memcpy(map, pix, bytes);
    SDL_UnmapGPUTransferBuffer(sDevice, tb);
    free(pix);
    cmd = SDL_AcquireGPUCommandBuffer(sDevice);
    cp = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src);
    src.transfer_buffer = tb;
    src.pixels_per_row = w;
    src.rows_per_layer = h * count;
    SDL_zero(dst);
    dst.texture = tex;
    dst.w = w;
    dst.h = h * count;
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(sDevice, tb);
    return tex;
}

static SDL_GPUTexture *load_strip(const char *env, const char *rel, uint32_t *w, uint32_t *h, uint32_t *count,
                                  const char *what) {
    const char *paths[3];
    uint8_t *pix = NULL;
    size_t bytes = 0;
    SDL_GPUTexture *tex;
    uint32_t i;

    char inData[600];
    // in the stages / songs folder (plat_extras.c); the environment variable names another file
    snprintf(inData, sizeof(inData), "%s", rel);
    paths[0] = getenv(env);
    paths[1] = inData;
    paths[2] = NULL;
    for (i = 0; i < 3 && pix == NULL; i++) {
        if (paths[i] != NULL) {
            pix = read_strip(paths[i], &bytes, w, h, count);
        }
    }
    if (pix == NULL) {
        return NULL;
    }
    tex = make_strip(pix, bytes, *w, *h, *count);
    if (tex != NULL) {
        fprintf(stderr, "bt3: %s overlay: %u names, %ux%u each\n", what, *count, *w, *h);
    }
    return tex;
}

// ---- the side bars ----
// Where the picture is narrower than the window (a 4:3 picture, or "Menus in 4:3", in a wide window) the sides are
// black, or with "Side bars: Border" a panel with a border along the picture. Two pictures: the border (trim.png),
// kept in proportion at the picture's edge and as high as the picture, and the plain panel (fill.png), stretched
// over the rest, so a bar of any width looks right. Both are drawn as they are on the left and mirrored on the right.
// The program carries a gold set (port/assets/bars, art by the project's owner: 34 x 720 and 126 x 720). A player
// replaces it by putting a trim.png and / or a fill.png of any size into a folder "bars" next to the game.
#include "bars_png.h"
static SDL_GPUTexture *sBarTrim, *sBarFill;
static int sBars = -1; // 0 black, 1 the border, 2 the border with the fight panels; -1 not read yet
static int sBarsTried;
static int sChatSize = -1; // 0 = compact, 1 = normal, 2 = large; -1 = not read yet

static float sBarTrimShape = 34.0f / 720.0f; // the border's width over its height

static SDL_GPUTexture *bar_texture(const char *file, const unsigned char *png, size_t size, float *shape) {
    SDL_Surface *s = SDL_LoadPNG(file), *rgba; // the player's own picture, if there is one
    if (s != NULL) {
        fprintf(stderr, "bt3: side bars: %s, %d x %d\n", file, s->w, s->h);
    } else {
        s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    }
    uint8_t *pix;
    size_t bytes;
    if (s == NULL) {
        return NULL;
    }
    rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == NULL) {
        return NULL;
    }
    if (shape != NULL && rgba->h > 0) {
        *shape = (float)rgba->w / (float)rgba->h;
    }
    bytes = (size_t)rgba->w * rgba->h * 4;
    pix = (uint8_t *)malloc(bytes);
    for (int y = 0; pix != NULL && y < rgba->h; y++) {
        memcpy(pix + (size_t)y * rgba->w * 4, (const uint8_t *)rgba->pixels + (size_t)y * rgba->pitch, (size_t)rgba->w * 4);
    }
    SDL_GPUTexture *tex = pix != NULL ? make_strip(pix, bytes, (uint32_t)rgba->w, (uint32_t)rgba->h, 1) : NULL;
    SDL_DestroySurface(rgba);
    return tex;
}

static bool bars_on(void) {
    if (sBars < 0) {
        sBars = Port_Setting("side_bars", 0);
        sBars = sBars < 0 ? 0 : sBars > 2 ? 2 : sBars;
    }
    return sBars != 0 && gUiPresentX > 0 && gUiPresentW > 0 && gUiPresentH > 0;
}

// For the fight panels (fight_panels_ui.cpp): textures of their own and the display faces.
static ImFont *sFontBold;
static ImFont *sModBodyFont,*sModTitleFont;
SDL_GPUTexture *Ui_Texture(uint8_t *pix, uint32_t w, uint32_t h) {
    return make_strip(pix, (size_t)w * h * 4, w, h, 1);
}
void Ui_TextureFree(SDL_GPUTexture *tex) {
    if (tex == NULL) {
        return;
    }
    if (sGL) {
        GsGl_StripTextureFree((unsigned)(intptr_t)tex);
    } else {
        SDL_ReleaseGPUTexture(sDevice, tex);
    }
}
ImFont *Ui_FontBold(void) { return sFontBold; }
bool FightPanels_Draw(ImDrawList *dl, float lx, float rx, float y0, float y1, float bar, float trim);

// The fight panels' two faces ("Name font", "Form font" in the settings): fonts of the game's own list (fonts.tsv,
// as the Menu font and the Dialogue / UI font), checked as those are (menu_text.c) and saved by their id; "Default"
// is the first of the list that can be used. A choice is loaded into the font atlas between two frames (fonts_apply).
struct PanelFont {
    const char *setting;
    ImFont *font;
    int choice;  // in use (0: Default)
    int pending; // chosen, loaded before the next frame (-1: nothing)
};
static PanelFont sPanelFont[2] = {{"panel_name_font_id", NULL, 0, -1}, {"panel_form_font_id", NULL, 0, -1}};
ImFont *Ui_FontName(void) { return sPanelFont[0].font; }
ImFont *Ui_FontForm(void) { return sPanelFont[1].font; }

// A choice of the list as an ImGui font (NULL: none can be used).
static ImFont *panel_font_load(int choice) {
    char path[1400];
    ImGuiIO &io = ImGui::GetIO();
    for (int i = choice > 0 ? choice : 1; i < MenuText_FontCount(); i++) {
        if (MenuText_FontFile(i, path, sizeof(path))) {
            return io.Fonts->AddFontFromFileTTF(path, 48.0f);
        }
        if (choice > 0) {
            break;
        }
    }
    return NULL;
}

static void fonts_apply(void) {
    for (PanelFont &pf : sPanelFont) {
        if (pf.pending < 0) {
            continue;
        }
        ImFont *f = panel_font_load(pf.pending);
        if (f != NULL) {
            if (pf.font != NULL) {
                ImGui::GetIO().Fonts->RemoveFont(pf.font);
            }
            pf.font = f;
            pf.choice = pf.pending;
            Port_SettingSave(pf.setting, MenuText_FontId(pf.choice));
            Port_SettingsWrite();
        }
        pf.pending = -1;
    }
}

// A combo of the game's fonts; the choice is loaded before the next frame.
static void font_picker(const char *label, PanelFont &pf, const char *tip) {
    if (ImGui::BeginCombo(label, pf.choice > 0 ? MenuText_FontName(pf.choice) : "Default")) {
        if (ImGui::Selectable("Default", pf.choice == 0)) {
            pf.pending = 0;
        }
        for (int i = 1; i < MenuText_FontCount(); i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), pf.choice == i)) {
                pf.pending = i;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tip);
}

#include "mod_startup_ui.inc"

static void bars_draw(void) {
    if (!sBarsTried) {
        sBarsTried = 1;
        sBarTrim = bar_texture("bars/trim.png", kBarsTrimPng, sizeof(kBarsTrimPng), &sBarTrimShape);
        sBarFill = bar_texture("bars/fill.png", kBarsFillPng, sizeof(kBarsFillPng), NULL);
    }
    if (sBarTrim == NULL || sBarFill == NULL) {
        return;
    }
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    float y0 = (float)gUiPresentY, y1 = y0 + (float)gUiPresentH;
    float bar = (float)gUiPresentX; // the width of each side
    float trim = (float)gUiPresentH * sBarTrimShape;
    float u0 = 0.0f;
    if (trim > bar) { // a bar narrower than the border: the border's inner part
        u0 = 1.0f - bar / trim;
        trim = bar;
    }
    float lx = (float)gUiPresentX, rx = lx + (float)gUiPresentW; // the picture's two edges
    ImTextureRef tt((ImTextureID)(intptr_t)sBarTrim), ft((ImTextureID)(intptr_t)sBarFill);
    if (bar > trim) {
        dl->AddImage(ft, ImVec2(lx - bar, y0), ImVec2(lx - trim, y1), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
        dl->AddImage(ft, ImVec2(rx + trim, y0), ImVec2(rx + bar, y1), ImVec2(1.0f, 0.0f), ImVec2(0.0f, 1.0f));
        // In a fight: each side's fighter over the plain panel, with the border along the picture at half its size
        // (in proportion, twice down the height) so the fighter has more room (asked for).
        const int tiles = 2;
        float thin = trim / tiles;
        if (sBars == 2 && FightPanels_Draw(dl, lx, rx, y0, y1, bar, thin)) {
            float th = (y1 - y0) / tiles;
            for (int i = 0; i < tiles; i++) {
                float a = y0 + th * i, b = a + th;
                dl->AddImage(tt, ImVec2(lx - thin, a), ImVec2(lx, b), ImVec2(u0, 0.0f), ImVec2(1.0f, 1.0f));
                dl->AddImage(tt, ImVec2(rx, a), ImVec2(rx + thin, b), ImVec2(1.0f, 0.0f), ImVec2(u0, 1.0f));
            }
            return;
        }
    }
    dl->AddImage(tt, ImVec2(lx - trim, y0), ImVec2(lx, y1), ImVec2(u0, 0.0f), ImVec2(1.0f, 1.0f));
    dl->AddImage(tt, ImVec2(rx, y0), ImVec2(rx + trim, y1), ImVec2(1.0f, 0.0f), ImVec2(u0, 1.0f));
}

static void load_strips(void) {
    char path[600];
    // The names of added stages and songs are drawn by this overlay, over the menu: from a strip of pre-rendered
    // names if the install script made one (names.rgba in the folder), else as text (frame_build). Either way the
    // game leaves the name to the overlay, so the flags are set whenever the overlay exists.
    snprintf(path, sizeof(path), "%s/names.rgba", PortStages_Dir());
    sNameTex = PortStages_Dir()[0] != '\0' ? load_strip("BT3_STAGE_NAMES", path, &sNameW, &sNameH, &sNameCount, "stage-name") : NULL;
    snprintf(path, sizeof(path), "%s/names.rgba", PortSongs_Dir());
    sSongTex = PortSongs_Dir()[0] != '\0' ? load_strip("BT3_SONG_NAMES", path, &sSongW, &sSongH, &sSongCount, "song-name") : NULL;
    if (sNameTex == NULL) { sNameCount = 0; }
    if (sSongTex == NULL) { sSongCount = 0; }
    gUiNameReady = 1;
    gUiSongReady = 1;
}

static void style() {
    ImGuiStyle &st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 10.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.TabRounding = 6.0f;
    st.PopupRounding = 6.0f;
    st.WindowPadding = ImVec2(18.0f, 16.0f);
    st.FramePadding = ImVec2(10.0f, 6.0f);
    st.ItemSpacing = ImVec2(10.0f, 9.0f);
    st.WindowBorderSize = 0.0f;
    st.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    ImVec4 *c = st.Colors;
    const ImVec4 accent(1.0f, 0.76f, 0.25f, 1.0f), accentDim(0.79f, 0.32f, 0.57f, 0.52f);
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.10f, 0.16f, 0.88f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.36f, 0.12f, 0.30f, 0.95f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.32f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.29f, 0.38f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered] = accentDim;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Header] = ImVec4(0.79f, 0.32f, 0.57f, 0.30f);
    c[ImGuiCol_HeaderHovered] = accentDim;
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.20f, 1.0f);
    c[ImGuiCol_TabHovered] = accentDim;
    c[ImGuiCol_TabSelected] = ImVec4(0.96f, 0.55f, 0.13f, 0.80f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.14f, 0.98f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);
}

int Ui_Init(SDL_Window *window, SDL_GPUDevice *device, void *gl_context, const char *gpu_name) {
    static const char *fonts[] = { // a proportional system font if there is one; else the library's built-in font
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
    };
    ImGui_ImplSDLGPU3_InitInfo info;

    snprintf(sGpuName, sizeof(sGpuName), "%s", gpu_name && gpu_name[0] ? gpu_name : "Unavailable");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL; // no window-layout file next to the game
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad; // (a controller opens it too: START + SELECT)
    style();
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        FILE *fp = fopen(fonts[i], "rb");
        if (fp != NULL) {
            fclose(fp);
            io.Fonts->AddFontFromFileTTF(fonts[i], 18.0f);
            break;
        }
    }
    { // the fight panels' faces: the two chosen ones (sPanelFont), and the list's first where one cannot be had
        if (io.Fonts->Fonts.Size == 0) {
            io.Fonts->AddFontDefault(); // (the window's font stays the first)
        }
        sFontBold = panel_font_load(0);
        {char path[1400];FILE *fp;
         snprintf(path,sizeof(path),"%s/tagteam-ui/menu-body.ttf",Port_FileRoot());fp=fopen(path,"rb");if(fp){fclose(fp);sModBodyFont=io.Fonts->AddFontFromFileTTF(path,48.f);}
         snprintf(path,sizeof(path),"%s/tagteam-ui/menu-title.ttf",Port_FileRoot());fp=fopen(path,"rb");if(fp){fclose(fp);sModTitleFont=io.Fonts->AddFontFromFileTTF(path,64.f);}
        }

        for (PanelFont &pf : sPanelFont) {
            pf.choice = MenuText_FontChoice(Port_Setting(pf.setting, 0));
            pf.font = panel_font_load(pf.choice);
        }
    }
    if (device == NULL) { // the OpenGL back end (gs_gl.c), with its context current
        sGL = 1;
        if (!ImGui_ImplSDL3_InitForOpenGL(window, (SDL_GLContext)gl_context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) {
            return 0;
        }
    } else {
        if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
            return 0;
        }
        info.Device = device;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window);
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info)) {
            return 0;
        }
    }
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll, NULL, 0); // any connected controller works the window
    sDevice = device;
    sReady = true;
    load_strips(); /* the stage- and song-name overlays; the ready flags stay 0 if there is no strip */
    if (getenv("BT3_UI_OPEN") != NULL) {
        sForceTab = atoi(getenv("BT3_UI_OPEN"));
        if (sForceTab == 9) { // testing: the online screen
            sForceTab = -1;
            gPortNetMenuRequest = 1;
        }
        if (sForceTab == 8) { // testing: the exit question
            sForceTab = -1;
            sQuit = true;
            PortQuit_Request();
        }
        sOpen = !sQuit;
        gPortOverlayOpen = 1;
    }
    return 1;
}

// The game gets nothing from keyboard or controller while the window or the exit question is up; and the button
// that closed either is not the game's.
static void input_owner(void) {
    int up = sOpen || sQuit;
    if (gPortOverlayOpen && !up) {
        PortInput_Swallow();
    }
    gPortOverlayOpen = up;
}

static void set_open(bool open) {
    if (open && sChat) {
        chat_close();
    }
    sOpen = open;
    sNet = false;
    sCapKind = 0;
    gPortInputCapture = 0;
    input_owner();
}

static void capture(int kind, int player, int action) {
    sCapKind = kind; sCapPlayer = player; sCapAction = action; gPortInputCapture = 1;
    sCapUntil = SDL_GetTicks() + 5000;
    sPadHold = true;
}

// A controller button pressed in the window (not while it waits for a binding, a list is open or a field is edited).
static bool pad_pressed(ImGuiKey key) {
    return sCapKind == 0 && !sPadHold && !sQuit && !ImGui::IsAnyItemActive() &&
           !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && ImGui::IsKeyPressed(key, false);
}

extern "C" void PortA11y_Say(const char *text, int flags);
extern "C" const char *PortA11y_Label(const char *label);
static void say_quit_button(int which, int flags);

int Ui_QuitAsk(void) {
    if (!sReady) {
        return 0;
    }
    if (sQuit) { return 1; }
    if (sOpen) { set_open(false); }
    sQuit = true;
    PortQuit_Request();
    input_owner();
    PortA11y_Say(PortA11y_Label("Close the game? Progress that was not saved is lost."), 1);
    say_quit_button(1, 0);
    PortA11y_Say(PortA11y_Label("Left and right choose, Enter confirms, Escape goes back to the game"), 0);
    return 1;
}

// The screen reader (a11y.c): it cannot see this window's question, which is drawn and not one of the game's screens,
// so the question, the button under the keyboard's focus and the keys are said here. Nothing while it is off.
static void say_quit_button(int which, int flags) {
    char text[128], pos[32];
    snprintf(pos, sizeof(pos), PortA11y_Label(", %d of %d"), which + 1, 2);
    snprintf(text, sizeof(text), "%s%s", PortA11y_Label(which == 0 ? "Exit" : "Cancel"), pos);
    PortA11y_Say(text, flags);
}

static void build_quit(void) {
    static int said = -1; // the button last said: 0 Exit, 1 Cancel
    ImGuiIO &io = ImGui::GetIO();
    bool stay = sQuitNo;
    int focus = -1;

    sQuitNo = false;
    if (!ImGui::IsPopupOpen("Exit game")) {
        ImGui::OpenPopup("Exit game");
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Exit game", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        static const char *const kAsk = "Close the game? Progress that was not saved is lost.";
        // the two buttons under the middle of the question (the window is as wide as its text)
        float line = ImGui::CalcTextSize(kAsk).x, pair = 140.0f * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::TextUnformatted(kAsk);
        ImGui::Spacing();
        if (line > pair) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (line - pair) * 0.5f);
        }
        if (ImGui::Button("Exit", ImVec2(140.0f, 0.0f))) {
            exit(0);
        }
        focus = ImGui::IsItemFocused() ? 0 : focus;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            stay = true;
        }
        focus = ImGui::IsItemFocused() ? 1 : focus;
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus(); // (on Cancel: a second press of the same button does not close the game)
            PortA11y_Say(PortA11y_Label(kAsk), 1);
            say_quit_button(1, 0);
            PortA11y_Say(PortA11y_Label("Left and right choose, Enter confirms, Escape goes back to the game"), 0);
            said = 1;
        } else if (focus >= 0 && focus != said) {
            say_quit_button(focus, 1);
            said = focus;
        }
        if (stay) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (stay) {
        sQuit = false;
        PortQuit_Dismiss();
        input_owner();
        PortA11y_Say(PortA11y_Label("Back to the game"), 1);
    }
}

// Dragon Net Battle: the screen behind the main menu's entry. For now only its shape: what a player fills in to
// host or join. Nothing is sent anywhere yet.
extern "C" {
extern volatile int gPortNetMenuRequest;
extern volatile int gPortNetWindowClose; // gs/state.c
}

static void net_open(void) {
    set_open(true);
    sNet = true;
    Port_LobbyHelloAsk(); // (once per opening: is there a newer release, and is this one still let in)
    gPortInputCapture = 1; // the game takes no input at all while this is open (it sits on the main menu behind it)
}

// Quick match (the third tab of the online screen): what stands under the queues.
static bool sNetBusy;   // the lobby is waiting or connecting
static int sQueue = -1; // the queue chosen: 0 single, 1 team, 2..4 DP battle of 10 / 15 / 20 DP

static void quick_status(int state, int &lastState, const char *name) {
    // A search ends by itself after three minutes: a window left open does not go on asking the service for hours,
    // and the player is told that nobody was there instead of being left to wonder. One press searches again.
    static const Uint64 kSearchMs = 180000;
    static Uint64 since;
    if (state == 3 && since != 0 && SDL_GetTicks() - since > kSearchMs) {
        Port_LobbyCancel();
        lastState = -5;
        state = 0;
    }
    if (state == 3 || state == 1) {
        ImGui::TextUnformatted(state == 1 ? "Found a player. Connecting..." : "Searching for a player in this queue...");
        if (state == 3 && since != 0) {
            Uint64 left = kSearchMs - (SDL_GetTicks() - since);
            ImGui::TextDisabled("Searching for %d:%02d more. Leave the window open.", (int)(left / 60000), (int)(left / 1000 % 60));
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            Port_LobbyCancel();
            lastState = 0;
        }
        return;
    }
    if (lastState == -3) {
        ImGui::TextWrapped("%s", Port_LobbyError());
    } else if (lastState == -4) {
        ImGui::TextWrapped("The two games could not reach each other, directly or through the relay. Search again for another player.");
    } else if (lastState == -2) {
        ImGui::TextWrapped("The other player's game is a different version. Search again.");
    } else if (lastState == -5) {
        ImGui::TextWrapped("Nobody was searching this queue in the last three minutes. Search again, or try another queue.");
    }
    if (ImGui::Button(lastState == -5 ? "Search again" : "Search for a match", ImVec2(200.0f, 0.0f))) {
        since = SDL_GetTicks() + 1;
        // The same for everyone in a quick match, whoever turns out to be the host: rollback of up to 8 frames and
        // the input delay measured from the connection. (The Host tab's own choices are for matches with someone
        // you can agree them with; a stranger's saved ones were not to be inherited unseen.)
        Port_NetOptions(Port_RollCan() ? 8 : 0, -2);
        Port_NetSpeedSet(Port_NetSpeedStandard()); // (every quick match: the competitive standard)
        Port_NetRulesSet(sQueue == 0 ? 0 : sQueue == 1 ? 1 : 2, sQueue >= 2 ? sQueue - 2 : 0, 4);
        Port_NetNameSet(name);
        Port_SettingTextSave("net_name", name);
        Port_SettingSave("net_queue", sQueue);
        Port_SettingsWrite();
        lastState = 0;
        Port_LobbyStartQuick(sQueue);
    }
}

static void build_net(void) {
    static char name[17] = "Player", address[64] = "", port[8] = "7000";
    static int tab, roll = -1, delay = 1, battle = -1, timeLimit = 3, speed = -1;
    static bool useCode = true;       // host with a room code (no port forwarding) or on a port
    static char roomCode[8] = "";     // the code to join with
    static int lastState = 0;         // what the lobby said last (for its messages after a failure)
    ImGuiIO &io = ImGui::GetIO();
    bool open = true;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (pad_pressed(ImGuiKey_GamepadFaceRight)) { // Circle / B: out of the window, as its close button
        open = false;
    }
    if (ImGui::Begin("Dragon Net Battle", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        {
            int min = 0, latest = 0, mine = Port_LobbyRelease(&min, &latest);
            if (min > mine) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.35f, 1.0f));
                ImGui::TextWrapped("Update available. Online servers are unreachable until you update.");
                ImGui::PopStyleColor();
                ImGui::TextDisabled("You have 0.%d.%d; 0.%d.%d or newer is needed. github.com/z3xox/Tenkaichi3Decomp/releases", mine / 100, mine % 100, min / 100, min % 100);
                ImGui::Separator();
            } else if (latest > mine) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Update available: release 0.%d.%d.", latest / 100, latest % 100);
                ImGui::TextDisabled("github.com/z3xox/Tenkaichi3Decomp/releases");
                ImGui::Separator();
            }
        }
        ImGui::TextWrapped("Fight a player on another computer. One of you hosts, the other joins with the host's address.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(260.0f);
        {
            static bool read;
            if (!read) { // the name used last time
                read = true;
                Port_SettingText("net_name", "Player", name, sizeof(name));
            }
        }
        ImGui::InputText("Player name", name, sizeof(name));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Port_SettingTextSave("net_name", name);
            Port_SettingsWrite();
        }
        ImGui::Spacing();
        if (ImGui::BeginTabBar("net")) {
            if (ImGui::BeginTabItem("Host")) {
                static const char *const kRoll[] = {"Off (wait for each other)", "Up to 2 frames", "Up to 4 frames", "Up to 6 frames", "Up to 8 frames"};
                tab = 0;
                ImGui::Checkbox("Use a room code (no port forwarding needed)", &useCode);
                ImGui::SetItemTooltip("You get a short code to tell the other player, and the two games find each other through a\n"
                                      "small matchmaking service. Works with most home routers. Without it you host on a port,\n"
                                      "which must be reachable from the other player (port forwarding or a virtual network).");
                ImGui::BeginDisabled(useCode);
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputText("Port", port, sizeof(port), ImGuiInputTextFlags_CharsDecimal);
                ImGui::EndDisabled();
                if (roll < 0) { // the choices of last time (new names: the first ones' defaults were 4 frames and delay 1)
                    roll = Port_RollCan() ? Port_Setting("net_rollback2", 8) / 2 : 0;
                    delay = Port_Setting("net_delay2", -1) + 1; // 0 = automatic, else the delay + 1
                    if (roll < 0 || roll > 4) { roll = 4; }
                    if (delay < 0 || delay > 7) { delay = 0; }
                }
                ImGui::BeginDisabled(!Port_RollCan());
                ImGui::SetNextItemWidth(260.0f);
                ImGui::Combo("Rollback", &roll, kRoll, 5);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("With rollback your own moves come out at once and the game corrects itself when the\n"
                                      "other player's buttons arrive. More frames cope with a slower connection and cost\n"
                                      "more processor time. Off: both games wait for each other every frame.");
                }
                ImGui::SetNextItemWidth(260.0f);
                {
                    static const char *const kDelay[] = {"Automatic (from the connection)", "0 frames", "1 frame", "2 frames", "3 frames",
                                                         "4 frames", "5 frames", "6 frames"};
                    ImGui::Combo("Input delay", &delay, kDelay, 8);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long after a press your own move comes out. A slow connection needs more, or the game\n"
                                      "has to guess too far ahead and stutters. Automatic measures the connection when the match\n"
                                      "starts: 1 frame on a good one, more on a slow one.");
                }
                {
                    // The match's rules: the game's own versus menu is not shown online, so they are chosen here.
                    static const char *const kBattle[] = {"Single battle", "Team battle", "DP battle (10 DP)", "DP battle (15 DP)", "DP battle (20 DP)"};
                    static const char *const kTime[] = {"60 seconds", "90 seconds", "180 seconds", "240 seconds", "No limit"};
                    if (battle < 0) {
                        battle = Port_Setting("net_battle", 0);
                        timeLimit = Port_Setting("net_time", 3);
                        if (battle < 0 || battle > 4) { battle = 0; }
                        if (timeLimit < 0 || timeLimit > 4) { timeLimit = 3; }
                    }
                    ImGui::Spacing();
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::Combo("Battle", &battle, kBattle, 5);
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::Combo("Time limit", &timeLimit, kTime, 5);
                    {
                        if (speed < 0) {
                            speed = Port_Setting("net_speed_host", Port_NetSpeedStandard()) /* (a new name with 0.1.31: the standard went from 120 to 114) */;
                        }
                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderInt("Game speed", &speed, 100, 150, "%d%%", ImGuiSliderFlags_AlwaysClamp);
                        ImGui::SetItemTooltip("How fast the match runs, for both players; the sound keeps its pitch.\n"
                                              "100%% is the game's own speed. %d%% is the competitive standard, and what\n"
                                              "quick matches run at.\n"
                                              "With a player on 0.1.29 only 100%% and 120%% are possible, with an older\n"
                                              "release only 100%%.", Port_NetSpeedStandard());
                    }
                }
                ImGui::TextDisabled("The host's choices apply to both players.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Quick match")) {
                static const char *const kQueue[] = {"Single battle", "Team battle", "DP battle (10 DP)", "DP battle (15 DP)", "DP battle (20 DP)"};
                static Uint64 asked;
                bool busy = sNetBusy; // (a search is under way: the queue is not changed under it)
                tab = 2;
                if (sQueue < 0) {
                    sQueue = Port_Setting("net_queue", 0);
                    if (sQueue < 0 || sQueue > 4) { sQueue = 0; }
                }
                {
                    // How many wait: asked when the tab is opened and then every thirty seconds, but only while the
                    // player is doing something here (a window left open all day asks nothing after two minutes),
                    // and not during a search (the search hears from the service anyway).
                    static Uint64 active;
                    bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsAnyItemActive();
                    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !moved; k++) {
                        moved = ImGui::IsKeyDown((ImGuiKey)k);
                    }
                    if (moved || active == 0) {
                        active = SDL_GetTicks() + 1;
                    }
                    if (!sNetBusy && (asked == 0 || (SDL_GetTicks() - asked > 30000 && SDL_GetTicks() - active < 120000))) {
                        asked = SDL_GetTicks() + 1;
                        Port_LobbyQueuesAsk();
                    }
                }
                ImGui::TextWrapped("Be matched with whoever is searching the same queue. All quick matches have no time limit and run at %d%% speed.", Port_NetSpeedStandard());
                ImGui::BeginDisabled(busy);
                for (int i = 0; i < 5; i++) {
                    char label[80];
                    int n = Port_LobbyQueueWaiting(i);
                    if (n < 0) {
                        snprintf(label, sizeof(label), "%s", kQueue[i]);
                    } else {
                        snprintf(label, sizeof(label), "%s   (%d searching)###q%d", kQueue[i], n, i);
                    }
                    ImGui::RadioButton(label, &sQueue, i);
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("The two games connect to each other directly when they can, as in most online fighting");
                ImGui::TextDisabled("games: a player who inspects their own network traffic can see the other's address.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Join")) {
                tab = 1;
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputText("Room code", roomCode, sizeof(roomCode), ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
                ImGui::SameLine();
                if (ImGui::Button("Paste")) { // the letters and digits of what was copied, as capitals
                    char *clip = SDL_GetClipboardText();
                    size_t n = 0;
                    for (const char *c = clip; c != NULL && *c != '\0' && n + 1 < sizeof(roomCode); c++) {
                        if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) {
                            roomCode[n++] = (char)(*c >= 'a' ? *c - 32 : *c);
                        }
                    }
                    roomCode[n] = '\0';
                    SDL_free(clip);
                }
                ImGui::TextDisabled("or, without a code, the host's address and port:");
                ImGui::BeginDisabled(roomCode[0] != '\0');
                ImGui::SetNextItemWidth(260.0f);
                ImGui::InputText("Host's address", address, sizeof(address));
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputText("Port", port, sizeof(port), ImGuiInputTextFlags_CharsDecimal);
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing();
        {
            int state = Port_LobbyPoll();
            sNetBusy = state == 1 || state == 3;
            if (state == 2) {
                Port_LobbyLaunch(); // the other player is there: the game turns into the session at its next blank
                set_open(false);
                sNet = false;
            }
            bool byCode = tab == 0 ? useCode : roomCode[0] != '\0';
            if (state != 0) {
                lastState = state;
            }
            if (tab == 2) {
                quick_status(state, lastState, name);
            } else if (state == 3 || (state == 1 && byCode)) { // a room code: asking the service, then reaching the other player
                if (tab == 0 && Port_LobbyCode()[0] != '\0') {
                    ImGui::TextUnformatted("Room code:");
                    ImGui::SameLine();
                    ImGui::SetWindowFontScale(1.8f);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", Port_LobbyCode());
                    ImGui::SetWindowFontScale(1.0f);
                    {
                        static Uint64 copiedAt;
                        ImGui::SameLine();
                        if (ImGui::Button("Copy")) {
                            copiedAt = SDL_SetClipboardText(Port_LobbyCode()) ? SDL_GetTicks() : 0;
                        }
                        ImGui::SetItemTooltip("Copy the code, to paste it to the other player.");
                        if (copiedAt != 0 && SDL_GetTicks() - copiedAt < 2000) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("Copied");
                        }
                    }
                    ImGui::TextUnformatted(state == 3 ? "Tell the other player this code. Waiting for them to join..." : "The other player has joined. Connecting...");
                } else {
                    ImGui::TextUnformatted(state == 1 ? "Found the room. Connecting to the host..." : tab == 0 ? "Getting a room code..." : "Looking for the room...");
                }
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                    Port_LobbyCancel();
                    lastState = 0;
                }
            } else if (state == 1) {
                if (tab == 0) {
                    ImGui::Text("Waiting for the other player on port %s...", port);
                    if (Port_LobbyOther()) {
                        ImGui::TextWrapped("A player with a different version of the game is trying to join. Both need the same release.");
                    }
                } else {
                    ImGui::Text("Looking for %s...", address);
                }
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                    Port_LobbyCancel();
                }
            } else {
                if (lastState == -3) {
                    ImGui::TextWrapped("%s", Port_LobbyError());
                } else if (lastState == -4) {
                    ImGui::TextWrapped("The two games could not reach each other, directly or through the relay. "
                                       "Hosting on a port (with port forwarding or a virtual network such as Tailscale) still works.");
                }
                bool can = byCode ? (tab == 0 || strlen(roomCode) == 6) : atoi(port) > 0 && (tab == 0 || address[0] != '\0');
                ImGui::BeginDisabled(!can);
                if (ImGui::Button(tab == 0 ? "Host a match" : "Join the match", ImVec2(200.0f, 0.0f))) {
                    if (tab == 0 && roll >= 0) {
                        Port_NetOptions(roll * 2, delay == 0 ? -2 : delay - 1);
                        if (battle >= 0) {
                            Port_NetSpeedSet(speed);
                            Port_SettingSave("net_speed_host", speed);
                            Port_NetRulesSet(battle == 0 ? 0 : battle == 1 ? 1 : 2, battle >= 2 ? battle - 2 : 0, timeLimit);
                            Port_SettingSave("net_battle", battle);
                            Port_SettingSave("net_time", timeLimit);
                        }
                        Port_SettingSave("net_rollback2", roll * 2);
                        Port_SettingSave("net_delay2", delay - 1);
                        Port_SettingsWrite();
                    } else {
                        Port_NetOptions(-1, -1); // joining: the host's choices arrive with its answer
                    }
                    Port_NetNameSet(name);
                    Port_SettingTextSave("net_name", name);
                    Port_SettingsWrite();
                    lastState = 0;
                    if (byCode) {
                        Port_LobbyStartCode(tab == 0, roomCode);
                    } else {
                        Port_LobbyStart(tab == 0, address, atoi(port));
                    }
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Back", ImVec2(120.0f, 0.0f))) {
                    open = false;
                }
                if (state == -2) {
                    ImGui::TextWrapped("The host's game is a different version. Both players need the same release.");
                } else if (state < 0) {
                    ImGui::TextWrapped(tab == 0 ? "That port cannot be used (another program has it?)." : "That address is not known.");
                }
            }
        }
        ImGui::Spacing();
        ImGui::TextDisabled("When you are connected the game switches to the match: both players get the same roster,");
        ImGui::TextDisabled("with everything unlocked, and your own save is left alone. Leaving the match brings you");
        ImGui::TextDisabled("back to where you were. The host's controls also work the menus.");
    }
    ImGui::End();
    if (!open) {
        Port_LobbyCancel();
        set_open(false);
    }
}

void Ui_Toggle(void) {
    if (sReady && !sQuit) {
        set_open(!sOpen);
    }
}

static void bind(int value) {
    if (sCapAction == PORT_ACTIONS) { // the row "Open this window"
        if (sCapKind == 1) {
            *PortInput_OverlayKey() = value > 0 ? value : SDL_SCANCODE_F1; // (cleared = F1 again: a keyboard always has a way in)
        } else if (value < PORT_PAD_LT) { // (a button, not a trigger)
            *PortInput_OverlayPad() = value;
        }
    } else if (sCapAction == PORT_ACTIONS + 1) { // the row "Open online chat"
        if (sCapKind == 1) {
            *PortInput_ChatKey() = value > 0 ? value : SDL_SCANCODE_TAB;
        }
    } else if (sCapKind == 1) {
        PortInput_Keys(sCapPlayer)[sCapAction] = value;
    } else {
        PortInput_PadButtons(sCapPlayer)[sCapAction] = value;
    }
    PortInput_Save();
}

static bool chat_can_open(void) {
    return sReady && !sOpen && !sQuit && PortChat_Enabled() && Port_NetActive();
}

static void chat_open(void) {
    sChat = true;
    sChatFocus = true;
    sChatKeyHeld = true;
    sChatSoon = false;
    gPortChatTyping = 1;
    ImGui::GetIO().ClearInputKeys(); // (it saw none of the keys while the line was put away: no key is down for it)
}

static void chat_close(void) {
    sChat = false;
    gPortChatTyping = 0;
    PortInput_SwallowKeys(); // (Enter, which sent the line, is START: not the game's until it is let go)
}

// The chat while a line is typed: the keyboard is the line's; Esc puts it away. 1 = the event was used.
static int chat_event(const SDL_Event *ev) {
    int key = *PortInput_ChatKey();
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_ESCAPE) {
        chat_close();
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !sChatKeyHeld && !ev->key.repeat && key > 0 && (int)ev->key.scancode == key) {
        chat_close();
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_UP && (int)ev->key.scancode == key) {
        sChatKeyHeld = false;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT && sChatKeyHeld) { // the letter of the key that opened the line
        return 1;
    }
    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT || ev->type == SDL_EVENT_TEXT_EDITING ||
        ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL ||
        ev->type == SDL_EVENT_MOUSE_MOTION) {
        ImGui_ImplSDL3_ProcessEvent(ev);
    }
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
}

int Ui_Event(const SDL_Event *ev) {
    if (!sReady) {
        return 0;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED || ev->type == SDL_EVENT_GAMEPAD_REMOVED) { // (also while it is closed: its list of controllers)
        ImGui_ImplSDL3_ProcessEvent(ev);
        return 0;
    }
    if (!sOpen && !sQuit && !sChat && PortInput_TboEvent(ev)) { // typed on a keypad of Tenkaichi Budokai Online
        return 1;
    }
    if (sQuit && PortQuit_Active()) { // game dialog: keyboard and controller operate its native Yes / No plates
        if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
            SDL_Scancode key = ev->key.scancode;
            if (key == SDL_SCANCODE_ESCAPE) { PortQuit_Action(PORT_QUIT_CANCEL); }
            else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER || key == SDL_SCANCODE_SPACE) {
                PortQuit_Action(PORT_QUIT_CONFIRM);
            } else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT ||
                       key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN) {
                if (PortQuit_Ready()) { say_quit_button(1 - PortQuit_Cursor(), 1); }
                PortQuit_Action(PORT_QUIT_MOVE);
            }
        } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            SDL_GamepadButton button = (SDL_GamepadButton)ev->gbutton.button;
            if (button == SDL_GAMEPAD_BUTTON_SOUTH) { PortQuit_Action(PORT_QUIT_CONFIRM); }
            else if (button == SDL_GAMEPAD_BUTTON_EAST) { PortQuit_Action(PORT_QUIT_CANCEL); }
            else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ||
                     button == SDL_GAMEPAD_BUTTON_DPAD_UP || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
                if (PortQuit_Ready()) { say_quit_button(1 - PortQuit_Cursor(), 1); }
                PortQuit_Action(PORT_QUIT_MOVE);
            }
        }
        return 1;
    }
    if (sChat && !sOpen && !sQuit) {
        return chat_event(ev);
    }
    if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat && *PortInput_ChatKey() > 0 && (int)ev->key.scancode == *PortInput_ChatKey() &&
        chat_can_open()) {
        chat_open();
        return 1;
    }
    if (sQuit) { // fallback when the native dialog resource cannot be loaded
        if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_ESCAPE) {
            sQuitNo = sQuitNo || !ev->key.repeat; // (taken away where it is drawn)
            return 1;
        }
        ImGui_ImplSDL3_ProcessEvent(ev);
        return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
               ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
    }
    if (!sOpen) {
        return 0;
    }
    if (sCapKind != 0) { // the next key or button is the binding; Esc leaves it as it was, Delete clears it
        bool done = false;
        if (ev->type == SDL_EVENT_KEY_DOWN) {
            if (ev->key.scancode == SDL_SCANCODE_DELETE || ev->key.scancode == SDL_SCANCODE_BACKSPACE) {
                bind(sCapKind == 1 ? 0 : -1);
            } else if (ev->key.scancode != SDL_SCANCODE_ESCAPE && sCapKind == 1) {
                bind((int)ev->key.scancode);
            }
            done = ev->key.scancode == SDL_SCANCODE_ESCAPE || ev->key.scancode == SDL_SCANCODE_DELETE ||
                   ev->key.scancode == SDL_SCANCODE_BACKSPACE || sCapKind == 1;
        } else if (sCapKind == 2 && ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            bind((int)ev->gbutton.button);
            done = true;
        } else if (sCapKind == 2 && ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && ev->gaxis.value > 20000 &&
                   (ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
            bind(ev->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PORT_PAD_LT : PORT_PAD_RT);
            done = true;
        }
        if (done) {
            sCapKind = 0;
            gPortInputCapture = 0;
        }
        if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
            return 1;
        }
    } else if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.scancode == SDL_SCANCODE_ESCAPE) {
        set_open(false);
        return 1;
    }
    ImGui_ImplSDL3_ProcessEvent(ev);
    return ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
           ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL;
}

static const char *pad_source_name(int src) {
    static const char *names[] = {"A / Cross", "B / Circle", "X / Square", "Y / Triangle", "Back / Select", "Guide", "Start",
                                  "Left stick click", "Right stick click", "Left shoulder", "Right shoulder", "D-pad up",
                                  "D-pad down", "D-pad left", "D-pad right", "Misc 1", "Right paddle 1", "Left paddle 1",
                                  "Right paddle 2", "Left paddle 2", "Touchpad"};
    static char other[24];
    if (src == PORT_PAD_LT) { return "Left trigger"; }
    if (src == PORT_PAD_RT) { return "Right trigger"; }
    if (src < 0) { return "-"; }
    if (src < (int)(sizeof(names) / sizeof(names[0]))) { return names[src]; }
    snprintf(other, sizeof(other), "Button %d", src);
    return other;
}

static bool tab(const char *label, int index) {
    ImGuiTabItemFlags flags = sForceTab == index ? ImGuiTabItemFlags_SetSelected : 0;
    bool on = ImGui::BeginTabItem(label, NULL, flags);
    if (on && sForceTab < 0) {
        sTab = index;
    }
    return on;
}

#include "mod_settings_l10n.inc"
static void video_tab(PortVideo &v) {
    static const struct { const char *name; int milli; } aspects[] = {{"4:3 (original)", 1333}, {"16:10", 1600}, {"16:9", 1778}, {"21:9", 2389}, {"32:9", 3556}};
    char label[64];
    int best = 0, count = 0;

    snprintf(label, sizeof(label), "%dx  (%d x %d)", v.scale, 512 * v.scale, 448 * v.scale);
    if (ImGui::BeginCombo(mod_l10n("Internal resolution"), label)) {
        for (int n = 1; n <= 8; n++) {
            snprintf(label, sizeof(label), "%dx  (%d x %d)", n, 512 * n, 448 * n);
            if (ImGui::Selectable(label, n == v.scale)) { v.scale = n; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(mod_l10n("How finely the game is drawn. 1x is the PlayStation 2's own resolution.\nHigher is sharper and uses more video memory."));
    for (int i = 1; i < (int)(sizeof(aspects) / sizeof(aspects[0])); i++) {
        if (abs(aspects[i].milli - v.aspectMilli) < abs(aspects[best].milli - v.aspectMilli)) { best = i; }
    }
    if (ImGui::BeginCombo(mod_l10n("Aspect ratio"), aspects[best].name)) {
        for (int i = 0; i < (int)(sizeof(aspects) / sizeof(aspects[0])); i++) {
            if (ImGui::Selectable(aspects[i].name, i == best)) { v.aspectMilli = aspects[i].milli; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(mod_l10n("Wider than 4:3 shows more of the fight to the sides. Menus are stretched."));
    if (ImGui::BeginCombo(mod_l10n("2D filtering"), v.smooth2d ? mod_l10n("Smooth") : mod_l10n("Sharp (console)"))) {
        if (ImGui::Selectable(mod_l10n("Sharp (console)"), !v.smooth2d)) { v.smooth2d = 0; }
        if (ImGui::Selectable(mod_l10n("Smooth"), v.smooth2d != 0)) { v.smooth2d = 1; }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(mod_l10n("How the display of a fight, the menus and text are drawn above 1x internal resolution.\nSharp: each of the PlayStation 2's pixels is a block. Smooth: as an emulator shows them.\nNo difference at 1x."));
    bool full = v.fullscreen != 0;
    if (ImGui::Checkbox(mod_l10n("Full screen"), &full)) { v.fullscreen = full; }
    ImGui::SameLine();
    ImGui::TextDisabled("(F11)");
    {
        bool vs = v.vsync != 0, fs = v.frameSkip != 0;
        if (ImGui::Checkbox("V-sync", &vs)) { v.vsync = vs; }
        ImGui::SetItemTooltip("Each picture waits for the display's refresh. Turn it on if the picture tears.\n"
                              "Off is the default: it adds a little delay.");
        if (ImGui::Checkbox(mod_l10n("Skip pictures instead of slowing down"), &fs)) { v.frameSkip = fs; }
        ImGui::SetItemTooltip("When this machine cannot show a picture in time, the game normally runs slower.\n"
                              "With this on it stays at full speed and leaves pictures out instead (choppier,\n"
                              "not slower). It helps where the graphics chip is the slow part. On by default.");
        {
            bool late = Port_LateInput() != 0;
            if (ImGui::Checkbox(mod_l10n("Low input lag (experimental)"), &late)) { Port_LateInputSet(late); }
            ImGui::SetItemTooltip("The game reads the controller, computes, and then waits before the picture is shown:\n"
                                  "about 33 ms in a fight. With this on the wait comes first, so the picture follows\n"
                                  "your press by the few milliseconds the machine needs. The game itself is unchanged.\n"
                                  "Not used online or with in-between pictures. Off by default: on a machine that\n"
                                  "cannot keep up it may stutter where the original order would not.");
        }
    }
    SDL_DisplayID *displays = SDL_GetDisplays(&count);
    if (v.display > count) { v.display = 0; }
    snprintf(label, sizeof(label), "%s", v.display == 0 ? mod_l10n("Chosen by the desktop") : SDL_GetDisplayName(displays[v.display - 1]));
    if (ImGui::BeginCombo(mod_l10n("Display"), label)) {
        if (ImGui::Selectable(mod_l10n("Chosen by the desktop"), v.display == 0)) { v.display = 0; }
        for (int i = 0; i < count; i++) {
            snprintf(label, sizeof(label), "%d: %s", i + 1, SDL_GetDisplayName(displays[i]));
            if (ImGui::Selectable(label, v.display == i + 1)) { v.display = i + 1; }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(mod_l10n("Takes effect the next time the game starts."));
    SDL_free(displays);
    {   // which renderer draws the game: Vulkan (the default) or OpenGL 3.3, for machines without a working Vulkan
        static const char *const kApi[] = {"Vulkan", "OpenGL"};
        static int api = -1;
        if (api < 0) {
            api = Port_Setting("gpu_api", 0) == 1 ? 1 : 0;
        }
        if (ImGui::Combo(mod_l10n("Renderer"), &api, kApi, 2)) {
            Port_SettingSave("gpu_api", api);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Takes effect the next time the game starts. Both draw the same picture; Vulkan is faster.\n"
                              "If the chosen one cannot start, the game tries the other.");
        if (api != (sGL ? 1 : 0)) {
            ImGui::TextDisabled("Running now: %s. %s from the next start.", kApi[sGL ? 1 : 0], kApi[api]);
        }
        ImGui::TextWrapped(mod_l10n("Active GPU: %s"), sGpuName);
    }
    {
        // In-between pictures (gs_draw.c): the fight is computed 30 times a second, as on the console; with this
        // on one more picture is drawn between each two. The game itself, its replays and online play are the same.
        const char *kSmooth[] = { mod_l10n("Off (30, as the console)"), mod_l10n("60 pictures a second"), mod_l10n("120 pictures a second"), mod_l10n("240 pictures a second") };
        int smooth = gsInterp < 0 ? 0 : gsInterp > 3 ? 3 : gsInterp;
        ImGui::SetNextItemWidth(416.0f);
        if (ImGui::Combo(mod_l10n("Smooth motion"), &smooth, kSmooth, 4)) {
            gsInterp = smooth;
            Port_SettingSave("interp", gsInterp);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Experimental. The game computes a fight 30 times a second. With this, more pictures are\n"
                              "drawn between each two. The fight itself does not change, and neither does online play.\n"
                              "The newest picture reaches the screen later: 17 ms at 60, 25 ms at 120, 29 ms at 240.\n"
                              "Choose what your screen can show. F9 switches it off and on.");
    }
    {
        bool on = meter_on();
        if (ImGui::Checkbox(mod_l10n("Show frame rate, and the connection in an online match"), &on)) {
            sMeter = on;
            Port_SettingSave("meter", sMeter);
            Port_SettingsWrite();
        }
    }
    ImGui::Spacing();
    if (v.texPackCount > 0) {
        bool on = v.texPack != 0;
        snprintf(label, sizeof(label), "Texture pack (%d textures)", v.texPackCount);
        if (ImGui::Checkbox(label, &on)) { v.texPack = on; }
        ImGui::SetItemTooltip("Replacement textures from the folder \"textures\" next to the game.");
    } else {
        ImGui::BeginDisabled();
        bool off = false;
        ImGui::Checkbox(mod_l10n("Texture pack (none found)"), &off);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Put a pack's files (PCSX2 naming, .dds or .png) into a folder \"textures\" next to the game.");
    }
}

static void effects_tab(PortVideo &v) {
    static const struct { const char *name, *tip; } fx[6] = {
        {"Outline", "The black line around the fighters."},
        {"See-through tint", "The tint that shows a fighter behind scenery."},
        {"Depth tint", "The haze that colours distant things."},
        {"Glare and glow", "The bloom around bright things and the sky's glare."},
        {"Distance blur", "The soft focus on distant scenery."},
        {"Movement blur", "The blur of the scenery while the view turns or moves fast."},
    };
    for (int i = 0; i < 6; i++) {
        bool on = !((v.fxOff >> i) & 1);
        if (ImGui::Checkbox(fx[i].name, &on)) { v.fxOff = (v.fxOff & ~(1 << i)) | (on ? 0 : 1 << i); }
        ImGui::SetItemTooltip("%s", fx[i].tip);
    }
    ImGui::Spacing();
    ImGui::SliderInt(mod_l10n("Glow strength"), &v.glow, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip(mod_l10n("100%% is what the PlayStation 2 draws; the default here is 60%%."));
}

// ---- "an update is available" ----
// Once per run the newest published release's number is asked of GitHub (net_match.c), unless "Check for updates" is
// off. If it is newer than this program: a line over the picture for ten seconds, and a line with a button to the
// release page at the top of this window for the rest of the run. Nothing is downloaded by the game.
// Not when nobody is looking (the offscreen and dummy video drivers); BT3_UPDATE_CHECK=1 / 0 says otherwise, and
// BT3_UPDATE_LATEST=<number> is taken as the answer without asking (testing).
static int sUpdCheck = -1; // the setting; -1 not read yet
static bool sUpdAsked, sUpdTold;

static int update_latest(void) {
    const char *fake = SDL_getenv("BT3_UPDATE_LATEST");
    if (sUpdCheck < 0) {
        sUpdCheck = Port_Setting("update_check", 0) != 0;
        // This fork's installed assets are maintained separately from upstream engine releases.
        // Migrate the old enabled preference once; an explicit later opt-in remains respected.
        if (!Port_Setting("mod_update_notice_v1", 0)) {
            sUpdCheck = 0;
            Port_SettingSave("update_check", 0);
            Port_SettingSave("mod_update_notice_v1", 1);
            Port_SettingsWrite();
        }
    }
    if (fake != NULL) {
        return SDL_atoi(fake);
    }
    if (!sUpdAsked) {
        const char *drv = SDL_GetCurrentVideoDriver(), *env = SDL_getenv("BT3_UPDATE_CHECK");
        bool seen = drv == NULL || (SDL_strcmp(drv, "offscreen") != 0 && SDL_strcmp(drv, "dummy") != 0);
        sUpdAsked = true;
        if (env != NULL ? SDL_atoi(env) != 0 : (sUpdCheck != 0 && seen)) {
            Match_UpdateAsk();
        }
    }
    return sUpdCheck != 0 || SDL_getenv("BT3_UPDATE_CHECK") != NULL ? Match_UpdateLatest() : 0;
}

static void misc_tab(void) {
    mod_credits_settings();
    {
        bool chk = sUpdCheck != 0;
        if (ImGui::Checkbox(mod_l10n("Check for upstream engine updates"), &chk)) {
            sUpdCheck = chk;
            Port_SettingSave("update_check", sUpdCheck);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Once per start, ask GitHub for the number of the newest release and say so if it is newer\n"
                              "than this one. Nothing is downloaded: the settings window then has a button to the page.");
    }
    bool thanks = Port_Setting("show_credits_at_startup", 1) != 0;
    if (ImGui::Checkbox(mod_l10n("Show Tag Team thank-you card at startup"), &thanks)) {
        Port_SettingSave("show_credits_at_startup", thanks);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip(mod_l10n("Our automatic credit card after all of the ISO creator and publisher cards. Next startup."));
    int modLanguage = Port_Setting("mod_language", 1) == 1 ? 1 : 0;
    const char *modLanguages[] = {mod_l10n("English"), mod_l10n("Espanol")};
    if (ImGui::Combo(mod_l10n("Tag Team card language"), &modLanguage, modLanguages, 2)) {
        Port_SettingSave("mod_language", modLanguage);
        Port_SettingsWrite();
    }
    // the start of the game (PR #50): read by FirstRun_Main, so they count from the next start
    static int sSkipLogos = -1, sSkipIntro = -1, sSkipCopy = -1;
    if (sSkipLogos < 0) {
        sSkipLogos = Port_Setting("skip_logos", 0) != 0;
        sSkipIntro = Port_Setting("skip_intro", 0) != 0;
        sSkipCopy = Port_Setting("skip_copyright", 0) != 0;
    }
    bool logos = sSkipLogos != 0, intro = sSkipIntro != 0, copy = sSkipCopy != 0;
    ImGui::Spacing();
    if (ImGui::Checkbox(mod_l10n("Skip logos"), &logos)) {
        sSkipLogos = logos;
        Port_SettingSave("skip_logos", sSkipLogos);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip(mod_l10n("Start without the publisher and developer logos. From the next start of the game."));
    if (ImGui::Checkbox(mod_l10n("Skip intro video"), &intro)) {
        sSkipIntro = intro;
        Port_SettingSave("skip_intro", sSkipIntro);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip(mod_l10n("Start without the opening movie. From the next start of the game."));
    if (ImGui::Checkbox(mod_l10n("Skip copyright page"), &copy)) {
        sSkipCopy = copy;
        Port_SettingSave("skip_copyright", sSkipCopy);
        Port_SettingsWrite();
    }
    ImGui::SetItemTooltip(mod_l10n("The page of legal text shown for five seconds before the title screen, every time."));
    ImGui::Spacing();
    bool chat = PortChat_Enabled() != 0;
    if (ImGui::Checkbox(mod_l10n("Online chat"), &chat)) {
        PortChat_EnabledSet(chat);
    }
    ImGui::SetItemTooltip("Text with the other player of an online match. Tab opens a line (setting key_chat), Enter sends it,\n"
                          "Esc puts it away; while it is typed the keyboard does not play, a controller does. Off: no lines\n"
                          "are shown or sent. A player without it sees nothing.");
    if (chat) {
        if (sChatSize < 0) {
            sChatSize = Port_Setting("chat_size", 1);
            if (sChatSize < 0 || sChatSize > 2) sChatSize = 1;
        }
        const char *const kChatSizes[] = {mod_l10n("Compact"), "Normal", mod_l10n("Large")};
        int size = sChatSize;
        if (ImGui::Combo(mod_l10n("Chat size"), &size, kChatSizes, 3)) {
            sChatSize = size;
            Port_SettingSave("chat_size", sChatSize);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("How wide the online chat is and how many lines it shows (while typing / otherwise).\n"
                              "Compact: 4 / 2, narrower.\n"
                              "Normal: 6 / 4.\n"
                              "Large: 8 / 6, wider, for large screens.");
    }
}

static void ui_tab(PortVideo &v) {
    int language=Port_Setting("mod_language",1);const char *languages[]={mod_l10n("English"),mod_l10n("Espanol")};
    if(ImGui::Combo(mod_l10n("Mod menu language"),&language,languages,2)){Port_SettingSave("mod_language",language);Port_SettingsWrite();}
    if (ImGui::BeginCombo(mod_l10n("Menu font"), MenuText_FontName(v.menuFont))) {
        for (int i = 0; i < MenuText_FontCount(); i++) {
            if (!MenuText_FontAvailable(i, MENU_TEXT_MENU)) { continue; }
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), v.menuFont == i)) { v.menuFont = i; }
            if (v.menuFont == i) { ImGui::SetItemDefaultFocus(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Original uses the game's sprites. Other fonts replace matching menu labels immediately.\nBattle announcements have their own setting below.");
    if (ImGui::BeginCombo(mod_l10n("Battle font"), MenuText_FontName(v.battleFont))) {
        for (int i = 0; i < MenuText_FontCount(); i++) {
            if (!MenuText_FontAvailable(i, MENU_TEXT_BATTLE)) { continue; }
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), v.battleFont == i)) { v.battleFont = i; }
            if (v.battleFont == i) { ImGui::SetItemDefaultFocus(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Choose original battle announcements and HUD text, or any battle font listed in fonts.tsv.");
    if (ImGui::BeginCombo("Dialogue / UI font", MenuText_FontName(v.textFont))) {
        for (int i = 0; i < MenuText_FontCount(); i++) {
            if (!MenuText_FontAvailable(i, MENU_TEXT_DIALOGUE)) { continue; }
            ImGui::PushID(i);
            if (ImGui::Selectable(MenuText_FontName(i), v.textFont == i)) { v.textFont = i; }
            if (v.textFont == i) { ImGui::SetItemDefaultFocus(); }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Replace the shared font used by dialogue, subtitles and ordinary UI text.\n"
                          "Original keeps the game's glyphs. Text placement, colors, shadows and icons are preserved.");

    const char *const kHud[] = {mod_l10n("At the screen's edges"), mod_l10n("At the edges, long health bars"), mod_l10n("In the middle, as in 4:3")};
    ImGui::Combo(mod_l10n("Original HUD placement"), &v.hudLayout, kHud, 3);
    ImGui::SetItemTooltip("Where a fight's health bars and gauges sit in a picture wider than 4:3.\n"
                          "Long health bars: the bars reach from the edges to the timer.\n"
                          "In the middle: the original 4:3 arrangement, next to the timer.");
    {
        bool m43 = Port_Menus43() != 0;
        if (ImGui::Checkbox(mod_l10n("Menus in 4:3"), &m43)) {
            Port_Menus43Set(m43);
            Port_SettingSave("menus_43", m43);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("In a picture wider than 4:3, show the menus at their own shape with black at the sides\n"
                              "instead of stretched to the width. Fights keep the wide picture.");
    }
    {
        const char *const kBars[] = {mod_l10n("Black"), mod_l10n("Border"), mod_l10n("Fighter panels")};
        int b = sBars < 0 ? 0 : sBars;
        if (ImGui::Combo(mod_l10n("Side bars"), &b, kBars, 3)) {
            sBars = b;
            Port_SettingSave("side_bars", sBars);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("What fills the sides where the picture is narrower than the window:\n"
                              "a 4:3 picture, or the menus with \"Menus in 4:3\", in a wide window.\n"
                              "Fighter panels: the border, and in a fight each side's fighter with its name and form.\n"
                              "Your own: a folder \"bars\" next to the game with trim.png (the strip along the\n"
                              "picture, kept in proportion) and fill.png (stretched over the rest). Read at start.");
    }
    if (sBars == 2) { // the fighter panels' faces
        font_picker("Name font", sPanelFont[0], "The fighter's name down the panel. The game's fonts, as for the Menu font\n"
                                                "(a font of your own: add it to fonts/fonts.tsv). Default: the first of the list.");
        font_picker("Form font", sPanelFont[1], "The form under the portrait. The game's fonts, as for the Menu font.\n"
                                                "Default: the first of the list.");
    }
}

extern "C" int gPortMusicFollows; // snd_adx.c: 1 = looping music is sped up or slowed down with the game

static void audio_tab(PortVideo &v) {
    ImGui::SliderInt(mod_l10n("Music"), &v.music, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt(mod_l10n("Sound effects"), &v.effects, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip(mod_l10n("Hits, blasts, menu sounds. Takes effect with the next sound that starts."));
    ImGui::SliderInt(mod_l10n("Voices"), &v.voices, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip(mod_l10n("The fighters' voices in a fight and the spoken lines outside one."));
    ImGui::SliderInt(mod_l10n("Stage sounds"), &v.stage, 0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip(mod_l10n("A stage's background sound (wind, water) and its own effects."));
    {
        bool follows = gPortMusicFollows > 0;
        ImGui::Spacing();
        if (ImGui::Checkbox(mod_l10n("Music follows game speed"), &follows)) {
            gPortMusicFollows = follows;
            Port_SettingSave("music_follows_speed", follows);
        }
        ImGui::SetItemTooltip("When the game runs at another speed than 100%% (an online match, the Cheats tab):\n"
                              "off, the music plays as it is; on, it is sped up or slowed down with the game,\n"
                              "at its own pitch, as an emulator does. Spoken lines always follow the game.");
    }
}

// Cheats. "Unlock everything" is the game's own leftover debug function (Save_UnlockAll, which nothing in the game
// calls): the request is set here and carried out on the game's side at the next vertical blank (headless.c).
extern "C" {
extern volatile int gPortUnlockAll, gPortUnlockDone;
}

extern "C" int Port_GameSpeed(void);          // plat_stub.c: the player's own game speed, percent; never used online
extern "C" void Port_GameSpeedSet(int percent);
extern "C" void Port_AudioRefresh(void);

static void cheats_tab(void) {
    {
        int speed = Port_GameSpeed();
        if (ImGui::SliderInt(mod_l10n("Game speed"), &speed, 50, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            Port_GameSpeedSet(speed);
            Port_AudioRefresh();
        }
        ImGui::SetItemTooltip("How fast the game runs outside online play (the sound keeps its pitch). 100%% is the game's own;\n"
                              "114%% is what competitive play uses. An online match never uses this value:\n"
                              "its speed is the host's or the queue's.");
        ImGui::SameLine();
        if (ImGui::Button(mod_l10n("Reset"))) {
            Port_GameSpeedSet(100);
            Port_AudioRefresh();
        }
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::TextWrapped("Unlock everything: all characters, stages, music and items, and the largest amount of Zenni. "
                       "This is a debug function the game's developers left in.");
    ImGui::Spacing();
    ImGui::TextWrapped("It also empties your records list, and it becomes permanent the next time the game saves. "
                       "Use it after your save has been loaded (from the main menu on).");
    ImGui::Spacing();
    if (ImGui::Button(mod_l10n("Unlock everything..."))) {
        ImGui::OpenPopup("Unlock everything?");
    }
    if (gPortUnlockDone) {
        ImGui::SameLine();
        ImGui::TextUnformatted(mod_l10n("Done."));
    }
    if (ImGui::BeginPopupModal("Unlock everything?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(mod_l10n("Your records list will be emptied. This cannot be undone once the game saves."));
        ImGui::Spacing();
        if (ImGui::Button(mod_l10n("Unlock"), ImVec2(140.0f, 0.0f))) {
            gPortUnlockAll = 1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(mod_l10n("Cancel"), ImVec2(140.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The Experiments tab: things that are not finished, each off until switched on, none of them in online play.
// Extra fighters: a table with a row for each (one today; the settings carry an index, plat_fighters.c).
static void chara_label(char *out, size_t size, int chara) {
    const char *name = Port_CharaName(chara);
    if (name[0] != '\0') {
        snprintf(out, size, "%s  (%d)", name, chara);
    } else {
        snprintf(out, size, "Character %d", chara);
    }
}

static void experiments_tab(void) {
    const char *const kTeams[] = {mod_l10n("Team 1 (player 1's)"), mod_l10n("Team 2 (player 2's)"), mod_l10n("On its own")}; // (more teams: more rows here)
    static const char *const kLast[] = {NULL,
                                        "The last fight had it.",
                                        "The last fight was left as the game's own: only Duel and training fights get one.",
                                        "The last fight was left as the game's own: a side had more than one member.",
                                        "The last fight was left as the game's own: the picture was split for two players.",
                                        "The last fight was left as the game's own: it was a replay.",
                                        "The last fight was left as the game's own: it had a script.",
                                        "The last fight was left as the game's own: an online match never has one.",
                                        "The last fight was left as the game's own: the character's files were not found."};
    static int checked = -1, ok, age; // the character whose files were looked for, what came of it, frames since
    static char why[160], refused[320];
    int count = Port_ExtraCount(), chara = Port_ExtraChara(0), team = Port_ExtraTeam(0), level = Port_ExtraLevel(0);
    int wasCount = count, wasChara = chara, wasTeam = team, wasLevel = level, last = Port_ExtraLast();
    bool on = count > 0;
    char label[96];

    ImGui::TextWrapped(mod_l10n("Unfinished things to try. They change how the game plays, so none of them is ever used in an online match."));
    ImGui::Spacing();
    ImGui::SeparatorText(mod_l10n("Local players"));
    {
        const char *const kLocal[] = {mod_l10n("2 (the game's own)"), "3", "4"};
        int local = Port_LocalPlayersPreference() - 2;
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(mod_l10n("Players on this computer"), &local, kLocal, 3)) {
            Port_LocalPlayersSet(local + 2);
        }
        ImGui::TextWrapped("For a two-player Team Battle or Free-for-All of the modes page (SELECT on the main menu), with "
                           "two members or more a team: controller 3 plays team 1's second member, controller 4 team 2's. "
                           "The screen is split in four, one quarter a player.");
        bool own = Port_Setting("coop_independent_selection", 1) != 0;
        if (ImGui::Checkbox(mod_l10n("Each player selects their own fighter"), &own)) {
            Port_SettingSave("coop_independent_selection", own);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("The second member of each team uses controller 3 or 4 in the team select, instead of the "
                              "team leader's controller.");
        bool all = Port_Setting("all_controllers_character_select", 0) != 0;
        if (ImGui::Checkbox(mod_l10n("Allow all controllers in character selection"), &all)) {
            Port_SettingSave("all_controllers_character_select", all);
            Port_SettingsWrite();
        }
        ImGui::SetItemTooltip("Any connected controller can move the selector in a one-player character select. "
                              "Two-player selects keep their own controller per side.");
    }
    ImGui::Spacing();
    ImGui::SeparatorText(mod_l10n("Extra fighters"));
    if (ImGui::Checkbox(mod_l10n("Third fighter (experimental)"), &on)) {
        count = on ? 1 : 0;
    }
    ImGui::SetItemTooltip("A third fighter, played by the computer, joins the next fight that starts.\n"
                          "Only in a one against one fight of Duel (against the computer) or of training;\n"
                          "story, tournaments, team battles, two-player split screen and replays stay as they are.\n"
                          "It has no health bar yet, and a replay saved from such a fight will not play back right.");
    if (Port_NetActive()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Off while you are in an online match.");
    }
    ImGui::BeginDisabled(!on);
    if (ImGui::BeginTable("extra", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthStretch, 2.6f);
        ImGui::TableSetupColumn("Team", ImGuiTableColumnFlags_WidthStretch, 2.3f);
        ImGui::TableSetupColumn("Computer level", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::PushID(0);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("3");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        chara_label(label, sizeof(label), chara);
        if (ImGui::BeginCombo("##chara", label, ImGuiComboFlags_HeightLarge)) {
            for (int c = 0; c <= 160; c++) {
                chara_label(label, sizeof(label), c);
                if (ImGui::Selectable(label, c == chara)) {
                    // a character whose files the game data does not have is not taken: the fight could not load it
                    if (Port_ExtraCharaOk(c, why, sizeof(why))) {
                        chara = c;
                        refused[0] = '\0';
                    } else {
                        snprintf(refused, sizeof(refused), "%s was not taken: %s.", label, why);
                    }
                }
                if (c == chara) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        {
            int at = team < 0 ? 2 : team;
            if (ImGui::Combo("##team", &at, kTeams, 3)) {
                team = at == 2 ? -1 : at;
            }
        }
        ImGui::SetItemTooltip("Whose side it fights on. Fighters of one team do not hit each other.\n"
                              "On its own: against both players.");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##level", &level, 0, 29, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetItemTooltip("How well the computer plays it, 0 to 29.\n"
                              "The game's own five levels are 0, 6, 13, 21 and 29.");
        ImGui::PopID();
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    if (count != wasCount || chara != wasChara || team != wasTeam || level != wasLevel) {
        Port_ExtraSet(count, 0, chara, team, level);
    }
    if (checked != chara || ++age > 120) { // (again now and then: the data folder is the player's to change)
        checked = chara;
        age = 0;
        ok = Port_ExtraCharaOk(chara, why, sizeof(why));
    }
    if (refused[0] != '\0') {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", refused);
        ImGui::PopStyleColor();
    }
    if (on && !ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.4f, 1.0f));
        ImGui::TextWrapped("This character cannot be used: %s. Fights stay as the game's own until another is chosen.", why);
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::TextWrapped(mod_l10n("A change here is used by the next fight that starts; a fight that is on keeps what it started with."));
    // the main menu's extra modes: Training takes its extra fighter from the row above whatever the switch says
    ImGui::TextWrapped("SELECT on the game's main menu shows the extra modes. Team Battle and Free-for-All take their fighters "
                       "from the game's team select; Training adds the fighter set up here. This switch is for every Duel "
                       "and training fight.");
    if (Port_ModesFight() != PORT_MODE_NONE) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s from the main menu's extra modes is on, until you are back at the main menu.",
                           Port_ModesName(Port_ModesFight()));
        ImGui::PopStyleColor();
    }
    if (last > 0 && last < (int)(sizeof(kLast) / sizeof(kLast[0])) && (on || last == 1)) {
        ImGui::TextDisabled("%s", kLast[last]);
    }
}

// The Controls tab's part for the sticks: each stick of the player's controller as it is now (the dot), its deadzone
// (the shaded middle: a circle, or the game's own square), what the game is given for it (the ring, and in numbers),
// the triggers, and the settings for these and for vibration. For seeing a stick that drifts and choosing a deadzone
// that covers it.
static void sticks_panel(int player) {
    const char *const kStick[2] = {mod_l10n("Left stick"), mod_l10n("Right stick")}, *const kTrigger[2] = {"Left trigger", "Right trigger"};
    static const char *const kDir[5] = {"", "up", "down", "left", "right"};
    const float R = 42.0f, box = 2.0f * R + 8.0f;
    const ImU32 colZone = ImGui::GetColorU32(ImGuiCol_Text, 0.22f);
    const ImU32 colRaw = ImGui::GetColorU32(ImGuiCol_Text), colGame = ImGui::GetColorU32(ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    int *dead = PortInput_Deadzone(player), *vib = PortInput_Vibration(player);
    PortStickView view;
    char label[64];

    PortInput_StickView(player, &view);
    if (!ImGui::BeginTable("sticks", 2)) {
        return;
    }
    float top = 0.0f;
    for (int s = 0; s < 2; s++) {
        ImGui::TableNextColumn();
        if (s == 0) {
            top = ImGui::GetCursorScreenPos().y;
        } else { // (the second cell would start lower by the text offset of the first one's last line)
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
        }
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetCursorScreenPos(), c(at.x + box * 0.5f, at.y + box * 0.5f);
        ImGui::Dummy(ImVec2(box, box));
        dl->AddCircleFilled(c, R, ImGui::GetColorU32(ImGuiCol_FrameBg), 48);
        if (dead[s] < 0) { // the game's own: each axis by itself, 50 of 127.5
            float h = R * 50.0f / 127.5f;
            dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), colZone);
        } else if (dead[s] > 0) {
            dl->AddCircleFilled(c, R * (float)dead[s] / 100.0f, colZone, 48);
        }
        dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), ImGui::GetColorU32(ImGuiCol_Border));
        dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), ImGui::GetColorU32(ImGuiCol_Border));
        dl->AddCircle(c, R, ImGui::GetColorU32(ImGuiCol_Border), 48);
        if (view.connected) {
            dl->AddCircle(ImVec2(c.x + view.game[s][0] * R, c.y + view.game[s][1] * R), 5.5f, colGame, 16, 1.5f);
            dl->AddCircleFilled(ImVec2(c.x + view.raw[s][0] * R, c.y + view.raw[s][1] * R), 3.0f, colRaw, 12);
        }
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, top));
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding(); // (in both cells: the second one's lines start that much lower anyway)
        if (view.connected) {
            ImGui::Text(view.dir[s] != 0 ? "%s: %s" : "%s", kStick[s], kDir[view.dir[s]]);
            ImGui::Text("stick  %+.2f %+.2f", view.raw[s][0], view.raw[s][1]);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colGame), "game   %+.2f %+.2f", view.game[s][0], view.game[s][1]);
        } else {
            ImGui::TextUnformatted(kStick[s]);
            ImGui::TextDisabled("no controller");
            ImGui::TextDisabled(" ");
        }
        ImGui::SetNextItemWidth(100.0f);
        snprintf(label, sizeof(label), "Deadzone##%d", s);
        ImGui::SliderInt(label, &dead[s], -1, PORT_DEADZONE_MAX, dead[s] < 0 ? "Original" : "%d%%", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            PortInput_Save();
        }
        ImGui::SetItemTooltip("How far the stick has to be moved before the game is told anything (the shaded middle).\n"
                              "Original: the game's own, 39%% for left / right and for up / down separately, a square.\n"
                              "0%% to 90%%: a round one of that size instead; from its edge the value grows evenly\n"
                              "to full at full tilt, and the stick counts as a direction half way between the two.\n"
                              "The white dot is the stick, the ring what the game is given; after the stick's name,\n"
                              "the direction the game takes it for. Choose a size that covers where the dot rests\n"
                              "when the stick is let go.");
        ImGui::EndGroup();
    }
    for (int s = 0; s < 2; s++) {
        ImGui::TableNextColumn();
        snprintf(label, sizeof(label), "%d%%%s", (int)(view.trigger[s] * 100.0f + 0.5f), view.triggerHeld[s] ? " pressed" : "");
        ImGui::ProgressBar(view.trigger[s], ImVec2(144.0f, 0.0f), label);
        ImGui::SetItemTooltip("A trigger counts as pressed from %d%% on. The game is told pressed or not, nothing between.",
                              (PORT_TRIGGER_AT * 100 + 16383) / 32767);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(kTrigger[s]);
    }
    ImGui::EndTable();
    bool on = vib[0] != 0;
    if (ImGui::Checkbox(mod_l10n("Vibration"), &on)) {
        vib[0] = on;
        PortInput_Save();
    }
    ImGui::SetItemTooltip("The game's own vibration (hits, clashes, charging), on this player's controller.\n"
                          "The game's Options > Controller > Vibration has to be on as well: it is, unless you turned it off.\n"
                          "The game gives none in a replay or for a side the computer plays.");
    ImGui::BeginDisabled(!on);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt(mod_l10n("Strength"), &vib[1], 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        PortInput_Save();
    }
    ImGui::SameLine();
    if (ImGui::Button(mod_l10n("Test"))) {
        PortInput_VibrationTest(player);
    }
    ImGui::SetItemTooltip(mod_l10n("Half a second of both motors at this strength."));
    ImGui::EndDisabled();
    if (view.connected && !view.canVibrate) {
        ImGui::SameLine();
        ImGui::TextDisabled("(this controller has none)");
    }
}

static void controls_tab(void) {
    static int player;
    char label[96];
    int count = 0;
    bool es = Port_Setting("mod_language", 1) == 1;
    int keyboard = PortInput_KeyboardPlayer();
    const char *choices[2][6] = {{"Auto","P1","P2","P3","P4","Per-player bindings only"},{"Auto","P1","P2","P3","P4","Solo asignaciones individuales"}};
    if (ImGui::Combo(es ? "Teclado compartido para" : "Shared keyboard assigned to", &keyboard, choices[es], 6)) PortInput_SetKeyboardPlayer(keyboard);
    ImGui::SetItemTooltip(es ? "Auto: primer jugador activo sin mando. En seleccion sigue la ranura activa. Las teclas compartidas se configuran en P1; se conservan las asignaciones individuales." : "Auto: first active player without a controller. In character select, follows the active slot. Shared keys are configured under P1; custom per-player bindings are preserved.");
    int currentKeyboard = PortInput_KeyboardOwner();
    if (currentKeyboard >= 0) ImGui::TextDisabled(es ? "Teclado actual: P%d" : "Current keyboard player: P%d", currentKeyboard + 1);
    ImGui::Separator();

    ImGui::RadioButton(mod_l10n("Player 1"), &player, 0);
    ImGui::SameLine();
    ImGui::RadioButton(mod_l10n("Player 2"), &player, 1);
    ImGui::SameLine();
    ImGui::RadioButton(mod_l10n("Player 3"), &player, 2); // (players 3 and 4: four local players, Experiments tab)
    ImGui::SameLine();
    ImGui::RadioButton(mod_l10n("Player 4"), &player, 3);
    int *slot = PortInput_PadSlot(player), *keys = PortInput_Keys(player), *pads = PortInput_PadButtons(player);
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (*slot < 0) {
        snprintf(label, sizeof(label), mod_l10n("None"));
    } else if (*slot < count) {
        snprintf(label, sizeof(label), "%d: %s", *slot + 1, SDL_GetGamepadNameForID(ids[*slot]));
    } else {
        snprintf(label, sizeof(label), "Controller %d (not connected)", *slot + 1);
    }
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo(mod_l10n("Controller"), label)) {
        if (ImGui::Selectable(mod_l10n("None"), *slot < 0)) { *slot = -1; PortInput_Save(); }
        for (int i = 0; i < (count > 4 ? count : 4); i++) {
            if (i < count) {
                snprintf(label, sizeof(label), "%d: %s", i + 1, SDL_GetGamepadNameForID(ids[i]));
            } else {
                snprintf(label, sizeof(label), "Controller %d (not connected)", i + 1);
            }
            if (ImGui::Selectable(label, *slot == i)) { *slot = i; PortInput_Save(); }
        }
        ImGui::EndCombo();
    }
    SDL_free(ids);
    ImGui::SameLine();
    if (ImGui::Button(mod_l10n("Reset to defaults"))) {
        PortInput_ResetDefaults(player);
        PortInput_Save();
    }
    if (ImGui::CollapsingHeader("Sticks, triggers and vibration", ImGuiTreeNodeFlags_DefaultOpen)) {
        sticks_panel(player);
    }
    ImGui::TextDisabled("Choose a binding, then press the new key or button. Esc keeps it, Delete clears it;");
    ImGui::TextDisabled("with a controller, press nothing for five seconds to keep it.");
    // (never lower than a few rows: in a small window the tab scrolls as a whole instead)
    if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0.0f, ImGui::GetContentRegionAvail().y > 140.0f ? ImGui::GetContentRegionAvail().y : 140.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(mod_l10n("Action"));
        ImGui::TableSetupColumn(mod_l10n("Keyboard"));
        ImGui::TableSetupColumn(mod_l10n("Controller"));
        ImGui::TableHeadersRow();
        {   // what opens and closes this window (not a player's: one for the program)
            const int a = PORT_ACTIONS;
            bool waitKey = sCapKind == 1 && sCapAction == a, waitPad = sCapKind == 2 && sCapAction == a;
            int key = *PortInput_OverlayKey(), pad = *PortInput_OverlayPad();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mod_l10n("Open this window"));
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##k%d", waitKey ? "press a key..." : key > 0 ? SDL_GetScancodeName((SDL_Scancode)key) : "-", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(1, player, a);
            }
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##p%d", waitPad ? "press a button..." : pad >= 0 ? pad_source_name(pad) : "START + SELECT", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(2, player, a);
            }
        }
        {   // what opens the online chat (not a player's: one for the program)
            const int a = PORT_ACTIONS + 1;
            bool waitKey = sCapKind == 1 && sCapAction == a;
            int key = *PortInput_ChatKey();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(mod_l10n("Open online chat"));
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##k%d", waitKey ? "press a key..." : key > 0 ? SDL_GetScancodeName((SDL_Scancode)key) : "Tab", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(1, player, a);
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("-");
        }
        for (int a = 0; a < PORT_ACTIONS; a++) {
            bool waitKey = sCapKind == 1 && sCapPlayer == player && sCapAction == a;
            bool waitPad = sCapKind == 2 && sCapPlayer == player && sCapAction == a;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(PortInput_ActionLabel(a));
            ImGui::TableNextColumn();
            snprintf(label, sizeof(label), "%s##k%d", waitKey ? "press a key..." : keys[a] > 0 ? SDL_GetScancodeName((SDL_Scancode)keys[a]) : "-", a);
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                capture(1, player, a);
            }
            ImGui::TableNextColumn();
            if (a < PORT_PAD_BUTTONS) {
                snprintf(label, sizeof(label), "%s##p%d", waitPad ? "press a button..." : pad_source_name(pads[a]), a);
                if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) {
                    capture(2, player, a);
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(a < 20 ? mod_l10n("Left stick") : mod_l10n("Right stick"));
            }
        }
        ImGui::EndTable();
    }
}

#include "mod_menu_icons.inc"
#include "mod_legacy_font.inc"
static void mod_hud_settings(){
        int style=Port_Setting("mod_hud_style",1);
        const char *labels[]={mod_l10n("Original game HUD"),mod_l10n("Overhead rings"),mod_l10n("Overhead hexagons"),mod_l10n("Compact player / target panels"),mod_l10n("Rings + compact panels"),mod_l10n("Hexagons + compact panels")};
        if(ImGui::Combo(mod_l10n("Battle HUD"),&style,labels,6)){Port_SettingSave("mod_hud_style",style);Port_SettingsWrite();}
        bool layered=Port_Setting("hud_layered_gauges",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Layered health / segmented Ki"),&layered)){Port_SettingSave("hud_layered_gauges",layered);Port_SettingsWrite();}
        bool portraits=Port_Setting("show_hud_portraits",1)!=0;if(ImGui::Checkbox(mod_l10n("Original costume portraits"),&portraits)){Port_SettingSave("show_hud_portraits",portraits);Port_SettingsWrite();}
        bool names=Port_Setting("mod_hud_names",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Fighter names on HUD"),&names)){Port_SettingSave("mod_hud_names",names);Port_SettingsWrite();}
        bool sparks=Port_Setting("show_hud_sparking_effects",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Sparking glow and sparks"),&sparks)){Port_SettingSave("show_hud_sparking_effects",sparks);Port_SettingsWrite();}
        int opacity=Port_Setting("mod_hud_opacity_percent",100);
        if(ImGui::SliderInt(mod_l10n("Battle HUD opacity"),&opacity,0,100,"%d%%")){Port_SettingSave("mod_hud_opacity_percent",opacity);Port_SettingsWrite();}
        int backing=Port_Setting("mod_hud_background_percent",100);
        if(ImGui::SliderInt(mod_l10n("HUD background opacity"),&backing,0,100,"%d%%")){Port_SettingSave("mod_hud_background_percent",backing);Port_SettingsWrite();}
        int size=Port_Setting("mod_hud_scale_percent",100);
        if(ImGui::SliderInt(mod_l10n("Battle HUD size"),&size,50,220,"%d%%")){Port_SettingSave("mod_hud_scale_percent",size);Port_SettingsWrite();}
        ImGui::TextWrapped(mod_l10n("Gauges and text render at your window resolution. Portraits retain the original game artwork; increase HUD size for readability, or hide portraits for a cleaner gauge."));
        ImGui::TextWrapped(mod_l10n("Size is relative to each view; gauges stay circular on ultrawide screens. Overall opacity also affects faces and text; background opacity only changes the backing."));

}
static void mod_fusion_settings(){
        bool allow=Port_Setting("fusion_enabled",1)!=0;if(ImGui::Checkbox(mod_l10n("Allow fusions"),&allow)){Port_SettingSave("fusion_enabled",allow);Port_SettingsWrite();}
        int control=Port_Setting("coop_fusion_controls",0);const char *modes[]={mod_l10n("Alternate both players"),mod_l10n("Leader movement / partner attacks"),mod_l10n("Partner movement / leader attacks"),mod_l10n("Alternate movement / attack roles"),mod_l10n("First player only"),mod_l10n("Partner only")};
        if(ImGui::Combo(mod_l10n("Fusion control"),&control,modes,6)){Port_SettingSave("coop_fusion_controls",control);Port_SettingsWrite();}
        int interval=Port_Setting("coop_fusion_swap_seconds",10);if(ImGui::SliderInt(mod_l10n("Control swap interval"),&interval,5,60,"%d seconds")){Port_SettingSave("coop_fusion_swap_seconds",interval);Port_SettingsWrite();}
        ImGui::SetItemTooltip(mod_l10n("The two actual human fusion partners share the fused fighter. CPU partners keep the human player's controls. Combat time excludes cinematics and pause."));


        bool timed=Port_Setting("fusion_duration_enabled",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Timed Fusion Dance"),&timed)){Port_SettingSave("fusion_duration_enabled",timed);Port_SettingsWrite();}
        int seconds=Port_Setting("fusion_duration_seconds",100);
        if(ImGui::SliderInt(mod_l10n("Fusion time budget"),&seconds,1,300,"%d seconds")){Port_SettingSave("fusion_duration_seconds",seconds);Port_SettingsWrite();}
        ImGui::SetItemTooltip(mod_l10n("Applies to the next dance fusion. Preselected fused characters are permanent."));
        int cooldown=Port_Setting("fusion_dance_cooldown_seconds",10);
        if(ImGui::SliderInt(mod_l10n("Dance fusion cooldown"),&cooldown,0,300,"%d seconds")){Port_SettingSave("fusion_dance_cooldown_seconds",cooldown);Port_SettingsWrite();}
        ImGui::SetItemTooltip("%s",mod_l10n("After separation, both partners must wait before another Fusion Dance. Counts combat time; 0 disables the cooldown. Potara is unaffected."));
        bool byForm=Port_Setting("fusion_time_by_form",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Stronger forms drain fusion time faster"),&byForm)){Port_SettingSave("fusion_time_by_form",byForm);Port_SettingsWrite();}
        bool defuse=Port_Setting("fusion_defuse_cinematic",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Defuse cinematic"),&defuse)){Port_SettingSave("fusion_defuse_cinematic",defuse);Port_SettingsWrite();}
        ImGui::SetItemTooltip("%s",mod_l10n("When a timed fusion ends, a light splits the fused fighter and both fighters are pushed apart (about 3 seconds). Off: the plain separation."));
        int preset=Port_Setting("fusion_form_drain_preset",1);const char *presets[]={mod_l10n("Mild"),"Normal",mod_l10n("Heavy")};
        if(ImGui::Combo(mod_l10n("Fusion form drain"),&preset,presets,3)){Port_SettingSave("fusion_form_drain_preset",preset);Port_SettingsWrite();}
        bool owner=Port_Setting("show_fusion_control_owner",1)!=0,swap=Port_Setting("show_fusion_control_countdown",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Show fusion control owner"),&owner)){Port_SettingSave("show_fusion_control_owner",owner);Port_SettingsWrite();}
        if(ImGui::Checkbox(mod_l10n("Show control swap countdown"),&swap)){Port_SettingSave("show_fusion_control_countdown",swap);Port_SettingsWrite();}
        bool timer=Port_Setting("show_fusion_timer",1)!=0;
        if(ImGui::Checkbox(mod_l10n("Show fusion countdown"),&timer)){Port_SettingSave("show_fusion_timer",timer);Port_SettingsWrite();}

}
static void mod_controls_settings(){
                bool modButtons = Port_Setting("mod_clash_buttons", 0) != 0;
                if (ImGui::Checkbox(mod_l10n("Tag Team struggle buttons: Square / Triangle"), &modButtons)) {
                    Port_SettingSave("mod_clash_buttons", modButtons);
                    Port_SettingsWrite();
                }
                ImGui::SetItemTooltip("Local fights: tap Square in beam struggles, Triangle in rush struggles.\n"
                                      "Uses the controller assigned to each fighter, including players 3 and 4.\n"
                                      "Outside struggles the original buttons keep their normal actions.\n"
                                      "Online play retains the upstream control setting below.");
                bool cs = Port_ClashSquare() != 0;
                if (ImGui::Checkbox(mod_l10n("Clashes: Square counts as a direction"), &cs)) {
                    Port_ClashSquareSet(cs);
                }
                ImGui::SetItemTooltip("In a clash of two beams or two rushes the game counts newly pressed directions:\n"
                                      "turning the stick, or tapping the d-pad. With this on, each press of Square\n"
                                      "counts as one of them while the clash lasts, as a tap of the d-pad does.\n"
                                      "It only changes your own pad, so it can be used in online play.");
                if (cs) {
                    static const int kQuarters[] = {4, 6, 7, 8};
                    static const char *const kWorth[] = {"1", "1.5", "1.75", "2"};
                    int at = 2;
                    for (int i = 0; i < 4; i++) {
                        if (kQuarters[i] == Port_ClashSquareX()) {
                            at = i;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    if (ImGui::Combo(mod_l10n("per press"), &at, kWorth, 4)) {
                        Port_ClashSquareXSet(kQuarters[at]);
                    }
                    ImGui::SetItemTooltip("What one press of Square is worth. The d-pad has four buttons to tap in turn and the\n"
                                          "stick four directions a turn; Square is one button. 1 is a single d-pad tap.\n"
                                          "Never more than one a frame, which is the most the game counts from any pad.");
                }
                ImGui::Separator();
                controls_tab();

}
static void mod_giant_settings(){
 ImGui::SeparatorText(mod_l10n("Giant size"));bool on=Port_Setting("canonical_giants",1)!=0;
 if(ImGui::Checkbox(mod_l10n("Larger giants"),&on)){Port_SettingSave("canonical_giants",on);Port_SettingsWrite();}
 int size=Port_Setting("giant_size_percent",400),camera=Port_Setting("giant_camera_distance_percent",150);
 if(ImGui::SliderInt(mod_l10n("Giant size multiplier"),&size,100,400,"%d%%")){Port_SettingSave("giant_size_percent",size);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Giant camera distance"),&camera,100,300,"%d%%")){Port_SettingSave("giant_camera_distance_percent",camera);Port_SettingsWrite();}
 ImGui::TextWrapped("%s",mod_l10n("400% is 4x. Each giant keeps its own original size. Models and collision volumes grow together; movement and animation speed stay native. Local fights only; online and replays are unchanged."));
}
static void mod_training_settings(){
 const char *policies[]={mod_l10n("Native"),mod_l10n("More often"),mod_l10n("When outmatched")};
 const char *presets[]={mod_l10n("Native thresholds"),mod_l10n("Aggressive"),mod_l10n("Relentless")};
 int allies=Port_Setting("cpu_transform_allies",0),enemies=Port_Setting("cpu_transform_enemies",0),preset=Port_Setting("cpu_tactics_preset",0);
 ImGui::SeparatorText(mod_l10n("CPU transformation tactics"));
 if(ImGui::Combo(mod_l10n("Allied CPU transformations"),&allies,policies,3)){Port_SettingSave("cpu_transform_allies",allies);Port_SettingsWrite();}
 if(ImGui::Combo(mod_l10n("Enemy CPU transformations"),&enemies,policies,3)){Port_SettingSave("cpu_transform_enemies",enemies);Port_SettingsWrite();}
 if(ImGui::Combo(mod_l10n("CPU tactics preset"),&preset,presets,3)){Port_SettingSave("cpu_tactics_preset",preset);Port_SettingsWrite();}
 ImGui::TextWrapped("%s",mod_l10n("When outmatched uses health, Ki, recent damage and known form tiers. Presets change pressure thresholds and cooldowns. Normal stock costs and model admission still apply. Native keeps the original AI. Training has separate behavior below; online and replays are unchanged."));
 bool block=Port_Setting("disable_npc_transformations",0)!=0,giants=Port_Setting("disable_npc_giant_transformations",0)!=0;
 if(ImGui::Checkbox(mod_l10n("Disable CPU transformations"),&block)){Port_SettingSave("disable_npc_transformations",block);Port_SettingsWrite();}
 if(ImGui::Checkbox(mod_l10n("Disable CPU giant transformations"),&giants)){Port_SettingSave("disable_npc_giant_transformations",giants);Port_SettingsWrite();}
 int chance=Port_Setting("npc_transform_chance_percent",100);
 if(ImGui::SliderInt(mod_l10n("CPU transformation chance"),&chance,0,100,"%d%%")){Port_SettingSave("npc_transform_chance_percent",chance);Port_SettingsWrite();}
 static int policyCid;const char *exceptionLabels[]={mod_l10n("Follow global rules"),mod_l10n("Allow this character"),mod_l10n("Block this character")};
 if(ImGui::BeginCombo(mod_l10n("Character transformation exception"),Port_CharaName(policyCid))){for(int i=0;i<291;i++){ImGui::PushID(i);if(ImGui::Selectable(Port_CharaName(i),i==policyCid))policyCid=i;ImGui::PopID();}ImGui::EndCombo();}
 char exceptionKey[48];snprintf(exceptionKey,sizeof(exceptionKey),"cpu_transform_%d",policyCid);int exception=Port_Setting(exceptionKey,0);
 if(ImGui::Combo(mod_l10n("Character policy"),&exception,exceptionLabels,3)){Port_SettingSave(exceptionKey,exception);Port_SettingsWrite();}
 ImGui::TextWrapped("%s",mod_l10n("Character exceptions override global bans. Chance still applies. Only CPU ordinary transformations are restricted; player transformations and fusion are unchanged."));
 ImGui::SeparatorText(mod_l10n("Training behavior"));

 int behavior=Port_Setting("training_cpu_behavior",1);const char *names[]={mod_l10n("Idle"),mod_l10n("Fight normally"),mod_l10n("Always try beam struggle"),mod_l10n("Always try rush struggle"),mod_l10n("Charge and use ultimate")};
 if(ImGui::Combo(mod_l10n("Training CPU behavior"),&behavior,names,5)){Port_SettingSave("training_cpu_behavior",behavior);Port_SettingsWrite();}
 bool heal=Port_Setting("training_refill_health",1)!=0;int healDelay=Port_Setting("training_health_delay_ms",2000);
 if(ImGui::Checkbox(mod_l10n("Refill training health and prevent knockouts"),&heal)){Port_SettingSave("training_refill_health",heal);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Health refill delay (ms)"),&healDelay,0,30000)){Port_SettingSave("training_health_delay_ms",healDelay);Port_SettingsWrite();}
 bool ki=Port_Setting("training_refill_ki",0)!=0,stocks=Port_Setting("training_refill_stocks",0)!=0;
 if(ImGui::Checkbox(mod_l10n("Refill training Ki"),&ki)){Port_SettingSave("training_refill_ki",ki);Port_SettingsWrite();}
 if(ImGui::Checkbox(mod_l10n("Refill training stocks"),&stocks)){Port_SettingSave("training_refill_stocks",stocks);Port_SettingsWrite();}
 ImGui::TextWrapped(mod_l10n("Applies to CPU fighters in local Training. Beam/rush modes repeatedly attempt the native attack; the other fighter must meet it to create a clash. Ultimate mode charges into Sparking before attempting its ultimate."));
 ImGui::SeparatorText(mod_l10n("Spectator takeover"));
 const char*takeKeys[]={"spectator_takeover_enabled","spectate_fallen_fighters","show_takeover_hints","show_takeover_confirmation"};
 const char*takeLabels[]={"Enable teammate takeover","Spectate after being defeated","Show takeover hints","Show takeover confirmation"};
 for(int i=0;i<4;i++){bool on=Port_Setting(takeKeys[i],1)!=0;if(ImGui::Checkbox(mod_l10n(takeLabels[i]),&on)){Port_SettingSave(takeKeys[i],on);Port_SettingsWrite();}}
 int notice=Port_Setting("takeover_confirmation_seconds",3);if(ImGui::SliderInt(mod_l10n("Takeover confirmation time"),&notice,1,10,"%d s")){Port_SettingSave("takeover_confirmation_seconds",notice);Port_SettingsWrite();}
 ImGui::TextWrapped("%s",mod_l10n("After being defeated, spectate a living CPU teammate and press L2 + Square to control them. A teammate already controlled by another player cannot be taken over. Ownership resets for the next match."));
 ImGui::SeparatorText(mod_l10n("Team revival"));
 bool revive=Port_Setting("revive_enabled",1)!=0;
 if(ImGui::Checkbox(mod_l10n("Enable teammate revival"),&revive)){Port_SettingSave("revive_enabled",revive);Port_SettingsWrite();}
 int cost=Port_Setting("revive_stock_cost",1),channel=Port_Setting("revive_channel_ms",1500),radius=Port_Setting("revive_radius",80),health=Port_Setting("revive_health_quarters",8),recovery=Port_Setting("revive_recovery_ms",1000);
 if(ImGui::SliderInt(mod_l10n("Revival stock cost"),&cost,0,10)){Port_SettingSave("revive_stock_cost",cost);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Revival channel time (ms)"),&channel,250,10000)){Port_SettingSave("revive_channel_ms",channel);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Revival distance (world units)"),&radius,5,400)){Port_SettingSave("revive_radius",radius);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Restored health (quarter bars)"),&health,1,40)){Port_SettingSave("revive_health_quarters",health);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Protected get-up time (ms)"),&recovery,250,10000)){Port_SettingSave("revive_recovery_ms",recovery);Port_SettingsWrite();}
 bool ring=Port_Setting("show_revive_ring",1)!=0;int ringOpacity=Port_Setting("revive_ring_opacity_percent",45),waveHeight=Port_Setting("revive_ring_wave_height",8),waveSpeed=Port_Setting("revive_ring_wave_speed_percent",100);
 if(ImGui::Checkbox(mod_l10n("Show revival range ring"),&ring)){Port_SettingSave("show_revive_ring",ring);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Revival ring opacity"),&ringOpacity,0,100,"%d%%")){Port_SettingSave("revive_ring_opacity_percent",ringOpacity);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Revival wave height"),&waveHeight,0,40)){Port_SettingSave("revive_ring_wave_height",waveHeight);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("Revival wave speed"),&waveSpeed,10,400,"%d%%")){Port_SettingSave("revive_ring_wave_speed_percent",waveSpeed);Port_SettingsWrite();}
 ImGui::TextWrapped("%s",mod_l10n("Local team fights: stay close to a downed teammate to revive them automatically. Moving in range is allowed; attacking, taking damage or leaving resets progress. Stocks are spent once when revival completes. Fusion-consumed partners cannot be revived."));

}
static void mod_struggle_settings(){
 const char *names[]={"Original","2x","4x"};int beam=Port_Setting("beam_struggle_length",0),rush=Port_Setting("dash_struggle_length",0);
 if(ImGui::Combo(mod_l10n("Beam struggle length"),&beam,names,3)){Port_SettingSave("beam_struggle_length",beam);Port_SettingsWrite();}
 if(ImGui::Combo(mod_l10n("Rush struggle length"),&rush,names,3)){Port_SettingSave("dash_struggle_length",rush);Port_SettingsWrite();}
 int bp=Port_Setting("beam_struggle_cpu_power_percent",100),rp=Port_Setting("dash_struggle_cpu_power_percent",100);
 if(ImGui::SliderInt(mod_l10n("CPU beam strength"),&bp,25,200,"%d%%")){Port_SettingSave("beam_struggle_cpu_power_percent",bp);Port_SettingsWrite();}
 if(ImGui::SliderInt(mod_l10n("CPU rush strength"),&rp,25,200,"%d%%")){Port_SettingSave("dash_struggle_cpu_power_percent",rp);Port_SettingsWrite();}
 bool buttons=Port_Setting("mod_clash_buttons",0)!=0;if(ImGui::Checkbox(mod_l10n("Square for beam / Triangle for rush"),&buttons)){Port_SettingSave("mod_clash_buttons",buttons);Port_SettingsWrite();}
 ImGui::TextWrapped(mod_l10n("Local fights only. CPU strength changes CPU scoring, never a human player's inputs. Online and recorded replays keep their original rules."));
 ImGui::SeparatorText(mod_l10n("Team assists"));
 bool formation=Port_Setting("assist_formation_enabled",1)!=0,follow=Port_Setting("assist_follow_camera",1)!=0;
 if(ImGui::Checkbox(mod_l10n("Helpers fly into formation"),&formation)){Port_SettingSave("assist_formation_enabled",formation);Port_SettingsWrite();}
 if(ImGui::Checkbox(mod_l10n("Follow the assisted fighter's camera"),&follow)){Port_SettingSave("assist_follow_camera",follow);Port_SettingsWrite();}

 const char*enableKeys[]={"beam_assist_enabled","rush_assist_enabled","ultimate_boost_enabled"};
 const char*cpuKeys[]={"beam_assist_cpu","rush_assist_cpu","ultimate_boost_cpu"};
 const char*rangeKeys[]={"beam_assist_range","rush_assist_range","ultimate_boost_range"};
 const char*maxKeys[]={"beam_assist_max_helpers","rush_assist_max_helpers","ultimate_boost_max_helpers"};
 const char*labels[]={"Beam helpers","Rush helpers","Ultimate helpers"};
 for(int i=0;i<3;i++){ImGui::PushID(i);bool on=Port_Setting(enableKeys[i],1)!=0,cpu=Port_Setting(cpuKeys[i],1)!=0;
  if(ImGui::Checkbox(mod_l10n(labels[i]),&on)){Port_SettingSave(enableKeys[i],on);Port_SettingsWrite();}
  if(ImGui::Checkbox(mod_l10n("CPU teammates may assist"),&cpu)){Port_SettingSave(cpuKeys[i],cpu);Port_SettingsWrite();}
  int range=Port_Setting(rangeKeys[i],1500),max=Port_Setting(maxKeys[i],4);
  if(ImGui::SliderInt(mod_l10n("Assist distance"),&range,10,3000)){Port_SettingSave(rangeKeys[i],range);Port_SettingsWrite();}
  if(ImGui::SliderInt(mod_l10n("Maximum helpers"),&max,1,10)){Port_SettingSave(maxKeys[i],max);Port_SettingsWrite();}
  if(i<2){const char*key=i?"rush_assist_ki_cost_percent":"beam_assist_ki_cost_percent";int cost=Port_Setting(key,10),mult=Port_Setting(i?"rush_assist_multiplier_percent":"beam_assist_multiplier_percent",300);
   if(ImGui::SliderInt(mod_l10n("Helper Ki cost"),&cost,0,100,"%d%%")){Port_SettingSave(key,cost);Port_SettingsWrite();}
   if(ImGui::SliderInt(mod_l10n("Clash contribution"),&mult,100,1000,"%d%%")){Port_SettingSave(i?"rush_assist_multiplier_percent":"beam_assist_multiplier_percent",mult);Port_SettingsWrite();}
  }
  if(i==0||i==2){
   const char*modeKey=i==0?"beam_assist_damage_mode":"ultimate_boost_damage_mode";int mode=Port_Setting(modeKey,0);if(mode<0||mode>2)mode=0;
   const char*modes[]={mod_l10n("Bonus per helper"),mod_l10n("By participant count"),mod_l10n("Fixed multiplier")};
   if(ImGui::Combo(mod_l10n("Damage scaling"),&mode,modes,3)){Port_SettingSave(modeKey,mode);Port_SettingsWrite();}
   if(i==2&&mode==0){int boost=Port_Setting("ultimate_boost_percent",50),cap=Port_Setting("ultimate_boost_total_cap",300);
    if(ImGui::SliderInt(mod_l10n("Ultimate bonus per helper"),&boost,0,500,"%d%%")){Port_SettingSave("ultimate_boost_percent",boost);Port_SettingsWrite();}
    if(ImGui::SliderInt(mod_l10n("Maximum ultimate multiplier"),&cap,100,1000,"%d%%")){Port_SettingSave("ultimate_boost_total_cap",cap);Port_SettingsWrite();}}
   if(mode==2){const char*key=i==0?"beam_assist_damage_fixed_percent":"ultimate_boost_fixed_percent";int fixed=Port_Setting(key,200);
    if(ImGui::SliderInt(mod_l10n("Fixed damage multiplier"),&fixed,100,1000,"%d%%")){Port_SettingSave(key,fixed);Port_SettingsWrite();}}
   if(i==0&&mode==0){int bonus=Port_Setting("beam_assist_damage_bonus_percent",Port_Setting("beam_assist_multiplier_percent",300)-100),cap=Port_Setting("beam_assist_damage_cap_percent",300);
    if(ImGui::SliderInt(mod_l10n("Damage bonus per helper"),&bonus,0,900,"%d%%")){Port_SettingSave("beam_assist_damage_bonus_percent",bonus);Port_SettingsWrite();}
    if(ImGui::SliderInt(mod_l10n("Maximum damage multiplier"),&cap,100,1000,"%d%%")){Port_SettingSave("beam_assist_damage_cap_percent",cap);Port_SettingsWrite();}}
   ImGui::TextWrapped("%s",mod_l10n("Participant mode counts the caster plus confirmed helpers. Fixed mode applies once when at least one helper joins. 200% means 2x. Beam damage uses only the winning team's helpers; clash strength is separate."));
  }
  if(i==1)ImGui::TextWrapped("%s",mod_l10n("Rush finisher damage scales with confirmed winning participants, up to 10,000 per losing fighter. Joined losing helpers are knocked away too."));
  ImGui::PopID();
 }
 ImGui::TextWrapped("%s",mod_l10n("Press R3 when invited. Beam and rush assistance spend Ki once; ultimate assistance spends one stock, without requiring Sparking. Helpers stop assisting when interrupted or when the attack ends. Local team fights only."));

}
// F1 and Select > Settings share one full-screen mod shell and the same live consumers.
static void build(void) {
    PortVideo v,was;GsGpu_GetSettings(&v);was=v;ImGuiIO &io=ImGui::GetIO();bool open=true;
    static double savedUntil;static bool savedOk;bool focusOptions=false,focusCategories=false;
    if(sCapKind==2&&SDL_GetTicks()>sCapUntil){sCapKind=0;gPortInputCapture=0;}
    if(sCapKind==0&&(pad_pressed(ImGuiKey_GamepadFaceRight)||pad_pressed(ImGuiKey_GamepadFaceUp)))open=false;
    if(sForceTab>=0&&sForceTab<12){sTab=sForceTab;sForceTab=-1;}
    if(sTab<0||sTab>=12)sTab=0;
    if(sCapKind==0&&(pad_pressed(ImGuiKey_GamepadL1)||pad_pressed(ImGuiKey_GamepadR1))){sTab=(sTab+(ImGui::IsKeyPressed(ImGuiKey_GamepadL1,false)?11:1))%12;sPadTab=false;}
    bool es=Port_Setting("mod_language",1)==1;
    static const char *labels[2][12]={{"Video","Effects","Audio","Interface","Controls","Cheats","System","Extra fighters","Battle HUD","Fusion","CPU / Training","Struggles"},{"Video","Efectos","Audio","Interfaz","Controles","Trucos","Sistema","Luchadores extra","HUD de combate","Fusion","CPU / Entrenamiento","Choques"}};
    static const char *desc[2][12]={{"Resolution, aspect ratio, display and graphics.","Lighting and battle effects.","Music, voices and sound levels.","Fonts, original HUD placement and interface.","Player controllers, bindings and struggle buttons.","Practice and game rule overrides.","Startup, updates and system preferences.","Additional fighters and multiplayer setup.","Rings, hexagons, health layers, Ki and Sparking.","Fusion Dance time, form drain and countdown.","Training opponents and repeated attack behavior.","Clash duration, CPU strength and win inputs."},{"Resolucion, relacion de aspecto y pantalla.","Iluminacion y efectos de combate.","Musica, voces y niveles de sonido.","Fuentes, HUD original e interfaz.","Mandos, botones y controles de choques.","Opciones de practica y reglas.","Inicio, actualizaciones y sistema.","Luchadores adicionales y multijugador.","Anillos, hexagonos, salud, Ki y Sparking.","Duracion de danza, desgaste y cuenta atras.","CPU y ataques repetidos en entrenamiento.","Duracion, fuerza CPU y controles de choques."}};
    float scale=SDL_max(.65f,SDL_min(1.65f,io.DisplaySize.y/850.f)),pad=16*scale,side=SDL_min(io.DisplaySize.x*.31f,225*scale);
    ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.15f,.035f,.15f,.94f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.08f,.025f,.10f,.57f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.40f,.15f,.34f,.65f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.62f,.30f,.38f,.85f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(.75f,.40f,.25f,.9f));
    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.78f,.42f,.17f,.58f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(.85f,.53f,.22f,.62f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,ImVec4(1,.68f,.22f,.70f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,ImVec4(1,.80f,.30f,1));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(1,.73f,.28f,1));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.55f,.24f,.26f,.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(pad,pad));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*scale);
    if(ImGui::Begin("Mod Settings###Settings",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings)){
        if(sModBodyFont)ImGui::PushFont(sModBodyFont,18.f);
        ImGui::SetWindowFontScale(scale);
        if(SDL_GPUTexture *brand=mod_brand_texture()){ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)brand),ImVec2(150*scale,50*scale));ImGui::SameLine();}
        ImGui::BeginGroup();ImGui::TextColored(ImVec4(1,.77f,.28f,1),es?"AJUSTES DEL MOD":"MOD SETTINGS");ImGui::TextDisabled("Tag Team Mod / Real Power Scale");ImGui::EndGroup();
        ImGui::Separator();
        float body=ImGui::GetContentRegionAvail().y-ImGui::GetFrameHeightWithSpacing()*3.8f;
        if(ImGui::BeginChild("categories",ImVec2(side,body),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened)){
            if(sCapKind==0&&ImGui::IsWindowFocused()&&(pad_pressed(ImGuiKey_GamepadDpadRight)||pad_pressed(ImGuiKey_GamepadFaceDown)||ImGui::IsKeyPressed(ImGuiKey_RightArrow))){focusOptions=true;ImGui::NavMoveRequestCancel();}
            for(int i=0;i<12;i++){
                ImVec2 pos=ImGui::GetCursorScreenPos();mod_icon_draw(ImGui::GetWindowDrawList(),(i==10?MI_TRAINING:i==11?MI_STRUGGLES:MI_VIDEO+i),ImVec2(pos.x+16*scale,pos.y+17*scale),11*scale,IM_COL32(255,204,104,255));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()+35*scale);
                if(ImGui::Selectable(labels[es][i],sTab==i,0,ImVec2(0,35*scale))){sTab=i;focusOptions=true;}
            }
        }ImGui::EndChild();ImGui::SameLine();
        if(ImGui::BeginChild("options",ImVec2(0,body),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened)){
            ImGui::TextColored(ImVec4(1,.81f,.35f,1),"%s",labels[es][sTab]);ImGui::TextWrapped("%s",desc[es][sTab]);ImGui::Separator();
            if(sCapKind==0&&ImGui::IsWindowFocused()&&!ImGui::IsAnyItemActive()&&(pad_pressed(ImGuiKey_GamepadDpadLeft)||ImGui::IsKeyPressed(ImGuiKey_LeftArrow))){focusCategories=true;ImGui::NavMoveRequestCancel();}
            if(focusOptions){ImGui::SetKeyboardFocusHere();ImGui::SetNavCursorVisible(true);}
            ImGui::PushItemWidth(SDL_min(300*scale,ImGui::GetContentRegionAvail().x*.47f));
            switch(sTab){case 0:video_tab(v);break;case 1:effects_tab(v);break;case 2:audio_tab(v);break;case 3:ui_tab(v);break;case 4:mod_controls_settings();break;case 5:cheats_tab();break;case 6:misc_tab();break;case 7:experiments_tab();mod_giant_settings();break;case 8:mod_hud_settings();break;case 9:mod_fusion_settings();break;case 10:mod_training_settings();break;case 11:mod_struggle_settings();break;}
            ImGui::PopItemWidth();
        }ImGui::EndChild();
        if(focusCategories){ImGuiContext &g=*ImGui::GetCurrentContext();for(ImGuiWindow *w:g.Windows)if(w->ParentWindow==ImGui::GetCurrentWindow()&&strstr(w->Name,"/categories_")){ImGui::FocusWindow(w);ImGui::SetNavID(w->GetID(labels[es][sTab]),ImGuiNavLayer_Main,w->NavRootFocusScopeId,ImRect(0,0,1,1));ImGui::SetNavCursorVisible(true);break;}}
        ImGui::Separator();
        bool save=ImGui::Button(es?"Guardar ajustes":"Save settings");
        if(sCapKind==0&&pad_pressed(ImGuiKey_GamepadFaceLeft))save=true;
        if(save){if(memcmp(&v,&was,sizeof(v))){GsGpu_SetSettings(&v);was=v;}savedOk=Port_SettingsFlush()!=0;savedUntil=ImGui::GetTime()+4;}
        ImGui::SameLine();if(ImGui::Button(es?"Volver":"Back"))open=false;
        ImGui::SameLine();if(ImGui::Button(es?"Salir del juego":"Exit game"))Ui_QuitAsk();
        if(savedUntil>ImGui::GetTime()){ImGui::SameLine();ImGui::TextColored(savedOk?ImVec4(.45f,1,.66f,1):ImVec4(1,.46f,.38f,1),"%s",savedOk?(es?"Ajustes guardados":"Settings saved"):(es?"No se pudo guardar. Reintenta.":"Save failed. Please retry."));}
        // Native geometry avoids unsupported font glyphs in PlayStation hints.
        ImVec2 pos=ImGui::GetCursorScreenPos();ImDrawList *dl=ImGui::GetWindowDrawList();float y=pos.y+10*scale,x=pos.x;
        dl->AddRect(ImVec2(x,y-5*scale),ImVec2(x+10*scale,y+5*scale),IM_COL32(235,135,207,255),0,0,1.5f);ImGui::SetCursorPosX(ImGui::GetCursorPosX()+18*scale);ImGui::TextDisabled(es?"Guardar    Derecha / X: opciones    X: editar\nIzquierda: categorias    Triangulo: volver":"Save    Right / Cross: options    Cross: edit\nLeft: categories    Triangle: back");
        if(sModBodyFont)ImGui::PopFont();
    }ImGui::End();ImGui::PopStyleVar(2);ImGui::PopStyleColor(11);
    if(memcmp(&v,&was,sizeof(v)))GsGpu_SetSettings(&v);
    if(!open)set_open(false);
}

#include "mod_menu_ui.inc"
#include "mod_tournament_ui.inc"
#include "mod_battle_hud.inc"

// ---- the hint of the extra modes -----------------------------------------------------------------------------------
// SELECT on the game's main menu switches the menu to the extra modes' entries and back (src/menu/menu_a.c,
// plat_modes.c; the entries are the menu's own plates, their names drawn by menu_text.c). Here only the small plate
// in the corner of the menu's message window that says so. It is drawn into the game's picture, behind the settings
// window, in the picture's own 512 x 448 units so that it is the same at every window size.
struct ModesBox { float x, y, kx, ky; }; // the 4:3 page in the window: its corner, and window pixels per unit

static ImVec2 modes_at(const ModesBox &b, float x, float y) {
    return ImVec2(b.x + x * b.kx, b.y + y * b.ky);
}

// A line of text; align 0 from x, 1 centred on x. Made narrower when it is wider than `fit` units.
static void modes_text(ImDrawList *dl, const ModesBox &b, float size, float x, float y, ImU32 colour, const char *text, int align, float fit) {
    ImFont *font = ImGui::GetFont();
    float px = size * b.ky;
    ImVec2 ext = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
    if (fit > 0.0f && ext.x > fit * b.kx) {
        px *= fit * b.kx / ext.x;
        ext = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
    }
    ImVec2 at = modes_at(b, x, y);
    at.x -= align == 1 ? ext.x * 0.5f : 0.0f;
    at.y += (size * b.ky - px) * 0.5f;
    dl->AddText(font, px, at, colour, text);
}

static void modes_draw(const PortModesView &v) {
    static int said = -1; // the entry the screen reader was told last, -1: the set is not showing
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ModesBox b;
    float pw = (float)gUiPresentW, ph = (float)gUiPresentH;

    if (pw <= 0.0f || ph <= 0.0f) {
        return;
    }
    {   // the 4:3 page in the middle of the picture (the menus are that page, also in a wide picture)
        float w = pw < ph * 4.0f / 3.0f ? pw : ph * 4.0f / 3.0f;
        b.x = (float)gUiPresentX + (pw - w) * 0.5f;
        b.y = (float)gUiPresentY;
        b.kx = w / 512.0f;
        b.ky = ph / 448.0f;
    }
    if (v.hint) {
        ImVec2 p0 = modes_at(b, 330.0f, 416.0f), p1 = modes_at(b, 502.0f, 440.0f), k0 = modes_at(b, 336.0f, 420.0f), k1 = modes_at(b, 392.0f, 436.0f);
        dl->AddRectFilled(p0, p1, IM_COL32(6, 14, 40, 215), 12.0f * b.ky);
        dl->AddRect(p0, p1, IM_COL32(246, 150, 30, 255), 12.0f * b.ky, 0, 1.5f * b.ky);
        dl->AddRectFilled(k0, k1, IM_COL32(230, 235, 245, 255), 8.0f * b.ky);
        modes_text(dl, b, 12.0f, 364.0f, 421.5f, IM_COL32(20, 24, 40, 255), "SELECT", 1, 50.0f);
        modes_text(dl, b, 14.0f, 399.0f, 420.5f, IM_COL32(255, 240, 200, 255), v.set ? "Back" : "More modes", 0, 96.0f);
    }
    if (!v.set || !v.hint) { // the screen reader: the set when it comes up, then the entry under the cursor when it changes
        said = -1;
    } else if (v.players && v.cursor + 10 != said && v.cursor >= 0 && v.cursor < 4) { // (the number of players)
        char speak[96];
        snprintf(speak, sizeof(speak), "%s, %d of 4", Port_ModesPlayersName(v.cursor + 1), v.cursor + 1);
        if (said < 10) {
            PortA11y_Say(PortA11y_Label("Players"), 1);
        }
        PortA11y_Say(speak, said < 10 ? 0 : 1);
        said = v.cursor + 10;
    } else if (!v.players && v.cursor != said && v.cursor >= 0 && v.cursor < PORT_MODE_COUNT - 1) {
        char speak[96];
        snprintf(speak, sizeof(speak), "%s, %d of %d", Port_ModesName(v.cursor + 1), v.cursor + 1, PORT_MODE_COUNT - 1);
        if (said < 0 || said >= 10) {
            PortA11y_Say(PortA11y_Label("Extra modes"), 1);
        }
        PortA11y_Say(speak, said < 0 || said >= 10 ? 0 : 1);
        said = v.cursor;
    }
}

static bool sBuilt; // this frame has something to draw: the window, or the line over the picture

void Ui_DrawAgain(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    ImDrawData *dd = sReady && sBuilt ? ImGui::GetDrawData() : NULL; // (sBuilt: Ui_Draw made a picture this frame)
    SDL_GPUColorTargetInfo ti;
    SDL_GPURenderPass *pass;

    if (dd == NULL || dd->CmdListsCount == 0 || dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) {
        return;
    }
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_zero(ti);
    ti.texture = target;
    ti.load_op = SDL_GPU_LOADOP_LOAD;
    ti.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(cmd, &ti, 1, NULL);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
}

static char sNotice[160];
static Uint64 sNoticeUntil;
static volatile int sNoticeNew;

extern "C" void Port_UiNotice(const char *text) {
    SDL_strlcpy(sNotice, text != NULL ? text : "", sizeof(sNotice));
    sNoticeNew = 1;
}

// The meter: frames shown and the game's speed, and in an online match the connection. A setting (Video tab).
static bool meter_on(void) {
    if (sMeter < 0) {
        sMeter = getenv("BT3_METER") != NULL ? atoi(getenv("BT3_METER")) != 0 : Port_Setting("meter", 0) != 0;
    }
    return sMeter != 0;
}
static float sFps, sSpeed; // frames shown per second; the game's vertical blanks per second as a share of 60

static void meter_count(void) { // once per frame shown
    static Uint64 t0;
    static unsigned frames, blanks0;
    Uint64 now = SDL_GetTicks();
    frames++;
    if (t0 == 0) {
        t0 = now;
        blanks0 = gPortLiveBlanks;
    } else if (now - t0 >= 500) {
        sFps = (float)frames * 1000.0f / (float)(now - t0);
        sSpeed = (float)(gPortLiveBlanks - blanks0) * 1000.0f / (float)(now - t0) / 60.0f * 100.0f;
        frames = 0;
        blanks0 = gPortLiveBlanks;
        t0 = now;
    }
}

static void meter_draw(void) {
    int n[8];
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##meter", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings)) {
        // a fight shows 30 frames a second and the menus 60, as on the console; the speed is what should stay at 100%
        ImGui::Text("%.0f fps   speed %.0f%%", sFps, sSpeed);
        if (Port_NetStats(n)) {
            if (n[0] >= 0) {
                ImGui::Text("ping %d ms%s", n[0], Port_LobbyRelayed() ? " (relay)" : "");
            } else {
                ImGui::TextUnformatted("ping ...");
            }
            if (n[5] > 0) {
                ImGui::Text("rollbacks %d/s, %.1f frames", n[1], (float)n[2] / 10.0f);
            } else {
                ImGui::TextUnformatted("rollback off");
            }
            ImGui::Text("waited %d ms/s (%d times)", n[4], n[3]);
            ImGui::Text("delay %d%s, rollback up to %d", n[6], n[7] ? " (auto)" : "", n[5]);
        }
    }
    ImGui::End();
}

// The name of an added stage or song as text, in the rectangle the menu gives (the strip images' place): cream
// letters with a dark outline, as tall as the rectangle allows, centred for a stage and from the left for a song.
static void name_text(ImDrawList *dl, const char *text, float x, float y, float w, float h, bool centre) {
    ImFont *font = ImGui::GetFont();
    float size = h * (centre ? 0.42f : 0.72f);
    ImVec2 ext = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    if (ext.x > w * 0.96f && ext.x > 0.0f) { // a long name: smaller, to fit
        size *= w * 0.96f / ext.x;
        ext = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    }
    float tx = centre ? x + (w - ext.x) * 0.5f : x + h * 0.15f, ty = y + (h - ext.y) * 0.5f, o = size * 0.07f + 1.0f;
    for (int k = 0; k < 8; k++) {
        static const float dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        dl->AddText(font, size, ImVec2(tx + dx[k] * o, ty + dy[k] * o), IM_COL32(40, 20, 0, 255), text);
    }
    dl->AddText(font, size, ImVec2(tx, ty), IM_COL32(255, 236, 170, 255), text);
}

/* Names in the game's own lettering (namefont.c): a name with no picture in a strip is put together from the letters
   of the disc's name pictures, once, and kept as a texture while it is the one shown (a few are kept, for going up
   and down a list). */
struct FontName {
    SDL_GPUTexture *tex; // NULL: this name cannot be drawn that way (a character the disc's names do not have)
    int style, w, h, choice, resolution;
    char text[64];
    bool used;
};
static FontName sFontNames[12];
static unsigned sFontNext;

static const FontName *font_name(int style, const char *text) {
    int choice = MenuText_GetFont();
    int resolution = choice ? 4 : 1; // (a font's strip: 4 times the disc's, for a window larger than the game's picture)
    if (text == NULL || text[0] == '\0' || (choice == 0 && !NameFont_Ready())) { // (not ready: asked again next frame)
        return NULL;
    }
    for (FontName &n : sFontNames) {
        if (n.used && n.style == style && n.choice == choice && n.resolution == resolution && strncmp(n.text, text, sizeof(n.text) - 1) == 0) {
            return n.tex != NULL ? &n : NULL;
        }
    }
    FontName &n = sFontNames[sFontNext++ % (sizeof(sFontNames) / sizeof(sFontNames[0]))];
    if (n.used && n.tex != NULL) {
        if (sGL) {
            GsGl_StripTextureFree((unsigned)(intptr_t)n.tex);
        } else {
            SDL_ReleaseGPUTexture(sDevice, n.tex);
        }
    }
    n.tex = NULL;
    n.w = n.h = 0;
    n.used = true;
    n.style = style;
    n.choice = choice; n.resolution = resolution;
    SDL_strlcpy(n.text, text, sizeof(n.text));
    unsigned char *pix;
    if (choice) {
        pix = style == NF_STAGE ? MenuText_RenderStage(n.text, resolution, &n.w, &n.h) :
                                  MenuText_RenderSong(n.text, style == NF_SONG_LIT, resolution, &n.w, &n.h);
    } else {
        pix = NameFont_Compose(style, n.text, &n.w, &n.h);
    }
    if (pix != NULL) {
        n.tex = make_strip(pix, (size_t)n.w * n.h * 4, (uint32_t)n.w, (uint32_t)n.h, 1); // (frees pix)
    }
    if (choice) { n.w /= resolution; n.h /= resolution; }
    return n.tex != NULL ? &n : NULL;
}

/* Draws such a name over the place of the disc's name picture: (x, y) is the corner of that picture's row in the
   window, kx and ky are window pixels per pixel of the row (512 wide). A name longer than the row is made smaller
   to fit, about the row's middle line; a stage name stays in the middle, a song name keeps its left edge. */
static void font_draw(ImDrawList *dl, const FontName *n, float x, float y, float kx, float ky, bool centre) {
    float k = n->w > 512 ? 512.0f / (float)n->w : 1.0f;
    float w = (float)n->w * kx * k, h = (float)n->h * ky * k;
    float x0 = centre ? x + (512.0f * kx - w) * 0.5f : x;
    float y0 = y + ((float)n->h * ky - h) * 0.5f;
    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)n->tex), ImVec2(x0, y0), ImVec2(x0 + w, y0 + h));
}

// The online chat, in the lower left corner. Put away, it shows the newest lines for a few seconds each in a dark
// rounded card, fading out, and takes no input. While a line is typed it shows the last ones, input field and
// allows selecting/copying text with mouse.
enum { CHAT_SHOWN_MS = 8000, CHAT_FADE_MS = 2000 };

static bool chat_recent(void) {
    PortChatLine l;
    unsigned n = PortChat_Count();
    return n > 0 && PortChat_Line(n - 1, &l) && SDL_GetTicks() - l.atMs < CHAT_SHOWN_MS;
}

static void chat_draw(void) {
    ImGuiIO &io = ImGui::GetIO();
    if (sChatSize < 0) {
        sChatSize = Port_Setting("chat_size", 1);
        if (sChatSize < 0 || sChatSize > 2) sChatSize = 1;
    }
    int rowsTyping = 6, rowsPassive = 4; // Normal: as the chat always was
    float wFactor = 0.42f, minW = 320.0f, maxW = 100000.0f;
    if (sChatSize == 0) { // Compact
        rowsTyping = 4;
        rowsPassive = 2;
        wFactor = 0.28f;
        minW = 240.0f;
        maxW = 380.0f;
    } else if (sChatSize == 2) { // Large
        rowsTyping = 8;
        rowsPassive = 6;
        wFactor = 0.52f;
        minW = 400.0f;
    }
    float w = io.DisplaySize.x * wFactor;
    if (w < minW) w = minW;
    if (w > maxW) w = maxW;
    unsigned n = PortChat_Count(), rows = sChat ? rowsTyping : rowsPassive, first = n > rows ? n - rows : 0;
    Uint64 now = SDL_GetTicks();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!sChat) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    // In the lower left corner (growing upwards) until it is dragged; then where it was left (off the screen: back).
    static ImVec2 sChatPos;
    static bool sChatMoved;
    if (sChatMoved && (sChatPos.x < 0.0f || sChatPos.y < 0.0f || sChatPos.x > io.DisplaySize.x - 50.0f ||
                       sChatPos.y > io.DisplaySize.y - 50.0f)) {
        sChatMoved = false;
    }
    if (!sChatMoved) {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.02f, io.DisplaySize.y * 0.86f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    } else if (!sChat) {
        ImGui::SetNextWindowPos(sChatPos, ImGuiCond_Always);
    }

    float bgAlpha = 0.85f;
    if (!sChat) {
        float newestAlpha = 1.0f;
        PortChatLine lastLine;
        if (n > 0 && PortChat_Line(n - 1, &lastLine)) {
            Uint64 age = now - lastLine.atMs;
            if (age >= CHAT_SHOWN_MS) {
                return;
            }
            if (age > CHAT_SHOWN_MS - CHAT_FADE_MS) {
                newestAlpha = (float)(CHAT_SHOWN_MS - age) / (float)CHAT_FADE_MS;
            }
        }
        bgAlpha = 0.65f * newestAlpha;
    }
    ImGui::SetNextWindowBgAlpha(bgAlpha);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, sChat ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.75f, 0.2f, 0.5f));

    if (sChat && sChatFocus) {
        ImGui::SetNextWindowFocus();
    }
    if (ImGui::Begin("##chat", NULL, flags)) {
        if (sChat && ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            sChatMoved = true;
        }
        if (sChat && sChatMoved) {
            sChatPos = ImGui::GetWindowPos();
        }
        const char *names[2] = {Port_NetName(Port_NetMe()), Port_NetName(!Port_NetMe())};
        if (sChat) {
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.28f, 1.0f), "Chat (Tab)");
            ImGui::SameLine();
            ImGui::TextDisabled("::");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Drag anywhere to move the chat window. Click a line to copy it.");
            }
            ImGui::SameLine(w - 70.0f);
            bool mute = PortChat_Muted() != 0;
            if (ImGui::Checkbox("Mute", &mute)) {
                PortChat_MutedSet(mute);
                sChatFocus = true;
            }
            ImGui::Separator();
        }

        for (unsigned i = first; i < n; i++) {
            PortChatLine l;
            float alpha = 1.0f;
            if (!PortChat_Line(i, &l)) {
                continue;
            }
            if (!sChat) {
                Uint64 age = now - l.atMs;
                if (age >= CHAT_SHOWN_MS) {
                    continue;
                }
                if (age > CHAT_SHOWN_MS - CHAT_FADE_MS) {
                    alpha = (float)(CHAT_SHOWN_MS - age) / (float)CHAT_FADE_MS;
                }
            }
            const char *sender = l.sender[0] != '\0' ? l.sender : (names[l.mine ? 0 : 1][0] != '\0' ? names[l.mine ? 0 : 1] : (l.mine ? "You" : "Rival"));
            ImGui::PushID((int)i);
            ImVec4 nameCol = l.mine ? ImVec4(0.35f, 0.85f, 1.0f, alpha) : ImVec4(1.0f, 0.82f, 0.28f, alpha);
            ImGui::TextColored(nameCol, "%s:", sender);
            ImGui::SameLine(0.0f, 4.0f);

            ImVec2 at = ImGui::GetCursorScreenPos();
            if (!sChat) {
                ImGui::GetWindowDrawList()->AddText(NULL, 0.0f, ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, (int)(230.0f * alpha)), l.text, NULL, w - (at.x - ImGui::GetWindowPos().x));
            }
            ImGui::PushTextWrapPos(w);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", l.text);
            ImGui::PopTextWrapPos();
            if (sChat) { // a click copies the line (a room code or an address is passed on this way); a drag does not
                static unsigned sCopiedIdx;
                static Uint64 sCopiedMs;
                bool copied = sCopiedIdx == i + 1 && now - sCopiedMs < 1500;
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(copied ? "Copied" : "Click to copy");
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 16.0f) {
                        SDL_SetClipboardText(l.text);
                        sCopiedIdx = i + 1;
                        sCopiedMs = now;
                        sChatFocus = true;
                    }
                }
            }
            ImGui::PopID();
        }

        if (sChat) {
            if (sChatFocus) {
                ImGui::SetKeyboardFocusHere();
                sChatFocus = false;
            }
            ImGui::SetNextItemWidth(w);
            if (ImGui::InputText("##chatline", sChatBuf, sizeof(sChatBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                const char *p = sChatBuf;
                while (*p == ' ') {
                    p++;
                }
                if (*p == '\0') {
                    chat_close();
                } else if (PortChat_Say(sChatBuf)) {
                    sChatBuf[0] = '\0';
                    chat_close();
                } else { // (said within a second of the line before: kept, to be sent again)
                    sChatSoon = true;
                    sChatFocus = true;
                }
            }
            ImGui::TextDisabled(sChatSoon ? "1 line/s: Enter to send" : "Enter: send   Esc/Tab: close   Ctrl+V: paste");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

// Builds this frame's picture of the overlay (the settings or the online window, the line over the picture).
// false: there is nothing to draw. The same for both back ends; each then draws ImGui's data its own way.
static bool frame_build(void) {
    PortInput_TboClipboard();
    { // a keypad of Tenkaichi Budokai Online takes text: the system's text input is on for it (letters as typed)
        static SDL_Window *on = NULL;
        SDL_Window *want = gPortTboTyping && !sOpen && !sQuit && !sChat ? SDL_GetKeyboardFocus() : NULL;
        if (want != on) {
            if (on != NULL) { SDL_StopTextInput(on); }
            if (want != NULL) { SDL_StartTextInput(want); }
            on = want;
        }
    }
    if (sQuit && !PortQuit_Active() && !PortQuit_Failed()) {
        sQuit = false;
        input_owner();
        PortA11y_Say(PortA11y_Label("Back to the game"), 1);
    }
    if (sReady && gPortNetWindowClose) { // back from an online session: the window it was started from closes
        gPortNetWindowClose = 0;
        gPortNetMenuRequest = 0;
        set_open(false);
    } else if (sReady && gPortNetMenuRequest) { // Dragon Net Battle was chosen in the game's main menu
        gPortNetMenuRequest = 0;
        net_open();
    }
    if (sReady && !sUpdTold && update_latest() > PORT_RELEASE && SDL_GetTicks() >= sNoticeUntil) {
        int latest = update_latest(); // (told once, and not over another notice)
        sUpdTold = true;
        SDL_snprintf(sNotice, sizeof(sNotice), "Update available: 0.%d.%d. F1 for the download page.", latest / 100, latest % 100);
        sNoticeUntil = SDL_GetTicks() + 10000;
    }
    bool notice = sNotice[0] != '\0' && SDL_GetTicks() < sNoticeUntil;
    if (sNoticeNew) { // (set from the game's side: the time starts when it is first drawn)
        sNoticeNew = 0;
        sNoticeUntil = SDL_GetTicks() + 6000;
        notice = sNotice[0] != '\0';
    }
    // the name of a stage added from outside the disc, over the stage select (a strip of pre-rendered names)
    bool nameOn = gUiNameReady != 0 && gUiNameIdx >= 0 && gUiNameIdx < PortStages_Count();
    bool songOn = gUiSongReady != 0 && gUiSongIdx >= 0 && gUiSongIdx < PortSongs_Count(); // (the same for an added song)
    bool overlay = nameOn || songOn;
    bool meter = sReady && meter_on();
    // the players' names under the gauges of an online fight
    bool names = sReady && Port_NetActive() && BtlChar_FindByObjId(0) != NULL && (Port_NetName(0)[0] != '\0' || Port_NetName(1)[0] != '\0');
    bool startup = sReady && sStartupCreditsFrame.load(std::memory_order_acquire) > 0;
    bool bars = sReady && bars_on();
    bool battleHud=sReady&&mod_battle_hud_visible();
    PortModesView modes; // the main menu's hint of the extra modes
    Port_ModesView(&modes);
    bool tournament = sReady && tournament_visible();
    bool modMenu = sReady && Port_ModMenuActive();
    int modSettings = sModMenuSettings.exchange(-1, std::memory_order_acq_rel);
    if (sReady && modSettings >= 0 && modSettings < 12) { set_open(true); sForceTab = modSettings; }
    bool modesOn = sReady && modes.hint && !modMenu;
    bool online = sReady && Port_NetActive() && PortChat_Enabled();
    if (sChat && !online) { // (the match ended while a line was typed)
        chat_close();
    }
    bool chat = online && (sChat || chat_recent());
    meter_count();
    sBuilt = false;
    if (!sReady || (!sOpen && !sQuit && !notice && !overlay && !meter && !names && !bars && !modesOn && !chat && !startup && !modMenu && !tournament && !battleHud)) {
        return false;
    }
    if (sPadHold && sCapKind != 2) { // a binding was taken: the controller works the window again once nothing is held
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        bool held = false;
        for (int i = 0; i < count; i++) {
            SDL_Gamepad *g = SDL_GetGamepadFromID(ids[i]);
            for (int b = 0; g != NULL && b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
                held = held || SDL_GetGamepadButton(g, (SDL_GamepadButton)b);
            }
        }
        SDL_free(ids);
        sPadHold = held;
    }
    // (the button that becomes the binding would also press the row it is bound in; while a chat line is typed the
    // controller is the game's)
    if (sPadHold || (sChat && !sOpen)) {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    } else {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    sBuilt = true;
    if (sGL) {
        ImGui_ImplOpenGL3_NewFrame();
    } else {
        ImGui_ImplSDLGPU3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    fonts_apply(); // (a face chosen for the fight panels, between two frames)
    ImGui::NewFrame();
    if (startup) { mod_startup_draw(); ImGui::Render(); return true; }
    if (bars) {
        bars_draw();
    }
    if (modesOn) {
        modes_draw(modes);
    }
    if (modMenu) { mod_menu_draw(); }
    if (tournament) { tournament_draw(); }
    if(battleHud&&!modMenu&&!tournament&&!sOpen)mod_battle_hud_draw();
    if (sOpen) {
        if (sNet) {
            build_net();
        } else {
            build();
        }
    }
    if (sQuit && PortQuit_Failed()) {
        build_quit();
    }
    if (meter) {
        meter_draw();
    }
    if (chat) {
        chat_draw();
    }
    if (names && GsDraw_HudShown() && gUiPresentW > 0 && gUiPresentH > 0) { // (with the HUD: not over an intro)
        // Under each side's health bar (the game's 512 x 448 page: the bars end at about y 50). The left place is
        // side 0's, or this player's own side when the HUD's sides are swapped.
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        float sx = (float)gUiPresentW / 512.0f, sy = (float)gUiPresentH / 448.0f;
        float size = 15.0f * sy;
        ImFont *font = ImGui::GetFont();
        for (int place = 0; place < 2; place++) {
            const char *text = Port_NetName(place ^ Port_HudSwap());
            if (text[0] == '\0') {
                continue;
            }
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            // 106 of the HUD's own pixels in from its outer edge: clear of the panel of a team's waiting fighters,
            // which reaches to about 100 (the name sat on it at 62: reported). The HUD is narrowed in a wide
            // picture and sits at the edges or about the middle (gs_draw.c), and the name goes with it.
            float narrow = Port_IsWide() ? 1333.333f / (float)Port_AspectMilli() : 1.0f;
            float in = Port_HudLayout() == 2 ? 256.0f + (106.0f - 256.0f) * narrow : 106.0f * narrow;
            float x = place == 0 ? (float)gUiPresentX + in * sx : (float)gUiPresentX + (512.0f - in) * sx - ts.x;
            float y = (float)gUiPresentY + 56.0f * sy;
            for (int ox = -1; ox <= 1; ox++) {
                for (int oy = -1; oy <= 1; oy++) {
                    if (ox != 0 || oy != 0) {
                        dl->AddText(font, size, ImVec2(x + (float)ox * 1.5f, y + (float)oy * 1.5f), IM_COL32(0, 0, 0, 220), text);
                    }
                }
            }
            dl->AddText(font, size, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        }
    }
    if (notice) { // a line over the picture for a few seconds (the end of an online match)
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("##notice", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted(sNotice);
        }
        ImGui::End();
    }
    if (overlay && gUiPresentW > 0 && gUiPresentH > 0) {
        /* Each name's rectangle is in the game's 512x448 pixels: map it through the picture's rectangle in the
           window (the letterbox gs_gpu.c just blitted into). */
        float sx = (float)gUiPresentW / 512.0f;
        float sy = (float)gUiPresentH / 448.0f;
        // With a menu font every name is drawn here, so the names belong to the game picture, behind the settings
        // and other windows. With "Original" only an added stage or song is, in front of everything as before.
        ImDrawList *dl = MenuText_GetFont() != 0 ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();

        if (nameOn && MenuText_GetFont() != 0) {
            const FontName *fn = font_name(NF_STAGE, PortStages_Name(gUiNameIdx));
            float x = (float)gUiPresentX + (float)gUiNameX * sx, y = (float)gUiPresentY + (float)gUiNameY * sy;
            if (fn != NULL) {
                float kx = (float)gUiNameW * sx / 384.0f, ky = (float)gUiNameH * sy / 48.0f;
                font_draw(dl, fn, x - 64.0f * kx, y + (1.5f - 14.0f) * ky, kx, ky, true);
            } else {
                name_text(dl, PortStages_Name(gUiNameIdx), x, y, (float)gUiNameW * sx, (float)gUiNameH * sy, true);
            }
        } else if (nameOn && gUiNameIdx >= (int)sNameCount) { // no pre-rendered name for this one
            /* In the game's lettering if the disc's names have all its characters, else as plain text. The menu's
               rectangle is the strip image's (384x48, set 64 right and 14 down in the 512x64 row of the disc's own
               name picture); the lettering goes where that row is. The menu's movie draws the row a little below
               the clip's position: kNameDy, measured by drawing a disc name both ways (the same for the songs). */
            static const float kNameDy = 1.5f;
            const FontName *fn = font_name(NF_STAGE, PortStages_Name(gUiNameIdx));
            if (fn != NULL) {
                float kx = (float)gUiNameW * sx / 384.0f, ky = (float)gUiNameH * sy / 48.0f;
                font_draw(dl, fn, (float)gUiPresentX + (float)gUiNameX * sx - 64.0f * kx, (float)gUiPresentY + (float)gUiNameY * sy + (kNameDy - 14.0f) * ky, kx, ky, true);
            } else {
                name_text(dl, PortStages_Name(gUiNameIdx), (float)gUiPresentX + (float)gUiNameX * sx, (float)gUiPresentY + (float)gUiNameY * sy,
                          (float)gUiNameW * sx, (float)gUiNameH * sy, true);
            }
        } else if (nameOn) {
            float x = (float)gUiPresentX + (float)gUiNameX * sx;
            float y = (float)gUiPresentY + (float)gUiNameY * sy;
            float w = (float)gUiNameW * sx;
            float h = (float)gUiNameH * sy;
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sNameTex), ImVec2(x, y), ImVec2(x + w, y + h),
                         ImVec2(0.0f, (float)gUiNameIdx / (float)sNameCount),
                         ImVec2(1.0f, (float)(gUiNameIdx + 1) / (float)sNameCount));
        }
        if (songOn && MenuText_GetFont() != 0) {
            const FontName *fn = font_name(gUiSongLit ? NF_SONG_LIT : NF_SONG, PortSongs_Name(gUiSongIdx));
            float x = (float)gUiPresentX + (float)gUiSongX * sx, y = (float)gUiPresentY + (float)gUiSongY * sy;
            if (fn != NULL) {
                font_draw(dl, fn, x, y, (float)gUiSongW * sx / 512.0f, (float)gUiSongH * sy / 32.0f, false);
            } else {
                name_text(dl, PortSongs_Name(gUiSongIdx), x, y, (float)gUiSongW * sx, (float)gUiSongH * sy, false);
            }
        } else if (songOn && gUiSongIdx >= (int)sSongCount) {
            // (the rectangle is the 512x32 row of the disc's song-name picture itself)
            const FontName *fn = font_name(gUiSongLit ? NF_SONG_LIT : NF_SONG, PortSongs_Name(gUiSongIdx));
            if (fn != NULL) {
                static const float kSongDx = -0.2f, kSongDy = 2.35f;
                float kx = (float)gUiSongW * sx / 512.0f, ky = (float)gUiSongH * sy / 32.0f;
                font_draw(dl, fn, (float)gUiPresentX + (float)gUiSongX * sx + kSongDx * kx, (float)gUiPresentY + (float)gUiSongY * sy + kSongDy * ky, kx, ky, false);
            } else {
                name_text(dl, PortSongs_Name(gUiSongIdx), (float)gUiPresentX + (float)gUiSongX * sx, (float)gUiPresentY + (float)gUiSongY * sy,
                          (float)gUiSongW * sx, (float)gUiSongH * sy, false);
            }
        } else if (songOn) {
            float x = (float)gUiPresentX + (float)gUiSongX * sx;
            float y = (float)gUiPresentY + (float)gUiSongY * sy;
            float w = (float)gUiSongW * sx;
            float h = (float)gUiSongH * sy;
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sSongTex), ImVec2(x, y), ImVec2(x + w, y + h),
                         ImVec2(0.0f, (float)gUiSongIdx / (float)sSongCount),
                         ImVec2(1.0f, (float)(gUiSongIdx + 1) / (float)sSongCount));
        }
    }
    ImGui::Render();
    return true;
}

void Ui_Draw(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    if (!sGL && frame_build()) {
        Ui_DrawAgain(cmd, target);
    }
}

// The OpenGL back end's turn: over the presented picture, just before the buffers are swapped.
void Ui_DrawGL(void) {
    if (sGL && frame_build()) {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}
