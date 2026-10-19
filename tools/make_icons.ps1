# Generates icon options into build\icons: one .ico per design (16/24/32/48,
# 32-bit BMP entries with alpha, which XP reads) plus options.png, a preview sheet.
#
# Retro pixel art, drawn once at 16x16 and scaled up with nearest neighbour
# (2x = 32, 3x = 48), so every size keeps the same chunky pixels. Each design
# is 16 rows of 16 palette letters; '.' is transparent.
#
# The app uses 3-chip: copy build\icons\option-3-chip.ico over src\app.ico.
#
#   powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1
param([string]$OutDir = (Join-Path $PSScriptRoot '..\build\icons'))
New-Item -ItemType Directory -Force $OutDir | Out-Null
Add-Type -AssemblyName System.Drawing

function C([string]$hex) { [System.Drawing.Color]::FromArgb(255, [Convert]::ToInt32($hex.Substring(0, 2), 16), [Convert]::ToInt32($hex.Substring(2, 2), 16), [Convert]::ToInt32($hex.Substring(4, 2), 16)) }

$designs = [ordered]@{
    # Beige CRT with a green prompt and power LED.
    '1-crt' = @{
        palette = @{ K = '000000'; L = 'd8d0bc'; D = '948a72'; S = '0b1f10'; G = '39ff6a'; q = '1c7a36' }
        rows = @(
            '................',
            '.KKKKKKKKKKKKKK.',
            '.KLLLLLLLLLLLDK.',
            '.KLSSSSSSSSSSDK.',
            '.KLSGSSSSSSSSDK.',
            '.KLSSGSSSSSSSDK.',
            '.KLSGSGGSSSSSDK.',
            '.KLSSSSSSSSSSDK.',
            '.KLSqqqqqqSSSDK.',
            '.KLSSSSSSSSSSDK.',
            '.KLLLLLLLLLLGDK.',
            '.KDDDDDDDDDDDDK.',
            '.KKKKKKKKKKKKKK.',
            '......KLLK......',
            '....KLLLLLLK....',
            '....KKKKKKKK....')
    }
    # 3.5" floppy: backups.
    '2-floppy' = @{
        palette = @{ K = '000000'; N = '233f9e'; M = 'cfcfcf'; o = '6e6e6e'; W = 'f4f4f4'; D = '9a9a9a'; R = 'd83a3a' }
        rows = @(
            'KKKKKKKKKKKKKK..',
            'KNNNMMMMMMMNNNK.',
            'KNNNMooMMMMNNNNK',
            'KNNNMooMMMMNNNNK',
            'KNNNMooMMMMNNNNK',
            'KNNNMMMMMMMNNNNK',
            'KNNNNNNNNNNNNNNK',
            'KNNWWWWWWWWWWNNK',
            'KNNWDDDDDDDDWNNK',
            'KNNWWWWWWWWWWNNK',
            'KNNWRRRRRWWWWNNK',
            'KNNWWWWWWWWWWNNK',
            'KNNWDDDDDDWWWNNK',
            'KNNWWWWWWWWWWNNK',
            'KNNNNNNNNNNNNNNK',
            'KKKKKKKKKKKKKKKK')
    }
    # DIP chip: the ECU being coded.
    '3-chip' = @{
        palette = @{ K = '000000'; B = '2e2e2e'; P = 'd0d0d0'; W = 'f4f4f4'; A = '4c8dff'; L = '9a9a9a' }
        rows = @(
            '................',
            '...KKKKKKKKKK...',
            '.PPKBBBKKBBBKPP.',
            '...KBBBBBBBBK...',
            '.PPKBWBBBBBBKPP.',
            '...KBBBBBBBBK...',
            '.PPKBBBBBBBBKPP.',
            '...KBAAAAAABK...',
            '.PPKBBBBBBBBKPP.',
            '...KBLLLLBBBK...',
            '.PPKBBBBBBBBKPP.',
            '...KBBBBBBBBK...',
            '.PPKBBBBBBBBKPP.',
            '...KBBBBBBBBK...',
            '...KKKKKKKKKK...',
            '................')
    }
    # Windows 95 window with two columns and one changed row: compare.
    '4-win95' = @{
        palette = @{ K = '000000'; W = 'ffffff'; L = 'c0c0c0'; D = '808080'; T = '000080'; S = 'ffffff'; X = '808080'; A = '000080' }
        rows = @(
            'WWWWWWWWWWWWWWWK',
            'WLLLLLLLLLLLLLDK',
            'WLTTTTTTTTTTTTDK',
            'WLTWWWWTTTTTTTDK',
            'WLLLLLLLLLLLLLDK',
            'WLDDDDDDDDDDDDDK',
            'WLDSSSSSDSSSSSDK',
            'WLDSXXXSDSXXXSDK',
            'WLDSSSSSDSSSSSDK',
            'WLDSXXXSDAAAAADK',
            'WLDSSSSSDSSSSSDK',
            'WLDSXXXSDSXXXSDK',
            'WLDSSSSSDSSSSSDK',
            'WLDDDDDDDDDDDDDK',
            'WDDDDDDDDDDDDDDK',
            'KKKKKKKKKKKKKKKK')
    }
    # Amber terminal spelling NCS, with scanlines.
    '5-amber' = @{
        palette = @{ B = '2a1c00'; K = '000000'; A = 'ffb000' }
        scanlines = $true
        rows = @(
            'BBBBBBBBBBBBBBBB',
            'BKKKKKKKKKKKKKKB',
            'BKKKKKKKKKKKKKKB',
            'BKKKKKKKKKKKKKKB',
            'BAKKAKKAAAKKAAAB',
            'BAAKAKAKKKKAKKKB',
            'BAKAAKAKKKKKAAKB',
            'BAKKAKAKKKKKKKAB',
            'BAKKAKKAAAKAAAKB',
            'BKKKKKKKKKKKKKKB',
            'BKKKKKKKKKKKKKKB',
            'BAAAKKKKKKKKKKKB',
            'BAAAKKKKKKKKKKKB',
            'BKKKKKKKKKKKKKKB',
            'BKKKKKKKKKKKKKKB',
            'BBBBBBBBBBBBBBBB')
    }
    # Car key fob with a blue button.
    '6-keyfob' = @{
        palette = @{ L = 'b8b8b8'; K = '000000'; B = '262626'; h = '4a4a4a'; D = '6e6e6e'; A = '4c8dff' }
        rows = @(
            '......LLLL......',
            '.....L....L.....',
            '......LLLL......',
            '.....KKKKKK.....',
            '....KhBBBBBK....',
            '....KhBBBBBK....',
            '....KhDDDDBK....',
            '....KhBBBBBK....',
            '....KhDDDDBK....',
            '....KhBBBBBK....',
            '....KhAAAABK....',
            '....KhBBBBBK....',
            '....KhBBBBBK....',
            '....KhBBBBBK....',
            '.....KKKKKK.....',
            '................')
    }
}

# Palette letters are case-insensitive keys (PowerShell hashtables): don't reuse a letter in another case.
# 16x16 base bitmap; scanline designs darken the accent on every other row.
function Base($d) {
    $bmp = New-Object System.Drawing.Bitmap 16, 16, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt 16; $y++) {
        $row = $d.rows[$y]
        if ($row.Length -ne 16) { throw "row $y of a design is $($row.Length) wide" }
        for ($x = 0; $x -lt 16; $x++) {
            $ch = [string]$row[$x]
            if ($ch -eq '.') { $bmp.SetPixel($x, $y, [System.Drawing.Color]::Transparent); continue }
            $c = C $d.palette[$ch]
            if ($d.scanlines -and ($y % 2 -eq 1) -and $ch -eq 'A') { $c = [System.Drawing.Color]::FromArgb(255, [int]($c.R * 0.72), [int]($c.G * 0.72), [int]($c.B * 0.72)) }
            $bmp.SetPixel($x, $y, $c)
        }
    }
    return $bmp
}

function Scale($base, [int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) { $bmp.SetPixel($x, $y, $base.GetPixel([Math]::Floor($x * 16 / $size), [Math]::Floor($y * 16 / $size))) }
    }
    return $bmp
}

function IconEntry($bmp) {
    $s = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $maskStride = [int]([Math]::Ceiling($s / 32.0) * 4)
    $bw.Write([int]40); $bw.Write([int]$s); $bw.Write([int]($s * 2)); $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]0); $bw.Write([int]($s * $s * 4 + $maskStride * $s)); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) { $c = $bmp.GetPixel($x, $y); $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A) }
    }
    for ($y = $s - 1; $y -ge 0; $y--) {  # AND mask: 1 = transparent
        $row = New-Object byte[] $maskStride
        for ($x = 0; $x -lt $s; $x++) { if ($bmp.GetPixel($x, $y).A -eq 0) { $row[[Math]::Floor($x / 8)] = $row[[Math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8)) } }
        $bw.Write($row)
    }
    $bw.Flush()
    return , $ms.ToArray()
}

function WriteIco($base, $path) {
    $entries = @()
    foreach ($s in 16, 24, 32, 48) { $b = Scale $base $s; $entries += , @($s, (IconEntry $b)); $b.Dispose() }
    $fs = [System.IO.File]::Create($path)
    $w = New-Object System.IO.BinaryWriter $fs
    $w.Write([int16]0); $w.Write([int16]1); $w.Write([int16]$entries.Count)
    $offset = 6 + 16 * $entries.Count
    foreach ($e in $entries) {
        $w.Write([byte]$e[0]); $w.Write([byte]$e[0]); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([int16]1); $w.Write([int16]32); $w.Write([int]$e[1].Length); $w.Write([int]$offset)
        $offset += $e[1].Length
    }
    foreach ($e in $entries) { $w.Write($e[1]) }
    $w.Close()
}

Get-ChildItem $OutDir -Filter 'option-*.ico' | Remove-Item
$bases = [ordered]@{}
foreach ($name in $designs.Keys) {
    $bases[$name] = Base $designs[$name]
    WriteIco $bases[$name] (Join-Path $OutDir "option-$name.ico")
}

# README logo: the app's icon (3-chip) at 12x, transparent background.
$logo = Scale $bases['3-chip'] 192
$logo.Save((Join-Path $PSScriptRoot '..\docs\logo.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$logo.Dispose()

# Preview sheet: 128 px (8x), then 48/32/16 at real size on dark and light strips.
$colW = 200; $sheet = New-Object System.Drawing.Bitmap ($colW * $designs.Count), 360
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear((C '111111'))
$light = New-Object System.Drawing.SolidBrush (C 'ececec')
$font = New-Object System.Drawing.Font 'Consolas', 13
$i = 0
foreach ($name in $bases.Keys) {
    $x0 = $i * $colW
    $g.DrawString($name.ToUpper(), $font, [System.Drawing.Brushes]::Gray, $x0 + 16, 10)
    $big = Scale $bases[$name] 128; $g.DrawImageUnscaled($big, $x0 + 36, 40); $big.Dispose()
    $g.FillRectangle($light, $x0, 260, $colW, 100)
    $x = $x0 + 24
    foreach ($s in 48, 32, 16) {
        $b = Scale $bases[$name] $s
        $g.DrawImageUnscaled($b, $x, 190 + (48 - $s)); $g.DrawImageUnscaled($b, $x, 280 + (48 - $s))
        $x += $s + 20; $b.Dispose()
    }
    $i++
}
$g.Dispose()
$sheet.Save((Join-Path $OutDir 'options.png'))
$sheet.Dispose()
"wrote $($designs.Count) icons and options.png to $OutDir"
