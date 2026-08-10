# make-canto-icon.ps1
#
# Generates the 粵 (U+7CB5) branding icon: a neutral mid-gray glyph on a
# transparent background, at a few sizes, as a classic 32bpp DIB .ico.
#
# It is deliberately plain. A third-party IME's brand icon cannot be theme-
# inverted in the dark taskbar the way the built-in IMEs are (that is done by
# Microsoft's IME host and gated on being a system IME - proven by pointing
# CantoIME at a built-in IME's own icon; it still rendered un-inverted). So we
# don't chase that: a fixed mid-gray reads on both light and dark. The icon is
# assembled as a DIB by hand because Bitmap.GetHicon mangles the alpha channel.
# The 中/英 mode icon is the one that themes live (see LangBarButton.cpp).
#
# Built by CMake on the Windows runner with Windows PowerShell 5.1.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

# One 32bpp DIB icon image (BITMAPINFOHEADER + bottom-up BGRA + zero AND mask).
function New-Image([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', [math]::Round($size * 0.95),
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 150, 150, 150))
    $g.DrawString([string][char]0x7CB5, $font, $brush,
        (New-Object System.Drawing.RectangleF(0, 0, $size, $size)), $fmt)
    $brush.Dispose(); $font.Dispose(); $g.Dispose()

    $bd = $bmp.LockBits((New-Object System.Drawing.Rectangle(0, 0, $size, $size)),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($bd.Stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $buf.Length)
    $st = $bd.Stride; $bmp.UnlockBits($bd); $bmp.Dispose()

    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr, 0)
    [BitConverter]::GetBytes([int32]$size).CopyTo($hdr, 4)
    [BitConverter]::GetBytes([int32]($size * 2)).CopyTo($hdr, 8) # height doubled (XOR+AND)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr, 12)
    [BitConverter]::GetBytes([uint16]32).CopyTo($hdr, 14)
    $xor = New-Object byte[] ($size * $size * 4)
    for ($y = 0; $y -lt $size; $y++) {                          # top-down -> bottom-up
        [Array]::Copy($buf, $y * $st, $xor, ($size - 1 - $y) * $size * 4, $size * 4)
    }
    $and = New-Object byte[] ([int]([math]::Floor(($size + 31) / 32)) * 4 * $size)  # zero: alpha masks
    return ($hdr + $xor + $and)
}

$sizes = 16, 20, 24, 32, 40, 48
$imgs = foreach ($s in $sizes) { , ([byte[]](New-Image $s)) }

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter($out)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$off = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $bw.Write([byte]$s); $bw.Write([byte]$s); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$imgs[$i].Length); $bw.Write([uint32]$off); $off += $imgs[$i].Length
}
foreach ($im in $imgs) { $bw.Write($im) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath (gray 粵, 32bpp DIB, $($sizes.Count) sizes)"
