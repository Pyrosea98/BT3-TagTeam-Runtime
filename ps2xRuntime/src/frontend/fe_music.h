#pragma once

#include <filesystem>
#include <string>

// Menu theme for the front-end, played through the vendored raylib audio device (miniaudio, so
// FLAC/MP3/OGG all decode). The track is NOT part of the repository: it is the game's own
// soundtrack and stays a local file next to the executable, under <exeDir>/music/. Everything
// here degrades to silence when the file is absent or the audio device cannot be opened.
namespace music
{
    // Looks for a track under <exeDir>/music and starts it. False when there is nothing to play.
    bool start(const std::filesystem::path &exeDir);

    // Pumps the stream and advances the volume ramp. dt in seconds.
    void update(float dt);

    // Ramps to silence over `seconds` and then stops. Used by the PLAY handoff so the menu
    // theme fades out under the screen fade. Returns immediately when already silent.
    void fadeOut(float seconds);

    // True once the fade-out has finished and the stream is stopped, i.e. safe to boot the game.
    bool silent();

    void setMuted(bool muted);
    bool muted();

    bool playing();
    std::string trackName();

    // Unloads the stream and closes the audio device. Must run before the game boots: the
    // runtime opens its own audio device, and two of them on Windows is a fight.
    void shutdown();
}
