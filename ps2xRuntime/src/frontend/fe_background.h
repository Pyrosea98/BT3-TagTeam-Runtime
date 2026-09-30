#pragma once

#include <cstdint>
#include <filesystem>

// Menu background (assets/background.png), loaded once and uploaded as a plain GL texture.
// Kept in its own translation unit on purpose: it needs SDL_opengl.h for the GL 1.1 entry
// points, and that header must not end up in the same TU as imgui_impl_opengl3.h, which
// carries its own loader.
namespace frontend
{
    // Reads the PNG's IHDR width/height and nothing else: no decode, no GL. The front-end needs the art's
    // aspect to pick the menu's window height BEFORE the window (and its GL context) exists, and
    // loadBackground() cannot be called that early -- it calls glGenTextures, so with no current context it
    // returns texture 0 and leaves GL in a state where ImGui's font atlas never uploads either (no text
    // anywhere in the shell). Decoding 2 MB of PNG just to read two integers is the alternative, and this is
    // two integers.
    bool probeBackgroundSize(const std::filesystem::path &png, int *outW, int *outH);

    // Returns 0 when the file is missing or cannot be decoded (the menu then draws without it).
    std::uint32_t loadBackground(const std::filesystem::path &png, int *outW, int *outH);
    void freeBackground(std::uint32_t tex);
}
