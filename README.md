# SMG Online

Online multiplayer for **Super Mario Galaxy (USA, RMGE01)**. Every player runs
their own copy of the game; a small relay server shows each player the other
Marios in the same galaxy and keeps everyone's save progress in sync, so a
group can play through the whole game together.

Built on Headpenguin's [SMGNetworkMultiplayer](https://github.com/Headpenguin/SMGNetworkMultiplayer)
(the game-side code) with a rewritten server and protocol (version 1.0). See
[CREDITS.md](CREDITS.md).

**Heavily developed using AI (local and cloud models).** Most of the code and
documentation here was written by AI under the author's direction. It is not
fully human-made, so read it and test it with that in mind. The cloud model
was Anthropic's Claude (Opus 5.5), used through Claude Code; the commits it
wrote are marked `Co-Authored-By: Claude`.

> **What has actually been tested:** the game mod in Dolphin on Windows
> (played over the internet), and the server on Windows and Linux.
> **Not tested on real hardware:** the Wii / Wii U version has never been run
> on an actual console, and the Mac launchers have never been run on a Mac.
> Those two are provided as-is; reports are welcome.

| Platform | Status |
|---|---|
| Dolphin on Windows | Tested; played over the internet by several people |
| Server on Windows / Linux | Tested (automated test suite on both) |
| Dolphin on macOS | Untested: launchers written, never run on a real Mac (`mac/`) |
| Wii / Wii U (Wii Mode) | Untested: builds and loads in Dolphin, never run on a console (`wii/`) |

**No game files are included.** You need your own copy of the game.

## Play

You need: Dolphin, your own dump of Super Mario Galaxy (USA), and
Python 3 on the PC that hosts.

| What | How |
|---|---|
| The easy way | double-click `SMG ONLINE.bat`: pick a colour, then Join or Host |
| Host + 2 players on this PC | double-click `START GALAXY ONLINE.bat` |
| Host + N players on this PC | `powershell -ExecutionPolicy Bypass -File start-galaxy.ps1 -Players 4` |
| Join a friend | double-click `JOIN A FRIEND.bat` and type their address |
| Server only (dedicated host) | `powershell -ExecutionPolicy Bypass -File start-server.ps1` |

The launcher finds Dolphin and the game in your Downloads folder; otherwise
pass `-Dolphin <path to Dolphin.exe>` and `-Game <path to the .wbfs/.iso/.rvz>`.

Each local player gets their own Dolphin profile in
`%APPDATA%\SMG-Online\profiles\pN` (so updating this folder never loses a save). On
first launch it copies your normal Dolphin settings and your SMG save into
it, so your regular Dolphin save is never touched. With one controller, the
focused window gets the input; for two local players, give `p2` its own
controller in Dolphin (Controllers > Wii Remote 1) for that window.

### Server settings

`SERVER SETTINGS.bat` opens `server\server-settings.ini` (created the first
time the server runs). Restart the server after changing it.

| Setting | What it does |
|---|---|
| `port` | UDP port to listen on (default 5029). Players join with `ip:port`. |
| `max_players` | Connection limit, 1-250 (default 64). |
| `world` | Name of the shared world. Each name has its own saved progress (`progress.json` for `default`, `progress-<name>.json` otherwise). |
| `share_progress` | `no` = players only see each other and keep their own progress. |
| `share_star_bits` | `no` = star bits are not sent to other players. |
| `player_timeout` | Seconds of silence before a player's slot is freed (default 60). |
| `update_rate` | Position updates per second, 10-60 (default 60). |
| `verbose` | `yes` = more detail in the server window. |

Every setting can also be given on the command line for one run
(`py -3 server\smg_server.py --help`).

### Playing over the internet

The host forwards **UDP port 5029** on their router to their PC (or everyone
joins the same Tailscale/ZeroTier network) and shares their public IP.
Friends then use `JOIN A FRIEND.bat` with that IP. Only the host needs Python.

## Launcher window, colours, title screen

`SMG ONLINE.bat` opens a small window: pick a player colour (8 outfits that
recolour only the shirt, cap and overalls), then Join or Host. Settings are
remembered per Windows user. The colour travels in the position packet (a
byte that used to be padding), so old and new versions can play together.

The title screen gets `ONLINE` under the logo. It is built on each player's
PC from their own game file (`title\build-title.ps1` + our own artwork in
`title\online-strip.bin`), so no game files are shipped.

## What is synced

| | |
|---|---|
| Player position, facing and movement | yes, smoothed |
| Animations | yes, including walk/run/swim blends (these used to freeze) and jumps |
| Ground pound / stomping other players | yes |
| Star bits you shoot | yes, other players see them and get hit |
| Save progress: Power Stars, Grand Stars, visited stars, galaxy and dome unlocks, story events, "N more stars" unlock countdowns, Luigi quest, Luma feeding | yes, shared by everyone on the server (from the Comet Observatory on) |
| Enemies, coins, objects, bosses | no, every player has their own world |

Players see each other only when they are in the same galaxy **and** the
same star (the Observatory counts as one). Up to 16 players can be visible
at once in one place (memory for all 16 is reserved and loads fine on stock
Dolphin settings; 4 at once has been tested in-game); the server itself
accepts up to 64 (`--max-players`, max 250), and players elsewhere cost
nothing.

### Beating the game together

Everyone shares one world's progress, so the group beats the game as a team:

1. For a new run the host starts the server once with
   `powershell -ExecutionPolicy Bypass -File start-server.ps1 -Fresh`
   (the old shared progress is kept as `server\progress.json.old`).
   Everyone makes a **new save file**.
2. Each player plays the intro on their own. Sharing starts the moment a
   player reaches the Comet Observatory.
3. From then on, any Power Star or Grand Star someone collects, and every
   galaxy, dome and story unlock it causes, reaches everyone, including the
   star counts that gate later galaxies and the final Bowser fight.
4. Anyone can play the final Bowser galaxy once the shared star total is
   high enough. When one player beats it, everyone's file counts as cleared.

Received progress is written into your save the next time your game saves
(after a star or when you leave a galaxy). It is also re-sent every time you
load a stage while connected, so nothing is lost if you quit without saving.

### Shared progress

The server keeps an ordered log of every progress change in
`server\progress.json` and sends it to every client until the client
confirms it, so nothing is lost to dropped packets, and anyone who joins
later (or reloads their save) receives everything. Changes are merges:
nobody loses a star they already have. To start a fresh shared world run the
server with `--fresh` (the old log is kept as `progress.json.old`).

## Stability

* A player who quits or crashes disappears for everyone within ~1.5 s, and
  their server slot is freed after 60 s of silence.
* If the server restarts, games notice within 5 s and reconnect by
  themselves; the progress log survives the restart.
* The server ignores malformed or spoofed packets and never trusts the
  player id a client claims.

What has been tested: the server test suite (`server\test_server.py`,
including 30% packet loss and a 64-player load test), and the real game in
Dolphin with two players plus simulated extra players: seeing each other,
walking animation, star bits reaching the other player, a player quitting,
server restart and reconnect,
progress delivery to late joiners, and players in different stages not
seeing each other.

## Troubleshooting

* **Nobody shows up:** both players must be in the same galaxy and star.
  Check the server window: every game appears as `player N joined`.
* **"protocol 0.x" warning in the server window:** that game still runs the
  old mod; copy `riivolution\CustomCode_USA.bin` from this folder to them.
* **Friends cannot connect:** UDP 5029 is not forwarded, or a firewall
  blocks Python. Allow Python through Windows Firewall when asked.

## What is in this repository

| Folder | What |
|---|---|
| `SMGNetworkMultiplayer/` | The code that runs inside the game (C/C++, built with CodeWarrior + Kamek) |
| `server/` | The relay server (Python, no dependencies) and its tests |
| top folder, `title/` | Windows launchers and the title-screen tool |
| `mac/`, `wii/` | macOS launchers; SD-card layout for Wii / Wii U |
| `riivolution/` | Ready-built mod file and loader patch for Dolphin |
| `patches/` | Our changes to the Petari and Bussun headers needed to build |
| `tools/` | Packaging, title artwork, privacy-checked export, in-emulator test harness |

## Building

The server, launchers and tools are plain scripts. To change the game-side
code see [docs/BUILDING.md](docs/BUILDING.md). After changing the server run
`python server/test_server.py`.

Headpenguin's original C++ server speaks the previous protocol and does not
work with this client.

## Licence and credits

Our own code is under the MIT licence; see [LICENSE](LICENSE) for exactly what
that covers (Headpenguin's code in `SMGNetworkMultiplayer/` is his and has no
licence). People and projects this builds on, and how the code was written,
are in [CREDITS.md](CREDITS.md).

This is a fan project. It is not affiliated with or endorsed by Nintendo.
