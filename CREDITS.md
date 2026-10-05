# Credits

SMG Online stands on other people's work.

## The mod it is built on

**[SMGNetworkMultiplayer](https://github.com/Headpenguin/SMGNetworkMultiplayer) by Headpenguin** is the
foundation: the in-game networking, drawing other players, hit detection
between players and star-bit sync all come from it. The `SMGNetworkMultiplayer/`
folder here is that project (commit `a67c85f`) with our changes on top. Its
original credits are in `SMGNetworkMultiplayer/credits.txt`.

That project has no licence file, so its code is not ours to relicense. If you
are its author and want this handled differently (a fork instead of a copy, a
different notice, or removal), open an issue and it will be done.

## Tools and research

- [Kamek](https://github.com/Treeki/Kamek) by Treeki: the code loader/linker
- [Petari](https://github.com/SMGCommunity/Petari) (SMG1 decompilation, CC0) and
  [Bussun](https://github.com/SMGCommunity/Bussun) by shibboleet and the SMG community: headers and symbols
- Syati by Aurum: the loader this mod is injected with
- [Dolphin](https://dolphin-emu.org) and its Riivolution support
- WiiBrew and libogc for the IOS network interfaces

## What this project added

- A new relay server (`server/`) with a new wire protocol: player slots that
  free themselves, per-stage relaying, reliable shared save progress, settings
- Client changes: stage filtering, timeouts and reconnects, walk/run/swim
  animation sync, shared progress, non-lethal star bits, player colours
- Launchers for Windows and macOS, a Wii/Wii U pack, the ONLINE title screen
  tool, tests and an in-emulator test harness

## How it was written

The code added by this project was written with heavy help from an AI coding
assistant (Claude), directed and play-tested by people.

No game code was decompiled for it. The Petari project does not allow
AI-assisted decompilation, and this project respects that: it only adds new
code that calls game functions by their known names and reads data layouts
already documented in Petari's headers and already-decompiled source.

## Not included

Nintendo's game is not part of this repository in any form. The title-screen
tool modifies a file taken from your own copy of the game on your own machine.
The word "ONLINE" on the title screen is our own artwork (`title/online-strip.bin`,
drawn by `tools/title/make_strip.py`).
