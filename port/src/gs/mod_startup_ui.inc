#include "mod_credits_data.inc"
// Approved mod branding, presented by the native SDL GPU/OpenGL overlay.
static SDL_GPUTexture *sModBrand;
static bool sModBrandTried;
static SDL_GPUTexture *mod_brand_texture(void) {
    if (sModBrandTried) return sModBrand;
    sModBrandTried = true;
    char path[1400];
    snprintf(path, sizeof(path), "%s/tagteam-ui/brand-logo.rgba", Port_FileRoot());
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { fprintf(stderr, "bt3: missing mod brand: %s\n", path); return NULL; }
    uint32_t h[2];
    if (fread(h, sizeof(h), 1, fp) != 1 || !h[0] || !h[1] || h[0] > 4096 || h[1] > 4096) {
        fclose(fp); return NULL;
    }
    size_t bytes = (size_t)h[0] * h[1] * 4;
    uint8_t *pix = (uint8_t *)malloc(bytes);
    if (pix == NULL || fread(pix, 1, bytes, fp) != bytes || fgetc(fp) != EOF) {
        free(pix); fclose(fp); return NULL;
    }
    fclose(fp);
    sModBrand = Ui_Texture(pix, h[0], h[1]); // upload owns pix, on the rendering thread
    return sModBrand;
}

static void mod_startup_draw(void) {
    int frame = sStartupCreditsFrame.load(std::memory_order_acquire);
    if (frame <= 0) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(0, 0, 0, 255));
    // Contain 640x448 composition; all unused space is the same black background.
    float k = screen.x / 640.0f;
    if (screen.y / 448.0f < k) k = screen.y / 448.0f;
    float ox = (screen.x - 640*k)*0.5f, oy = (screen.y - 448*k)*0.5f;
    bool thanksPage = frame > 120;
    int cardFrame = thanksPage ? frame-120 : frame;
    float fade = cardFrame < 15 ? cardFrame/15.0f : cardFrame > 105 ? (120-cardFrame)/15.0f : 1.0f;
    auto at = [=](float x, float y) { return ImVec2(ox+x*k, oy+y*k); };
    auto colour = [=](int r, int g, int b, int alpha=255) { return IM_COL32(r,g,b,(int)(alpha*fade)); };
    ImFont *font = Ui_FontBold();
    if (font == NULL) font = ImGui::GetFont();
    auto line = [&](const char *t, float y, float size, ImU32 col) {
        float fs = size*k;
        ImVec2 ext = font->CalcTextSizeA(fs, 10000, 0, t);
        if (ext.x > 560*k) fs *= 560*k/ext.x;
        ext = font->CalcTextSizeA(fs, 10000, 0, t);
        dl->AddText(font, fs, ImVec2((screen.x-ext.x)*0.5f, oy+y*k), col, t);
    };
    for (int i=0;i<28;i++) dl->AddCircleFilled(at(16+(i*173)%608,12+(i*79)%410), (i%5==0?1.2f:.55f)*k, colour(255,221,126,65));
    dl->AddRectFilled(ImVec2(0,oy),ImVec2(screen.x,oy+3*k),colour(228,183,66,210));
    dl->AddRectFilled(ImVec2(0,oy+445*k),ImVec2(screen.x,oy+448*k),colour(228,183,66,210));
    SDL_GPUTexture *brand = mod_brand_texture();
    if (brand) dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)brand),at(100,28),at(540,176),ImVec2(0,0),ImVec2(1,1),colour(255,255,255));
    else line("Tag Team Mod - Real Power Scale",74,28,colour(255,209,85));
    bool es = Port_Setting("mod_language",1) == 1;
    line(es ? "Un proyecto mod de Budokai Tenkaichi 3" : "A Budokai Tenkaichi 3 mod project",184,14,colour(255,255,255));
    if (!thanksPage) {
        for (int i=0;i<2;i++) {
            float y=222+i*66;char title[160];
            dl->AddRectFilled(at(42,y),at(598,y+60),colour(18,30,43,165));
            dl->AddRectFilled(at(42,y),at(44,y+60),colour(228,183,66,210));
            snprintf(title,sizeof(title),"%s: %s",es?kModCreditRolesEs[i]:kModCreditRoles[i],kModCreditNames[i]);
            line(title,y+10,17,colour(255,192,72));
            line(kModCreditUrls[i],y+35,11,colour(255,255,255));
        }
        line(kModCreditNames[3],379,14,colour(255,192,72));
        line(kModCreditUrls[3],401,11,colour(255,255,255));
    } else {
        char title[160];snprintf(title,sizeof(title),"%s: %s",es?kModCreditRolesEs[2]:kModCreditRoles[2],kModCreditNames[2]);
        dl->AddRectFilled(at(42,238),at(598,327),colour(18,30,43,165));
        dl->AddRectFilled(at(42,238),at(44,327),colour(228,183,66,210));
        line(title,251,20,colour(255,192,72));
        line(kModCreditUrls[2],292,13,colour(255,255,255));
        line(kModThanks[es?1:0],355,12,colour(255,255,255));
        line(es?"Gracias!":"Thank you!",394,22,colour(255,192,72));
    }
}

// Same credit source as the startup and installer assets; keyboard/gamepad links.
static void mod_credits_settings(void) {
    bool es=Port_Setting("mod_language",1)==1;
    if(!ImGui::CollapsingHeader(es?"Creditos y agradecimientos":"Credits and thanks"))return;
    for(int i=0;i<4;i++){
        ImGui::PushID(i);
        ImGui::TextColored(ImVec4(1,.77f,.28f,1),"%s: %s",es?kModCreditRolesEs[i]:kModCreditRoles[i],kModCreditNames[i]);
        ImGui::TextLinkOpenURL(kModCreditUrls[i],kModCreditUrls[i]);
        ImGui::PopID();
    }
    ImGui::TextWrapped("%s",kModThanks[es?1:0]);
    ImGui::Separator();
}
