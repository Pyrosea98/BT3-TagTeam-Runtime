// [notify] The UI sounds. See include/runtime/ps2x_notify.h for what this is for.
//
// Split from ps2x_notify.cpp because this half needs the audio backend and the bus does not: the
// queue has to be linkable by a headless probe, and dragging PS2AudioBackend and the host sink in
// to test a FIFO would be absurd.
//
// Synthesised rather than sampled, and that is a decision rather than a shortcut. There are no
// audio assets anywhere in this tree and no dependency that could decode one. Adding a WAV loader,
// or a format library, to play a two-note chime would be a lot of new surface for a very small
// result. Every tone here is a sum of sines under an envelope: a few dozen lines, nothing to ship,
// nothing that can go missing from an install, nothing that fails on a machine with no decoder, and
// a few hundred bytes each instead of a few hundred kilobytes.

#include "runtime/ps2x_notify.h"
#include "runtime/ps2_audio.h"          // PS2AudioBackend, for the volume factors
#include "runtime/ps2_host_audio.h"     // ps2x_audio::playSound, the host-side sink

#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;

// One note is a fundamental plus its first harmonic, detuned very slightly so the result is not a
// pure test tone, under a fast attack and an exponential tail. The attack matters more than it
// looks: a tone that starts at full amplitude clicks, and a click on a notification is worse than
// no notification at all.
struct Note
{
    float hz;
    float at;        // seconds from the start
    float dur;       // seconds
    float gain;
};

std::vector<int16_t> render(const std::vector<Note> &notes, float length)
{
    const size_t n = static_cast<size_t>(length * kSampleRate);
    std::vector<float> acc(n, 0.0f);

    for (const Note &note : notes)
    {
        const size_t begin = static_cast<size_t>(note.at * kSampleRate);
        const size_t count = static_cast<size_t>(note.dur * kSampleRate);
        // Two partials, the second quieter and a hair sharp. The beating between them is what stops
        // this sounding like a modem.
        const float partialGain[2] = {0.5f, 0.11f};
        const float partialMul[2] = {1.0f, 2.0f};
        for (int p = 0; p < 2; ++p)
        {
            const float amp = note.gain * partialGain[p];
            if (amp <= 0.0f)
                continue;
            const float f = note.hz * partialMul[p] * 1.00035f;
            const float w = 2.0f * 3.14159265f * f / static_cast<float>(kSampleRate);
            for (size_t i = 0; i < count; ++i)
            {
                const size_t idx = begin + i;
                if (idx >= n)
                    break;
                const float t = static_cast<float>(i) / kSampleRate;
                // 6 ms attack, then an exponential decay that is still audible at the end and then
                // stops, so the note has a tail instead of being cut off.
                const float env = t < 0.006f ? (t / 0.006f) : std::exp(-3.4f * (t - 0.006f));
                acc[idx] += amp * env * std::sin(w * static_cast<float>(i));
            }
        }
    }

    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i)
    {
        // Soft clip rather than a hard one: a hard clamp on a summed tone is audible as grit, and
        // this keeps the peaks in range without the fizz.
        const float v = acc[i] * 0.9f;
        out[i] = static_cast<int16_t>((v / (1.0f + std::fabs(v))) * 32000.0f);
    }
    return out;
}

// The tones.
//
// C-E-G at 523/659/784 Hz is the interval set that reads as "something good happened" without
// having to be taught to anyone, so Great is that triad rolled. Good is the same interval set
// compressed into two notes, Bad is it inverted because falling intervals read as wrong in a way
// rising ones do not, and Info is a single blip because it only has to say "something started".
struct ToneSpec
{
    std::vector<Note> notes;
    float length;
};

ToneSpec specFor(NotifyTone tone)
{
    switch (tone)
    {
    case NotifyTone::None:
        return {{}, 0.0f};
    case NotifyTone::Info:
        return {{{740.0f, 0.00f, 0.10f, 0.55f}}, 0.16f};
    case NotifyTone::Good:
        return {{{587.0f, 0.00f, 0.13f, 0.60f}, {880.0f, 0.10f, 0.24f, 0.60f}}, 0.38f};
    case NotifyTone::Great:
        // Deliberately the longest and the brightest of the four: this is the one that has to be
        // noticed over a fight.
        return {{{523.0f, 0.00f, 0.30f, 0.62f},
                 {659.0f, 0.085f, 0.30f, 0.58f},
                 {784.0f, 0.170f, 0.42f, 0.55f},
                 {1046.0f, 0.255f, 0.40f, 0.34f}}, 0.72f};
    case NotifyTone::Bad:
        return {{{392.0f, 0.00f, 0.16f, 0.58f}, {294.0f, 0.13f, 0.32f, 0.58f}}, 0.50f};
    }
    return {{}, 0.0f};
}

// Rendered once per tone, on first use. playSound() copies the buffer, so the cache can be handed
// out by pointer forever after.
const std::vector<int16_t> *cachedTone(NotifyTone tone)
{
    static std::array<std::vector<int16_t>, 5> cache;
    static std::array<bool, 5> built{};

    const size_t i = static_cast<size_t>(tone);
    if (i >= cache.size())
        return nullptr;
    if (!built[i])
    {
        const ToneSpec spec = specFor(tone);
        cache[i] = render(spec.notes, spec.length);
        built[i] = true;
    }
    return cache[i].empty() ? nullptr : &cache[i];
}

}   // namespace

void ps2xNotifyPlayTone(NotifyTone tone)
{
    if (tone == NotifyTone::None || !ps2xNotifySoundEnabled())
        return;

    const std::vector<int16_t> *pcm = cachedTone(tone);
    if (!pcm)
        return;

    // The game's own master and SFX sliders apply, so a notification sits in the mix where the
    // player put it. The curtain mute does NOT: that factor exists to keep the player from hearing
    // the game drive its own menus during the netjump walk, and a host-side chime saying the
    // session is up is not that noise. It is the one deliberate exception, and naming it here is
    // why it is not folded into the volume line above.
    const float vol = PS2AudioBackend::masterVolume() * PS2AudioBackend::sfxVolume() * 0.55f;
    if (vol <= 0.001f)
        return;

    ps2x_audio::playSound(pcm->data(), pcm->size(), kSampleRate, 1.0f, vol);
}
