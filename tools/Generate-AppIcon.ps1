param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\src\KotohaIME.App\Assets')
)

Add-Type -AssemblyName System.Drawing

$size = 256
$bitmap = [System.Drawing.Bitmap]::new($size, $size)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

try {
    $bounds = [System.Drawing.RectangleF]::new(10, 10, 236, 236)
    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $bounds,
        [System.Drawing.Color]::FromArgb(18, 72, 68),
        [System.Drawing.Color]::FromArgb(36, 122, 89),
        42
    )
    $accent = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(232, 116, 76))
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()

    try {
        $radius = 54
        $diameter = $radius * 2
        $path.AddArc(10, 10, $diameter, $diameter, 180, 90)
        $path.AddArc(246 - $diameter, 10, $diameter, $diameter, 270, 90)
        $path.AddArc(246 - $diameter, 246 - $diameter, $diameter, $diameter, 0, 90)
        $path.AddArc(10, 246 - $diameter, $diameter, $diameter, 90, 90)
        $path.CloseFigure()
        $graphics.FillPath($background, $path)

        $graphics.FillEllipse($accent, 176, 28, 48, 48)
        $graphics.FillRectangle($white, 185, 92, 13, 104)
        $graphics.FillRectangle($white, 169, 92, 45, 10)
        $graphics.FillRectangle($white, 169, 186, 45, 10)

        $font = [System.Drawing.Font]::new('Segoe UI', 116, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        try {
            $graphics.DrawString('A', $font, $white, 35, 65)
        }
        finally {
            $font.Dispose()
        }

        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        $pngPath = Join-Path $OutputDirectory 'AstelioIME.png'
        $icoPath = Join-Path $OutputDirectory 'AstelioIME.ico'
        $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)

        $pngBytes = [System.IO.File]::ReadAllBytes($pngPath)
        $stream = [System.IO.File]::Create($icoPath)
        $writer = [System.IO.BinaryWriter]::new($stream)
        try {
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]1)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$pngBytes.Length)
            $writer.Write([uint32]22)
            $writer.Write($pngBytes)
        }
        finally {
            $writer.Dispose()
            $stream.Dispose()
        }
    }
    finally {
        $path.Dispose()
        $background.Dispose()
        $accent.Dispose()
        $white.Dispose()
    }
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}