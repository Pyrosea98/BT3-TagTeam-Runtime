#include "frontend/fe_app.h"

#include <cstring>

#include "frontend/fe_background.h"
#include "frontend/fe_hash.h"
#include "frontend/fe_hw.h"
#include "frontend/fe_iso9660.h"
#include "frontend/fe_music.h"
#include "frontend/fe_install.h"
#include "frontend/fe_pages.h"
#include "frontend/fe_picker.h"
#include "frontend/fe_ui.h"
#include "frontend/fe_window.h"

#include "imgui.h"
#include "runtime/pad_config.h"
#include "runtime/ps2_host_pad.h"
#include "runtime/ps2x_settings.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr float DBZ_R = 1.00f, DBZ_G = 0.62f, DBZ_B = 0.10f;
    constexpr float GOLD_R = 1.00f, GOLD_G = 0.80f, GOLD_B = 0.30f;

    ImVec4 dbz(float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); }
    ImVec4 accent(float a = 1.0f) { return dbz(DBZ_R, DBZ_G, DBZ_B, a); }
    ImVec4 gold(float a = 1.0f) { return dbz(GOLD_R, GOLD_G, GOLD_B, a); }

    void applyStyle()
    {
        ImGuiStyle &s = ImGui::GetStyle();
        s.WindowPadding    = ImVec2(14, 10);
        s.FramePadding     = ImVec2(6, 4);
        s.ItemSpacing      = ImVec2(8, 6);
        s.ItemInnerSpacing = ImVec2(5, 4);
        s.ScrollbarSize    = 12.0f;
        s.WindowRounding   = 2.0f;
        s.FrameRounding    = 2.0f;
        s.GrabRounding     = 1.0f;
        s.TabRounding      = 0.0f;
        s.ScrollbarRounding= 2.0f;
        s.WindowBorderSize = 0.0f;
        s.FrameBorderSize  = 1.0f;
        s.TabBarBorderSize = 1.0f;
        s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    }

    struct ThemeScope
    {
        static constexpr int kColors = 40;
        ThemeScope()
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg,            dbz(0.04f, 0.06f, 0.08f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg,             dbz(0.06f, 0.08f, 0.10f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg,             dbz(0.04f, 0.06f, 0.08f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border,              accent(0.55f));
            ImGui::PushStyleColor(ImGuiCol_BorderShadow,        dbz(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg,             dbz(0.04f, 0.06f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive,       dbz(0.04f, 0.06f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,    dbz(0.04f, 0.06f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_Text,                dbz(0.84f, 0.89f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled,        dbz(0.29f, 0.39f, 0.44f));
            ImGui::PushStyleColor(ImGuiCol_TextSelectedBg,      accent(0.30f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,             dbz(0.07f, 0.09f, 0.11f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,      dbz(0.10f, 0.13f, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,       dbz(1.00f, 0.62f, 0.10f, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,          accent());
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,    gold());
            ImGui::PushStyleColor(ImGuiCol_CheckMark,           accent());
            ImGui::PushStyleColor(ImGuiCol_Button,              dbz(0.07f, 0.09f, 0.11f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,       dbz(1.00f, 0.62f, 0.10f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,        dbz(1.00f, 0.62f, 0.10f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_Header,              dbz(0.09f, 0.11f, 0.13f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,       accent(0.22f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,        accent(0.35f));
            ImGui::PushStyleColor(ImGuiCol_Separator,           dbz(0.12f, 0.16f, 0.19f));
            ImGui::PushStyleColor(ImGuiCol_SeparatorHovered,    accent(0.55f));
            ImGui::PushStyleColor(ImGuiCol_SeparatorActive,     gold());
            ImGui::PushStyleColor(ImGuiCol_Tab,                 dbz(0.04f, 0.06f, 0.08f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_TabHovered,          accent(0.20f));
            ImGui::PushStyleColor(ImGuiCol_TabActive,           accent(0.14f));
            ImGui::PushStyleColor(ImGuiCol_TabUnfocused,        dbz(0.04f, 0.06f, 0.08f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive,  accent(0.10f));
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,       dbz(0.08f, 0.10f, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBg,          dbz(0.05f, 0.07f, 0.09f, 0.50f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,       dbz(1.00f, 0.62f, 0.10f, 0.04f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,         dbz(0.04f, 0.06f, 0.08f, 0.60f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,       accent(0.45f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,accent(0.70f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, gold());
            ImGui::PushStyleColor(ImGuiCol_PlotLines,           accent());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,       accent());
        }
        ~ThemeScope() { ImGui::PopStyleColor(kColors); }
        ThemeScope(const ThemeScope &) = delete;
        ThemeScope &operator=(const ThemeScope &) = delete;
    };

    bool iequals(const std::string &a, const char *b)
    {
        size_t i = 0;
        for (; i < a.size() && b[i]; ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                return false;
        return i == a.size() && b[i] == '\0';
    }

    bool endsWithNoCase(const std::string &s, const char *suffix)
    {
        const size_t n = std::char_traits<char>::length(suffix);
        if (s.size() < n)
            return false;
        return iequals(s.substr(s.size() - n), suffix);
    }

    struct ScanResult
    {
        bool dataDir = false;
        unsigned afsCount = 0;
        unsigned long long afsBytes = 0;
        std::string firstAfs;
        std::string elf;
    };

    ScanResult scanDeploy(const std::filesystem::path &exeDir)
    {
        ScanResult out;
        std::error_code ec;
        const std::filesystem::path data = exeDir / "data";
        if (!std::filesystem::is_directory(data, ec))
            return out;
        out.dataDir = true;

        const std::filesystem::path root = std::filesystem::absolute(data, ec);
        if (ec)
            return out;

        std::vector<std::filesystem::path> elfs;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             it != end && !ec; it.increment(ec))
        {
            const std::filesystem::path rel = std::filesystem::relative(it->path(), root, ec);
            if (ec)
                break;
            if (std::distance(rel.begin(), rel.end()) > 4)
            {
                if (it->is_directory())
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec))
                continue;

            const std::string name = it->path().filename().string();
            if (endsWithNoCase(name, ".afs"))
            {
                ++out.afsCount;
                out.afsBytes += (unsigned long long)it->file_size(ec);
                if (out.firstAfs.empty())
                    out.firstAfs = it->path().string();
                continue;
            }
            if (endsWithNoCase(name, ".elf") || endsWithNoCase(name, ".78") ||
                (name.size() > 4 && std::equal(name.begin(), name.begin() + 4, "SLUS",
                                               [](char a, char b) {
                                                   return std::toupper((unsigned char)a) == b;
                                               })))
                elfs.push_back(it->path());
        }

        if (!elfs.empty())
        {
            std::sort(elfs.begin(), elfs.end());
            out.elf = elfs.front().string();
        }
        return out;
    }

    // Footer actions. QUIT is the way back to the menu now that the top VOLVER is gone: the
    // window is only really quit from the menu (Esc or the X).
    void drawPlayQuit(bool canPlay, bool &playAsked, bool &backAsked)
    {
                        ImGui::PushStyleColor(ImGuiCol_Button, dbz(0.18f, 0.55f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dbz(0.25f, 0.73f, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Text, dbz(0.02f, 0.06f, 0.03f));
        ImGui::BeginDisabled(!canPlay);
        const bool pressedPlay = ImGui::Button("PLAY", ImVec2(74.0f, 0.0f));
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (pressedPlay && canPlay)
            playAsked = true;

        ImGui::SameLine(0.0f, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, dbz(0.55f, 0.16f, 0.12f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dbz(0.80f, 0.24f, 0.18f));
        if (ImGui::Button("QUIT", ImVec2(74.0f, 0.0f)))
            backAsked = true;
        ImGui::PopStyleColor(2);
    }

    // Menu buttons sit on the artwork, so they get a glow: a few concentric translucent rounded
    // rects behind the ImGui button, pulsing slowly. Cheap (4 draw-list rects) and it reads as
    // "glowing" without a shader.
    void glowButton(const char *label, const ImVec2 &size, bool enabled, bool *pressed,
                    const ImVec4 &tint)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float pulse = 0.5f + 0.5f * (float)std::sin((double)ImGui::GetTime() * 2.2);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        for (int i = 3; i >= 1; --i)
        {
            const float grow = 2.0f + 5.0f * (float)i * (0.75f + 0.25f * pulse);
            const ImU32 col = ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z,
                                                        (0.10f * (4 - i) / 3.0f) * (0.65f + 0.35f * pulse)));
            dl->AddRect(ImVec2(at.x - grow, at.y - grow), ImVec2(at.x + size.x + grow, at.y + size.y + grow),
                        col, 8.0f + grow, 0, 2.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, enabled ? tint : ImVec4(0.16f, 0.17f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(tint.x * 1.25f, tint.y * 1.25f, tint.z * 1.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(tint.x * 0.8f, tint.y * 0.8f, tint.z * 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.02f, 0.05f, 0.03f, 1.0f));
        ImGui::BeginDisabled(!enabled);
        *pressed = ImGui::Button(label, size);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(4);
    }

    void drawMenu(std::uint32_t bgTex, const ImVec2 &size, bool canPlay, bool &playAsked,
                  bool &settingsAsked, bool &navFocus)
    {
        // The bar owns the bottom strip; the artwork is laid out in what is left above it, so
        // nothing important ends up hidden behind the bar.
        const float barH = 76.0f;
        const ImVec2 artSize(size.x, size.y - barH);

        if (bgTex)
        {
            ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
            ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)bgTex), artSize);
        }

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 barTop(0.0f, artSize.y);
        dl->AddRectFilled(barTop, ImVec2(size.x, size.y), IM_COL32(0, 0, 0, 255));
        dl->AddLine(barTop, ImVec2(size.x, barTop.y), IM_COL32(255, 255, 255, 28), 1.0f);

        const ImVec2 btn(210.0f, 44.0f);
        const float by = barTop.y + (barH - btn.y) * 0.5f;
        bool pressed = false;

        ImGui::SetCursorPos(ImVec2(18.0f, by));
        // The navigation cursor has no target until we give it one, otherwise the D-pad does
        // nothing on the very first screen.
        if (navFocus)
        {
            ImGui::SetKeyboardFocusHere();
            navFocus = false;
        }
        glowButton("PLAY", btn, canPlay, &pressed, ImVec4(0.18f, 0.55f, 0.30f, 1.0f));
        if (pressed && canPlay)
            playAsked = true;

        ImGui::SetCursorPos(ImVec2(size.x - btn.x - 18.0f, by));
        pressed = false;
        glowButton("SETTINGS", btn, true, &pressed, ImVec4(0.22f, 0.36f, 0.58f, 1.0f));
        if (pressed)
            settingsAsked = true;
    }

    void drawHeader(const std::string &title)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, gold());
        ImGui::TextUnformatted(title.c_str());
        ImGui::PopStyleColor();
    }

    void statusRow(const char *label, bool ok, const char *okText, const char *badText)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(220.0f);
        ImGui::TextColored(ok ? dbz(0.25f, 0.73f, 0.31f) : dbz(0.82f, 0.60f, 0.13f),
                           "%s", ok ? okText : badText);
    }
}

namespace frontend
{
    FeAction run(const FeConfig &cfg, std::string &bootElfOut)
    {
        const std::filesystem::path exeDir(cfg.exeDir);

        // [settings] The SAME savedata/settings.toml the in-game overlay reads. It is loaded
        // before the window opens because the front-end reopens at the size the user left it
        // ([frontend] width/height), which is NOT the game's window_w/window_h: those are what
        // the game window applies on PLAY.
        const std::string configDir = (exeDir / "savedata").string();
        ps2x_settings::Settings settings;
        const bool settingsExisted = ps2x_settings::load(settings, configDir);
        ps2x_settings::Settings settingsSaved = settings;

        FeWindow win;
        if (!win.open(cfg.title, settings.feWidth, settings.feHeight))
        {
            std::fprintf(stderr, "[fe] front-end unavailable; booting directly\n");
            return FeAction::Boot;
        }
        std::fprintf(stderr, "[fe] front-end window %dx%d\n", settings.feWidth, settings.feHeight);

        ScanResult scan = scanDeploy(exeDir);
        if (scan.elf.empty() && !cfg.defaultElf.empty())
        {
            const std::filesystem::path p(cfg.defaultElf);
            std::error_code ec;
            if (std::filesystem::is_regular_file(p, ec))
                scan.elf = std::filesystem::absolute(p, ec).string();
        }

        applyStyle();
        if (win.dpiScale() > 1.0f)
            ImGui::GetStyle().ScaleAllSizes(win.dpiScale());
        // Density: the stock ImGui spacing costs ~30 px per row, which does not fit the longest
        // page (Video) into a 600 px window. These are set once on the style the front-end owns,
        // NOT pushed/popped per frame: a popup is a second ImGui window, and the per-frame
        // Push/PopStyleVar pair then unbalances against the window ImGui itself opens, which
        // spams "PopStyleVar() too many times" the moment a popup shows.
        {
            ImGuiStyle &st = ImGui::GetStyle();
            st.ItemSpacing = ImVec2(8.0f, 4.0f);
            st.FramePadding = ImVec2(6.0f, 3.0f);
            st.CellPadding = ImVec2(6.0f, 2.0f);
            st.ItemInnerSpacing = ImVec2(5.0f, 2.0f);
            st.ScrollbarSize = 11.0f;
            st.GrabMinSize = 11.0f;
            // Gamepad/keyboard navigation needs a cursor you can actually see on a dark UI.
            st.Colors[ImGuiCol_NavCursor] = dbz(1.00f, 0.78f, 0.20f, 0.95f);
        }

        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->Clear();
        const std::filesystem::path font = exeDir / "assets" / "fonts" / "RussoOne-Regular.ttf";
        std::error_code fec;
        if (std::filesystem::is_regular_file(font, fec))
            io.Fonts->AddFontFromFileTTF(font.string().c_str(), 16.0f * win.dpiScale());
        else
            io.Fonts->AddFontDefault();

        static const char *const kPages[] = {
            "Status", "Video", "Audio", "Pads", "Logging", "Misc", "About"
        };
        // The first screen is just the artwork with PLAY and SETTINGS; the tabbed window is
        // what SETTINGS opens. PS2X_FE_PAGE jumps straight into it for the headless checks.
        enum class Screen
        {
            Menu,
            Settings
        } screen = Screen::Menu;
        int page = 0;
        if (const char *p = std::getenv("PS2X_FE_PAGE"))
        {
            const int n = std::atoi(p);
            if (n >= 0 && n < (int)(sizeof(kPages) / sizeof(kPages[0])))
            {
                page = n;
                screen = Screen::Settings;
            }
        }
        bool wantBoot = false;
        bool wantQuit = false;

        // [anim] Screen, tab and boot transitions. The swap happens while the fade is at full
        // darkness, so nothing pops. PLAY holds the fade instead of returning: the game creates
        // its own window and this one is destroyed on the way out.
        enum class Transition
        {
            None,
            OpenSettings,
            BackToMenu,
            SwitchPage,
            Play
        };
        fe::Fader fader;
        Transition transition = Transition::None;
        int transitionPage = 0;
        // PLAY stops here after the fade: the screen is black and the theme is fading out, and
        // the boot only happens once the audio device has been let go.
        bool playPending = false;
        // True when the navigation cursor still needs a target (first frame, or just after the
        // screen/page it was pointing at stopped existing).
        bool navFocusWanted = true;
        auto t_prev = std::chrono::steady_clock::now();

        PageContext pageCtx;
        pageCtx.settings = &settings;
        pageCtx.exeDir = exeDir;
        pageCtx.configDir = configDir;
        pageCtx.bootElf = scan.elf;
        // [bt3save] One-shot result of the memory-card save install, drawn by the Misc page.
        static uint64_t frameCounter = 0u;
        ++frameCounter;

        // [install] The install wizard is a whole-window view on top of the tabs; it is only
        // created when the user asks for it (Varios page) or installs a texture pack (Video).
        std::unique_ptr<InstallWizard> wizard;
        FilePicker picker;
        bool showWizard = false;
        std::error_code homeEc;
        (void)homeEc;
        const std::filesystem::path homeDir = std::filesystem::path(std::getenv("USERPROFILE")
                                                                        ? std::getenv("USERPROFILE")
                                                                        : ".");

        // PS2X_INSTALL_TEST=<dump> opens the wizard straight away and drives it end to end,
        // so the verify -> extract path can be checked without a human clicking through.
        const char *installTest = std::getenv("PS2X_INSTALL_TEST");
        if (installTest && installTest[0])
        {
            wizard = std::make_unique<InstallWizard>(exeDir);
            wizard->begin(false);
            showWizard = true;
        }
        else
        {
        // The boot ELF decides whether there is anything to play, and it is checked by hash
        // rather than by name: an interrupted install, or a dump from another revision, leaves a
        // file that exists and is not this game. 2 MB of SHA-256 at startup costs nothing.
        {
            std::string why;
            if (scan.elf.empty())
            {
                why = "no boot ELF in data/";
            }
            else
            {
                bool hashed = false;
                const std::string got = fe::sha256Hex(scan.elf, hashed);
                if (!hashed)
                    why = "could not read the ELF: " + scan.elf;
                else if (got != DiscVerify::kExpectedDiscElfSha256)
                    why = "the ELF is not this revision's (sha256 " + got.substr(0, 16) + "...)";
            }
            if (why.empty())
            {
                std::fprintf(stderr, "[fe] boot ELF ok: %s\n", scan.elf.c_str());
            }
            else
            {
                std::fprintf(stderr, "[fe] %s -> opening the installer\n", why.c_str());
                // The shell opened the wizard on the user's behalf, so the welcome page would
                // only repeat what the file browser already asks for.
                wizard = std::make_unique<InstallWizard>(exeDir);
                wizard->begin(false);
                wizard->startAtLocatePage();
                showWizard = true;
            }
        }
        }


        // Headless checks: PS2X_FE_AUTOPLAY / PS2X_FE_AUTOQUIT pick the action after
        // PS2X_FE_DELAY_MS so the shell, the teardown and the handoff can be verified
        // without a human clicking. Inert unless one of them is set.
        int autoDelayMs = 0;
        if (const char *d = std::getenv("PS2X_FE_DELAY_MS"))
            autoDelayMs = std::atoi(d);
        const bool autoPlay = autoDelayMs > 0 && std::getenv("PS2X_FE_AUTOPLAY") != nullptr;
        const bool measure = std::getenv("PS2X_FE_MEASURE") != nullptr;
        const bool autoQuit = autoDelayMs > 0 && std::getenv("PS2X_FE_AUTOQUIT") != nullptr;
        const auto t_start = std::chrono::steady_clock::now();

        int bgW = 0, bgH = 0;
        const std::uint32_t bgTex = loadBackground(exeDir / "assets" / "background.png", &bgW, &bgH);
    // Menu theme from <exeDir>/music. Silence when there is no file, which is the normal case
    // for anyone who did not drop a track there.
    music::setMuted(settings.musicMuted);
    music::start(exeDir);

        // The per-player pad profiles live in <exeDir>/savedata; point PadConfig there and read
        // them once so the Mandos page shows what the game will actually use.
        ps2_stubs::PadConfig::instance().setDefaultDir(exeDir.string());
        if (ps2_stubs::PadConfig::instance().load())
            std::fprintf(stderr, "[fe] pad profiles loaded from %s\n", exeDir.string().c_str());

        // The pad tester reads the same host layer the game polls. Its SDL2 backend only needs
        // SDL_InitSubSystem(GAMECONTROLLER), so it works from this window; the raylib fallback
        // would want a raylib window we do not have, so if we land there we skip the tester.
        ps2x_pad::init();
        const bool padTesterUsable = std::strcmp(ps2x_pad::backendName(), "sdl2") == 0;
        if (!padTesterUsable)
            std::fprintf(stderr, "[fe] pad tester disabled (host pad backend is %s)\n",
                         ps2x_pad::backendName());
        else
            std::fprintf(stderr, "[fe] pad tester ready (host pad backend: %s)\n",
                         ps2x_pad::backendName());

        while (!wantBoot && !wantQuit && !win.closeRequested())
        {
            if (autoPlay || autoQuit)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - t_start)
                                         .count();
                if (elapsed >= autoDelayMs)
                {
                    if (autoPlay && !scan.elf.empty())
                        wantBoot = true;
                    else
                        wantQuit = true;
                    break;
                }
            }
            win.beginFrame();
            {
                ThemeScope theme;
                ImGuiViewport *vp = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(vp->WorkPos);
                ImGui::SetNextWindowSize(vp->WorkSize);
                const ImGuiWindowFlags hostFlags =
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::Begin("##fe_host", nullptr, hostFlags);

                const bool canPlay = !scan.elf.empty();
                // Cleared every frame so a page's footer hint never leaks into the next one.
                pageCtx.footerHint = nullptr;
                if (screen == Screen::Menu)
                {
                    bool settingsAsked = false;
                    bool playAsked = false;
                    drawMenu(bgTex, ImGui::GetContentRegionAvail(), canPlay, playAsked,
                              settingsAsked, navFocusWanted);
                    if (transition == Transition::None)
                    {
                        if (settingsAsked && fader.start(1.0f, 0.16f, 0.20f))
                            transition = Transition::OpenSettings;
                        else if (playAsked && fader.start(1.0f, 0.45f, 0.20f))
                            transition = Transition::Play;
                    }
                }
    else
    {

                        drawHeader(cfg.title);
                    ImGui::Separator();

                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    if (showWizard && wizard)
                    {
                        if (ImGui::BeginChild("##fe_wizard", ImVec2(0.0f, avail.y), ImGuiChildFlags_Borders))
                            wizard->draw();
                        ImGui::EndChild();

                        if (wizard->wantedPick() != InstallWizard::PickKind::None && !picker.isOpen())
                        {
                            if (wizard->wantedPick() == InstallWizard::PickKind::Dump)
                                picker.open("Select your game dump", homeDir,
                                            {".iso", ".img", ".7z", ".zip", ".rar", ".tar", ".gz", ".tgz"});
                            else
                                picker.open("Select the texture pack", homeDir, {".7z", ".zip"});
                            wizard->clearWantedPick();
                        }
                        if (picker.draw())
                        {
                            if (wizard->packMode())
                                wizard->onPickedPack(picker.result());
                            else
                                wizard->onPickedDump(picker.result());
                        }
                        if (wizard->closing())
                        {
                            wizard.reset();
                            showWizard = false;
                        }
                    }
                    else
                    {
                        // Sidebar padding too, so the tab labels are not glued to its border.
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                            ImVec2(ImGui::GetFontSize() * 0.6f,
                                                   ImGui::GetFontSize() * 0.4f));
                        if (ImGui::BeginChild("##fe_side", ImVec2(190.0f, avail.y), ImGuiChildFlags_Borders))
                        {
                            for (int i = 0; i < (int)(sizeof(kPages) / sizeof(kPages[0])); ++i)
                            {
                            // Park the navigation cursor on the tab being shown, so a fresh
                            // screen or a swapped page starts from a known place.
                            if (i == page && navFocusWanted)
                            {
                                ImGui::SetKeyboardFocusHere();
                                navFocusWanted = false;
                            }
                            if (ImGui::Selectable(kPages[i], page == i) && page != i &&
                                transition == Transition::None)
                            {
                                // A tab swap does not need a black screen; a quick dip reads as
                                // "the page changed" without flashing the whole window.
                                if (fader.start(0.80f, 0.08f, 0.12f))
                                {
                                    transitionPage = i;
                                    transition = Transition::SwitchPage;
                                }
                            }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleVar();

                        ImGui::SameLine(0.0f, 0.0f);
                        // Page padding keeps every label and section header off the border.
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                                            ImVec2(ImGui::GetFontSize() * 0.7f,
                                                   ImGui::GetFontSize() * 0.4f));
                        if (ImGui::BeginChild("##fe_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
                    {
                        // Fixed height, less the footer, so the action bar is pinned to the bottom.
                        // The hint line is only reserved when one was shown last frame: the page
                        // sets it later in the frame, so measuring it here would size the region
                        // without it (buttons falling off the bottom) or leave a permanent gap
                        // on the pages that never use one (bar floating mid-window).
                        static bool hintShownLastFrame = false;
                        const float ff = ImGui::GetFontSize();
                        const float footerH = ff * (hintShownLastFrame ? 1.45f : 0.0f) + ff * 2.35f;
                        if (ImGui::BeginChild("##fe_scroll", ImVec2(0.0f, -footerH), ImGuiChildFlags_None))
                        {
                        if (page == 0)
                        {
                            ImGui::TextColored(gold(), "STATUS");
                            ImGui::Separator();
                            statusRow("Game data (data/)", scan.dataDir,
                                      "present", "MISSING: install the game data");
                            // [afs] The install wizard CONVERTS the .afs files into folders, so
                            // zero of them left in data/ is the finished state, not a problem.
                            // Reporting that as a yellow "none" read as if something were missing.
                            if (scan.afsCount > 0)
                            {
                                char buf[128];
                                std::snprintf(buf, sizeof buf, "%u  (%.1f MB)",
                                              scan.afsCount, scan.afsBytes / 1048576.0);
                                statusRow("AFS containers", true, buf, "");
                            }
                            else
                            {
                                statusRow("AFS containers", true, "Extracted", "");
                            }
                            statusRow("Boot ELF", canPlay, "found", "MISSING: the ELF was not found");
                            if (canPlay)
                                ImGui::TextWrapped("%s", scan.elf.c_str());
                            ImGui::Separator();
                            ImGui::TextColored(gold(), "YOUR HARDWARE");
                            // Probed once: this walks the registry and DXGI, which has no business
                            // running 60 times a second.
                            {
                                static const std::string hwLine = hw::summary(hw::detect());
                                if (hwLine.empty())
                                    ImGui::TextDisabled("Could not read the hardware");
                                else
                                    ImGui::TextWrapped("%s", hwLine.c_str());
                            }
                            ImGui::Separator();
                            ImGui::Separator();
                            if (!scan.dataDir || scan.afsCount == 0)
                            {
                                if (fe::primaryButton("INSTALL GAME DATA", ImVec2(240.0f, 30.0f)))
                                {
                                    wizard = std::make_unique<InstallWizard>(exeDir);
                                    wizard->begin(false);
                                    showWizard = true;
                                }
                            }
                            ImGui::TextDisabled("You can also install them from Misc > Reinstall mode.");
                        }
                        else if (page == 1)
                        {
                            drawVideoPage(pageCtx);
                        }
                        else if (page == 2)
                        {
                            drawAudioPage(pageCtx);
                        }
                        else if (page == 3)
                        {
                            drawInputPage(pageCtx);
                        }
                        else if (page == 4)
                        {
                            drawLoggingPage(pageCtx);
                        }
                        else if (page == 5)
                        {
                            drawMiscPage(pageCtx);
                        }
                        else
                        {
                            drawAboutPage(pageCtx);
                        }
                        // PS2X_FE_MEASURE reports how tall each page really is against the
                        // viewport, so "does it all fit" is a number and not a guess.
                        if (measure)
                            std::fprintf(stderr, "[fe-measure] %-10s %5.0f px content / %5.0f px window  %s\n",
                                         kPages[page], ImGui::GetCursorPosY(), ImGui::GetWindowHeight(),
                                         ImGui::GetCursorPosY() > ImGui::GetWindowHeight() ? "DESBORDA" : "entra");
                        }   // fe_scroll
                        ImGui::EndChild();

                        // Footer: a page can leave one line of text that goes just above the bar
                        // (so it costs no page height), and the bar is only a separator line
                        // here, not a black slab: the menu owns the heavy bar.
                        {
                            const float f = ImGui::GetFontSize();
                            const char *hint = pageCtx.footerHint;
                            if (hint)
                            {
                                const float hintH = f * 1.35f;
                                ImGui::Dummy(ImVec2(0.0f, hintH));
                                const ImVec2 hp = ImGui::GetCursorScreenPos();
                                ImDrawList *hdl = ImGui::GetWindowDrawList();
                                hdl->AddText(ImGui::GetFont(), f * 0.85f,
                                             ImVec2(hp.x, hp.y - hintH * 0.95f),
                                             ImGui::GetColorU32(fe::warnCol()), hint);
                            }
                            hintShownLastFrame = hint != nullptr;
                            const float barTop = ImGui::GetCursorScreenPos().y - f * 0.45f;
                            ImDrawList *fdl = ImGui::GetWindowDrawList();
                            fdl->AddLine(ImVec2(0.0f, barTop), ImVec2(ImGui::GetWindowWidth(), barTop),
                                         ImGui::GetColorU32(fe::accent(0.35f)), 1.0f);
                        }

                    bool playAsked = false;
                    bool backAsked = false;
                    drawPlayQuit(canPlay, playAsked, backAsked);
                        if (transition == Transition::None)
                        {
                            if (backAsked && fader.start(1.0f, 0.16f, 0.20f))
                                transition = Transition::BackToMenu;
                            else if (playAsked && fader.start(1.0f, 0.45f, 0.20f))
                                transition = Transition::Play;
                        }
                        ImGui::SameLine(0.0f, 18.0f);
                        if (settings != settingsSaved)
                            ImGui::TextColored(fe::gold(), "unsaved changes");
                        else
                            ImGui::TextDisabled("no pending changes");
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f);
                        if (fe::primaryButton("SAVE AND APPLY", ImVec2(150.0f, 0.0f), true))
                        {
                            // Save and apply: the file is written now, and the per-player pad
                            // profiles are flushed too, so what the game reads at boot is exactly
                            // what the window shows.
                            const bool okSettings = ps2x_settings::save(settings, configDir);
                            const bool okPads = ps2_stubs::PadConfig::instance().save();
                            if (okSettings)
                                settingsSaved = settings;
                            std::fprintf(stderr, "[fe] settings %s, pad config %s\n",
                                         okSettings ? "saved" : "SAVE FAILED",
                                         okPads ? "saved" : "not written");
                        }
                        }   // fe_page
                        ImGui::PopStyleVar();

                        // A page asked for the install view or a pack picker.
                        if (pageCtx.requestInstallWizard || pageCtx.requestPackInstall)
                        {
                            wizard = std::make_unique<InstallWizard>(exeDir);
                            wizard->begin(pageCtx.requestInstallWizard && scan.afsCount > 0);
                            wizard->setPackMode(pageCtx.requestPackInstall);
                            showWizard = true;
                            if (pageCtx.requestPackInstall)
                                wizard->requestPick(InstallWizard::PickKind::Pack);
                            pageCtx.requestInstallWizard = false;
                            pageCtx.requestPackInstall = false;
                        }
                        // [bt3save] Install the progressed memory-card save. The mc* layer cannot
                        // write the card back, so this is the only way to get a card with progress
                        // on it. Backup the old one, drop the new one in, and say so plainly:
                        // anything earned in-session is still lost on exit.
                        if (pageCtx.requestSaveInstall)
                        {
                            pageCtx.requestSaveInstall = false;
                            const std::filesystem::path src = exeDir / "saves" / "BASLUS-21678DBZT3" / "BASLUS-21678DBZT3";
                            const std::filesystem::path dir = exeDir / "savedata" / "BASLUS-21678DBZT3";
                            const std::filesystem::path dst = dir / "BASLUS-21678DBZT3";
                            std::error_code ec;
                            std::string msg, col = "ok";
                            if (!std::filesystem::is_regular_file(src, ec))
                            {
                                msg = "completed save not found in saves/";
                                col = "bad";
                            }
                            else
                            {
                                std::filesystem::create_directories(dir, ec);
                                if (std::filesystem::is_regular_file(dst, ec))
                                {
                                    ec.clear();
                                    std::filesystem::copy_file(dst, dir / "BASLUS-21678DBZT3.bak",
                                                               std::filesystem::copy_options::overwrite_existing, ec);
                                    if (ec)
                                    {
                                        msg = "backup failed: " + ec.message();
                                        col = "bad";
                                    }
                                }
                                ec.clear();
                                std::filesystem::copy_file(src, dst,
                                                           std::filesystem::copy_options::overwrite_existing, ec);
                                if (ec)
                                {
                                    msg = "copy failed: " + ec.message();
                                    col = "bad";
                                }
                                else
                                {
                                    msg = "Completed save installed. Restart the game.";
                                    std::fprintf(stderr, "[fe] bt3 save installed: %s\n", dst.string().c_str());
                                }
                            }
                            pageCtx.footerHint = nullptr;
                            // [bt3save] the page reads these back; PageContext is per-frame so this
                            // is how the result crosses from the handler to the UI.
                            pageCtx.saveInstallMsg = msg;
                            pageCtx.saveInstallOk = (col == "ok");
                            pageCtx.saveInstallMsgFrame = frameCounter;
                        }
                    }
                    ImGui::EndChild();

                }
                ImGui::End();
                ImGui::PopStyleVar();
            }
                // Esc walks back one level: Ajustes -> menu, menu -> quit.
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                    transition == Transition::None)
                {
                    if (screen == Screen::Settings)
                    {
                        if (fader.start(1.0f, 0.16f, 0.20f))
                            transition = Transition::BackToMenu;
                    }
                    else
                        wantQuit = true;
                }

                // [anim] Advance the fade and run the pending swap at full darkness, so the menu,
                // the settings page and the handoff to the game never cut hard.
                const auto t_now = std::chrono::steady_clock::now();
                const float dt = std::chrono::duration<float>(t_now - t_prev).count();
                t_prev = t_now;
                fader.tick(dt, [&] {
                    switch (transition)
                    {
                    case Transition::OpenSettings:
                        screen = Screen::Settings;
                        navFocusWanted = true;
                        break;
                    case Transition::BackToMenu:
                        screen = Screen::Menu;
                        navFocusWanted = true;
                        break;
                    case Transition::SwitchPage:
                        page = transitionPage;
                        navFocusWanted = true;
                        break;
                    case Transition::Play:
                        // Hold on black while the menu theme fades away, then let the game have
                        // the audio device. playPending is what the frame loop watches.
                        playPending = true;
                        fader.hold();
                        music::fadeOut(0.55f);
                        break;
                    case Transition::None:
                        break;
                    }
                transition = Transition::None;
                });
                music::update(dt);
                if (playPending && music::silent())
                {
                    wantBoot = true;
                    playPending = false;
                }
                fader.draw();
                win.endFrame();
        }
        // Remember how the user left this window. Queried before shutdown, and saved whenever
        // it moved, on top of the usual "only if the game settings changed" rule.
        {
            int curW = settings.feWidth, curH = settings.feHeight;
            if (win.querySize(&curW, &curH) && curW > 0 && curH > 0 &&
                (curW != settings.feWidth || curH != settings.feHeight))
            {
                settings.feWidth = curW;
                settings.feHeight = curH;
                settingsSaved.feWidth = curW;
                settingsSaved.feHeight = curH;
                if (ps2x_settings::save(settings, configDir))
                    std::fprintf(stderr, "[fe] front-end window size saved: %dx%d\n", curW, curH);
            }
        }
        if (settings != settingsSaved)
        {
            if (ps2x_settings::save(settings, configDir))
                std::fprintf(stderr, "[fe] settings written to %s\n",
                             ps2x_settings::configPath(configDir).c_str());
            else
                std::fprintf(stderr, "[fe] settings NOT written (save failed)\n");
        }
        if (padTesterUsable)
            ps2x_pad::shutdown();
        std::fprintf(stderr, "[fe] settings: %s\n",
                     settingsExisted ? "loaded existing settings.toml"
                                     : "no settings.toml yet (defaults written)");

        // The runtime opens its own audio device, and two WASAPI clients fight over it: hand the
    // device back before returning, whatever way the front-end ended.
    music::shutdown();
    win.shutdown();
        if (wantBoot && !scan.elf.empty())
            bootElfOut = scan.elf;
        std::fprintf(stderr, "[fe] front-end done (boot=%d elf=%s)\n",
                     wantBoot ? 1 : 0, bootElfOut.c_str());
        return wantBoot ? FeAction::Boot : FeAction::Quit;
    }
}
