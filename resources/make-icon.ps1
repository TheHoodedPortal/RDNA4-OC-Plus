$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngImages = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.Clear([System.Drawing.Color]::FromArgb(230, 0, 58))

    # Taskbar uses the 16px frame: give the lettering enough height to remain
    # legible there while retaining the in-app badge's font, color, and centering.
    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $format.FormatFlags = [System.Drawing.StringFormatFlags]::NoWrap
    $fontFamily = [System.Drawing.FontFamily]::new('Segoe UI')
    $ocPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $plusPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $emSize = [single]($size * 0.58)
    $ocPath.AddString('OC', $fontFamily, [int][System.Drawing.FontStyle]::Bold, $emSize, [System.Drawing.PointF]::new(0, 0), [System.Drawing.StringFormat]::GenericTypographic)
    $ocBounds = $ocPath.GetBounds()

    # Give OC a little more width and draw the small plus as crisp geometry.
    $ocWidth = $ocBounds.Width * 1.12
    $plusWidth = $size * 0.22
    $plusHeight = $size * 0.22
    $armWidth = $plusWidth * 0.34
    $armHeight = $plusHeight * 0.34
    $plusPoints = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(0, $armHeight),
        [System.Drawing.PointF]::new(($plusWidth - $armWidth) / 2.0, $armHeight),
        [System.Drawing.PointF]::new(($plusWidth - $armWidth) / 2.0, 0),
        [System.Drawing.PointF]::new(($plusWidth + $armWidth) / 2.0, 0),
        [System.Drawing.PointF]::new(($plusWidth + $armWidth) / 2.0, $armHeight),
        [System.Drawing.PointF]::new($plusWidth, $armHeight),
        [System.Drawing.PointF]::new($plusWidth, ($plusHeight + $armHeight) / 2.0),
        [System.Drawing.PointF]::new(($plusWidth + $armWidth) / 2.0, ($plusHeight + $armHeight) / 2.0),
        [System.Drawing.PointF]::new(($plusWidth + $armWidth) / 2.0, $plusHeight),
        [System.Drawing.PointF]::new(($plusWidth - $armWidth) / 2.0, $plusHeight),
        [System.Drawing.PointF]::new(($plusWidth - $armWidth) / 2.0, ($plusHeight + $armHeight) / 2.0),
        [System.Drawing.PointF]::new(0, ($plusHeight + $armHeight) / 2.0)
    )
    $plusPath.AddPolygon($plusPoints)
    $gap = $size * 0.025
    $groupWidth = $ocWidth + $gap + $plusWidth
    $fitX = [single][Math]::Min(1.0, (($size * 0.9) / $groupWidth))
    $left = [single](($size - ($groupWidth * $fitX)) / 2.0)
    $ocMatrix = [System.Drawing.Drawing2D.Matrix]::new(($fitX * 1.12), 0, 0, 1, ($left - ($ocBounds.X * $fitX * 1.12)), (($size / 2.0) - ($ocBounds.Y + ($ocBounds.Height / 2.0))))
    $plusLeft = [single]($left + ($ocWidth * $fitX) + ($gap * $fitX))
    # Raise the plus by three pixels in the 16px taskbar frame, scaled for larger frames.
    $plusTop = [single](($size / 2.0) - ($size * 0.1875) - ($plusHeight / 2.0))
    $plusMatrix = [System.Drawing.Drawing2D.Matrix]::new($fitX, 0, 0, 1, $plusLeft, $plusTop)
    $ocPath.Transform($ocMatrix)
    $plusPath.Transform($plusMatrix)

    # A dark, one-pixel down-right shadow gives the small white lettering definition.
    $shadowX = [single]($size / 32.0)
    $shadowY = [single]($size / 16.0)
    $shadowMatrix = [System.Drawing.Drawing2D.Matrix]::new(1, 0, 0, 1, $shadowX, $shadowY)
    $ocShadow = $ocPath.Clone()
    $plusShadow = $plusPath.Clone()
    $ocShadow.Transform($shadowMatrix)
    $plusShadow.Transform($shadowMatrix)
    $shadowBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(102, 20, 0, 5))
    $graphics.FillPath($shadowBrush, $ocShadow)
    $graphics.FillPath($shadowBrush, $plusShadow)
    $graphics.FillPath([System.Drawing.Brushes]::White, $ocPath)
    $graphics.FillPath([System.Drawing.Brushes]::White, $plusPath)

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngImages.Add($stream.ToArray())

    $stream.Dispose()
    $format.Dispose()
    $ocMatrix.Dispose()
    $plusMatrix.Dispose()
    $shadowMatrix.Dispose()
    $shadowBrush.Dispose()
    $ocShadow.Dispose()
    $plusShadow.Dispose()
    $ocPath.Dispose()
    $plusPath.Dispose()
    $fontFamily.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

$iconPath = Join-Path $PSScriptRoot 'ocplus.ico'
$iconStream = [System.IO.File]::Create($iconPath)
$writer = [System.IO.BinaryWriter]::new($iconStream)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)

$imageOffset = 6 + 16 * $sizes.Count
for ($index = 0; $index -lt $sizes.Count; $index++) {
    $size = $sizes[$index]
    $image = $pngImages[$index]
    $dimension = if ($size -eq 256) { [byte]0 } else { [byte]$size }
    $writer.Write($dimension)
    $writer.Write($dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$image.Length)
    $writer.Write([UInt32]$imageOffset)
    $imageOffset += $image.Length
}
foreach ($image in $pngImages) { $writer.Write($image) }

$writer.Dispose()
$iconStream.Dispose()
Write-Host "Generated $iconPath"
