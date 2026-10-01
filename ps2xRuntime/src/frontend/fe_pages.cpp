#include "frontend/fe_pages.h"

#include "frontend/fe_gpu.h"
#include "frontend/fe_hash.h"
#include "frontend/fe_hw.h"
#include "frontend/fe_iso9660.h"
#include "frontend/fe_music.h"
#include "frontend/fe_ui.h"
#include "frontend/fe_window.h"

#include "imgui.h"
#include "raylib.h"
#include "runtime/pad_config.h"
#include "runtime/ps2_host_pad.h"
#include "runtime/ps2x_achieve.h"   // [ach] the counts the Misc page shows

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr const char *const kResolutionLabels[] = {
        "1024 x 768", "1280 x 720", "1360 x 768", "1366 x 768", "1440 x 900",
        "1600 x 900", "1920 x 1080", "2560 x 1440", "3440 x 1440", "3840 x 2160"
    };
    constexpr int kResolutionW[] = {1024, 1280, 1360, 1366, 1440, 1600, 1920, 2560, 3440, 3840};
    constexpr int kResolutionH[] = {768, 720, 768, 768, 900, 900, 1080, 1440, 1440, 2160};
    constexpr int kResolutionCount = 10;
    const char *const kCustomResolution = "Custom";

    // SLUS_216.78 (US) -- must stay in sync with games/bt3/setup.py.
    // The expected boot-ELF digest lives in fe_iso9660.h next to the code that verifies the
// dump: one constant, so the status page and the wizard cannot disagree.
using DiscVerify::kExpectedDiscElfSha256;

    enum class DataState
    {
        Missing,
        Corrupt,
        Valid
    };

    unsigned long long dirSize(const std::filesystem::path &root, int maxDepth = 6)
    {
        std::error_code ec;
        unsigned long long total = 0;
        if (!std::filesystem::is_directory(root, ec))
            return 0;
        std::filesystem::recursive_directory_iterator it(root, ec), end;
        for (; it != end && !ec; it.increment(ec))
        {
            if (it->is_directory(ec))
            {
                if (std::distance(it->path().begin(), it->path().end()) > maxDepth)
                    it.disable_recursion_pending();
                continue;
            }
            const std::uintmax_t sz = it->file_size(ec);
            if (!ec)
                total += (unsigned long long)sz;
        }
        return total;
    }

    DataState verifyInstalledData(const std::filesystem::path &dataDir)
    {
        const std::filesystem::path boot = dataDir / "SLUS_216.78";
        std::error_code ec;
        if (!std::filesystem::exists(boot, ec))
            return DataState::Missing;
        bool hashed = false;
        const std::string got = fe::sha256Hex(boot, hashed);
        if (!hashed)
            return DataState::Corrupt;
        return got == kExpectedDiscElfSha256 ? DataState::Valid : DataState::Corrupt;
    }

    struct PackStatus
    {
        unsigned files = 0;
        unsigned long long bytes = 0;
    };

    PackStatus scanTexturePack(const std::filesystem::path &dataDir)
    {
        PackStatus st;
        std::error_code ec;
        const std::filesystem::path root = dataDir / "Textures";
        if (!std::filesystem::is_directory(root, ec))
            return st;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            ++st.files;
            const std::uintmax_t sz = it->file_size(ec);
            if (!ec)
                st.bytes += (unsigned long long)sz;
        }
        return st;
    }
}

namespace frontend
{
    // Video: the settings you almost always want (renderer, GPU, resolution, scale, monitor,
    // window mode) stay open and unboxed; the rest is grouped into collapsible sections and
    // the "only in game" note is a plain line instead of a section.
    static const char *const kVideoSections[] = {
        "EFFECTS AND FILTERING", "PACK AND OPTIONS", "RECOMMENDED"
    };

    // The game window's GL context is created by SDL with no adapter argument, so on Windows the
    // only lever the OS exposes is the per-app graphics preference. That makes this a
    // launch-time choice: it is written the moment it changes and lands on the next game start.
    static void drawGpuRow(ps2x_settings::Settings &s)
    {
        static std::vector<gpu::Adapter> list;
        static std::vector<std::string> names;
        static std::vector<const char *> ptrs;
        static std::string lastError;
        static bool listed = false;
        if (!listed)
        {
            listed = true;
            list = gpu::adapters(gpu::currentRenderer());
            for (const gpu::Adapter &a : list)
                std::fprintf(stderr, "[fe] gpu: %s (%llu MB)%s%s\n", a.name.c_str(),
                             static_cast<unsigned long long>(a.vramMB), a.software ? " software" : "",
                             a.active ? " ACTIVE" : "");
            // WARP and the other software adapters are not a choice: picking one would run the
            // game on the CPU rasterizer. They stay in the log, out of the dropdown.
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [](const gpu::Adapter &a) { return a.software; }),
                       list.end());
        }
        if (list.empty())
        {
            if (!gpu::supported())
            {
                fe::rowLabel("GPU");
                ImGui::TextDisabled("GPU selection is Windows-only");
            }
            return;
        }

        names.clear();
        ptrs.clear();
        names.push_back("Automatic (Windows decides)");
        ptrs.push_back(names.back().c_str());
        int current = 0;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            char buf[320];
            std::snprintf(buf, sizeof buf, "%s  (%llu GB)%s", list[i].name.c_str(),
                          static_cast<unsigned long long>((list[i].vramMB + 512) / 1024),
                          list[i].active ? "   <-- ACTIVE" : "");
            names.push_back(buf);
            ptrs.push_back(names.back().c_str());
            if (!s.gpu.empty() && list[i].name == s.gpu)
                current = static_cast<int>(i) + 1;
        }

        if (list.size() == 1)
        {
            // One adapter is not a choice; name it and move on.
            fe::rowLabel("GPU");
            ImGui::Text("%s", names[1].c_str());
            if (s.gpu.empty() && list[0].active)
                s.gpu = list[0].name;
            return;
        }

        if (fe::comboRowStr("GPU", &current, ptrs.data(), static_cast<int>(ptrs.size()),
                            names[current].c_str()))
        {
            s.gpu = current == 0 ? std::string() : list[current - 1].name;
            std::string err;
            lastError = gpu::applyPreference(s.gpu, &err) ? std::string() : err;
        }
        if (!lastError.empty())
            ImGui::TextColored(fe::warnCol(), "%s", lastError.c_str());
        else if (current > 0 && !list[current - 1].active)
            ImGui::TextDisabled("Applied when the game restarts");
    }

    // Write a detected recommendation into the settings the shell owns. The texture pack is only
    // switched on when one is actually installed, so a recommendation never points at files
    // that are not there.
    static void applyRecommendation(ps2x_settings::Settings &s, const hw::Recommendation &r,
                                    const std::filesystem::path &dataDir)
    {
        std::error_code ec;
        const std::filesystem::path packDir = dataDir / "Textures";
        const bool packThere =
            std::filesystem::is_directory(packDir, ec) &&
            std::filesystem::directory_iterator(packDir, ec) != std::filesystem::directory_iterator();

        s.renderScale = r.renderScale;
        // widescreen is not touched here: it is fixed, so a recommendation has no say over it.
        s.texPack = r.texPackFull && packThere;
        s.fps60 = r.fps60;
        s.windowMode = r.windowMode;
        s.fullscreen = r.windowMode == 2;
        if (r.windowMode != 2)
        {
            int w = 0, h = 0;
            if (frontend::monitorSize(s.monitor, &w, &h))
            {
                s.windowW = w;
                s.windowH = h;
            }
        }
    }

    void drawVideoPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;
        const std::filesystem::path dataDir = ctx.exeDir / "data";

        // --- fixed, always visible ---
        fe::sectionHeader("DISPLAY");
        {
            // [nativeopt] the entries are what this build can do; the value is the settings' renderer id, not the row
            static const char *const items[] = {"OpenGL (New)", "Software",
#if defined(PS2X_HAVE_PGS)
                "paraLLEl-GS (Vulkan)",
#endif
#if defined(PS2X_HAVE_SEAMVK)
                "Native Vulkan (engine seam)",
#endif
            };
            static const int values[] = {ps2x_settings::kRendererOpenGL, ps2x_settings::kRendererSoftware,
#if defined(PS2X_HAVE_PGS)
                ps2x_settings::kRendererParallelGS,
#endif
#if defined(PS2X_HAVE_SEAMVK)
                ps2x_settings::kRendererNative,
#endif
            };
            const int count = (int)(sizeof(values) / sizeof(values[0]));
            int row = 0;
            for (int i = 0; i < count; ++i) if (values[i] == s.renderer) { row = i; break; }
            fe::comboRow("Graphics", &row, items, count);
            if (values[row] != s.renderer)
                s.renderer = values[row];
        }
        {
            const int n = frontend::monitorCount();
            static std::vector<std::string> names;
            static std::vector<const char *> namePtrs;
            names.clear();
            namePtrs.clear();
            for (int i = 0; i < n; ++i)
            {
                names.push_back(std::to_string(i) + ": " + frontend::monitorName(i));
                namePtrs.push_back(names.back().c_str());
            }
            if (namePtrs.empty())
            {
                fe::rowLabel("Monitor");
                ImGui::TextDisabled("Could not enumerate monitors");
            }
            else
            {
                int mon = s.monitor;
                fe::comboRow("Monitor", &mon, namePtrs.data(), (int)namePtrs.size());
                if (mon >= 0 && mon < n)
                    s.monitor = mon;
            }
        }
        {
            fe::rowLabel("Window mode");
            ImGui::RadioButton("Windowed", &s.windowMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Borderless", &s.windowMode, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Fullscreen", &s.windowMode, 2);
        }

        {
            int current = -1;
            for (int i = 0; i < kResolutionCount; ++i)
                if (s.windowW == kResolutionW[i] && s.windowH == kResolutionH[i])
                    current = i;
            const char *label = current >= 0 ? kResolutionLabels[current] : kCustomResolution;
            if (fe::comboRowStr("Resolution", &current, kResolutionLabels, kResolutionCount, label))
            {
                if (current >= 0)
                {
                    s.windowW = kResolutionW[current];
                    s.windowH = kResolutionH[current];
                }
            }
        }
        {
            fe::rowLabel("Render scale");
            ImGui::RadioButton("1x", &s.renderScale, 1);
            ImGui::SameLine();
            ImGui::RadioButton("2x", &s.renderScale, 2);
            ImGui::SameLine();
            ImGui::RadioButton("3x", &s.renderScale, 3);
        }
        drawGpuRow(s);

        if (fe::beginSection("EFFECTS AND FILTERING", false,
                             "The cel outline at 199% replicates the console's line; lower means "
                             "thinner ink. Only the switches the chosen renderer reads are shown."))
        {
            fe::toggleSwitch("Cel outline", &s.outline);
            if (s.outline)
                fe::intSliderRow("Outline strength", &s.inkStrength, 100, 400, "%d %%");
            fe::toggleSwitch("Character shadows", &s.shadows);
            fe::toggleSwitch("Depth of field (DoF)", &s.dofBlur);
            if (s.dofBlur)
                fe::intSliderRow("DoF range", &s.dofZFar, 20000, 800000, "%d k");
            // [rendererfx] only what the chosen renderer reads: the glow and bilinear switches are the OpenGL
            // renderer's (ps2_gs_gpu_renderer.cpp is their only consumer), force filtering is OpenGL's and
            // paraLLEl-GS's; the native renderer reads none of them.
            if (s.renderer == ps2x_settings::kRendererOpenGL)
            {
                fe::toggleSwitch("Glow (Kaioken aura)", &s.glow);
                fe::toggleSwitch("Bilinear filter", &s.bilinear);
            }
            if (s.renderer == ps2x_settings::kRendererOpenGL || s.renderer == ps2x_settings::kRendererParallelGS)
                fe::toggleSwitch("Force filtering (soft terrain)", &s.forceBilinear);
        }

        if (fe::beginSection("PACK AND OPTIONS", false,
                             "The pack installs into the folder above: the file is decompressed "
                             "there. The automatic download no longer exists. The options apply when the "
                             "to start the game."))
        {
            const PackStatus pack = scanTexturePack(dataDir);
            if (pack.files > 0)
            {
                char size[32];
                fe::formatBytes(pack.bytes, size, sizeof size);
                fe::statusRow("Pack", fe::okCol(), "installed");
                char line[96];
                std::snprintf(line, sizeof line, "%u files, %s", pack.files, size);
                fe::statusRow("Content", fe::dbz(0.84f, 0.89f, 0.92f), line);
            }
            else
            {
                fe::statusRow("Pack", fe::warnCol(), "not installed");
            }
            char path[512];
            std::snprintf(path, sizeof path, "%s", (dataDir / "Textures").string().c_str());
            fe::pathRow("Folder", path);

            fe::toggleSwitch("Enable texture replacement", &s.texPack);
            fe::rowLabel("Install");
            if (ImGui::Button("Install pack from file..."))
                ctx.requestPackInstall = true;
            fe::toggleSwitch("4K intro video", &s.introVideo);
            static const char *const buttons[] = {"PS2 (original buttons)", "Xbox"};
            fe::comboRow("Button layout", &s.buttonLayout, buttons, 2);
        }

        if (fe::beginSection("RECOMMENDED", false,
                             "Detects your CPU, RAM and GPU and runs a single-threaded benchmark to "
                             "decide the tier. Apply writes the render scale, "
                             "the texture pack, 60 fps and window mode."))
        {
            static hw::Recommendation rec;
            static std::string recText;
            static bool haveRec = false;
            static bool applied = false;
            if (fe::primaryButton("DETECT", ImVec2(120.0f, 28.0f)))
            {
                rec = hw::recommend(hw::detect(), hw::benchSingleThreadR());
                char buf[256];
                std::snprintf(buf, sizeof buf,
                              "%s  -  %dx render, pack %s, %d fps, %s",
                              rec.tierName.c_str(), rec.renderScale,
                              rec.texPackFull ? "4K" : "lite", rec.fps60 ? 60 : 30,
                              rec.windowMode == 2 ? "fullscreen"
                                                  : rec.windowMode == 1 ? "borderless" : "windowed");
                recText = buf;
                haveRec = true;
                applied = false;
            }
            ImGui::SameLine(0.0f, 10.0f);
            if (fe::primaryButton("APPLY", ImVec2(120.0f, 28.0f)) && haveRec)
            {
                applyRecommendation(s, rec, dataDir);
                applied = true;
            }
            if (!recText.empty())
            {
                ImGui::TextWrapped("%s", recText.c_str());
                if (applied)
                    ImGui::TextColored(fe::okCol(), "Applied. Save it so it survives closing.");
                else if (haveRec)
                    ImGui::TextDisabled("Applies the values above to this page's settings.");
            }
        }

        ctx.footerHint = "More toggles (widescreen, 60 fps, HUD layout) live in the game overlay: Shift+Tab.";
    }

    void drawAudioPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;

    if (fe::beginSection("VOLUMES", true,
                         "The game and the Dragon Net menu share these three volumes."))
    {
        fe::sliderRow("Master", &s.master, 0.0f, 1.0f, "%.2f");
        fe::sliderRow("Music", &s.music, 0.0f, 1.0f, "%.2f");
        fe::sliderRow("SFX", &s.sfx, 0.0f, 0.4f, "%.2f");
    }

    // The menu theme is a local file, not part of the project, so muting it is a shell setting
    // and not one of the game's own volume sliders. It applies on the spot instead of waiting
    // for the session to end, which is why it is written out immediately.
    {
        bool muted = s.musicMuted;
        if (fe::toggleSwitch("Mute the menu music", &muted) && muted != s.musicMuted)
        {
            s.musicMuted = muted;
            music::setMuted(muted);
            if (ps2x_settings::save(s, ctx.configDir.string()))
                std::fprintf(stderr, "[fe] menu theme %s\n", muted ? "muted" : "unmuted");
        }
        if (music::trackName().empty())
            fe::hintPending("No track. Drop a music.flac in "
                            "assets/music/ turns it on.");
    }
    }

    // Live pad tester: reads the same host pad layer the game polls, so what lights up here is
    // exactly what the game will see. Button and axis indices are raylib's GAMEPAD_* values,
    // which is the convention pad_pN.conf persists.
    void drawPadTester(int player, ps2_stubs::PadConfig &pads)
    {
        ps2x_pad::update();

        // Which physical pad is the selected player bound to?
        const ps2_stubs::PadPlayerConfig cfg = pads.snapshot((size_t)player);
        int slot = -1;
        if (cfg.device.kind == ps2_stubs::PadDeviceKind::Gamepad)
            slot = cfg.device.gamepad;

        // Fall back to the first real controller so "Automatico" is still testable.
        if (slot < 0)
        {
            for (int i = 0; i < ps2x_pad::kMaxSlots; ++i)
                if (ps2x_pad::isController(i))
                {
                    slot = i;
                    break;
                }
        }

        if (slot < 0 || !ps2x_pad::available(slot))
        {
            fe::hintPending("No controller connected. Connect one and the tester turns on by "
                            "itself; until then the game uses the keyboard.");
            return;
        }

        fe::kv("Physical pad", ps2x_pad::name(slot) ? ps2x_pad::name(slot) : "(no name)");
        fe::kv("Assigned to", cfg.device.kind == ps2_stubs::PadDeviceKind::Gamepad
                                 ? "this pad, fixed"
                                 : (cfg.device.kind == ps2_stubs::PadDeviceKind::Keyboard
                                        ? "keyboard only"
                                        : "automatic (first pad)"));

        // [padtest] The same tester as the in-game overlay's Controllers tab (drawGamepadTestArea), so the two
        // read the same: a grid of the buttons with the pressed ones lit, then one row of the two sticks and the
        // two trigger bars with a readout under each. The old one drew its own d-pad / diamond / boxes.
        static const struct { int idx; const char *label; } kButtons[] = {
            { GAMEPAD_BUTTON_MIDDLE_LEFT, "Select" }, { GAMEPAD_BUTTON_MIDDLE_RIGHT, "Start" }, { GAMEPAD_BUTTON_MIDDLE, "Guide" },
            { GAMEPAD_BUTTON_LEFT_FACE_UP, "D-Up" },  { GAMEPAD_BUTTON_LEFT_FACE_DOWN, "D-Down" },
            { GAMEPAD_BUTTON_LEFT_FACE_LEFT, "D-Left" }, { GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "D-Right" },
            { GAMEPAD_BUTTON_RIGHT_FACE_UP, "Y / Tri" }, { GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "B / Cir" },
            { GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "A / X" }, { GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "X / Sq" },
            { GAMEPAD_BUTTON_LEFT_TRIGGER_1, "LB / L1" }, { GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "RB / R1" },
            { GAMEPAD_BUTTON_LEFT_THUMB, "L3" }, { GAMEPAD_BUTTON_RIGHT_THUMB, "R3" },
        };
        static const int kNumBtns = (int)(sizeof(kButtons) / sizeof(kButtons[0]));
        const float f = fe::unit();
        if (ImGui::BeginChild("##fe_pad_test", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Buttons");
            ImGui::Spacing();
            const float availX = ImGui::GetContentRegionAvail().x;
            const float btnW = std::max(24.0f, (availX - 8.0f * 9.0f) / 8.0f);
            const float btnH = f * 1.5f;
            int col = 0;
            for (int i = 0; i < kNumBtns; ++i)
            {
                const bool down = ps2x_pad::buttonDown(slot, kButtons[i].idx);
                if (down)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, fe::accent());
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fe::gold());
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, fe::gold());
                    ImGui::PushStyleColor(ImGuiCol_Text, fe::dbz(0.10f, 0.07f, 0.03f));
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, fe::dbz(0.13f, 0.13f, 0.20f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fe::dbz(0.18f, 0.18f, 0.26f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, fe::dbz(0.20f, 0.20f, 0.28f));
                    ImGui::PushStyleColor(ImGuiCol_Text, fe::dbz(0.55f, 0.55f, 0.62f));
                }
                ImGui::Button(kButtons[i].label, ImVec2(btnW, btnH));
                ImGui::PopStyleColor(4);
                if (++col < 8) ImGui::SameLine(); else col = 0;
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("Axes");
            ImGui::Spacing();

            ImDrawList *dl = ImGui::GetWindowDrawList();
            const float stickR = 28.0f, gap = 14.0f, stickBox = stickR * 2.0f;
            const float trH = 42.0f, trW = 16.0f, colGap = 28.0f;
            const ImU32 dotFill = ImGui::GetColorU32(fe::accent()), dotEdge = ImGui::GetColorU32(fe::gold());
            auto drawStick = [&](int axX, int axY) {
                const float sx = ps2x_pad::axis(slot, axX), sy = ps2x_pad::axis(slot, axY);
                ImGui::Dummy(ImVec2(stickBox, stickBox));
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                dl->AddCircleFilled(c, stickR, IM_COL32(24, 24, 38, 255), 48);
                dl->AddCircle(c, stickR, IM_COL32(255, 255, 255, 50), 48, 1.5f);
                dl->AddLine(ImVec2(c.x - stickR, c.y), ImVec2(c.x + stickR, c.y), IM_COL32(255, 255, 255, 40), 1.0f);
                dl->AddLine(ImVec2(c.x, c.y - stickR), ImVec2(c.x, c.y + stickR), IM_COL32(255, 255, 255, 40), 1.0f);
                const float mag = std::sqrt(sx * sx + sy * sy);
                float dx = sx, dy = sy;
                if (mag > 1.0f) { dx /= mag; dy /= mag; }
                const ImVec2 pos(c.x + dx * stickR * 0.85f, c.y + dy * stickR * 0.85f);
                dl->AddCircleFilled(pos, 7.0f, dotFill, 24);
                dl->AddCircle(pos, 7.0f, dotEdge, 24, 1.5f);
            };
            auto drawTrigger = [&](int ax) {
                const float v = ps2x_pad::axis(slot, ax);
                ImGui::Dummy(ImVec2(trW, trH));
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                dl->AddRectFilled(mn, mx, IM_COL32(30, 30, 45, 255), 4.0f);
                const float fillH = trH * std::min(1.0f, std::fabs(v));
                if (fillH > 1.0f)
                    dl->AddRectFilled(ImVec2(mn.x, mx.y - fillH), mx, ImGui::GetColorU32(fe::accent(0.6f + 0.4f * std::fabs(v))), 4.0f);
            };
            auto centredLabel = [&](float groupW, const char *lbl) {
                // a fixed-width item with the text centred inside: the readout's width must not move the row
                const ImVec2 ts = ImGui::CalcTextSize(lbl);
                ImGui::Dummy(ImVec2(groupW, ts.y));
                const ImVec2 mn = ImGui::GetItemRectMin();
                dl->AddText(ImVec2(mn.x + (groupW - ts.x) * 0.5f, mn.y), ImGui::GetColorU32(fe::gold(0.9f)), lbl);
            };
            char lbl[64];
            const float stickGroupW = std::max(stickBox, ImGui::CalcTextSize("L +0.00 +0.00").x + 4.0f);
            const float trigBarsW = trW * 2.0f + gap;
            const float trigGroupW = std::max(trigBarsW, ImGui::CalcTextSize("LT 0.00 RT 0.00").x + 4.0f);
            const float rowW = stickGroupW * 2.0f + trigGroupW + colGap * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - rowW) * 0.5f));
            auto stickGroup = [&](int axX, int axY, const char *tag) {
                ImGui::BeginGroup();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (stickGroupW - stickBox) * 0.5f);
                drawStick(axX, axY);
                std::snprintf(lbl, sizeof lbl, "%s %+0.2f %+0.2f", tag, ps2x_pad::axis(slot, axX), ps2x_pad::axis(slot, axY));
                centredLabel(stickGroupW, lbl);
                ImGui::EndGroup();
            };
            stickGroup(GAMEPAD_AXIS_LEFT_X, GAMEPAD_AXIS_LEFT_Y, "L");
            ImGui::SameLine(0.0f, colGap);
            stickGroup(GAMEPAD_AXIS_RIGHT_X, GAMEPAD_AXIS_RIGHT_Y, "R");
            ImGui::SameLine(0.0f, colGap);
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(trigGroupW, (stickBox - trH) * 0.5f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (trigGroupW - trigBarsW) * 0.5f);
            drawTrigger(GAMEPAD_AXIS_LEFT_TRIGGER);
            ImGui::SameLine(0.0f, gap);
            drawTrigger(GAMEPAD_AXIS_RIGHT_TRIGGER);
            std::snprintf(lbl, sizeof lbl, "LT %.2f RT %.2f", ps2x_pad::axis(slot, GAMEPAD_AXIS_LEFT_TRIGGER), ps2x_pad::axis(slot, GAMEPAD_AXIS_RIGHT_TRIGGER));
            centredLabel(trigGroupW, lbl);
            ImGui::EndGroup();
            ImGui::Spacing();
        }
        ImGui::EndChild();
    }

    void drawInputPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;
        ps2_stubs::PadConfig &pads = ps2_stubs::PadConfig::instance();
        static int player = 0;
        if (player < 0 || (size_t)player >= ps2_stubs::PadConfig::kPlayerCount)
            player = 0;

        fe::sectionHeader("CONTROLLER");
        {
            // The runtime has always had two per-player profiles (savedata/pad_p1.conf and
            // pad_p2.conf); the front-end only exposed P1. Editing them here goes through the
            // same PadConfig the game polls, so what you set is what it reads.
            static const char *const kPlayers[] = {"Player 1", "Player 2"};
            fe::comboRow("Player", &player, kPlayers, (int)ps2_stubs::PadConfig::kPlayerCount);

            const ps2_stubs::PadPlayerConfig cur = pads.snapshot((size_t)player);

            // The list is the pads actually plugged in, by their real names, so nobody has to
            // guess which physical pad is "Pad 2".
            ps2x_pad::update();
            static std::vector<std::string> devNames;
            static std::vector<const char *> devPtrs;
            static std::vector<int> devSlots;
            devNames.clear();
            devPtrs.clear();
            devSlots.clear();
            devNames.emplace_back("Automatic (first pad)");
            devNames.emplace_back("Keyboard");
            for (int i = 0; i < ps2x_pad::kMaxSlots; ++i)
            {
                if (!ps2x_pad::isController(i))
                    continue;
                const char *n = ps2x_pad::name(i);
                devNames.emplace_back(n && *n ? n : ("Pad " + std::to_string(i + 1)));
                devSlots.push_back(i);
            }
            for (size_t i = 0; i < devNames.size(); ++i)
                devPtrs.push_back(devNames[i].c_str());

            int chosen = 0;
            if (cur.device.kind == ps2_stubs::PadDeviceKind::Keyboard)
                chosen = 1;
            else if (cur.device.kind == ps2_stubs::PadDeviceKind::Gamepad)
            {
                for (size_t i = 0; i < devSlots.size(); ++i)
                    if (devSlots[i] == cur.device.gamepad)
                        chosen = (int)i + 2;
            }

            int pick = chosen;
            fe::comboRow("Device", &pick, devPtrs.data(), (int)devPtrs.size());
            if (pick != chosen)
            {
                ps2_stubs::PadDevice dev;
                if (pick == 1)
                {
                    dev.kind = ps2_stubs::PadDeviceKind::Keyboard;
                }
                else if (pick >= 2 && (size_t)(pick - 2) < devSlots.size())
                {
                    dev.kind = ps2_stubs::PadDeviceKind::Gamepad;
                    dev.gamepad = devSlots[pick - 2];
                }
                else
                {
                    dev.kind = ps2_stubs::PadDeviceKind::None;
                    dev.gamepad = -1;
                }
                pads.setDevice((size_t)player, dev);
                pads.save();
            }
            fe::pathRow("Profile", pads.playerConfigPath((size_t)player).c_str());
            fe::sliderRow("Stick deadzone", &s.deadzone, 0.0f, 0.5f, "%.2f");
            fe::toggleSwitch("Vibration", &s.rumble);   // [rumble]
            if (s.rumble)
            {
                fe::intSliderRow("Vibration strength", &s.rumbleStrength, 0, 200, "%d%%");
                fe::hint("100% = the game's own strengths. The game's Options > Controller > Vibration must be on too.");
            }
        }

        fe::sectionHeader("GAMEPAD TEST");
        drawPadTester(player, pads);

        ctx.footerHint = "ASSIGN CONTROLS: edited from the game overlay (Shift+Tab).";
    }

    void drawLoggingPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;

        static const char *const kLevels[] = {"OFF", "Balanced", "Detailed", "Debug"};
        static const char *const kDescriptions[4] = {
            "All silent. No log file, none of the runtime's lines, no extra cost.",
            "The per-second lines (fps, renderer and memory stats), state changes, warnings, profile, "
            "mclog and scheduler events. Recommended for playing: a few lines a second.",
            "Adds the per-frame game state, scheduler state and sound-block lines, GPU draws, memory-card "
            "traffic, audio voice state and pad events. For hunting a specific failure.",
            "Everything the runtime can emit: every IOP call and RPC (about twenty lines per frame), "
            "per-opcode recompiler, VU1 JIT, walks of the game heap and raw VRAM dumps. Very slow: "
            "failure diagnosis only."
        };
        static const char *const kLogNote =
            "Logs go to logs/bt3.log next to the executable. The level matches the in-game overlay "
            "(Shift+Tab).";

        if (fe::beginSection("LEVEL", true, kLogNote))
        {
            bool enabled = s.logLevel > 0;
            if (fe::toggleSwitch("Enable logging", &enabled))
                s.logLevel = enabled ? (s.logLevel > 0 ? s.logLevel : 1) : 0;

            int level = s.logLevel;
            if (fe::comboRow("Level", &level, kLevels, 4))
                s.logLevel = level;
            fe::rowLabel("Status");
            if (s.logLevel == 0)
                ImGui::TextColored(fe::gold(), "Logging disabled");
            else
                ImGui::TextColored(fe::gold(), "Level %d", s.logLevel);
            fe::hint(kDescriptions[s.logLevel]);
        }
    }

    void drawMiscPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;
        const std::filesystem::path dataDir = ctx.exeDir / "data";

        if (fe::beginSection("GAME DATA", true,
                             "The wizard extracts the ELF and the resources from your own ISO. The "
                             "size and state are checked against the boot hash."))
        {
            fe::rowLabel("Size");
            {
                char size[32];
                fe::formatBytes(dirSize(dataDir), size, sizeof size);
                ImGui::TextDisabled("%s", size);
            }

            if (ctx.dataState < 0)
            {
                ctx.dataState = (int)verifyInstalledData(dataDir);
                std::fprintf(stderr, "[fe] data state: %s\n",
                             ctx.dataState == (int)DataState::Valid ? "valid"
                             : ctx.dataState == (int)DataState::Corrupt ? "corrupt"
                                                                        : "missing");
            }
            switch ((DataState)ctx.dataState)
            {
            case DataState::Valid:
                fe::statusRow("Status", fe::okCol(), "installed and verified");
                break;
            case DataState::Corrupt:
                fe::statusRow("Status", fe::badCol(), "corrupt: reinstall");
                break;
            case DataState::Missing:
                fe::statusRow("Status", fe::badCol(), "missing");
                break;
            }

            char path[512];
            std::snprintf(path, sizeof path, "%s", dataDir.string().c_str());
            fe::pathRow("Folder", path);

            fe::toggleSwitch("Reinstall mode", &ctx.reinstallMode);
            if (ctx.reinstallMode)
            {
                fe::rowLabel("Asistente");
                if (ImGui::Button("Install game data...", ImVec2(240.0f, 0.0f)))
                    ctx.requestInstallWizard = true;
            }
        }

        {   // [bt3save] Install the progressed memory-card save (bug 8). The runtime's mc* layer is
            // a stub, so a save the game writes itself is 99.2% zeros -- nothing persists, and
            // with no progress on the card nothing ever unlocks. This ships a real save extracted
            // from an Mcd001.ps2 (the BASLUS-21678DBZT3 entry) and drops it over the card dir.
            // The previous save is kept as .bak next to it. Env PS2X_BT3SAVE=0 hides the section.
            static const bool s_showSave = [](){ const char *v = std::getenv("PS2X_BT3SAVE"); return !(v && v[0] == (char)48); }();
            const std::filesystem::path saveSrc = ctx.exeDir / "saves" / "BASLUS-21678DBZT3" / "BASLUS-21678DBZT3";
            const std::filesystem::path cardDir = ctx.exeDir / "savedata" / "BASLUS-21678DBZT3";
            const std::filesystem::path cardFile = cardDir / "BASLUS-21678DBZT3";
            if (s_showSave && fe::beginSection("COMPLETED SAVE", false,
                                                "Copies a real save that already has story progress, "
                                                "so you start with the match underway and the characters, "
                                                "stages, missions and items that progress "
                                                "unlocked. The game reads it as a normal card."))
            {
                std::error_code ec;
                const bool haveSrc = std::filesystem::is_regular_file(saveSrc, ec);
                const bool haveCard = std::filesystem::is_regular_file(cardFile, ec);
                char sbuf[32];
                if (haveCard)
                {
                    fe::formatBytes((unsigned long long)std::filesystem::file_size(cardFile, ec), sbuf, sizeof sbuf);
                    fe::statusRow("Current save", haveSrc ? fe::okCol() : fe::warnCol(), sbuf);
                }
                else
                {
                    fe::statusRow("Current save", fe::warnCol(), "no save");
                }
                if (!haveSrc)
                {
                    fe::statusRow("Completed save", fe::badCol(), "no encontrada en saves/");
                }
                fe::toggleSwitch("Install completed save", &ctx.installSaveMode);
                if (ctx.installSaveMode)
                {
                    fe::rowLabel("Accion");
                    ImGui::BeginDisabled(!haveSrc);
                    if (ImGui::Button("Install now", ImVec2(180.0f, 0.0f)))
                        ctx.requestSaveInstall = true;
                    ImGui::EndDisabled();
                    fe::hint("Replaces the save in savedata/BASLUS-21678DBZT3/ and keeps the previous "
                             "as .bak next to it. The match you play from now on is NOT "
                             "saved on exit: the memory card still cannot write back, "
                             "so this is for starting with the match unlocked, "
                             "not for carrying progress between sessions.");
                }
                if (!ctx.saveInstallMsg.empty())
                {
                    fe::statusRow("Resultado", ctx.saveInstallOk ? fe::okCol() : fe::badCol(),
                                  ctx.saveInstallMsg.c_str());
                }
            }
        }

    // The overlay shortcut belongs with the other runtime switches, not with the pad.
    if (fe::beginSection("GAME OVERLAY", false,
                         "Shortcut to open and close the settings menu during a match. "
                         "Buttons and keys are captured by holding the button or the "
                         "combination for 3 s, from the game's own overlay."))
    {
        fe::toggleSwitch("Game overlay (Shift+Tab)", &s.overlayEnabled);
        fe::kv("Pad button", s.overlayPadBtns.c_str());
        fe::kv("Keys", s.overlayKeys.c_str());
        fe::toggleSwitch("FPS meter (corner)", &s.showPerf);
        fe::hint("Shows the presents per second in the top-right corner, during the match. "
                 "The overlay's Video tab has the detail: frame-time distribution (p50/p95/max) "
                 "and GPU usage, measured from what the renderer has and labelled with the "
                 "coverage it actually has.");
    }

    if (fe::beginSection("NETPLAY OVERLAY", false,
                         "A Netplay label in the corner of the main menu, the panel behind it, and "
                         "the automatic walk to character select when a session connects -- with a "
                         "black curtain and \"Loading...\" while it happens. The default is the "
                         "NET_OVERLAY environment variable; this switch and the overlay's own "
                         "Netplay tab both write the same saved setting, so they never disagree."))
    {
        fe::toggleSwitch("Netplay overlay", &s.netOverlay);
        fe::hint("On by default. It puts a label in the corner of the main menu and nothing else; "
                 "the panel and the transition only appear once a session is live, and only while "
                 "you are in that menu. NET_OVERLAY=0 turns it off for one run.");
    }

    // [ach] Its own section rather than a line under the netplay one: the two share nothing but
    // the shape of the switch, and putting them together would read as "achievements are a netplay
    // feature". They are not -- this one is single-player and never opens a socket.
    if (fe::beginSection("ACHIEVEMENTS", false,
                         "RetroAchievements, tracked locally. The 154 definitions are a file in "
                         "assets/ and your progress is a file in savedata/ -- no account, no "
                         "network, nothing sent anywhere. The five whose memory addresses are "
                         "mapped work; the rest are listed and cannot be earned by playing yet, "
                         "because they read an address this build does not use. On by default; "
                         "ACHIEVEMENTS=0 turns it off for one run. The achievements, the badges "
                         "and the rcheevos library that evaluates them are RetroAchievements' work."))
    {
        fe::toggleSwitch("Achievements", &s.achievements);
        const int total = ps2xAchTotal();
        char buf[192];
        if (total > 0)
            std::snprintf(buf, sizeof buf,
                          "%d of %d tracked, %d of %d points. On by default; ACHIEVEMENTS=0 turns "
                          "it off for one run. The list, the badges and the progress file are all "
                          "RetroAchievements' definitions and their work -- see the overlay's "
                          "Achievements tab.", ps2xAchUnlocked(), total,
                          ps2xAchPointsEarned(), ps2xAchPointsTotal());
        else
            std::snprintf(buf, sizeof buf,
                          "Nothing loaded yet -- the counts appear once the game has run a frame.");
        fe::hint(buf);
    }
}

    // [mods] Loadable mods: every *.so / *.dll in mods/ next to the runner (see mods/README.md and
    // ps2x_mod_api.h). A switch per file plus the master switch; the loader reads the result at the
    // next PLAY, so a change here needs a restart of the game, not of the launcher.
    namespace
    {
        struct ModFile { std::string name; std::string file; unsigned long long bytes = 0; };
        std::vector<ModFile> scanMods(const std::filesystem::path &dir)
        {
            std::vector<ModFile> out;
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) return out;
            for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            {
                if (!e.is_regular_file(ec)) continue;
                const std::string ext = e.path().extension().string();
#if defined(_WIN32)
                if (ext != ".dll") continue;
#elif defined(__APPLE__)
                if (ext != ".dylib" && ext != ".so") continue;
#else
                if (ext != ".so") continue;
#endif
                ModFile m; m.name = e.path().stem().string(); m.file = e.path().filename().string();
                const std::uintmax_t sz = e.file_size(ec); m.bytes = ec ? 0ull : (unsigned long long)sz;
                out.push_back(m);
            }
            std::sort(out.begin(), out.end(), [](const ModFile &a, const ModFile &b) { return a.name < b.name; });
            return out;
        }
        bool csvHas(const std::string &csv, const std::string &name)
        {
            size_t p = 0;
            while (p <= csv.size())
            {
                size_t q = csv.find(',', p); if (q == std::string::npos) q = csv.size();
                if (csv.compare(p, q - p, name) == 0) return true;
                p = q + 1;
            }
            return false;
        }
        std::string csvWith(const std::string &csv, const std::string &name, bool present)
        {
            std::vector<std::string> items; size_t p = 0;
            while (p <= csv.size())
            {
                size_t q = csv.find(',', p); if (q == std::string::npos) q = csv.size();
                const std::string it = csv.substr(p, q - p);
                if (!it.empty() && it != name) items.push_back(it);
                p = q + 1;
            }
            if (present) items.push_back(name);
            std::string out;
            for (size_t i = 0; i < items.size(); ++i) { if (i) out += ','; out += items[i]; }
            return out;
        }
    }

    void drawModsPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;
        const std::filesystem::path modsDir = ctx.exeDir / "mods";
        static std::vector<ModFile> s_mods;
        static uint64_t s_scanned = 0;
        static std::string s_scannedDir;
        if (s_scannedDir != modsDir.string() || (ImGui::GetFrameCount() - s_scanned) > 120u)
        {   // a cheap directory listing twice a second: dropping a file in shows up without a restart
            s_mods = scanMods(modsDir); s_scanned = ImGui::GetFrameCount(); s_scannedDir = modsDir.string();
        }

        if (fe::beginSection("MODS", true,
                             "Game mods are shared libraries in the mods folder next to the game. Each one is "
                             "loaded when the game starts and hooks the game through the runtime's mod API. "
                             "A change here applies at the next PLAY."))
        {
            fe::toggleSwitch("Load mods", &s.modsEnabled);
            char path[512];
            std::snprintf(path, sizeof path, "%s", modsDir.string().c_str());
            fe::pathRow("Folder", path);
            if (s_mods.empty())
            {
                fe::rowLabel("Installed");
                ImGui::TextDisabled("none found");
                fe::hint("Put a mod's .so/.dll file in the folder above. The Tag Team mod ships as tagteam.");
            }
            else
            {
                for (const ModFile &m : s_mods)
                {
                    bool on = !csvHas(s.modsDisabled, m.name);
                    ImGui::BeginDisabled(!s.modsEnabled);
                    if (fe::toggleSwitch(m.name.c_str(), &on))
                        s.modsDisabled = csvWith(s.modsDisabled, m.name, !on);
                    ImGui::EndDisabled();
                    char size[32]; fe::formatBytes(m.bytes, size, sizeof size);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s  %s", m.file.c_str(), size);
                }
                if (!s.modsEnabled) fe::hint("Mods are off: nothing in the folder loads.");
            }
        }

        if (fe::beginSection("TAG TEAM", true,
                             "The Tag Team mod: up to six fighters at once, Team Battle, Free-for-all and Co-op, "
                             "picked from a TAG TEAM entry on the game's main menu. It needs the 128 MB memory build."))
        {
            bool have = false;
            for (const ModFile &m : s_mods) if (m.name == "tagteam") have = true;
            if (!have) fe::statusRow("Status", fe::warnCol(), "tagteam.so / tagteam.dll not in the mods folder");
            else if (!s.modsEnabled || csvHas(s.modsDisabled, "tagteam")) fe::statusRow("Status", fe::gold(), "installed, switched off");
            else fe::statusRow("Status", fe::okCol(), "installed, loads at PLAY");
            fe::hint("In the game: Main Menu > TAG TEAM. Lock-on switches with R3. Co-op puts player 2 on the second pad.");
        }
    }

    void drawAboutPage(PageContext &ctx)
    {
        // All fixed: the page is short and there is nothing to fold away.
        fe::sectionHeader("ABOUT");
        fe::rowLabel("Game");
        ImGui::TextColored(fe::gold(), "Dragon Ball Z: Budokai Tenkaichi 3 Recompiled");
        fe::hint("Static recompilation of the PS2 game: the MIPS code on disc is translated to "
                 "C++ and runs natively, with the PS2 hardware emulated on the host process.");

        fe::sectionHeader("COMPONENTS");
        fe::kv("Interface", "ImGui + SDL2 (inside the runtime)");
        fe::kv("Graphics", "OpenGL 3.3 / Native Vulkan (engine seam) / paraLLEl-GS");
        fe::kv("Video", "FFmpeg");
        fe::kv("Audio", "the game's own SE/ADX engine");
        fe::kv("Pad", "SDL2 gamecontroller");

        fe::sectionHeader("DEVELOPERS");
        fe::kv("z3xox", "owner / lead developer - recompiler, the runtime core "
                        "(EE/GS/VU1/scheduler, the GS replay), the native Vulkan "
                        "renderer, game overrides, generators");
        fe::kv("RexxColder", "supporter / collaborator - the OpenGL renderer, optimisation "
                             "(perf/async), front-end and install wizard, native IOP modules, "
                             "input and gamepads, build/release, deploy, game data, texture packs");
        fe::kv("valenvivaldi", "collaborator - macOS arm64 port, packaging, audio");
        fe::kv("KaibaSammy", "collaborator (mods) - Mod Manager project, mod design, mod support");
        ImGui::Spacing();
        fe::sectionHeader("CREDITS");
        fe::hint("PS2Recomp (ran-j) - static recompiler (GPL-3.0)\n"
                 "paraLLEl-GS (Arntzen-Software) - GS backend (LGPL-3.0-or-later)\n"
                 "BT3-Recomp (z3xox) - this project\n"
                 "ViveTheModder - NTSC-U AFS file lists (Apache-2.0)\n"
                 "Sal9im - \"4K 2D Textures Lite\" pack (GBATemp), not distributed: drop it in "
                 "data/Textures/. The DXT5 encoding and the runtime replacement work are "
                 "RexxColder's\n"
                 "Russo One - typeface (SIL Open Font License)");
    }
}
