// [notify] One notification bus for the whole runtime. See src/lib/ps2x_notify.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

// Popups and their sounds, shared by every feature that wants to tell the player something without
// stealing the focus.
//
// This exists because two unrelated features needed the same thing and were about to grow their
// own: netplay reporting a session state, and the achievement tracker reporting an unlock. They
// are different features with different triggers and different lifetimes, and they stay separate
// all the way down -- separate producers, separate kinds, separate tones. What they share is the
// presentation: where the popup goes, how it moves, and what it sounds like. Putting that in one
// place is the difference between a third feature taking ten lines and taking a rewrite.
//
// Nothing here draws and nothing here polls. Producers push; the overlay drains once per frame.

// Who is talking. The kind is presentation only -- it picks the accent colour, the glyph and the
// default tone -- so a new producer gets consistent styling by picking one, and an existing one
// never has to be edited to look right.
enum class NotifyKind : uint8_t
{
    Netplay,
    Achievement,
};

// The sound. Kept separate from the kind because the two do not move together: an achievement
// unlock and a connected session are both "good news" but they should not be the same noise, and
// netplay's FAILED TO CONNECT is bad news from a feature whose other states are not.
enum class NotifyTone : uint8_t
{
    None,    // no sound -- for a state the player is already looking at
    Info,    // one soft blip: something started
    Good,    // two notes up: it worked
    Great,   // a short rising arpeggio: the rare one, reserved for an achievement
    Bad,     // two notes down: it did not work
};

struct NotifyEvent
{
    NotifyKind kind = NotifyKind::Netplay;
    NotifyTone tone = NotifyTone::None;
    const char *title = "";
    const char *body = "";
};

// The queue. Bounded, and the oldest entry is dropped when it is full: a popup is a thing that
// happened, and if the player was away for a minute the answer to "what did I miss" is the log, not
// forty stacked cards. Callers are on the game's own threads, so this is safe from any of them.
void ps2xNotifyPush(const NotifyEvent &ev);

// Playback control. Sounds are opt-out as a whole because a runtime that beeps at you in a game is
// worse than one that is silent, and the switch belongs next to the volume sliders, not in an env
// var nobody sets.
void ps2xNotifySetSoundEnabled(bool on);
bool ps2xNotifySoundEnabled();

// Drains up to `max` events, oldest first. Returns how many were written. The overlay owns the
// animation, so it wants the events, not a rendered widget.
int ps2xNotifyDrain(NotifyEvent *out, int max);

// How many are waiting, so the overlay can skip opening a frame when there is nothing to say.
int ps2xNotifyPending();

// ---------------------------------------------------------------------------------------------
// The drop box
// ---------------------------------------------------------------------------------------------
//
// A file the runtime watches, so something outside the process can raise a notification. Written
// to `<dir>/notify.request`, one line per request:
//
//     <kind> <tone> <title> :: <body>        raise a card
//     dump <prefix>                           write <prefix>.<n>.bin, the whole 32 MB of guest RAM
//
//   kind  net | ach          -- Netplay | Achievement
//   tone  none | info | good | great | bad
//
// The file is read once and then removed, so writing it is how you say it; there is no state to
// keep in sync and nothing to unregister. Use it from a shell like this -- the temp file plus
// rename is what makes the write atomic, so the runtime can never read a half-written request:
//
//     printf 'ach great Hello :: World\n' > "$D/savedata/.notify.tmp" \
//         && mv "$D/savedata/.notify.tmp" "$D/savedata/notify.request"
//
// `dump` is why the poller takes the guest RAM: it is the same buffer PS2X_DUMPKEY + F9 writes,
// reachable from a terminal instead of only from a keypress. That is the entire input surface a
// headless diagnosis needs, and it is also the shape a real "capture the guest state" feature
// should take.
//
// It exists because the events that raise a card are all things that have to happen in the game --
// a session connecting, an achievement earned -- and neither can be provoked from a terminal. It is
// also the shape any future "tell the running game something" feature should take, so it is a
// control channel rather than a test switch: it does not change what the game does, it only adds a
// card to a stack that already exists.
//
// Polled from the guest tick rather than from the overlay's draw(): the tick runs every frame
// whatever the UI is doing, and it is the only place with the guest RAM in hand. Throttled
// internally to one look at the file every 15 calls, so a caller can pass this once per frame and
// pay nothing. Returns the number of notifications raised.
int ps2xNotifyPollDropBox(const char *dir, const uint8_t *rdram);

// Renders and plays one tone. Called by ps2xNotifyPush(); declared here because the bus and the
// synth are separate files and this is the seam between them. It respects the sound switch, so a
// caller that wants a tone does not have to check it.
void ps2xNotifyPlayTone(NotifyTone tone);
