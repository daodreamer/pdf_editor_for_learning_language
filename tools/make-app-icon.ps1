# Renders the LinguaPDF app icon (pine rounded square with two flowing white lines,
# the same mark as the in-app logo) to PNGs and a multi-size Windows .ico.
# Uses only System.Drawing from Windows PowerShell; output is committed under assets/icons.
param([string]$OutDir = (Join-Path $PSScriptRoot '..\assets\icons'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$pine = [System.Drawing.Color]::FromArgb(255, 0x2c, 0x7a, 0x68)

function New-Rounded([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$pngs = @{}
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    # Small sizes keep less inset so the mark stays legible.
    $inset = if ($s -le 24) { 0 } else { [single]($s * 0.06) }
    $box = [single]($s - 2 * $inset)
    $body = New-Rounded $inset $inset $box $box ([single]($box * 0.26))
    $g.FillPath((New-Object System.Drawing.SolidBrush $pine), $body)
    # Two waves on a 24-unit grid, matching LineIcon "wave".
    $u = $box / 24.0
    $stroke = [single][Math]::Max(1.25, 2.0 * $u * 1.15)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), $stroke
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    foreach ($row in 9.5, 15.5) {
        $pt = { param($px, $py) New-Object System.Drawing.PointF ([single]($inset + $px * $u)), ([single]($inset + $py * $u)) }
        # Matches "M4 y c3-3 6-3 9 0 s6 3 9 0" from the in-app mark, slightly widened.
        $pts = [System.Drawing.PointF[]]@(
            (& $pt 4.5 $row), (& $pt 7.5 ($row - 3.2)), (& $pt 9.5 ($row - 3.2)), (& $pt 12 $row),
            (& $pt 14.5 ($row + 3.2)), (& $pt 16.5 ($row + 3.2)), (& $pt 19.5 $row))
        $g.DrawBeziers($pen, $pts)
    }
    $g.Dispose()
    $file = Join-Path $OutDir "linguapdf-$s.png"
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $pngs[$s] = [System.IO.File]::ReadAllBytes($file)
}

# ICO container with PNG-compressed entries (supported since Windows Vista).
$icoSizes = 16, 20, 24, 32, 40, 48, 64, 256
$ms = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ms
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$icoSizes.Count)
$offset = 6 + 16 * $icoSizes.Count
foreach ($s in $icoSizes) {
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$pngs[$s].Length); $w.Write([UInt32]$offset)
    $offset += $pngs[$s].Length
}
foreach ($s in $icoSizes) { $w.Write($pngs[$s]) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $OutDir 'linguapdf.ico'), $ms.ToArray())
# Only the sizes the app loads at runtime are kept as loose PNGs.
foreach ($s in $sizes) { if (@(16, 24, 32, 48, 64, 128, 256) -notcontains $s) { Remove-Item (Join-Path $OutDir "linguapdf-$s.png") } }
Write-Output "icons written to $OutDir"
