# In-emulator test harness

Drives real Dolphin instances running the patched game, without touching the
desktop: instances run on a hidden Windows desktop, input is injected and
frames/memory are read through the Python-scripting build of Dolphin
(`DOLPHIN` in drive.py points at one; any Felk-style scripting fork works).

    py -3 drive.py launch 1          # needs profiles\test1 (Dolphin user dir)
    py -3 goto_game.py 1             # strap screen -> title -> save file 2 -> game
    py -3 drive.py shot 1 name       # name-1.png from the emulator framebuffer
    py -3 drive.py seq 1 "stick=0;1 NONE:60 A:8"
    py -3 state.py 1 2               # dump the mod's slot table from memory
    py -3 drive.py kill

Profiles: a Dolphin user dir with Config\ (WiimoteNew.ini with an emulated
Wii Remote + Nunchuk, Source = 1) and a copy of an SMG save in
Wii\title\00010000\524d4745. Run a server first (server\smg_server.py) and
point riivolution\serverIP.txt at it.

state.py reads `info__11Multiplayer` at loader base 0x806C25A0 + its offset in
the Kamek map; regenerate the offset after changing the client
(Kamek ... -output-map=x.map) - the base comes from "Patch addr" in the
Dolphin log (Logger.ini: OSREPORT + WriteToFile).

Screenshots can show two frames mixed (the copy races the renderer);
take two in a row before trusting what is on them.
