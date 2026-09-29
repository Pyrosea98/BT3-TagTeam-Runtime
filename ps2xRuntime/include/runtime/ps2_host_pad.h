#pragma once

#include <cstdint>

// Host gamepad layer: the one place the runtime reads controllers from.
//
// Two backends behind the same calls: raylib (GLFW joysticks, what shipped until now) and SDL2
// (its own joystick subsystem; the window stays raylib's). PS2X_HOSTPAD=raylib|sdl2 picks at
// startup, default sdl2 when built in.
//
// Button and axis indices are raylib's GAMEPAD_BUTTON_* / GAMEPAD_AXIS_* values on both
// backends -- pad_pN.conf persists them, so a saved binding must mean the same thing whichever
// backend is live. Sticks read -1..+1, triggers 0..1 (the evdev reader's convention);
// LEFT/RIGHT_TRIGGER_2 read as buttons once the trigger passes 0.1, like raylib.
namespace ps2x_pad
{
    enum class Backend { Raylib, Sdl2 };
    Backend backend();
    const char *backendName();

    // Call once after the raylib window exists; safe to call again (no-op).
    void init();
    void shutdown();
    // Once per frame on the render thread. Refreshes every slot's state and handles hotplug.
    void update();

    constexpr int kMaxSlots = 16;
    bool available(int slot);
    // A real controller, not something udev merely tagged ID_INPUT_JOYSTICK (keyboards with a
    // Consumer Control page, a DualSense's motion-sensor node). PS2X_PAD_ALLDEV=1 accepts all.
    bool isController(int slot);
    const char *name(int slot);
    int axisCount(int slot);
    int buttonCount(int slot);
    bool buttonDown(int slot, int button);
    float axis(int slot, int axis);
    // [rumble] Drive the pad's motors: low = the big (low-frequency) motor, high = the small one, 0..65535,
    // for ms milliseconds. False when the slot has no device or the device cannot rumble.
    bool rumble(int slot, uint16_t low, uint16_t high, uint32_t ms);

    // SDL mapping-format text, one mapping per line (PS2X_PAD_MAPPINGS file). Render thread.
    void addMappings(const char *text);
}
