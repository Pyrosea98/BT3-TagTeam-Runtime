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

#include <algorithm>
#include <cstdio>
#include <cstring>
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
    const char *const kCustomResolution = "Personalizado";

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
        {
            static const char *const items[] = {"OpenGL (New)", "Software", "paraLLEl-GS (Vulkan)"};
            int count = 2;
#if defined(PS2X_HAVE_PGS)
            count = 3;
#endif
            int renderer = s.renderer;
            if (renderer < 0 || renderer >= count)
                renderer = 0;
            fe::comboRow("Graphics", &renderer, items, count);
            if (renderer != s.renderer)
                s.renderer = renderer;
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
                             "thinner ink. DoF and the glow (Kaioken aura) are enabled "
                             "here too."))
        {
            fe::toggleSwitch("Cel outline", &s.outline);
            if (s.outline)
                fe::intSliderRow("Outline strength", &s.inkStrength, 100, 400, "%d %%");
            fe::toggleSwitch("Character shadows", &s.shadows);
            fe::toggleSwitch("Depth of field (DoF)", &s.dofBlur);
            if (s.dofBlur)
                fe::intSliderRow("DoF range", &s.dofZFar, 20000, 800000, "%d k");
            fe::toggleSwitch("Glow (Kaioken aura)", &s.glow);
            fe::toggleSwitch("Bilinear filter", &s.bilinear);
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
            fe::kv("Folder", path);

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

        ctx.footerHint = "Half texel, skip post, stale VRAM, widescreen, 60 fps and the HUD "
                         "change live from the game overlay (Shift+Tab).";
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

        const float f = fe::unit();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 org = ImGui::GetCursorScreenPos();

        const ImU32 off = ImGui::GetColorU32(fe::dbz(0.13f, 0.15f, 0.17f));
        const ImU32 on = ImGui::GetColorU32(fe::okCol());
        const ImU32 edge = ImGui::GetColorU32(fe::dbz(0.28f, 0.32f, 0.35f));
        const ImU32 dim = ImGui::GetColorU32(fe::dbz(0.55f, 0.60f, 0.63f));

        // The tester is laid out across the whole content width: d-pad, face buttons and the two
        // analog boxes spread out so they nearly reach the right edge, all the same size, and the
        // shoulder row on its own line underneath. Dummy() reserves both rows plus the captions,
        // so the hint below never collides with the drawing.
        const float f2 = f;
        const float u = f2 * 1.5f;          // one button
        const float block = u * 3.0f;      // d-pad and analog boxes are all 3u square
        const float W = ImGui::GetContentRegionAvail().x;
        // Inset from the content edges so nothing sits flush against the border.
        const float m = f2 * 0.7f;
        const float inner = W - m * 2.0f;
        const float top = f2 * 0.9f;
        const float capY = top + block + f2 * 0.25f;
        const float row2Y = capY + f2 * 2.0f;
        ImGui::Dummy(ImVec2(0.0f, row2Y + f2 * 1.2f));

        auto pad = [&](float x, float y, float w, float h, bool pressed, const char *label) {
            const ImVec2 a(org.x + x, org.y + y);
            const ImVec2 b(a.x + w, a.y + h);
            dl->AddRectFilled(a, b, pressed ? on : off, 3.0f);
            dl->AddRect(a, b, pressed ? on : edge, 3.0f, 0, pressed ? 1.5f : 1.0f);
            if (label && *label)
            {
                const ImVec2 t = ImGui::CalcTextSize(label);
                dl->AddText(ImGui::GetFont(), f * 0.8f,
                            ImVec2(a.x + (w - t.x) * 0.5f, a.y + (h - f * 0.8f) * 0.5f),
                            ImGui::GetColorU32(pressed ? fe::dbz(0.02f, 0.06f, 0.03f)
                                                       : fe::dbz(0.70f, 0.74f, 0.76f)),
                            label);
            }
        };

        // D-pad: a plus sign.
        const float dpx = m;
        pad(dpx + u, top, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_FACE_UP), "^");
        pad(dpx, top + u, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_FACE_LEFT), "<");
        pad(dpx + u * 2.0f, top + u, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_FACE_RIGHT), ">");
        pad(dpx + u, top + u * 2.0f, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_FACE_DOWN), "v");
        dl->AddText(ImGui::GetFont(), f2 * 0.8f, ImVec2(org.x + dpx, org.y + capY), dim, "CRUCETA");

        // Face buttons in a diamond, PS2 names.
        const float fx = m + inner * 0.20f;
        pad(fx + u, top, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_FACE_UP), "TRI");
        pad(fx, top + u, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_FACE_LEFT), "CUAD");
        pad(fx + u * 2.0f, top + u, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT), "CIR");
        pad(fx + u, top + u * 2.0f, u, u, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_FACE_DOWN), "CRU");
        dl->AddText(ImGui::GetFont(), f2 * 0.8f, ImVec2(org.x + fx, org.y + capY), dim, "BUTTONS");

        // Analogs: same 3u square, live dot, caption and value underneath.
        auto analog = [&](float x, int axisX, int axisY, bool clicked, const char *name) {
            const ImVec2 a(org.x + x, org.y + top);
            const ImVec2 b(a.x + block, a.y + block);
            dl->AddRectFilled(a, b, off, 3.0f);
            dl->AddRect(a, b, clicked ? on : edge, 3.0f, 0, clicked ? 1.5f : 1.0f);
            const float vx = ps2x_pad::axis(slot, axisX);
            const float vy = ps2x_pad::axis(slot, axisY);
            dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f + vx * (b.x - a.x) * 0.40f,
                                       (a.y + b.y) * 0.5f + vy * (b.y - a.y) * 0.40f),
                                  f2 * 0.30f, on);
            dl->AddText(ImGui::GetFont(), f2 * 0.8f, ImVec2(a.x, org.y + capY), dim, name);
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.2f / %.2f", vx, vy);
            dl->AddText(ImGui::GetFont(), f2 * 0.8f, ImVec2(a.x, org.y + capY + f2 * 0.95f),
                        ImGui::GetColorU32(fe::dbz(0.45f, 0.50f, 0.53f)), buf);
        };
        analog(m + inner * 0.50f, GAMEPAD_AXIS_LEFT_X, GAMEPAD_AXIS_LEFT_Y,
               ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_THUMB), "LEFT STICK (L3)");
        analog(m + inner * 0.80f, GAMEPAD_AXIS_RIGHT_X, GAMEPAD_AXIS_RIGHT_Y,
               ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_THUMB), "RIGHT STICK (R3)");

        // Shoulder row: L1/L2 hard left, SEL/PS/STA centred, R2/R1 hard right.
        const float bw = u * 1.6f;
        const float bh = f2 * 0.9f;
        const float step = bw + f2 * 0.5f;
        pad(m, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_TRIGGER_1), "L1");
        pad(m + step, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_LEFT_TRIGGER_2), "L2");

        const float cgap = bw * 0.25f;
        const float clusterW = bw * 3.0f + cgap * 2.0f;
        const float cx = m + (inner - clusterW) * 0.5f;
        pad(cx, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_MIDDLE_LEFT), "SEL");
        pad(cx + bw + cgap, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_MIDDLE), "PS");
        pad(cx + (bw + cgap) * 2.0f, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_MIDDLE_RIGHT), "STA");

        pad(m + inner - bw * 2.0f - step, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_TRIGGER_2), "R2");
        pad(m + inner - bw, row2Y, bw, bh, ps2x_pad::buttonDown(slot, GAMEPAD_BUTTON_RIGHT_TRIGGER_1), "R1");
    }

    void drawInputPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;
        ps2_stubs::PadConfig &pads = ps2_stubs::PadConfig::instance();
        static int player = 0;
        if (player < 0 || (size_t)player >= ps2_stubs::PadConfig::kPlayerCount)
            player = 0;

        fe::sectionHeader("PAD");
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
            fe::kv("Profile", pads.playerConfigPath((size_t)player).c_str());
            fe::sliderRow("Stick deadzone", &s.deadzone, 0.0f, 0.5f, "%.2f");
        }

        fe::sectionHeader("TEST PAD");
        drawPadTester(player, pads);

        ctx.footerHint = "ASSIGN CONTROLS: edited from the game overlay (Shift+Tab).";
    }

    void drawLoggingPage(PageContext &ctx)
    {
        ps2x_settings::Settings &s = *ctx.settings;

        static const char *const kLevels[] = {"OFF", "Balanced", "Detailed", "Debug"};
        static const char *const kDescriptions[4] = {
            "All silent. No logs are written and the game runs with no extra cost.",
            "Profile + mclog + scheduler events: stalls at boot, save storms and pacing hitches. "
            "Recommended for playing.",
            "Adds per-frame GPU draws, memory-card traffic, audio voice state and pad events. "
            "For hunting a specific failure.",
            "Everything the runtime can emit (per-opcode recompiler, VU1 JIT, walks of the game "
            "heap and raw VRAM dumps). Very slow: failure diagnosis only."
        };
        static const char *const kLogNote =
            "Logs go to logs/bt3.log next to the executable. The level matches the in-game overlay "
            "(Shift+Tab).";

        if (fe::beginSection("NIVEL", true, kLogNote))
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
            fe::kv("Folder", path);

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
        fe::kv("Graphics", "OpenGL 3.3 / paraLLEl-GS (Vulkan)");
        fe::kv("Video", "FFmpeg");
        fe::kv("Audio", "the game's own SE/ADX engine");
        fe::kv("Pad", "SDL2 gamecontroller");

        fe::sectionHeader("CREDITS");
        fe::hint("PS2Recomp (ran-j) - static recompiler (GPL-3.0)\n"
                 "paraLLEl-GS (Arntzen-Software) - GS backend (LGPL-3.0-or-later)\n"
                 "BT3-Recomp (z3xox) - this project\n"
                 "ViveTheModder - NTSC-U AFS file lists (Apache-2.0)\n"
                 "Russo One - typeface (SIL Open Font License)\n"
                 "Menu music: track 08 \"Shine\" from the Dragon Ball Z: Budokai Tenkaichi 3 "
                 "soundtrack (2007). The audio is copyrighted material and is NOT distributed "
                 "with the project: drop it in as assets/music/music.flac and the front-end "
                 "plays it from there.");
    }
}
