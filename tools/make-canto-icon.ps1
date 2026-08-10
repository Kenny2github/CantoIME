# make-canto-icon.ps1
#
# Renders the 粵 (U+7CB5) branding icon as a classic monochrome DIB icon: a
# black glyph on a transparent background, no box. Windows' own IME icons (e.g.
# Microsoft Quick) are monochrome DIB glyphs, and the shell recolors them to the
# taskbar theme (dark strokes on light, light strokes on dark). The .ico is
# assembled by hand from the 32-bit pixels (BITMAPINFOHEADER + BGRA + AND mask);
# Bitmap.GetHicon mangles the alpha channel and yields a broken icon, so it is
# avoided. Built at build time by CMake on the Windows runner using Windows
# PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

# Returns the raw BGRA bytes (top-down, stride = size*4) for a black 粵 glyph on
# a transparent canvas.
function Get-Bgra([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint  = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $fs = [math]::Max(1, [math]::Round($size * 0.86))
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', $fs,
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment     = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags   = [System.Drawing.StringFormatFlags]::NoClip
    $rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
    $g.DrawString([string][char]0x7CB5, $font, [System.Drawing.Brushes]::Black, $rect, $fmt)
    $font.Dispose(); $g.Dispose()

    $r  = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $bd = $bmp.LockBits($r, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($bd.Stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $buf.Length)
    $stride = $bd.Stride
    $bmp.UnlockBits($bd); $bmp.Dispose()
    return @{ Buf = $buf; Stride = $stride }
}

# Builds one DIB icon image (BITMAPINFOHEADER + bottom-up BGRA XOR + zero AND).
function New-DibImage([int]$size) {
    $px = Get-Bgra $size
    $buf = $px.Buf; $stride = $px.Stride

    $xor = New-Object byte[] ($size * $size * 4)
    for ($y = 0; $y -lt $size; $y++) {
        [Array]::Copy($buf, $y * $stride, $xor, ($size - 1 - $y) * $size * 4, $size * 4)
    }
    $andStride = [int]([math]::Floor(($size + 31) / 32) * 4)
    $and = New-Object byte[] ($andStride * $size)   # all zero: alpha does masking

    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr, 0)          # biSize
    [BitConverter]::GetBytes([int32]$size).CopyTo($hdr, 4)        # biWidth
    [BitConverter]::GetBytes([int32]($size * 2)).CopyTo($hdr, 8)  # biHeight (XOR+AND)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr, 12)          # biPlanes
    [BitConverter]::GetBytes([uint16]32).CopyTo($hdr, 14)         # biBitCount

    $img = New-Object byte[] ($hdr.Length + $xor.Length + $and.Length)
    [Array]::Copy($hdr, 0, $img, 0, $hdr.Length)
    [Array]::Copy($xor, 0, $img, $hdr.Length, $xor.Length)
    [Array]::Copy($and, 0, $img, $hdr.Length + $xor.Length, $and.Length)
    return $img
}

$sizes  = 16, 20, 24, 32, 40, 48
$images = foreach ($s in $sizes) { ,([byte[]](New-DibImage $s)) }

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
    $s = $sizes[$i]
    $b = [byte]($(if ($s -ge 256) { 0 } else { $s }))
    $bw.Write($b); $bw.Write($b)      # width, height
    $bw.Write([byte]0)                # color count
    $bw.Write([byte]0)                # reserved
    $bw.Write([UInt16]1)              # planes
    $bw.Write([UInt16]32)             # bit count
    $bw.Write([UInt32]$images[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
$bw.Flush()

[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath (monochrome DIB, $($sizes.Count) sizes)"
