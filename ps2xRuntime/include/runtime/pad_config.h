#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <mutex>

// Per-player virtual-pad configuration for the PS2X host layer.
//
// Replaces the old "merge every gamepad + keyboard into every pad" behaviour
// with up to 4 independently configurable player profiles. Each profile binds
// every PS2 action (D-pad, face buttons, L1..R2, L3/R3, Select/Start and both
// analog sticks) to either a keyboard key or a gamepad button/axis, and picks
// which physical device (if any) feeds that player. A player with no explicit
// device still falls back to "any connected gamepad + keyboard", so existing
// single-pad behaviour is preserved until devices are assigned.
//
// Persisted as a simple line-based file: PS2X_PAD_CONFIG env var, otherwise
// <ELF dir>/pad.conf. Written atomically via temp+rename.
namespace ps2_stubs
{
    enum class PadAction : uint8_t
    {
        Select = 0,
        L3,
        R3,
        Start,
        Up,
        Right,
        Down,
        Left,
        L2,
        R2,
        L1,
        R1,
        Triangle,
        Circle,
        Cross,
        Square,
        LStickXNeg,
        LStickXPos,
        LStickYNeg,
        LStickYPos,
        RStickXNeg,
        RStickXPos,
        RStickYNeg,
        RStickYPos,
        Count
    };

    enum class PadBindKind : uint8_t
    {
        None,
        Key,
        Button,
        Axis
    };

    struct PadBind
    {
        PadBindKind kind = PadBindKind::None;
        int value = -1;    // Key: raylib KEY_*, Button: gamepad button index, Axis: gamepad axis index
        float sign = 1.0f; // Axis: direction (+1 / -1)
        float deadzone = 0.15f;
    };

    enum class PadDeviceKind : uint8_t
    {
        None,     // any connected gamepad + keyboard (legacy behaviour)
        Keyboard, // keyboard only
        Gamepad   // one specific gamepad index, no keyboard
    };

    struct PadDevice
    {
        PadDeviceKind kind = PadDeviceKind::None;
        int gamepad = -1;
    };

    struct PadPlayerConfig
    {
        PadDevice device;
        std::array<PadBind, static_cast<size_t>(PadAction::Count)> binds{};
    };

    // Result of polling one player: active-low button word + analog bytes.
    struct PadPacket
    {
        uint16_t buttons = 0xFFFFu; // active-low: 0 = pressed
        uint8_t lx = 0x80u;
        uint8_t ly = 0x80u;
        uint8_t rx = 0x80u;
        uint8_t ry = 0x80u;
    };

    class PadConfig
    {
    public:
        // [launcher] BT3 is a 1v1 fighter: cap local players at 2.
        static constexpr size_t kPlayerCount = 2;

        static PadConfig &instance();
        // [overlay] Suspend gamepad->guest input while the settings overlay is open, so
        // pad navigation of the overlay never bleeds into the running game.
        static void setInputSuspended(bool suspended);
        static bool inputSuspended();

        // Thread-safe snapshot / mutators (the guest thread reads via poll(), the
        // configurator UI writes via these; both hold the internal mutex).
        PadPlayerConfig snapshot(size_t p) const;
        void setDevice(size_t p, const PadDevice &device);
        void setBind(size_t p, PadAction action, const PadBind &bind);
        void setPlayerDefaults(size_t p, PadDeviceKind kind);
        void resetPlayer(size_t p);

        void setDefaultDir(const std::string &elfDir);
        std::string defaultPath() const;

        // Per-player savedata files: <ELF dir>/savedata/pad_p1.conf (p1) and
        // pad_p2.conf (p2). defaultPath() is kept for compatibility so the
        // legacy single pad.conf can be found for migration.
        std::string playerConfigPath(size_t p) const;
        std::string legacyConfigPath() const;

        bool load();  // load per-player savedata (legacy pad.conf migration)
        bool save() const;

        bool loaded() const { return m_loaded; }

        // Poll one player's live packet from the assigned device(s).
        PadPacket poll(size_t player) const;

    private:
        PadConfig();
        PadConfig(const PadConfig &) = delete;
        PadConfig &operator=(const PadConfig &) = delete;

        mutable std::mutex m_mutex;
        static bool s_inputSuspended;
        std::array<PadPlayerConfig, kPlayerCount> m_players;
        std::string m_dir;
        bool m_loaded = false;
    };

    // Called once at runtime start with the ELF directory so defaults and the
    // config file resolve to the game folder. Idempotent.
    void padConfigInit(const std::string &elfDir);

    // Poll a player (clamped to [0,3]).
    PadPacket padPollPlayer(size_t player);
    // [rumble] Drive the motors of the device that feeds `player` (the same resolution poll() uses):
    // low = big motor, high = small motor, 0..65535, for ms milliseconds.
    void padRumblePlayer(int player, uint16_t low, uint16_t high, uint32_t ms);

    // Legacy helper used by game pad hooks (e.g. BT3 sceDbc): builds the packet
    // for `player` and returns the active-low button word.
    uint16_t ps2xLivePadButtons(int player, uint8_t &lx, uint8_t &ly, uint8_t &rx, uint8_t &ry);

    // Map a kernel pad port/slot pair to a player profile (port 0/1, slot 0/1).
    inline int padPlayerForPortSlot(int port, int slot)
    {
        return slot * 2 + port;
    }

    const char *padActionName(PadAction action);
    const char *padBindKindName(PadBindKind kind);
    std::string padBindDisplay(const PadBind &bind);
    std::string padDeviceDisplay(const PadDevice &device);

    // "Gamepad N" (the pad_pN.conf / launcher convention: N-th controller GLFW
    // reports) <-> the raw GLFW slot. Keeps the config files independent of the
    // slot numbering GLFW happens to assign (e.g. an Xbox on slot 1 = "Gamepad 0").
    int padGamepadSlot(int index);   // index -> GLFW slot, or -1
    int padGamepadIndex(int glfwSlot); // GLFW slot -> index, or -1

    // Is gamepad slot `g` an actual controller worth offering to the player?
    //
    // Not everything GLFW hands a joystick slot is a controller: udev sets
    // ID_INPUT_JOYSTICK=1 on keyboards that expose System Control / Consumer Control HID
    // pages, and a DualSense additionally publishes separate "Motion Sensors" and "Touchpad"
    // joysticks. Shared so the config UI and the input path agree on what counts.
    bool padSlotIsController(int g);
}
