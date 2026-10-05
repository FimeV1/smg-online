# SMG Online - launcher window.
# Pick your colour, then join a friend's server or host one. Everything you set
# here is remembered. It drives start-galaxy.ps1, which does the actual work.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$base = $PSScriptRoot
$settingsFile = "$env:APPDATA\SMG-Online\settings.txt"
$serverIni = "$base\server\server-settings.ini"

# ---- remembered values ------------------------------------------------
$saved = @{}
foreach ($file in @($settingsFile, "$base\launcher-paths.txt")) {
    if (-not (Test-Path -LiteralPath $file)) { continue }
    foreach ($line in Get-Content -LiteralPath $file) {
        $k, $v = $line -split "=", 2
        if (-not $v) { continue }
        $k = $k.Trim().TrimStart([char]0xFEFF)
        if (-not $saved.ContainsKey($k)) { $saved[$k] = $v.Trim() }
    }
}

function Get-IniValue([string]$key, [string]$default) {
    if (Test-Path -LiteralPath $serverIni) {
        $m = Select-String -LiteralPath $serverIni -Pattern "^\s*$key\s*=\s*(.*)$" | Select-Object -First 1
        if ($m) { return $m.Matches[0].Groups[1].Value.Trim() }
    }
    return $default
}

function Set-IniValues([hashtable]$values) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (Test-Path -LiteralPath $serverIni) { foreach ($l in [System.IO.File]::ReadAllLines($serverIni)) { $lines.Add($l) } }
    if (-not ($lines | Where-Object { $_.Trim() -eq "[server]" })) { $lines.Insert(0, "[server]") }
    foreach ($key in $values.Keys) {
        $done = $false
        for ($n = 0; $n -lt $lines.Count; $n++) {
            if ($lines[$n] -match "^\s*$key\s*=") { $lines[$n] = "$key = $($values[$key])"; $done = $true; break }
        }
        if (-not $done) { $lines.Add("$key = $($values[$key])") }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $serverIni) | Out-Null
    [System.IO.File]::WriteAllLines($serverIni, $lines, (New-Object System.Text.UTF8Encoding($false)))
}

# Must match the colour table in the mod (playerColors.cpp): shirt + cap, overalls
$colors = @(
    @{ Name = "Mario (normal)";    Shirt = @(225, 30, 30);   Overalls = @(40, 70, 215) },
    @{ Name = "Green";             Shirt = @(40, 200, 70);   Overalls = @(40, 70, 215) },
    @{ Name = "Yellow and purple"; Shirt = @(255, 215, 0);   Overalls = @(140, 60, 200) },
    @{ Name = "Purple and black";  Shirt = @(150, 70, 225);  Overalls = @(45, 45, 60) },
    @{ Name = "Blue and red";      Shirt = @(60, 130, 255);  Overalls = @(215, 40, 40) },
    @{ Name = "White and red";     Shirt = @(240, 240, 240); Overalls = @(215, 40, 40) },
    @{ Name = "Orange and teal";   Shirt = @(255, 135, 25);  Overalls = @(0, 150, 150) },
    @{ Name = "Pink and white";    Shirt = @(255, 115, 185); Overalls = @(225, 225, 240) }
)

# ---- window -----------------------------------------------------------
$font = New-Object System.Drawing.Font("Segoe UI", 9.5)
$form = New-Object System.Windows.Forms.Form
$form.Text = "Super Mario Galaxy Online"
$form.Font = $font
$form.ClientSize = New-Object System.Drawing.Size(520, 600)
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.StartPosition = "CenterScreen"

function Add-Control($parent, $type, [int]$x, [int]$y, [int]$w, [int]$h, [string]$text) {
    $c = New-Object "System.Windows.Forms.$type"
    $c.Location = New-Object System.Drawing.Point($x, $y)
    $c.Size = New-Object System.Drawing.Size($w, $h)
    if ($text) { $c.Text = $text }
    $parent.Controls.Add($c)
    return $c
}

$header = Add-Control $form "Label" 16 10 488 30 "Super Mario Galaxy Online"
$header.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)

# -- player
$gPlayer = Add-Control $form "GroupBox" 12 46 496 96 "You"
[void](Add-Control $gPlayer "Label" 14 28 60 22 "Colour")
$cbColor = Add-Control $gPlayer "ComboBox" 76 25 190 26 ""
$cbColor.DropDownStyle = "DropDownList"
foreach ($c in $colors) { [void]$cbColor.Items.Add($c.Name) }
$swShirt = Add-Control $gPlayer "Panel" 280 25 44 24 ""
$swOveralls = Add-Control $gPlayer "Panel" 326 25 44 24 ""
$swShirt.BorderStyle = "FixedSingle"; $swOveralls.BorderStyle = "FixedSingle"
[void](Add-Control $gPlayer "Label" 378 28 110 22 "shirt / overalls")
$chkTitle = Add-Control $gPlayer "CheckBox" 16 60 300 24 "Show ONLINE on the title screen"
$cbColor.Add_SelectedIndexChanged({
    $c = $colors[$cbColor.SelectedIndex]
    $swShirt.BackColor = [System.Drawing.Color]::FromArgb($c.Shirt[0], $c.Shirt[1], $c.Shirt[2])
    $swOveralls.BackColor = [System.Drawing.Color]::FromArgb($c.Overalls[0], $c.Overalls[1], $c.Overalls[2])
})
$idx = 0
if ($saved["color"] -match '^[0-7]$') { $idx = [int]$saved["color"] }
$cbColor.SelectedIndex = $idx
$chkTitle.Checked = ($saved["title"] -ne "off")

# -- join
$gJoin = Add-Control $form "GroupBox" 12 150 496 76 "Join a friend's game"
[void](Add-Control $gJoin "Label" 14 32 60 22 "Address")
$tbServer = Add-Control $gJoin "TextBox" 76 29 270 26 $saved["server"]
$btnJoin = Add-Control $gJoin "Button" 360 26 122 32 "Join"
[void](Add-Control $gJoin "Label" 76 54 300 18 "")

# -- host
$gHost = Add-Control $form "GroupBox" 12 234 496 176 "Host a game on this PC"
[void](Add-Control $gHost "Label" 14 30 60 22 "Port")
$numPort = Add-Control $gHost "NumericUpDown" 76 27 90 26 ""
$numPort.Minimum = 1; $numPort.Maximum = 65535; $numPort.Value = [int](Get-IniValue "port" "5029")
[void](Add-Control $gHost "Label" 186 30 90 22 "Max players")
$numMax = Add-Control $gHost "NumericUpDown" 276 27 70 26 ""
$numMax.Minimum = 1; $numMax.Maximum = 250; $numMax.Value = [int](Get-IniValue "max_players" "64")
[void](Add-Control $gHost "Label" 14 64 60 22 "World")
$tbWorld = Add-Control $gHost "TextBox" 76 61 150 26 (Get-IniValue "world" "default")
[void](Add-Control $gHost "Label" 236 64 250 22 "(each name keeps its own progress)")
$chkProgress = Add-Control $gHost "CheckBox" 16 94 220 24 "Share stars and progress"
$chkProgress.Checked = ((Get-IniValue "share_progress" "yes") -match '^(yes|true|1|on)$')
$chkStarBits = Add-Control $gHost "CheckBox" 250 94 220 24 "Share star bits"
$chkStarBits.Checked = ((Get-IniValue "share_star_bits" "yes") -match '^(yes|true|1|on)$')
[void](Add-Control $gHost "Label" 14 132 140 22 "Players on this PC")
$numLocal = Add-Control $gHost "NumericUpDown" 156 129 50 26 ""
$numLocal.Minimum = 1; $numLocal.Maximum = 4; $numLocal.Value = 1
$btnHost = Add-Control $gHost "Button" 360 124 122 32 "Host"
$hostNote = Add-Control $gHost "Label" 220 132 136 22 "needs Python 3"
$hostNote.ForeColor = [System.Drawing.Color]::Gray

# -- files
$gFiles = Add-Control $form "GroupBox" 12 418 496 104 "Files (asked once, then remembered)"
[void](Add-Control $gFiles "Label" 14 30 60 22 "Dolphin")
$tbDolphin = Add-Control $gFiles "TextBox" 76 27 322 26 $saved["dolphin"]
$btnDolphin = Add-Control $gFiles "Button" 404 25 78 28 "Browse"
[void](Add-Control $gFiles "Label" 14 66 60 22 "Game")
$tbGame = Add-Control $gFiles "TextBox" 76 63 322 26 $saved["game"]
$btnGame = Add-Control $gFiles "Button" 404 61 78 28 "Browse"

$status = Add-Control $form "Label" 16 532 488 56 "Pick a colour, then Join or Host. You see other players when you are in the same galaxy and star."
$status.ForeColor = [System.Drawing.Color]::DimGray

function Browse([string]$title, [string]$filter, $box) {
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = $title
    $dlg.Filter = $filter
    if ($box.Text -and (Test-Path -LiteralPath $box.Text)) { $dlg.InitialDirectory = Split-Path $box.Text }
    if ($dlg.ShowDialog() -eq "OK") { $box.Text = $dlg.FileName }
}
$btnDolphin.Add_Click({ Browse "Where is Dolphin.exe?" "Dolphin|Dolphin.exe|Programs|*.exe" $tbDolphin })
$btnGame.Add_Click({ Browse "Your Super Mario Galaxy (USA) game file" "Wii games|*.wbfs;*.iso;*.rvz;*.wia;*.gcz|All files|*.*" $tbGame })

function Fail([string]$message) {
    $status.ForeColor = [System.Drawing.Color]::Firebrick
    $status.Text = $message
    return $false
}

function Check-Files {
    if (-not $tbDolphin.Text -or -not (Test-Path -LiteralPath $tbDolphin.Text -PathType Leaf)) { return (Fail "Choose where Dolphin.exe is first (Browse).") }
    if (-not $tbGame.Text -or -not (Test-Path -LiteralPath $tbGame.Text -PathType Leaf)) { return (Fail "Choose your Super Mario Galaxy (USA) game file first (Browse).") }
    return $true
}

# Runs start-galaxy.ps1 in its own console; the console stays open if it fails.
function Start-Game([string[]]$extra) {
    $title = "off"
    if ($chkTitle.Checked) { $title = "on" }
    $psArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$base\start-galaxy.ps1`"",
        "-Color", $cbColor.SelectedIndex, "-Title", $title,
        "-Dolphin", "`"$($tbDolphin.Text)`"", "-Game", "`"$($tbGame.Text)`"") + $extra
    $cmd = "powershell " + ($psArgs -join " ") + " || pause"
    Start-Process -FilePath "cmd.exe" -ArgumentList "/c `"$cmd`"" -WorkingDirectory $base
    $status.ForeColor = [System.Drawing.Color]::SeaGreen
    $status.Text = "Starting... Dolphin opens in a moment. You can close this window."
}

$btnJoin.Add_Click({
    $address = $tbServer.Text.Trim()
    if ($address -notmatch '^\d{1,3}(\.\d{1,3}){3}(:\d{1,5})?$') { [void](Fail "Type the host's address, like 203.0.113.7 or 203.0.113.7:1027."); return }
    if (-not (Check-Files)) { return }
    Start-Game @("-Players", "1", "-Server", $address)
})

$btnHost.Add_Click({
    if (-not (Check-Files)) { return }
    if (-not (Get-Command py -ErrorAction SilentlyContinue) -and -not (Get-Command python -ErrorAction SilentlyContinue)) {
        [void](Fail "Hosting needs Python 3. Install it from python.org (tick 'Add python.exe to PATH'), then try again."); return
    }
    $world = ($tbWorld.Text.Trim() -replace '[^A-Za-z0-9_-]', '_')
    if (-not $world) { $world = "default" }
    $yn = @{ $true = "yes"; $false = "no" }
    Set-IniValues @{
        port = [int]$numPort.Value; max_players = [int]$numMax.Value; world = $world
        share_progress = $yn[$chkProgress.Checked]; share_star_bits = $yn[$chkStarBits.Checked]
    }
    Start-Game @("-Players", [int]$numLocal.Value)
    $status.Text = "Starting the server and your game. Friends join with your public IP and port $([int]$numPort.Value) (forward that UDP port on your router). A server that is already running keeps its old settings until you close its window."
})

[void]$form.ShowDialog()
