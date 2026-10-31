$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$output = Join-Path $PSScriptRoot 'src\pulse.ico'
$stream = [IO.File]::Create($output)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $frames = @()
    foreach ($size in @(32,256)) {
        $bitmap = [Drawing.Bitmap]::new($size,$size)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.ScaleTransform($size/256.0,$size/256.0)
        $path = [Drawing.Drawing2D.GraphicsPath]::new()
        $path.AddArc(8,8,96,96,180,90)
        $path.AddArc(152,8,96,96,270,90)
        $path.AddArc(152,152,96,96,0,90)
        $path.AddArc(8,152,96,96,90,90)
        $path.CloseFigure()
        $fill = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(165,245,122))
        $graphics.FillPath($fill,$path)
        $pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(23,49,16),15)
        $pen.StartCap = $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        $points = [Drawing.PointF[]]@([Drawing.PointF]::new(55,132),[Drawing.PointF]::new(86,132),[Drawing.PointF]::new(113,82),[Drawing.PointF]::new(146,176),[Drawing.PointF]::new(174,123),[Drawing.PointF]::new(199,123))
        $graphics.DrawLines($pen,$points)
        $png = [IO.MemoryStream]::new()
        $bitmap.Save($png,[Drawing.Imaging.ImageFormat]::Png)
        $frames += ,@($size,$png.ToArray())
        $png.Dispose();$pen.Dispose();$fill.Dispose();$path.Dispose();$graphics.Dispose();$bitmap.Dispose()
    }
    $writer.Write([uint16]0);$writer.Write([uint16]1);$writer.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($frame in $frames) {
        $dimension = if ($frame[0] -eq 256) {0} else {$frame[0]}
        $writer.Write([byte]$dimension);$writer.Write([byte]$dimension);$writer.Write([uint16]0)
        $writer.Write([uint16]1);$writer.Write([uint16]32)
        $writer.Write([uint32]$frame[1].Length);$writer.Write([uint32]$offset)
        $offset += $frame[1].Length
    }
    foreach ($frame in $frames) { $writer.Write([byte[]]$frame[1]) }
} finally { $writer.Dispose();$stream.Dispose() }
