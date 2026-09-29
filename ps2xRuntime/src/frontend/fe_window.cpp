#include "frontend/fe_window.h"

#include "lib/ps2_host_sdl.h"

#include "imgui.h"
#include "imgui_internal.h"   // [nowindowing] ImGuiContext::ConfigNavWindowing*
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace
{
    const char *const kGlslVersion = "#version 330";
}

namespace frontend
{
    int monitorCount()
    {
        const int n = SDL_GetNumVideoDisplays();
        return n > 0 ? n : 0;
    }

    bool monitorSize(int index, int *width, int *height)
    {
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(index, &mode) != 0 || mode.w <= 0 || mode.h <= 0)
            return false;
        if (width)
            *width = mode.w;
        if (height)
            *height = mode.h;
        return true;
    }

    std::string monitorName(int index)
    {
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(index, &mode) != 0)
            return "display " + std::to_string(index);
        const int w = mode.w > 0 ? mode.w : 0;
        const int h = mode.h > 0 ? mode.h : 0;
        if (w <= 0 || h <= 0)
            return "display " + std::to_string(index);
        return std::to_string(w) + "x" + std::to_string(h) + " @" + std::to_string(mode.refresh_rate) + "Hz";
    }

    FeWindow::~FeWindow()
    {
        shutdown();
    }

    bool FeWindow::open(const std::string &title, int width, int height)
    {
        // GAMECONTROLLER is what the ImGui SDL2 backend polls to fill the gamepad navigation
        // keys, and it has to be up before the backend looks for devices. The pad layer opens
        // the very same controllers for the game, and SDL refcounts the subsystem.
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0)
        {
            std::fprintf(stderr, "[fe] SDL_Init failed: %s\n", SDL_GetError());
            // Retry without the pad subsystem: a front-end with no gamepad navigation still beats
            // no front-end at all.
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
            {
                std::fprintf(stderr, "[fe] SDL_Init (no gamepad) failed: %s\n", SDL_GetError());
                return false;
            }
            std::fprintf(stderr, "[fe] continuing without gamepad navigation\n");
        }
        m_sdlUp = true;

        int pads = 0;
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
        {
            if (SDL_IsGameController(i))
                ++pads;
        }
        std::fprintf(stderr, "[fe] gamepads for navigation: %d\n", pads);

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        // Never open bigger than the desktop, and never smaller than the layout can take.
        int drawW = width;
        int drawH = height;
        SDL_DisplayMode mode;
        if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
        {
            drawW = std::min(drawW, (int)(mode.w * 0.90f));
            drawH = std::min(drawH, (int)(mode.h * 0.90f));
        }
        // [bgaspect] floor 800x360: the 1024x408 default (the menu art's own 3.1:1 above the button bar) sits
        // under the old 520 floor, which forced every window taller than its art and cropped the picture
        if (drawW < 800) drawW = 800;
        if (drawH < 320) drawH = 320;

        m_window = SDL_CreateWindow(title.c_str(),
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    drawW, drawH,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!m_window)
        {
            std::fprintf(stderr, "[fe] SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetWindowMinimumSize(m_window, 800, 320);

        m_gl = SDL_GL_CreateContext(m_window);
        if (!m_gl)
        {
            std::fprintf(stderr, "[fe] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(m_window, m_gl);
        SDL_GL_SetSwapInterval(1);

        int display = SDL_GetWindowDisplayIndex(m_window);
        if (display < 0)
            display = 0;
        float ddpi = 96.0f, hdpi = 96.0f, vdpi = 96.0f;
        if (SDL_GetDisplayDPI(display, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 1.0f)
        {
            m_dpiScale = ddpi / 96.0f;
            if (m_dpiScale < 1.0f) m_dpiScale = 1.0f;
            if (m_dpiScale > 2.0f) m_dpiScale = 2.0f;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        // [imgui-error] All three channels are OFF by default. With them on, a single unbalanced
        // Begin/End pair raises the same error on EVERY frame: a modal "MESSAGE FROM DEAR IMGUI"
        // box that sits on top of the UI and has to be dismissed by hand, plus a console line
        // per frame per error. Both were on to keep an imbalance visible, but the box lands on
        // top of the very UI it is reporting on, which is worse than the report for a player.
        //
        // Not lost, just moved: fe_imgui_probe and fe_picker_probe assert the same invariants
        // headlessly (window stack back to base, popup stack empty, ErrorCountCurrentFrame 0), so
        // `cmake --build build --target fe_imgui_probe fe_picker_probe` catches this class of bug
        // without a window. And PS2X_IMGUI_ERRORS=1 turns the channels back on to watch a live
        // run.
        const bool imguiErrors = std::getenv("PS2X_IMGUI_ERRORS") != nullptr;
        io.ConfigErrorRecoveryEnableTooltip = imguiErrors;
        io.ConfigErrorRecoveryEnableAssert = imguiErrors;
        io.ConfigErrorRecoveryEnableDebugLog = imguiErrors;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        // [nowindowing] ImGui's window switcher (Ctrl+Tab, or holding Square / X on a pad) dims the screen and lists
        // the windows to pick from. This launcher has one window and it has no title, so the switcher was a white
        // flash with "untitled" in the middle whenever a face button was held on the Pads page. Off on both inputs.
        {
            ImGuiContext *g = ImGui::GetCurrentContext();
            g->ConfigNavWindowingWithGamepad = false;
            g->ConfigNavWindowingKeyNext = 0;
            g->ConfigNavWindowingKeyPrev = 0;
        }

        if (!ImGui_ImplSDL2_InitForOpenGL(m_window, m_gl))
        {
            std::fprintf(stderr, "[fe] ImGui_ImplSDL2_InitForOpenGL failed\n");
            ImGui::DestroyContext();
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init(kGlslVersion))
        {
            std::fprintf(stderr, "[fe] ImGui_ImplOpenGL3_Init failed\n");
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        m_imguiUp = true;
        std::fprintf(stderr, "[fe] front-end window up (dpi scale %.2f)\n", m_dpiScale);
        return true;
    }

    void FeWindow::beginFrame()
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
                m_closeRequested = true;
            ImGui_ImplSDL2_ProcessEvent(&ev);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void FeWindow::endFrame()
    {
        // The host window paints the whole viewport every frame, so the drawable is fully
        // overwritten and no explicit glClear is needed (the backend sets the viewport).
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(m_window);
    }

    void FeWindow::setSize(int width, int height)
    {
        if (m_window && width > 0 && height > 0)
            SDL_SetWindowSize(m_window, width, height);
    }

    bool FeWindow::querySize(int *width, int *height) const
{
    if (!m_window)
        return false;
    int w = 0, h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w <= 0 || h <= 0)
        return false;
    if (width)
        *width = w;
    if (height)
        *height = h;
    return true;
}

void FeWindow::shutdown()
    {
        if (m_imguiUp)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            m_imguiUp = false;
        }
        if (m_gl)
        {
            SDL_GL_DeleteContext(m_gl);
            m_gl = nullptr;
        }
        if (m_window)
        {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        if (m_sdlUp)
        {
            // raylib's SDL platform calls SDL_Init itself when the emulator boots, so the
            // whole subsystem has to be handed back cleanly for that handoff.
            SDL_Quit();
            m_sdlUp = false;
        }
    }
}
