# =====================================================================
#  SMG Online - launcher
#
#    .\start-galaxy.ps1                      host a server + 2 local players
#    .\start-galaxy.ps1 -Players 4           host + 4 local players
#    .\start-galaxy.ps1 -Players 1 -Server 203.0.113.7:5029
#                                            join a friend's server
#
#  1. Starts the relay server (server\smg_server.py) if we are hosting and
#     nothing is listening yet
#  2. Writes the server address into riivolution\serverIP.txt
#  3. Boots one Dolphin per local player straight into patched SMG1, each
#     with its own profile (%APPDATA%\SMG-Online\profiles\pN) so saves and settings
#     never collide
#
#  Everything is resolved relative to this script, so the folder can be moved.
# =====================================================================
param(
    [int]$Players = 2,
    # "ip" or "ip:port" of a server to JOIN. Leave empty to host on this PC.
    [string]$Server = "",
    [int]$Port = 5029,
    [string]$Dolphin = "",
    [string]$Game = "",
    # Player colour 0-7 (0 = normal Mario). Remembered once set.
    [int]$Color = -1,
    # Title screen: "on" adds ONLINE under the logo (built from your own game
    # file), "off" keeps the normal one. Remembered once set.
    [ValidateSet("", "on", "off")][string]$Title = "",
    # Ask for the server address (Enter = the last one used), then join it
    [switch]$Join,
    # Only set/change the remembered Dolphin and game locations
    [switch]$SetPaths,
    # Prepare everything (server address, preset, profiles) but launch nothing
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$base = $PSScriptRoot

function Find-First([string[]]$candidates) {
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    return $null
}

# ---- locate Dolphin and the game ------------------------------------
# Paths (and the last server joined) are remembered per Windows user, so they
# survive replacing this folder with a newer copy of the mod. A copy next to
# the script is kept too.
$savedFiles = @("$env:APPDATA\SMG-Online\settings.txt", "$base\launcher-paths.txt")
$saved = @{}
foreach ($file in $savedFiles) {
    if (-not (Test-Path -LiteralPath $file)) { continue }
    foreach ($line in Get-Content -LiteralPath $file) {
        $k, $v = $line -split "=", 2
        if (-not $v) { continue }
        $k = $k.Trim().TrimStart([char]0xFEFF)
        if (-not $saved.ContainsKey($k)) { $saved[$k] = $v.Trim() }
    }
}

function Save-Settings {
    $out = @()
    foreach ($k in @("dolphin", "game", "server", "color", "title")) { if ($saved[$k]) { $out += "$k=$($saved[$k])" } }
    foreach ($file in $savedFiles) {
        try {
            New-Item -ItemType Directory -Force -Path (Split-Path $file) | Out-Null
            Set-Content -LiteralPath $file -Value $out -Encoding UTF8
        } catch { }
    }
}

# Asks for a file. With a current value, pressing Enter keeps it.
function Ask-Path([string]$what, [string]$example, [string]$current) {
    while ($true) {
        if ($current) { $prompt = "$what`n  now: $current`n  New full path (or just press Enter to keep it)" }
        else { $prompt = "Where is $what? Paste the full path (e.g. $example)" }
        $p = (Read-Host $prompt).Trim().Trim('"')
        if (-not $p -and $current) { return $current }
        if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { return (Resolve-Path -LiteralPath $p).Path }
        Write-Host "      File not found, try again. (Shift + right-click the file > Copy as path)" -ForegroundColor Yellow
    }
}

$dolphinExe = Find-First @(
    $Dolphin,
    $saved["dolphin"],
    "$base\Dolphin-x64\Dolphin.exe",
    "$env:USERPROFILE\Downloads\Dolphin-x64\Dolphin.exe",
    "$env:USERPROFILE\Desktop\Dolphin-x64\Dolphin.exe",
    "$env:ProgramFiles\Dolphin\Dolphin.exe",
    "$env:ProgramFiles\Dolphin-x64\Dolphin.exe"
)
$gameName = "Super Mario Galaxy (USA, Canada) (En,Fr,Es)"
$gameFile = Find-First @(
    $Game,
    $saved["game"],
    "$base\game\$gameName.wbfs",
    "$env:USERPROFILE\Downloads\$gameName.wbfs",
    "$env:USERPROFILE\Downloads\$gameName\$gameName.wbfs"
)

if ($SetPaths) {
    # Change the remembered locations, then stop
    $dolphinExe = Ask-Path "Dolphin.exe" "C:\Dolphin-x64\Dolphin.exe" $dolphinExe
    $gameFile = Ask-Path "Your Super Mario Galaxy (USA) game file (.wbfs/.iso/.rvz)" "C:\Games\SMG.wbfs" $gameFile
    $saved["dolphin"] = $dolphinExe; $saved["game"] = $gameFile
    Save-Settings
    Write-Host "Saved. You will not be asked again." -ForegroundColor Green
    return
}

if (-not $dolphinExe) { $dolphinExe = Ask-Path "Dolphin.exe" "C:\Dolphin-x64\Dolphin.exe" "" }
if (-not $gameFile) { $gameFile = Ask-Path "your Super Mario Galaxy (USA) game file (.wbfs/.iso/.rvz)" "C:\Games\SMG.wbfs" "" }
$saved["dolphin"] = $dolphinExe; $saved["game"] = $gameFile

if ($Join) {
    # Ask which server to join; Enter reuses the last one
    while (-not $Server) {
        if ($saved["server"]) { $Server = (Read-Host "Server address (press Enter for $($saved['server']))").Trim() ; if (-not $Server) { $Server = $saved["server"] } }
        else { $Server = (Read-Host "Server address (e.g. 203.0.113.7 or 203.0.113.7:5029)").Trim() }
    }
}
if ($Server) { $saved["server"] = $Server }
if ($Color -ge 0) { $saved["color"] = "$Color" }
if ($Title) { $saved["title"] = $Title }
$playerColor = 0
if ($saved["color"] -match '^[0-7]$') { $playerColor = [int]$saved["color"] }
$useTitle = ($saved["title"] -ne "off")
Save-Settings

if ($Players -lt 1 -or $Players -gt 8) { throw "-Players must be between 1 and 8 on one PC." }

# ---- 1: server ------------------------------------------------------
if ($Server) {
    $address = $Server
    if ($address -notmatch ":") { $address = "$address`:$Port" }
    Write-Host "[1/3] Joining server $address" -ForegroundColor Cyan
} else {
    # The port (and everything else about the server) is set in
    # server\server-settings.ini; -Port on the command line overrides it.
    $settingsFile = "$base\server\server-settings.ini"
    $portGiven = $PSBoundParameters.ContainsKey("Port")
    if (-not $portGiven -and (Test-Path -LiteralPath $settingsFile)) {
        $m = Select-String -LiteralPath $settingsFile -Pattern '^\s*port\s*=\s*(\d+)' | Select-Object -First 1
        if ($m) { $Port = [int]$m.Matches[0].Groups[1].Value }
    }
    $address = "127.0.0.1:$Port"
    Write-Host "[1/3] Hosting on this PC (UDP $Port)..." -ForegroundColor Cyan
    $listening = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue
    if ($DryRun) {
        Write-Host "      (dry run: not starting the server)" -ForegroundColor DarkGray
    } elseif ($listening) {
        Write-Host "      server already up." -ForegroundColor DarkGray
    } else {
        $python = Get-Command py -ErrorAction SilentlyContinue
        $pyArgs = @("-3")
        if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue; $pyArgs = @() }
        if (-not $python) { throw "Python 3 is needed to host the server (https://www.python.org/downloads/)." }
        # Its own window: shows who joins, and closing it stops the server.
        # cmd /k keeps the window open if the server stops with an error.
        $serverCmd = "`"$($python.Source)`" $($pyArgs -join ' ') `"$base\server\smg_server.py`""
        if ($portGiven) { $serverCmd += " --port $Port" }
        Start-Process -FilePath "cmd.exe" -ArgumentList "/k `"title SMG Online server & $serverCmd`"" -WorkingDirectory "$base\server"
        Start-Sleep -Seconds 2
    }
}

# The game reads this file (through Riivolution) when it boots
# "address:port c=N": N is this player's colour (older versions ignore it)
$serverLine = $address
if ($playerColor -gt 0) { $serverLine = "$address c=$playerColor" }
Set-Content -LiteralPath "$base\riivolution\serverIP.txt" -Value $serverLine -Encoding ascii -NoNewline
Write-Host "      serverIP.txt -> $serverLine" -ForegroundColor Green

# ---- title screen ("Super Mario Galaxy ONLINE") -------------------------
# Built once from this player's own game file; rebuilt if the game file or the
# artwork changes. Without it the normal title screen is used.
$modXml = "$base\riivolution\riivo_USA.xml"
$titleArc = "$base\riivolution\TitleLogo.arc"
$titleStamp = "$base\riivolution\TitleLogo.stamp"
if ($useTitle -and (Test-Path -LiteralPath "$base\title\build-title.ps1")) {
    $strip = Get-Item -LiteralPath "$base\title\online-strip.bin" -ErrorAction SilentlyContinue
    $want = "$gameFile|$((Get-Item -LiteralPath $gameFile).Length)|$($strip.Length)|$($strip.LastWriteTimeUtc.Ticks)"
    $have = ""
    if (Test-Path -LiteralPath $titleStamp) { $have = (Get-Content -LiteralPath $titleStamp -Raw).Trim() }
    if (-not (Test-Path -LiteralPath $titleArc) -or $have -ne $want) {
        Write-Host "      building the ONLINE title screen from your game file..." -ForegroundColor DarkGray
        Remove-Item -LiteralPath $titleArc -Force -ErrorAction SilentlyContinue
        $built = & "$base\title\build-title.ps1" -Dolphin $dolphinExe -Game $gameFile -Out $titleArc
        if ($built) { Set-Content -LiteralPath $titleStamp -Value $want -Encoding UTF8 }
    }
    if (Test-Path -LiteralPath $titleArc) {
        # Same patch plus one more file; kept as its own xml so the plain one
        # keeps working when there is no title file.
        $modXml = "$base\riivolution\riivo_USA_title.xml"
        $xml = Get-Content -LiteralPath "$base\riivolution\riivo_USA.xml" -Raw
        $line = '<file disc="/LayoutData/TitleLogo.arc" external="TitleLogo.arc" />'
        $xml = $xml.Replace('</patch>', "`t$line`r`n`t</patch>")
        [System.IO.File]::WriteAllText($modXml, $xml, (New-Object System.Text.UTF8Encoding($false)))
    }
}

# ---- 2: Dolphin game preset (absolute paths, regenerated every launch) ----
$preset = "$base\galaxy-online.json"
$descriptor = [ordered]@{
    type           = "dolphin-game-mod-descriptor"
    version        = 1
    "base-file"    = $gameFile.Replace("\", "/")
    "display-name" = "SMG Online (USA)"
    riivolution    = @{
        patches = @(
            @{
                xml     = $modXml.Replace("\", "/")
                root    = "$base\riivolution".Replace("\", "/")
                options = @(@{ "section-name" = "Syati Loader"; "option-id" = "smgmp"; choice = 1 })
            }
        )
    }
}
# No BOM: Dolphin's JSON parser rejects one
[System.IO.File]::WriteAllText($preset, ($descriptor | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))

# ---- 3: one Dolphin per player ---------------------------------------
# Where this person's normal Dolphin settings live: portable install, the
# current default location, or the older Documents location.
$dolphinDir = Split-Path $dolphinExe
$globalUser = "$env:APPDATA\Dolphin Emulator"
if (Test-Path -LiteralPath "$dolphinDir\portable.txt") { $globalUser = "$dolphinDir\User" }
elseif (-not (Test-Path -LiteralPath "$globalUser\Config")) {
    $docs = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Dolphin Emulator"
    if (Test-Path -LiteralPath "$docs\Config") { $globalUser = $docs }
}
$saveRel = "Wii\title\00010000\524d4745"   # RMGE = Super Mario Galaxy (USA)

# Sets one "key = value" in a section of a Dolphin .ini (creating file, section
# or key as needed). Written without a BOM: Dolphin does not expect one.
function Set-IniValue([string]$path, [string]$section, [string]$key, [string]$value) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $path) { foreach ($l in [System.IO.File]::ReadAllLines($path)) { $lines.Add($l) } }
    $start = -1
    for ($n = 0; $n -lt $lines.Count; $n++) { if ($lines[$n].Trim() -eq "[$section]") { $start = $n; break } }
    if ($start -lt 0) {
        $lines.Add("[$section]"); $lines.Add("$key = $value")
    } else {
        $done = $false
        for ($n = $start + 1; $n -lt $lines.Count -and -not $lines[$n].Trim().StartsWith("["); $n++) {
            if ($lines[$n] -match "^\s*$([regex]::Escape($key))\s*=") { $lines[$n] = "$key = $value"; $done = $true; break }
        }
        if (-not $done) { $lines.Insert($start + 1, "$key = $value") }
    }
    [System.IO.File]::WriteAllLines($path, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

for ($i = 1; $i -le $Players; $i++) {
    # Profiles (settings + the save the mod plays on) live with the Windows
    # user, not in this folder, so updating the mod never loses a save.
    $userDir = "$env:APPDATA\SMG-Online\profiles\p$i"
    $oldDir = "$base\dolphin-users\p$i"
    if (-not (Test-Path -LiteralPath $userDir) -and (Test-Path -LiteralPath $oldDir)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $userDir) | Out-Null
        Copy-Item -LiteralPath $oldDir -Destination $userDir -Recurse
        Write-Host "      moved profile p$i to $userDir" -ForegroundColor DarkGray
    }
    if (-not (Test-Path -LiteralPath $userDir)) {
        # First run for this player: start from your normal Dolphin settings
        # (controller mapping, graphics) and your existing SMG save, as copies.
        New-Item -ItemType Directory -Force -Path "$userDir\Config" | Out-Null
        foreach ($f in @("Dolphin.ini", "GFX.ini", "WiimoteNew.ini", "GCPadNew.ini", "Hotkeys.ini")) {
            if (Test-Path -LiteralPath "$globalUser\Config\$f") {
                Copy-Item -LiteralPath "$globalUser\Config\$f" -Destination "$userDir\Config\$f"
            }
        }
        if (Test-Path -LiteralPath "$globalUser\$saveRel") {
            New-Item -ItemType Directory -Force -Path (Split-Path "$userDir\$saveRel") | Out-Null
            Copy-Item -LiteralPath "$globalUser\$saveRel" -Destination "$userDir\$saveRel" -Recurse
        }
        Write-Host "      created profile p$i ($userDir)" -ForegroundColor DarkGray
    }

    # Arcade (Triforce) hardware in GameCube slot SP1 makes Dolphin refuse to
    # boot any normal game ("Non-Triforce games cannot be booted..."). A Wii
    # game never uses that slot, so make sure it is empty (255 = nothing).
    New-Item -ItemType Directory -Force -Path "$userDir\Config" | Out-Null
    Set-IniValue "$userDir\Config\Dolphin.ini" "Core" "SerialPort1" "255"

    # No Wii Remote set up (file missing, or Wii Remote 1 switched off) means
    # the game stops at "Communications with the Wii Remote have been
    # interrupted". Give the player working keyboard + mouse controls; they can
    # switch to a gamepad in this Dolphin window under Controllers.
    $wiimoteIni = "$userDir\Config\WiimoteNew.ini"
    $needControls = -not (Test-Path -LiteralPath $wiimoteIni)
    if (-not $needControls) {
        $inFirst = $false
        foreach ($l in [System.IO.File]::ReadAllLines($wiimoteIni)) {
            $t = $l.Trim()
            if ($t.StartsWith("[")) { $inFirst = ($t -eq "[Wiimote1]") }
            elseif ($inFirst -and $t -match '^Source\s*=\s*0\s*$') { $needControls = $true }
        }
        if ($needControls) { Copy-Item -LiteralPath $wiimoteIni -Destination "$wiimoteIni.before-smg-online" -Force }
    }
    if ($needControls) {
        $controls = @(
            '[Wiimote1]', 'Device = DInput/0/Keyboard Mouse', 'Source = 1',
            'Buttons/A = `SPACE` | `Click 0`', 'Buttons/B = `Click 1`',
            'Buttons/1 = `1`', 'Buttons/2 = `2`', 'Buttons/- = `BACK`', 'Buttons/+ = `RETURN`', 'Buttons/Home = `HOME`',
            'D-Pad/Up = `UP`', 'D-Pad/Down = `DOWN`', 'D-Pad/Left = `LEFT`', 'D-Pad/Right = `RIGHT`',
            'IR/Up = `Cursor Y-`', 'IR/Down = `Cursor Y+`', 'IR/Left = `Cursor X-`', 'IR/Right = `Cursor X+`',
            'Shake/X = `E` | `Click 2`', 'Shake/Y = `E` | `Click 2`', 'Shake/Z = `E` | `Click 2`',
            'Extension = Nunchuk',
            'Nunchuk/Buttons/C = `C`', 'Nunchuk/Buttons/Z = `LSHIFT`',
            'Nunchuk/Stick/Up = `W`', 'Nunchuk/Stick/Down = `S`', 'Nunchuk/Stick/Left = `A`', 'Nunchuk/Stick/Right = `D`',
            '[Wiimote2]', 'Source = 0', '[Wiimote3]', 'Source = 0', '[Wiimote4]', 'Source = 0', '[BalanceBoard]', 'Source = 0'
        )
        [System.IO.File]::WriteAllLines($wiimoteIni, $controls, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "      no Wii Remote was set up for player $i - using keyboard + mouse:" -ForegroundColor Yellow
        Write-Host "        WASD move, SPACE/left click = A (jump), right click = B (star bits)," -ForegroundColor Yellow
        Write-Host "        E = spin, Left Shift = Z (crouch), C = camera, mouse = pointer, Enter = +" -ForegroundColor Yellow
        Write-Host "        For a gamepad: Controllers > Wii Remote 1 > Configure in THIS Dolphin window." -ForegroundColor Yellow
    }

    Write-Host "[2/3] Launching player $i..." -ForegroundColor Cyan
    if ($DryRun) { continue }
    Start-Process -FilePath $dolphinExe -ArgumentList "-u", "`"$userDir`"", "-e", "`"$preset`""
    if ($i -lt $Players) { Start-Sleep -Seconds 3 }   # stagger disc access
}

Write-Host ""
Write-Host "[3/3] Done. Players see each other when they are in the same galaxy and star." -ForegroundColor Green
if (-not $Server) {
    Write-Host "      Keep the server window open while playing." -ForegroundColor Green
    Write-Host "      Friends join with:  .\start-galaxy.ps1 -Players 1 -Server <your IP>:$Port" -ForegroundColor Green
}
