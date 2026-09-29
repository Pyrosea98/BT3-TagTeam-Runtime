#include "runtime/ps2_host_pad.h"
#include "ps2_host_backend.h"
#include "ps2_host_window.h"   // [B] native (HWND) window handle for the D3D11 swap chain

#if defined(__linux__)
#include "runtime/pad_evdev_linux.h"
#endif

#if defined(PS2X_HAVE_SDL2)
#include "ps2_host_sdl.h"
#endif

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace ps2x_pad
{
    namespace
    {
        // raylib GAMEPAD_BUTTON_* values, spelled out so this file does not depend on the enum.
        constexpr int kBtnDpadUp = 1, kBtnDpadRight = 2, kBtnDpadDown = 3, kBtnDpadLeft = 4;
        constexpr int kBtnY = 5, kBtnB = 6, kBtnA = 7, kBtnX = 8;
        constexpr int kBtnLB = 9, kBtnLT = 10, kBtnRB = 11, kBtnRT = 12;
        constexpr int kBtnBack = 13, kBtnGuide = 14, kBtnStart = 15, kBtnLS = 16, kBtnRS = 17;
        constexpr int kAxLX = 0, kAxLY = 1, kAxRX = 2, kAxRY = 3, kAxLT = 4, kAxRT = 5;
        constexpr int kMaxButtons = 32, kMaxAxes = 8;

        Backend s_backend = Backend::Raylib;
        bool s_inited = false;

        bool envFlag(const char *name)
        {
            const char *v = std::getenv(name);
            return v && v[0] && v[0] != '0';
        }

        Backend pickBackend()
        {
#if defined(PS2X_HAVE_SDL2)
            if (const char *v = std::getenv("PS2X_HOSTPAD"))
            {
                if (std::strcmp(v, "raylib") == 0) return Backend::Raylib;
                if (std::strcmp(v, "sdl2") == 0) return Backend::Sdl2;
            }
            return Backend::Sdl2;
#else
            return Backend::Raylib;
#endif
        }

        // ---- raylib backend ---------------------------------------------------------------
        // raylib's own gamepad API only. The GLFW side channel this used to read (joystick name and
        // "is this slot actually a controller") is gone with the GLFW platform: raylib now takes its
        // window/input from SDL2, which is also the default pad backend here.
        namespace rl
        {
            void init()
            {
#if !defined(PLATFORM_VITA)
                // Mappings raylib/GLFW-style for the Linux evdev naming (unchanged).
                SetGamepadMappings(
                    "03000000c82d00000631000014010000,8BitDo Ultimate Wireless,platform:Linux,"
                    "a:b0,b:b1,x:b2,y:b3,back:b6,start:b7,guide:b8,leftstick:b9,rightstick:b10,"
                    "leftshoulder:b4,rightshoulder:b5,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
                    "leftx:a0,lefty:a1,rightx:a3,righty:a4,lefttrigger:a2,righttrigger:a5");
                SetGamepadMappings(
                    "060000005e040000120b000017050000,Microsoft Xbox Controller,platform:Linux,"
                    "a:b48,b:b49,x:b51,y:b52,back:b58,start:b59,guide:b60,leftstick:b61,rightstick:b62,"
                    "leftshoulder:b54,rightshoulder:b55,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
                    "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5");
#endif
            }

            bool isController(int g)
            {
                if (!IsGamepadAvailable(g))
                    return false;
#if defined(PLATFORM_VITA)
                return true;
#else
                static const bool s_all = envFlag("PS2X_PAD_ALLDEV");
                if (s_all) // escape hatch if this ever rejects a legitimate pad
                    return true;
                // Enough axes to be a pad rather than a mislabelled keyboard or a sensor node.
                return GetGamepadAxisCount(g) >= 4;
#endif
            }

            const char *name(int g) { return GetGamepadName(g); }

            int buttonCount(int g) { return IsGamepadAvailable(g) ? 16 : 0; }   // raylib exposes no count
        } // namespace rl

        // ---- SDL2 backend -----------------------------------------------------------------
#if defined(PS2X_HAVE_SDL2)
        namespace sdl
        {
            struct Slot
            {
                SDL_GameController *gc = nullptr;
                SDL_Joystick *js = nullptr;   // gc's joystick, or the raw joystick when unmapped
                SDL_JoystickID id = -1;
                std::string name;
                bool controller = false;      // has an SDL mapping, or passed the raw-layout test
                int nAxes = 0, nButtons = 0;
                // Level state, refreshed by update() on the render thread and read from the
                // guest thread -- plain arrays, the same torn-read class raylib's own tables are.
                std::array<uint8_t, kMaxButtons> btn{};
                std::array<float, kMaxAxes> ax{};
            };
            std::array<Slot, kMaxSlots> s_slots;
            std::set<SDL_JoystickID> s_rejected;   // instance ids that failed the controller test
            bool s_up = false;

            void log(const char *fmt, ...)
            {
                static const bool s_on = envFlag("PS2X_PADLOG");
                if (!s_on) return;
                va_list ap; va_start(ap, fmt);
                std::fputs("[padlog] ", stderr);
                std::vfprintf(stderr, fmt, ap);
                std::fputc('\n', stderr);
                va_end(ap);
            }

            bool init()
            {
                // SDL_INIT_EVENTS would otherwise take SIGINT/SIGTERM and turn them into SDL_QUIT
                // events nobody reads: Ctrl-C and the rig's kill would stop working.
                SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
                // The window is raylib's, so SDL never has keyboard focus; without this it drops
                // joystick input as "background".
                SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
                SDL_SetMainReady();
                if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
                {
                    std::fprintf(stderr, "[hostpad] SDL_InitSubSystem(GAMECONTROLLER) failed: %s -- falling back to raylib\n",
                                 SDL_GetError());
                    return false;
                }
                // We poll; no event traffic wanted (an unread queue just fills to its cap).
                SDL_JoystickEventState(SDL_IGNORE);
                SDL_GameControllerEventState(SDL_IGNORE);
                s_up = true;
                std::fprintf(stderr, "[hostpad] SDL2 %d.%d.%d gamecontroller backend (PS2X_HOSTPAD=raylib restores GLFW)\n",
                             SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
                return true;
            }

            void closeSlot(Slot &s)
            {
                if (s.gc) SDL_GameControllerClose(s.gc);
                else if (s.js) SDL_JoystickClose(s.js);
                s = Slot{};
            }

            void shutdown()
            {
                if (!s_up) return;
                for (Slot &s : s_slots) closeSlot(s);
                SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
                s_up = false;
            }

            bool openDevice(int devIndex, Slot &s)
            {
                const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(devIndex);
                if (SDL_IsGameController(devIndex))
                {
                    s.gc = SDL_GameControllerOpen(devIndex);
                    if (!s.gc) return false;
                    s.js = SDL_GameControllerGetJoystick(s.gc);
                    s.controller = true;
                    const char *n = SDL_GameControllerName(s.gc);
                    s.name = n ? n : "Gamepad";
                }
                else
                {
                    s.js = SDL_JoystickOpen(devIndex);
                    if (!s.js) return false;
                    const char *n = SDL_JoystickName(s.js);
                    s.name = n ? n : "Joystick";
                }
                s.id = id;
                s.nAxes = SDL_JoystickNumAxes(s.js);
                s.nButtons = SDL_JoystickNumButtons(s.js);
                if (!s.gc)
                {
                    // No mapping in SDL's database. Same layout test the GLFW path used: enough
                    // axes and buttons to be a pad, not a mislabelled keyboard or a sensor node.
                    static const bool s_all = envFlag("PS2X_PAD_ALLDEV");
                    s.controller = s_all || (s.nAxes >= 4 && s.nButtons >= 8);
                }
                return true;
            }

            void scanHotplug()
            {
                // Removals first, so a re-plugged pad can take its old slot back.
                for (Slot &s : s_slots)
                    if (s.js && !SDL_JoystickGetAttached(s.js))
                    {
                        log("slot lost: '%s'", s.name.c_str());
                        closeSlot(s);
                    }
                const int n = SDL_NumJoysticks();
                for (int i = 0; i < n; ++i)
                {
                    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
                    if (id < 0 || s_rejected.count(id)) continue;
                    bool known = false;
                    for (const Slot &s : s_slots) if (s.js && s.id == id) { known = true; break; }
                    if (known) continue;
                    Slot *free = nullptr;
                    for (Slot &s : s_slots) if (!s.js) { free = &s; break; }
                    if (!free) break;
                    if (!openDevice(i, *free))
                    {
                        s_rejected.insert(id);
                        continue;
                    }
                    if (!free->controller)
                    {
                        // Not a pad. Let go of it (SDL would otherwise keep reading it) and never
                        // offer it; the slot stays free for a real one.
                        log("skipped '%s' (axes=%d buttons=%d, not a controller)", free->name.c_str(),
                            free->nAxes, free->nButtons);
                        s_rejected.insert(id);
                        closeSlot(*free);
                        continue;
                    }
                    log("slot %d: '%s' mapped=%d axes=%d buttons=%d", int(free - s_slots.data()),
                        free->name.c_str(), free->gc ? 1 : 0, free->nAxes, free->nButtons);
                }
            }

            float norm(Sint16 v) { return v < 0 ? float(v) / 32768.0f : float(v) / 32767.0f; }
            float trig(Sint16 v) { return v <= 0 ? 0.0f : float(v) / 32767.0f; }

            void readMapped(Slot &s)
            {
                SDL_GameController *gc = s.gc;
                auto b = [&](SDL_GameControllerButton sb) { return SDL_GameControllerGetButton(gc, sb) ? 1 : 0; };
                s.btn[kBtnA]     = b(SDL_CONTROLLER_BUTTON_A);
                s.btn[kBtnB]     = b(SDL_CONTROLLER_BUTTON_B);
                s.btn[kBtnX]     = b(SDL_CONTROLLER_BUTTON_X);
                s.btn[kBtnY]     = b(SDL_CONTROLLER_BUTTON_Y);
                s.btn[kBtnBack]  = b(SDL_CONTROLLER_BUTTON_BACK);
                s.btn[kBtnGuide] = b(SDL_CONTROLLER_BUTTON_GUIDE);
                s.btn[kBtnStart] = b(SDL_CONTROLLER_BUTTON_START);
                s.btn[kBtnLS]    = b(SDL_CONTROLLER_BUTTON_LEFTSTICK);
                s.btn[kBtnRS]    = b(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
                s.btn[kBtnLB]    = b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
                s.btn[kBtnRB]    = b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
                s.btn[kBtnDpadUp]    = b(SDL_CONTROLLER_BUTTON_DPAD_UP);
                s.btn[kBtnDpadDown]  = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                s.btn[kBtnDpadLeft]  = b(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                s.btn[kBtnDpadRight] = b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                s.ax[kAxLX] = norm(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
                s.ax[kAxLY] = norm(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
                s.ax[kAxRX] = norm(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
                s.ax[kAxRY] = norm(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
                s.ax[kAxLT] = trig(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
                s.ax[kAxRT] = trig(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
            }

            // Unmapped joystick: assume the xpad/evdev layout (buttons A B X Y LB RB Back Start
            // Guide LS RS, axes LX LY LT RX RY RT, hat 0 = d-pad), which is what every Xbox-shaped
            // pad reports on Linux. PS2X_PAD_MAPPINGS is the fix for anything else.
            void readRaw(Slot &s)
            {
                SDL_Joystick *js = s.js;
                auto b = [&](int i) { return (i < s.nButtons && SDL_JoystickGetButton(js, i)) ? 1 : 0; };
                auto a = [&](int i) -> Sint16 { return i < s.nAxes ? SDL_JoystickGetAxis(js, i) : 0; };
                s.btn[kBtnA] = b(0);  s.btn[kBtnB] = b(1);  s.btn[kBtnX] = b(2);  s.btn[kBtnY] = b(3);
                s.btn[kBtnLB] = b(4); s.btn[kBtnRB] = b(5); s.btn[kBtnBack] = b(6); s.btn[kBtnStart] = b(7);
                s.btn[kBtnGuide] = b(8); s.btn[kBtnLS] = b(9); s.btn[kBtnRS] = b(10);
                const Uint8 hat = SDL_JoystickNumHats(js) > 0 ? SDL_JoystickGetHat(js, 0) : 0;
                s.btn[kBtnDpadUp]    = (hat & SDL_HAT_UP) ? 1 : 0;
                s.btn[kBtnDpadDown]  = (hat & SDL_HAT_DOWN) ? 1 : 0;
                s.btn[kBtnDpadLeft]  = (hat & SDL_HAT_LEFT) ? 1 : 0;
                s.btn[kBtnDpadRight] = (hat & SDL_HAT_RIGHT) ? 1 : 0;
                s.ax[kAxLX] = norm(a(0)); s.ax[kAxLY] = norm(a(1));
                s.ax[kAxRX] = norm(a(3)); s.ax[kAxRY] = norm(a(4));
                // Raw triggers rest at -32768 on evdev; fold to 0..1.
                s.ax[kAxLT] = (norm(a(2)) + 1.0f) * 0.5f;
                s.ax[kAxRT] = (norm(a(5)) + 1.0f) * 0.5f;
            }

            void update()
            {
                if (!s_up) return;
                SDL_GameControllerUpdate();   // joystick update + hotplug detection
                // [sdlflush] Only the joystick/controller range (0x600-0x6FF). SDL owns the WINDOW now
                // (raylib's SDL platform polls the same queue), so flushing everything could drop
                // window/keyboard events queued between raylib's polls, e.g. the resize a
                // SetWindowSize/fullscreen toggle posts synchronously.
                SDL_FlushEvents(SDL_JOYAXISMOTION, SDL_FINGERDOWN - 1);
                scanHotplug();
                for (Slot &s : s_slots)
                {
                    if (!s.js) continue;
                    if (s.gc) readMapped(s); else readRaw(s);
                    s.btn[kBtnLT] = s.ax[kAxLT] > 0.1f ? 1 : 0;   // raylib's trigger-as-button rule
                    s.btn[kBtnRT] = s.ax[kAxRT] > 0.1f ? 1 : 0;
                }
            }

            const Slot *slot(int i)
            {
                if (!s_up || i < 0 || i >= kMaxSlots || !s_slots[size_t(i)].js) return nullptr;
                return &s_slots[size_t(i)];
            }

            void addMappings(const char *text)
            {
                if (!s_up || !text) return;
                // One mapping per line; SDL_GameControllerAddMapping takes exactly one.
                std::string line;
                int added = 0;
                for (const char *p = text;; ++p)
                {
                    if (*p == '\n' || *p == '\0')
                    {
                        if (!line.empty() && line[0] != '#')
                            if (SDL_GameControllerAddMapping(line.c_str()) >= 0) ++added;
                        line.clear();
                        if (*p == '\0') break;
                    }
                    else if (*p != '\r')
                        line.push_back(*p);
                }
                log("added %d mappings", added);
                // A pad opened raw before its mapping arrived stays raw; SDL applies new mappings to
                // pads opened afterwards. Reopen anything unmapped so it picks the mapping up.
                for (Slot &s : s_slots)
                    if (s.js && !s.gc) closeSlot(s);
                scanHotplug();
            }
        } // namespace sdl
#endif
    } // namespace

    Backend backend() { return s_backend; }
    const char *backendName() { return s_backend == Backend::Sdl2 ? "sdl2" : "raylib"; }

    void init()
    {
        if (s_inited) return;
        s_inited = true;
        s_backend = pickBackend();
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2 && !sdl::init())
            s_backend = Backend::Raylib;
#endif
        if (s_backend == Backend::Raylib)
            rl::init();
#if defined(__linux__)
        // The evdev reader exists for pads GLFW cannot map; SDL reads evdev itself, so with the
        // SDL backend it would just be a second reader of the same device.
        ps2_stubs::PadEvdevLinux::setEnabled(s_backend == Backend::Raylib);
#endif
        // PS2X_PAD_MAPPINGS: a file of SDL-format mappings for pads neither database knows.
        if (const char *mf = std::getenv("PS2X_PAD_MAPPINGS"))
        {
            if (FILE *f = std::fopen(mf, "rb"))
            {
                std::fseek(f, 0, SEEK_END);
                const long sz = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                if (sz > 0 && sz < 4 * 1024 * 1024)
                {
                    std::string buf(static_cast<size_t>(sz), '\0');
                    if (std::fread(buf.data(), 1, buf.size(), f) == buf.size())
                        addMappings(buf.c_str());
                }
                std::fclose(f);
            }
        }
    }

    void shutdown()
    {
#if defined(PS2X_HAVE_SDL2)
        sdl::shutdown();
#endif
        s_inited = false;
    }

    void update()
    {
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { sdl::update(); return; }
#endif
#if defined(__linux__)
        ps2_stubs::PadEvdevLinux::instance().update();
#endif
        // raylib refreshes its own tables inside EndDrawing()/PollInputEvents().
    }

    bool available(int slot)
    {
        if (slot < 0 || slot >= kMaxSlots) return false;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) return sdl::slot(slot) != nullptr;
#endif
        return IsGamepadAvailable(slot);
    }

    bool isController(int slot)
    {
        if (slot < 0 || slot >= kMaxSlots) return false;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s && s->controller; }
#endif
        return rl::isController(slot);
    }

    const char *name(int slot)
    {
        if (slot < 0 || slot >= kMaxSlots) return nullptr;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s ? s->name.c_str() : nullptr; }
#endif
        return rl::name(slot);
    }

    int axisCount(int slot)
    {
        if (slot < 0 || slot >= kMaxSlots) return 0;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s ? s->nAxes : 0; }
#endif
        return GetGamepadAxisCount(slot);
    }

    int buttonCount(int slot)
    {
        if (slot < 0 || slot >= kMaxSlots) return 0;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s ? s->nButtons : 0; }
#endif
        return rl::buttonCount(slot);
    }

    bool buttonDown(int slot, int button)
    {
        if (slot < 0 || slot >= kMaxSlots || button < 0 || button >= kMaxButtons) return false;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s && s->btn[size_t(button)] != 0; }
#endif
        return IsGamepadButtonDown(slot, button);
    }

    float axis(int slot, int ax)
    {
        if (slot < 0 || slot >= kMaxSlots || ax < 0 || ax >= kMaxAxes) return 0.0f;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { const auto *s = sdl::slot(slot); return s ? s->ax[size_t(ax)] : 0.0f; }
#endif
        return GetGamepadAxisMovement(slot, ax);
    }

    bool rumble(int slot, uint16_t low, uint16_t high, uint32_t ms)
    {   // [rumble] SDL takes the joystick lock itself, so this is safe from the guest thread
        if (slot < 0 || slot >= kMaxSlots) return false;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2)
        {
            const auto *s = sdl::slot(slot);
            if (!s) return false;
            if (s->gc) return SDL_GameControllerRumble(s->gc, low, high, ms) == 0;
            if (s->js) return SDL_JoystickRumble(s->js, low, high, ms) == 0;
        }
#endif
        (void)low; (void)high; (void)ms;
        return false;   // the raylib/GLFW backend has no rumble
    }

    void addMappings(const char *text)
    {
        if (!text) return;
#if defined(PS2X_HAVE_SDL2)
        if (s_backend == Backend::Sdl2) { sdl::addMappings(text); return; }
#endif
        SetGamepadMappings(text);
    }
}
