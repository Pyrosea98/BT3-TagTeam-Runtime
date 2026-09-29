# Notifications

One bus for "something just happened". See `include/runtime/ps2x_notify.h`.

Netplay reporting a session state and the achievement tracker reporting an unlock are unrelated
features: different triggers, different lifetimes, different things the player might want to switch
off. What they share is that both need to say something without stealing the focus, and that both
want the same place to say it and the same way to sound.

That shared part is here. Everything else stays with the feature that owns it.

## The split

Producers raise an event and nothing else:

```cpp
const NotifyEvent ev{NotifyKind::Netplay, NotifyTone::Good, "Connected", "Player 2 is in."};
ps2xNotifyPush(ev);
```

The bus queues it, plays the tone, and forgets about it. It does not draw, it does not poll, and it
does not know what a session or an achievement is.

`NotifyKind` picks the accent colour, the glyph and the source label. `NotifyTone` picks the sound.
They are separate enums on purpose, because the two do not move together: an achievement unlock and
a connected session are both good news and must not be the same noise, and netplay's failures are
bad news from a feature whose other states are not.

| | netplay | achievement |
|---|---|---|
| accent | `#5CBDD0` cyan | the overlay's gold |
| glyph | filled diamond | five-point star |
| label | `NETPLAY` | `ACHIEVEMENT` |

The two glyphs are drawn from geometry rather than loaded. There is no art for either, and two lines
of trigonometry beat a new asset to install and keep in sync.

## Where the cards go

Top-left, newest on top, 336 px wide, at most five at once.

The corner was not free. The netplay label owns the bottom-right of the main menu, the perf meter
owns the top-right during a match, and the settings panel is centred. That is what is left, and it
happens to be the right place on its own terms: it is where the eye goes for something that was just
added, it is far from the thumbstick's resting arc, and it is nowhere near the netplay label — so a
session change and an unlock arriving in the same second are two readable cards rather than one
smear.

## Motion

Slide in from the left while fading up over 0.20 s, hold, fade out over 0.45 s. Holds differ by kind
(3.6 s for an achievement, 2.8 s for netplay) because they are read at different rates: an unlock is
worth a second longer than a session state.

Cards below ease toward their slot over 0.09 s rather than snapping, so a new arrival pushes the
stack down smoothly. Cards retire from the tail, which is the oldest — retiring from the front
would make everything below jump a slot while a card was still on screen.

Every card is positioned from the viewport on the draw list and takes no input, so one animating
never disturbs the ImGui layout of another, and the whole thing survives a panel that is opening or
closing underneath it.

## The sounds

Synthesised in `ps2x_notify_sound.cpp`. That is a decision, not a shortcut: there are no audio
assets anywhere in this tree and no dependency that could decode one, so a WAV loader or a format
library would be a lot of new surface for a two-note chime. Every tone is a sum of sines under an
envelope, which is a few dozen lines with no failure modes — nothing to ship, nothing that can go
missing from an install, nothing that fails on a machine with no decoder, a few hundred bytes each.

| tone | shape | length | used by |
|---|---|---|---|
| `Info` | one blip, 740 Hz | 0.16 s | netplay started / listening |
| `Good` | 587 → 880 Hz | 0.38 s | netplay connected |
| `Great` | C-E-G rolled, + octave | 0.72 s | achievement unlock |
| `Bad` | 392 → 294 Hz, falling | 0.50 s | connect failed, session ended |

C-E-G is the interval set that reads as "something good happened" without having to be taught, so
`Great` is that triad rolled. `Good` is the same intervals compressed into two notes. `Bad` is
inverted, because falling intervals read as wrong in a way rising ones do not.

Two details in the synth that are load-bearing:

- **The 6 ms attack.** A tone that starts at full amplitude clicks, and a click on a notification is
  worse than no notification at all.
- **The soft clip.** A hard clamp on a summed tone is audible as grit; `v / (1 + |v|)` keeps the
  peaks in range without the fizz.

Each tone is rendered once, on first use, and cached: `playSound()` copies the buffer, so the cache
is handed out by pointer forever after.

### Volume

The game's own master and SFX sliders apply, so a notification sits in the mix where the player put
it. The curtain mute does **not**: that factor exists to keep the player from hearing the game drive
its own menus during the netjump walk, and a host-side chime saying the session is up is not that
noise. It is the one deliberate exception.

The switch is in the overlay's **Audio** tab, under the mixer, because that is where the sliders it
rides are. It is one switch for all tones: a player who wants the game quiet does not want a chime
from a feature they never turned on.

## The drop box

`scripts/notify.sh` raises a card in a running game:

```sh
scripts/notify.sh ach great "I. Am. Powerful." "Reach Maximum Star Level"
scripts/notify.sh net bad  "Session ended" "The peer went away."
```

It writes `<deploy>/savedata/notify.request` and the runtime reads it once and removes it. The write
goes to a temp file and is renamed into place, which is what makes it atomic — the runtime deletes
the request as soon as it reads it, so a half-written file would be a half-written notification.

The format is one event per line, `<kind> <tone> <title> :: <body>`, with `#` for comments. It is
parsed tolerantly: a request typed by hand should produce a card with a slightly wrong title, not
silently nothing.

This exists because every event that raises a card is something that has to happen in the game — a
session connecting, an achievement earned — and neither can be provoked from a terminal. It is a
control channel rather than a test switch: it does not change what the game does, it only adds a
card to a stack that already exists, and the same mechanism would serve a "tell the running game
something" feature later.

The runtime looks for the file once every 15 frames, so the cost on a frame where nobody wrote
anything is one `stat`. It is polled from `draw()` before the early-out, not from
`drawNotifyStack()`, because the point is that a card can be raised while nothing else is being
drawn.

## Adding a producer

Push an event. There is nothing else to do — no registration, no switch statement, no case to add to
the drawer. If it needs its own look, add a `NotifyKind`; if it needs its own noise, add a
`NotifyTone`.
