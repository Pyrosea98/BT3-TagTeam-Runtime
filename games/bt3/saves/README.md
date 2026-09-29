# Save with progress — `BASLUS-21678DBZT3`

A memory-card save for Dragon Ball Z: Budokai Tenkaichi 3 (SLUS-216.78) that carries actual
story progress and unlocks. Copying it over the empty save the runtime creates gives you a
card that the game reads as "already played" — the characters, stages, missions and items the
progress earned are available.

Just the 16 KB save block. No `icon.sys` or `.ico`: those are the save's browser metadata and
the runtime's memory card never renders a browser anyway, so they are not needed for the game
to read the data.

## Why this exists

The runtime's memory card is a stub: 40 of 43 `mc*` functions are placeholders, so the save the
game itself writes is 99.2% zeros — a header and nothing else. Nothing ever persists, and
nothing the game writes is ever read back. That is bug 8: characters and stages stay locked
because there is no progress to read, not because the game refuses to unlock them.

This save is the other half of that fix: the data side was the only thing missing. The game
reads it normally — `[mclog] Read fd=4 dst=0x90f1f8 size=16384`.

It came from a real 8.25 MB `Mcd001.ps2` memory card image (one of the usual save-dump
collections), extracted with the `BASLUS-21678DBZT3` entry of that card. See
`docs/BUGFIXES-TODO.md` for the card parser details.

## Install

The front-end has a toggle for this under the memory-card section. It copies this directory
over `<deploy>/savedata/BASLUS-21678DBZT3/`. Your previous save is backed up next to it as
`BASLUS-21678DBZT3.bak` before being replaced, so nothing is lost.

To do it by hand, replace the contents of `<deploy>/savedata/BASLUS-21678DBZT3/` with the files
here.

## Note on persistence

Once installed, this save is not *maintained* — the runtime still cannot write the game back to
the card, so new progress you make in the session is not saved to disk and will be gone next
launch. The toggle restores a known-good progressed state; it is not a save-file system. Full
`mc*` support is still open work.
