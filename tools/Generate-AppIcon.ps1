<#
.SYNOPSIS
    Draws the Astelio IME icon: an atom (three orbits, electrons and a nucleus) on a gradient tile.
.DESCRIPTION
    Writes AstelioIME.ico (16 to 256 px), AstelioIME.png (256 px) and AstelioIME-512.png (store listings) to
    -OutputDirectory. Small sizes get thicker lines so the orbits still read at 16 px.
.EXAMPLE
    ./tools/Generate-AppIcon.ps1
#>
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\icon')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-Color([string]$Hex, [int]$Alpha = 255) {
    $value = [Convert]::ToInt32($Hex.TrimStart('#'), 16)
    return [System.Drawing.Color]::FromArgb($Alpha, ($value -shr 16) -band 255, ($value -shr 8) -band 255, $value -band 255)
}

function New-RoundedRectangle([float]$X, [float]$Y, [float]$Width, [float]$Height, [float]$Radius) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

# Draws the icon on a 256-unit canvas scaled to $Size pixels.
function New-IconBitmap([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.ScaleTransform($Size / 256.0, $Size / 256.0)
    $small = $Size -le 32
    $disposables = [System.Collections.Generic.List[System.IDisposable]]::new()
    try {
        # Tile: indigo -> violet -> cyan, corner to corner.
        $inset = if ($small) { 4 } else { 12 }
        $tile = New-RoundedRectangle $inset $inset (256 - 2 * $inset) (256 - 2 * $inset) ($(if ($small) { 44 } else { 56 }))
        $disposables.Add($tile)
        $gradient = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            [System.Drawing.PointF]::new(0, 0), [System.Drawing.PointF]::new(256, 256),
            (New-Color '#1A1B5E'), (New-Color '#12C8E6'))
        $blend = [System.Drawing.Drawing2D.ColorBlend]::new(3)
        $blend.Colors = @((New-Color '#1A1B5E'), (New-Color '#6B3FE6'), (New-Color '#12C8E6'))
        $blend.Positions = @(0.0, 0.52, 1.0)
        $gradient.InterpolationColors = $blend
        $disposables.Add($gradient)
        $graphics.FillPath($gradient, $tile)

        # A soft light behind the nucleus.
        $glowPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
        $glowPath.AddEllipse(38, 38, 180, 180)
        $disposables.Add($glowPath)
        $glow = [System.Drawing.Drawing2D.PathGradientBrush]::new($glowPath)
        $glow.CenterColor = New-Color '#FFFFFF' 90
        $glow.SurroundColors = @((New-Color '#FFFFFF' 0))
        $disposables.Add($glow)
        $graphics.FillPath($glow, $glowPath)

        # Three orbits at 0, 60 and 120 degrees.
        $orbitWidth = if ($small) { 16 } else { 7 }
        $orbitPen = [System.Drawing.Pen]::new((New-Color '#FFFFFF' 235), $orbitWidth)
        $disposables.Add($orbitPen)
        $electron = [System.Drawing.SolidBrush]::new((New-Color '#E9FDFF'))
        $disposables.Add($electron)
        $electronRadius = if ($small) { 0 } else { 11 }
        $rx = 98.0
        $ry = 36.0
        foreach ($orbit in @(@{ Angle = 0; At = 200 }, @{ Angle = 60; At = 20 }, @{ Angle = 120; At = 110 })) {
            $state = $graphics.Save()
            $graphics.TranslateTransform(128, 128)
            $graphics.RotateTransform($orbit.Angle)
            $graphics.DrawEllipse($orbitPen, -$rx, -$ry, 2 * $rx, 2 * $ry)
            if ($electronRadius -gt 0) {
                $t = $orbit.At * [Math]::PI / 180
                $ex = $rx * [Math]::Cos($t)
                $ey = $ry * [Math]::Sin($t)
                $graphics.FillEllipse($electron, $ex - $electronRadius, $ey - $electronRadius, 2 * $electronRadius, 2 * $electronRadius)
            }
            $graphics.Restore($state)
        }

        # Nucleus: protons (warm) and neutrons (cool) packed together; one warm sphere at small sizes.
        $spheres = if ($small) {
            @(@{ X = 0; Y = 0; R = 34; Light = '#FFF1C9'; Dark = '#FF6A5C' })
        } else {
            @(
                @{ X = 11; Y = 9; R = 17; Light = '#D9F7FF'; Dark = '#2E8BD8' },
                @{ X = -12; Y = 8; R = 17; Light = '#FFE3C2'; Dark = '#FF5F57' },
                @{ X = 10; Y = -11; R = 17; Light = '#FFE3C2'; Dark = '#FF5F57' },
                @{ X = -9; Y = -10; R = 17; Light = '#D9F7FF'; Dark = '#2E8BD8' },
                @{ X = 0; Y = 0; R = 16; Light = '#FFF1C9'; Dark = '#FF7A45' }
            )
        }
        foreach ($sphere in $spheres) {
            $cx = 128 + $sphere.X
            $cy = 128 + $sphere.Y
            $r = $sphere.R
            $spherePath = [System.Drawing.Drawing2D.GraphicsPath]::new()
            $spherePath.AddEllipse($cx - $r, $cy - $r, 2 * $r, 2 * $r)
            $disposables.Add($spherePath)
            $shade = [System.Drawing.Drawing2D.PathGradientBrush]::new($spherePath)
            $shade.CenterPoint = [System.Drawing.PointF]::new($cx - $r * 0.35, $cy - $r * 0.35)
            $shade.CenterColor = New-Color $sphere.Light
            $shade.SurroundColors = @((New-Color $sphere.Dark))
            $disposables.Add($shade)
            $graphics.FillPath($shade, $spherePath)
        }
    }
    finally {
        foreach ($item in $disposables) {
            $item.Dispose()
        }
        $graphics.Dispose()
    }
    return $bitmap
}

function Get-PngBytes([System.Drawing.Bitmap]$Bitmap) {
    $stream = [System.IO.MemoryStream]::new()
    try {
        $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return , $stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$sizes = @(16, 20, 24, 32, 40, 48, 64, 256)
$images = foreach ($size in $sizes) {
    $bitmap = New-IconBitmap $size
    try {
        [pscustomobject]@{ Size = $size; Bytes = (Get-PngBytes $bitmap) }
    }
    finally {
        $bitmap.Dispose()
    }
}

# ICO: a directory of PNG images (supported since Windows Vista).
$icoPath = Join-Path $OutputDirectory 'AstelioIME.ico'
$stream = [System.IO.File]::Create($icoPath)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($image in $images) {
        $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$image.Bytes.Length)
        $writer.Write([uint32]$offset)
        $offset += $image.Bytes.Length
    }
    foreach ($image in $images) {
        $writer.Write($image.Bytes)
    }
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

foreach ($size in 256, 512) {
    $bitmap = New-IconBitmap $size
    try {
        $name = if ($size -eq 256) { 'AstelioIME.png' } else { "AstelioIME-$size.png" }
        $bitmap.Save((Join-Path $OutputDirectory $name), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
}
Write-Host "Wrote $icoPath and the PNG files"
