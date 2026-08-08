# make-canto-icon.ps1
#
# Renders the 粵 (U+7CB5) branding icon for the IME. It follows Microsoft's IME
# branding-icon look (a white box with a black glyph and a 1px 50%-black outer
# stroke) and, importantly, is emitted as a *classic DIB (BMP) icon*, not a
# PNG-compressed icon. Windows' own IME icons (e.g. Microsoft Quick, in
# ResourceDll.dll) are DIB, and the modern input indicator only themes / uses
# DIB icons - a PNG-in-ICO is left un-themed and falls back to a generic glyph
# in the picker. Built at build time by CMake on the Windows runner using
# Windows PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

function New-SizeBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    # White box + 1px 50%-black outer stroke (Microsoft IME branding spec). The
    # black/white-only design is what lets the shell theme it for the dark tray.
    $g.FillRectangle([System.Drawing.Brushes]::White, 0, 0, $size, $size)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(128, 0, 0, 0), 1)
    $g.DrawRectangle($pen, 0, 0, $size - 1, $size - 1)
    $pen.Dispose()

    $fs = [math]::Max(1, [math]::Round($size * 0.72))
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', $fs,
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment     = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags   = [System.Drawing.StringFormatFlags]::NoClip
    $rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
    $g.DrawString([string][char]0x7CB5, $font, [System.Drawing.Brushes]::Black, $rect, $fmt)

    $font.Dispose()
    $g.Dispose()
    return $bmp
}

# Microsoft's guideline resolution set.
$sizes   = 16, 20, 24, 32, 40, 48
$entries = @()
$images  = @()

foreach ($s in $sizes) {
    $bmp   = New-SizeBitmap $s
    # GetHicon -> Icon -> Save produces a *DIB* icon (BITMAPINFOHEADER + BGRA +
    # AND mask), the classic format. Each Save is a one-image .ico we then merge.
    $hicon = $bmp.GetHicon()
    $icon  = [System.Drawing.Icon]::FromHandle($hicon)
    $ms    = New-Object System.IO.MemoryStream
    $icon.Save($ms)
    $b     = $ms.ToArray()
    $ms.Dispose(); $icon.Dispose(); $bmp.Dispose()

    # Layout of a 1-image .ico: [0..5] ICONDIR, [6..21] ICONDIRENTRY, [22..] image.
    $entries += ,([byte[]]($b[6..21]))
    $images  += ,([byte[]]($b[22..($b.Length - 1)]))
}

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter($out)
$bw.Write([UInt16]0)              # reserved
$bw.Write([UInt16]1)              # type = icon
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $e = $entries[$i]
    $bw.Write([byte[]]($e[0..7]))     # W, H, ColorCount, Reserved, Planes(2), BitCount(2)
    $bw.Write([UInt32]$images[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
$bw.Flush()

[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath (DIB, $($sizes.Count) sizes)"
