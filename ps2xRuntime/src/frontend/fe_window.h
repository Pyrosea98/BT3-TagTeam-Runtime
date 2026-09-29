#pragma once

#include <string>

struct SDL_Window;
typedef void *FeGlContext;

namespace frontend
{
    // SDL2 display enumeration (the Qt launcher used the EVI-N HID library for this; the
    // runtime already has SDL2, so the front-end does not need another dependency).
    int monitorCount();
    std::string monitorName(int index);
    // Native desktop size of a display, used to size a borderless/windowed game window.
    bool monitorSize(int index, int *width, int *height);

    class FeWindow
    {
    public:
        FeWindow() = default;
        ~FeWindow();

        FeWindow(const FeWindow &) = delete;
        FeWindow &operator=(const FeWindow &) = delete;

        bool open(const std::string &title, int width, int height);
        void beginFrame();
        void endFrame();
        void shutdown();

        void requestClose() { m_closeRequested = true; }
        bool closeRequested() const { return m_closeRequested; }
        float dpiScale() const { return m_dpiScale; }

        void *sdlWindow() const { return m_window; }
        // Current drawable size of the window, so the shell can remember it. False when the
        // window is already gone.
        bool querySize(int *width, int *height) const;

    private:
        SDL_Window *m_window = nullptr;
        FeGlContext m_gl = nullptr;
        bool m_imguiUp = false;
        bool m_sdlUp = false;
        bool m_closeRequested = false;
        float m_dpiScale = 1.0f;
    };
}
