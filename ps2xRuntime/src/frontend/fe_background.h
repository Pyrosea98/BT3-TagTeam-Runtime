#pragma once

#include <cstdint>
#include <filesystem>

// Menu background (assets/background.png), loaded once and uploaded as a plain GL texture.
// Kept in its own translation unit on purpose: it needs SDL_opengl.h for the GL 1.1 entry
// points, and that header must not end up in the same TU as imgui_impl_opengl3.h, which
// carries its own loader.
namespace frontend
{
    // Returns 0 when the file is missing or cannot be decoded (the menu then draws without it).
    std::uint32_t loadBackground(const std::filesystem::path &png, int *outW, int *outH);
    void freeBackground(std::uint32_t tex);
}
