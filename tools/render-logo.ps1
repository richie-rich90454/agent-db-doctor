# Renders the assets from the logo geometry.
#
# The logo is drawn twice: once as SVG, which is what the README and any website
# use, and once here with the drawing library that ships with Windows, which is
# what produces the PNG sizes and the multi size Windows icon. The shapes and the
# numbers in this file are the same shapes and numbers as assets/logo.svg, so the
# two cannot drift apart silently; if the mark changes, both places change.
#
# Usage: powershell -File tools/render-logo.ps1
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\assets")
)
Add-Type -AssemblyName System.Drawing

$indigo = [Drawing.Color]::FromArgb(79, 70, 229)
$amber = [Drawing.Color]::FromArgb(245, 158, 11)

# Draws the mark at the given size. Below 96 pixels the simplified geometry is
# used, because the full mark's lines are thinner than a pixel there.
function New-MarkBitmap([int]$size, [bool]$simplified) {
    $bitmap = New-Object Drawing.Bitmap($size, $size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([Drawing.Color]::Transparent)
    $scale = $size / 512.0
    if ($simplified) {
        $ringPen = New-Object Drawing.Pen($indigo, (44 * $scale))
        $pulsePen = New-Object Drawing.Pen($amber, (40 * $scale))
        $ringCentres = @(172, 340)
        $ringWidth = 2 * 156
        $ringHeight = 2 * 56
        $pulsePoints = @(
            @(30, 256), @(140, 256), @(176, 190), @(212, 322), @(244, 256),
            @(330, 256), @(368, 196), @(400, 316), @(430, 256), @(482, 256)
        )
        $sparkPoints = @(@(466, 196), @(500, 256), @(466, 316), @(432, 256))
    } else {
        $ringPen = New-Object Drawing.Pen($indigo, (30 * $scale))
        $pulsePen = New-Object Drawing.Pen($amber, (26 * $scale))
        $ringCentres = @(150, 256, 362)
        $ringWidth = 2 * 150
        $ringHeight = 2 * 52
        $pulsePoints = @(
            @(40, 256), @(130, 256), @(158, 200), @(186, 312), @(214, 256),
            @(320, 256), @(344, 216), @(366, 296), @(386, 256), @(470, 256)
        )
        $sparkPoints = @(@(470, 206), @(494, 256), @(470, 306), @(446, 256))
    }
    foreach ($centreY in $ringCentres) {
        $graphics.DrawEllipse(
            $ringPen,
            [float]((256 - ($ringWidth / 2)) * $scale),
            [float](($centreY - ($ringHeight / 2)) * $scale),
            [float]($ringWidth * $scale),
            [float]($ringHeight * $scale)
        )
    }
    $pulsePen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    $pulsePen.StartCap = [Drawing.Drawing2D.LineCap]::Round
    $pulsePen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $points = New-Object 'System.Collections.Generic.List[Drawing.PointF]'
    foreach ($point in $pulsePoints) {
        $points.Add((New-Object Drawing.PointF([float]($point[0] * $scale), [float]($point[1] * $scale))))
    }
    $graphics.DrawLines($pulsePen, $points.ToArray())
    $sparkBrush = New-Object Drawing.SolidBrush($amber)
    $spark = New-Object 'System.Collections.Generic.List[Drawing.PointF]'
    foreach ($point in $sparkPoints) {
        $spark.Add((New-Object Drawing.PointF([float]($point[0] * $scale), [float]($point[1] * $scale))))
    }
    $graphics.FillPolygon($sparkBrush, $spark.ToArray())
    $sparkBrush.Dispose()
    $ringPen.Dispose()
    $pulsePen.Dispose()
    $graphics.Dispose()
    return $bitmap
}

# Writes one PNG at the given size.
function Save-MarkPng([int]$size, [string]$fileName, [bool]$simplified) {
    $bitmap = New-MarkBitmap $size $simplified
    $path = Join-Path $OutputDirectory $fileName
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Host "wrote $fileName"
}

# Writes a Windows icon holding PNG frames at several sizes, which is the format
# Windows has accepted since Vista.
function Save-MultiSizeIcon([int[]]$sizes, [string]$fileName) {
    $frames = @()
    foreach ($size in $sizes) {
        $bitmap = New-MarkBitmap $size ($size -le 64)
        $memoryStream = New-Object IO.MemoryStream
        $bitmap.Save($memoryStream, [Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        $frames += , @($size, $memoryStream.ToArray())
    }
    $path = Join-Path $OutputDirectory $fileName
    $fileStream = [IO.File]::Create($path)
    $writer = New-Object IO.BinaryWriter($fileStream)
    # ICONDIR: reserved, type 1 (icon), then the number of frames.
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$frames.Count)
    # The frames start after the six byte header and one sixteen byte entry each.
    $dataOffset = 6 + (16 * $frames.Count)
    foreach ($frame in $frames) {
        $size = $frame[0]
        $data = $frame[1]
        # A size of 256 is written as zero, which is the format's convention.
        $writer.Write([byte]$(if ($size -ge 256) { 0 } else { $size }))
        $writer.Write([byte]$(if ($size -ge 256) { 0 } else { $size }))
        $writer.Write([byte]0)   # palette colours
        $writer.Write([byte]0)   # reserved
        $writer.Write([UInt16]1) # colour planes
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$data.Length)
        $writer.Write([UInt32]$dataOffset)
        $dataOffset += $data.Length
    }
    foreach ($frame in $frames) {
        $writer.Write($frame[1])
    }
    $writer.Dispose()
    $fileStream.Dispose()
    Write-Host "wrote $fileName"
}

Save-MarkPng 512 "logo-512.png" $false
foreach ($size in 16, 32, 48, 64, 180, 192, 512) {
    Save-MarkPng $size "favicon-$size.png" ($size -le 64)
}
Save-MultiSizeIcon @(16, 32, 48, 256) "app.ico"
