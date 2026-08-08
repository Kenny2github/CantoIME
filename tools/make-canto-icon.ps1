# make-canto-icon.ps1
#
# Renders the 粵 (U+7CB5) branding icon in the exact format Windows' own IME
# icons use (verified by ripping Microsoft Quick's icon from ResourceDll.dll):
#
#   * a GRAYSCALE tile - near-white box, ~50% gray 1px border, dark glyph;
#   * shipped as an 8bpp (grayscale-palette) image AND a 32bpp copy per size;
#   * classic DIB (BITMAPINFOHEADER + pixels + AND mask), never PNG;
#   * corners cut out via the AND mask.
#
# The shell recolors this grayscale tile to the taskbar theme (dark glyph on
# light, light glyph on dark), which is how Quick transitions. A plain colored
# or 32bpp-only glyph is shown as-is and never themed. Built at build time by
# CMake on the Windows runner using Windows PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

# Render the grayscale tile and return the gray value (0..255) per pixel,
# top-down, as a flat byte[] of length size*size.
function Get-GrayTile([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::White)

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 128, 128, 128), 1)
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
    $font.Dispose(); $g.Dispose()

    $r  = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $bd = $bmp.LockBits($r, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($bd.Stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $buf.Length)
    $stride = $bd.Stride
    $bmp.UnlockBits($bd); $bmp.Dispose()

    $gray = New-Object byte[] ($size * $size)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $gray[$y * $size + $x] = $buf[$y * $stride + $x * 4 + 2] # R (B=G=R)
        }
    }
    return $gray
}

# 1 = transparent pixel. Only the four corners are cut out (matches Quick).
function Is-Corner([int]$x, [int]$y, [int]$size) {
    return (($x -eq 0 -or $x -eq $size - 1) -and ($y -eq 0 -or $y -eq $size - 1))
}

# Bottom-up 1bpp AND mask, stride padded to 4 bytes.
function Build-AndMask([int]$size) {
    $stride = [int]([math]::Floor(($size + 31) / 32)) * 4
    $m = New-Object byte[] ($stride * $size)
    for ($y = 0; $y -lt $size; $y++) {
        $row = $size - 1 - $y # bottom-up
        for ($x = 0; $x -lt $size; $x++) {
            if (Is-Corner $x $y $size) {
                $m[$row * $stride + [int]([math]::Floor($x / 8))] = `
                    $m[$row * $stride + [int]([math]::Floor($x / 8))] -bor [byte](0x80 -shr ($x % 8))
            }
        }
    }
    return @{ Bytes = $m; Stride = $stride }
}

function New-Header([int]$w, [int]$h, [int]$bpp) {
    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr, 0)
    [BitConverter]::GetBytes([int32]$w).CopyTo($hdr, 4)
    [BitConverter]::GetBytes([int32]($h * 2)).CopyTo($hdr, 8) # XOR + AND
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr, 12)
    [BitConverter]::GetBytes([uint16]$bpp).CopyTo($hdr, 14)
    return $hdr
}

# 8bpp grayscale DIB image: 256-entry grayscale palette, index = gray value.
function New-Dib8([int]$size, [byte[]]$gray) {
    $hdr = New-Header $size $size 8
    $pal = New-Object byte[] (256 * 4)
    for ($i = 0; $i -lt 256; $i++) { $pal[$i*4] = $i; $pal[$i*4+1] = $i; $pal[$i*4+2] = $i }
    $rowLen = [int]([math]::Floor(($size * 8 + 31) / 32)) * 4
    $xor = New-Object byte[] ($rowLen * $size)
    for ($y = 0; $y -lt $size; $y++) {
        $row = $size - 1 - $y
        for ($x = 0; $x -lt $size; $x++) { $xor[$row * $rowLen + $x] = $gray[$y * $size + $x] }
    }
    $mask = Build-AndMask $size
    $img = New-Object byte[] ($hdr.Length + $pal.Length + $xor.Length + $mask.Bytes.Length)
    $o = 0
    foreach ($part in @($hdr, $pal, $xor, $mask.Bytes)) { [Array]::Copy($part,0,$img,$o,$part.Length); $o += $part.Length }
    return $img
}

# 32bpp grayscale DIB image: BGRA with B=G=R=gray, opaque except cut corners.
function New-Dib32([int]$size, [byte[]]$gray) {
    $hdr = New-Header $size $size 32
    $xor = New-Object byte[] ($size * $size * 4)
    for ($y = 0; $y -lt $size; $y++) {
        $row = $size - 1 - $y
        for ($x = 0; $x -lt $size; $x++) {
            $v = $gray[$y * $size + $x]; $p = $row * $size * 4 + $x * 4
            $xor[$p] = $v; $xor[$p+1] = $v; $xor[$p+2] = $v
            $xor[$p+3] = $(if (Is-Corner $x $y $size) { 0 } else { 255 })
        }
    }
    $mask = Build-AndMask $size
    $img = New-Object byte[] ($hdr.Length + $xor.Length + $mask.Bytes.Length)
    $o = 0
    foreach ($part in @($hdr, $xor, $mask.Bytes)) { [Array]::Copy($part,0,$img,$o,$part.Length); $o += $part.Length }
    return $img
}

# NOTE: rc.exe rejects hand-built 8bpp icon images (RC2176 "old DIB"), so we ship
# only the 32bpp grayscale tiles. The tile is still grayscale (B=G=R), which is
# what the shell keys its theme recolor on.
$sizes = 48, 40, 32, 24, 20, 16
$imgs = New-Object System.Collections.ArrayList
foreach ($s in $sizes) { $g = Get-GrayTile $s; [void]$imgs.Add((New-Dib32 $s $g)) }
$dims = @($sizes)
$bpps = @(32) * $sizes.Count

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter($out)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$imgs.Count)
$offset = 6 + 16 * $imgs.Count
for ($i = 0; $i -lt $imgs.Count; $i++) {
    $s = $dims[$i]; $b = [byte]($(if ($s -ge 256) { 0 } else { $s }))
    $bw.Write($b); $bw.Write($b)
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]$bpps[$i])
    $bw.Write([UInt32]$imgs[$i].Length); $bw.Write([UInt32]$offset)
    $offset += $imgs[$i].Length
}
foreach ($img in $imgs) { $bw.Write($img) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath ($($imgs.Count) images: 8bpp + 32bpp grayscale DIB)"
