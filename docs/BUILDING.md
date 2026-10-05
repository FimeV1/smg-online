# Building the game-side mod

You only need this to change the code that runs inside the game
(`SMGNetworkMultiplayer/`). The server, launchers and tools are plain scripts,
and ready-built mod files are in `riivolution/` and `wii/`.

Building works on Linux (WSL on Windows is fine).

## What you need

| | Where from |
|---|---|
| Headpenguin's Petari fork, branch `bussun-development` | https://github.com/Headpenguin/Petari |
| Headpenguin's Bussun fork | https://github.com/Headpenguin/BussunChanges |
| Kamek, built for your system (.NET) | https://github.com/Headpenguin/Kamek |
| The CodeWarrior PowerPC compiler `mwcceppc` | not included and not redistributable; the SMG modding community documents how to obtain it |

Check out the exact commits listed in `patches/BASES.txt`, then apply our
header changes:

```
cd Petari        && git apply /path/to/smg-online/patches/petari-mp.patch
cd BussunChanges && git apply /path/to/smg-online/patches/bussun.patch
```

Put the compiler at `BussunChanges/deps/CodeWarrior/mwcceppc.exe` and Kamek at
`BussunChanges/deps/Kamek/Kamek`.

## Build

```
cd SMGNetworkMultiplayer
cp config.mk.template config.mk      # then set the paths in it (no spaces in paths)
make dolphinDebug                    # Dolphin build  -> bin/Debug/Dolphin/CustomCode_USA.bin
make all                             # Wii build      -> bin/Debug/CustomCode_USA.bin
```

After changing a header, delete `obj/*.o obj/Dolphin/*.o` first; the Makefile
does not track header dependencies.

Copy the Dolphin build to `riivolution/CustomCode_USA.bin` and the Wii build to
`wii/SD-card/smgonline/CustomCode_USA.bin`, then `python tools/pack_all.py`
makes the three zips in `packs/`.

## Things worth knowing

- The server hardcodes the packet sizes; `source/packets.cpp` has compile-time
  checks that fail the build if a packet struct changes size.
- `s32` is `long` in this toolchain. A game function taking `int` must be
  declared with `int`, or its mangled name will not match the symbol map.
- The mod's own memory comes from a small heap the game needs at boot. Keep
  big buffers out of static storage; allocate them when a stage loads
  (see `playerColors.cpp`).
- Texture table entries in a model file are 0x20 bytes; the `ResTIMG` struct in
  the Petari fork has an extra runtime field, so do not index an array of it.

## Tests

```
python server/test_server.py            # server: simulated clients, packet loss, settings
python server/test_server.py --load 64  # plus a 64-player load test
bash mac/test-launcher.sh .             # Mac launcher logic against a fake home folder
```

`tools/emutest/` drives real Dolphin instances from scripts (input, screenshots,
memory reads) using a Python-scripting build of Dolphin; see its README.
