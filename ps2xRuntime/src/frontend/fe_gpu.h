#pragma once

#include <cstdint>
#include <string>
#include <vector>

// GPU enumeration and selection for the front-end. Windows-only by design: the game window is
// created by SDL without an adapter argument, so the only lever the OS exposes is the per-app
// graphics preference, which we write here. Everywhere else the list is empty and the page
// says so instead of pretending there is a choice.
namespace gpu
{
    struct Adapter
    {
        std::string name;
        std::uint64_t vramMB = 0;
        bool software = false;
        // The adapter the live OpenGL context is actually running on, matched by GL_RENDERER.
        bool active = false;
    };

    // Hardware adapters, most VRAM first. `activeName` is the GL_RENDERER string of the current
    // context (may be empty); the matching entry comes back with active = true.
    std::vector<Adapter> adapters(const std::string &activeName);

    // GL_RENDERER of the context that is current right now, or empty when there is none.
    std::string currentRenderer();

    // Makes the game window land on `adapterName` from now on. Empty name clears the preference
    // and lets Windows decide again. False + error when the platform cannot do it.
    bool applyPreference(const std::string &adapterName, std::string *error);

    // True when this build can list adapters at all.
    bool supported();
}
