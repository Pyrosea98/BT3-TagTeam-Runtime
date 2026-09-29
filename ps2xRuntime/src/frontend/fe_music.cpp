#include "frontend/fe_music.h"

#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

namespace music
{
    namespace
    {
        Music s_music = {};
        bool s_haveStream = false;
        bool s_playing = false;
        bool s_muted = false;
        std::string s_name;
        float s_volume = 0.0f;   // what the ramp is heading towards
        float s_current = 0.0f;  // what SetMusicVolume was last given
        float s_from = 0.0f;     // where the current ramp started
        float s_fadeSeconds = 0.0f;
        float s_fadeStep = 0.0f;
        bool s_stopping = false;  // PLAY asked for silence on purpose
        bool s_deviceOpen = false;

        const float kMenuVolume = 0.55f;

        bool hasAudioExtension(const std::filesystem::path &p)
        {
            std::string e = p.extension().string();
            std::transform(e.begin(), e.end(), e.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return e == ".flac" || e == ".mp3" || e == ".ogg" || e == ".wav" || e == ".qoa" ||
                   e == ".xm" || e == ".mod";
        }

        int preference(const std::filesystem::path &p)
        {
            // music.flac is the name setup.py normalises any download to, and what a user dropping
            // their own track is told to use, so it wins. Anything else in the folder is still
            // accepted rather than ignored.
            std::string n = p.filename().string();
            std::transform(n.begin(), n.end(), n.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (n == "music.flac")
                return 0;
            if (n.find("shine") != std::string::npos)
                return 1;
            return 2;
        }
    }

    std::string trackName()
    {
        return s_name;
    }

    bool playing()
    {
        return s_playing;
    }

    bool muted()
    {
        return s_muted;
    }

    void setMuted(bool m)
    {
        if (s_muted == m)
            return;
        s_muted = m;
        // The ramp goes from wherever the volume is now to the new target. Anchoring it to the
        // current value is what makes unmuting audible again: a ramp that always started at 0
        // with the step left at 0 pinned the target to silence forever.
        s_from = s_current;
        s_volume = m ? 0.0f : kMenuVolume;
        s_fadeSeconds = m ? 0.25f : 0.05f;   // muting is a short fade, unmuting is instant
        s_fadeStep = 0.0f;
    }

    bool start(const std::filesystem::path &exeDir)
    {
        if (s_haveStream)
            return true;

        // <exeDir>/music is the documented spot, <exeDir>/assets/music is where it also works so
        // the track can ride along with the rest of the assets.
        const std::filesystem::path dirs[2] = {exeDir / "music", exeDir / "assets" / "music"};
        std::error_code ec;
        std::vector<std::filesystem::path> found;
        std::filesystem::path picked;
        for (const std::filesystem::path &dir : dirs)
        {
            found.clear();
            for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (entry.is_regular_file(ec) && hasAudioExtension(entry.path()))
                    found.push_back(entry.path());
            }
            if (!found.empty())
            {
                picked = dir;
                break;
            }
        }
        if (found.empty())
        {
            std::fprintf(stderr, "[fe] no menu track in %s or %s (silence)\n",
                         dirs[0].string().c_str(), dirs[1].string().c_str());
            return false;
        }
        std::stable_sort(found.begin(), found.end(), [](const std::filesystem::path &a,
                                                        const std::filesystem::path &b) {
            return preference(a) < preference(b);
        });

        InitAudioDevice();
        if (!IsAudioDeviceReady())
        {
            std::fprintf(stderr, "[fe] no audio device for the menu theme\n");
            return false;
        }
        s_deviceOpen = true;

        s_music = LoadMusicStream(found.front().string().c_str());
        if (!s_music.stream.buffer)
        {
            std::fprintf(stderr, "[fe] could not decode %s\n", found.front().string().c_str());
            CloseAudioDevice();
            s_deviceOpen = false;
            s_music = Music();
            return false;
        }
        s_haveStream = true;
        s_name = found.front().filename().string();
        s_volume = s_muted ? 0.0f : kMenuVolume;
        s_current = 0.0f;
        s_from = 0.0f;
        s_stopping = false;
        s_fadeSeconds = 1.5f;   // ease the theme in when the window opens
        s_fadeStep = 0.0f;
        PlayMusicStream(s_music);
        s_playing = true;
        SetMusicVolume(s_music, 0.0f);
        std::fprintf(stderr, "[fe] menu theme: %s\n", s_name.c_str());
        return true;
    }

    void update(float dt)
    {
        if (!s_haveStream)
            return;
        // A stream that stopped on its own (device loss, decoder hiccup) has to come back, or the
        // unmuted menu would be silent for the rest of the session.
        if (!s_playing && !s_stopping && s_volume > 0.0f)
        {
            PlayMusicStream(s_music);
            s_playing = true;
        }
        if (s_playing)
            UpdateMusicStream(s_music);

        if (s_fadeSeconds > 0.0f)
        {
            s_fadeStep += dt / s_fadeSeconds;
            if (s_fadeStep >= 1.0f)
            {
                s_fadeStep = 1.0f;
                s_fadeSeconds = 0.0f;
            }
        }
        const float target = s_from + (s_volume - s_from) * s_fadeStep;
        if (target != s_current)
        {
            s_current = target;
            SetMusicVolume(s_music, s_current);
        }
        // The PLAY ramp is done: let go of the stream so the game can have the device.
        if (s_stopping && s_current <= 0.0f && s_playing)
        {
            StopMusicStream(s_music);
            s_playing = false;
        }
    }

    void fadeOut(float seconds)
    {
        if (!s_haveStream)
            return;
        s_stopping = true;
        s_from = s_current;
        s_volume = 0.0f;
        if (s_muted || s_current <= 0.0f)
        {
            // Nothing to hear: stop now so the boot is not held up by a silent ramp.
            s_fadeSeconds = 0.0f;
            s_fadeStep = 1.0f;
            s_current = 0.0f;
            if (s_playing)
            {
                StopMusicStream(s_music);
                s_playing = false;
            }
            return;
        }
        s_fadeSeconds = seconds;
        s_fadeStep = 0.0f;
    }

    bool silent()
    {
        return !s_playing && s_current <= 0.0f;
    }

    void shutdown()
    {
        if (s_haveStream)
        {
            if (s_playing)
                StopMusicStream(s_music);
            UnloadMusicStream(s_music);
            s_music = Music();
            s_haveStream = false;
        }
        s_playing = false;
        s_current = 0.0f;
        s_volume = 0.0f;
        s_from = 0.0f;
        s_fadeSeconds = 0.0f;
        s_fadeStep = 0.0f;
        s_stopping = false;
        if (s_deviceOpen)
        {
            CloseAudioDevice();
            s_deviceOpen = false;
        }
    }
}
