# make-canto-icon.ps1
#
# Generates the 粵 (U+7CB5) branding icon: a Windows-8-style black glyph on a
# white box with a 1px 50%-black border, at a few sizes, 32bpp.
#
# Emitted as a linkable .res (not an .rc) because rc.exe rejects an opaque DIB
# icon (RC2176 "old DIB"), and the box must be a classic DIB - the modern input
# indicator ignores PNG-encoded icons. The icon is built as a DIB by hand
# because Bitmap.GetHicon mangles the alpha channel.
#
# Note: a third-party IME's brand icon can't be theme-inverted in the dark
# taskbar the way the built-in IMEs are - that's done by Microsoft's IME host
# and gated on being a system IME (proven). The 中/英 mode icon is the one that
# themes live (LangBarButton.cpp). Built by CMake on the Windows runner.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

# One 32bpp DIB image: white box, 50% gray border, black 粵 sized ~94% of tile.
function New-Dib([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::White)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(128, 0, 0, 0), 1)
    $g.DrawRectangle($pen, 0, 0, $size - 1, $size - 1); $pen.Dispose()
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', ($size * 1.05),
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip
    $g.DrawString([string][char]0x7CB5, $font, [System.Drawing.Brushes]::Black,
        (New-Object System.Drawing.RectangleF(0, 0, $size, $size)), $fmt)
    $font.Dispose(); $g.Dispose()

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
    $and = New-Object byte[] ([int]([math]::Floor(($size + 31) / 32)) * 4 * $size) # zero
    return ($hdr + $xor + $and)
}

# Emits one Win32 resource entry (.res): ordinal type + name, then padded data.
function Write-Res($bw, [int]$type, [int]$name, [byte[]]$data, [int]$mem) {
    $bw.Write([uint32]$data.Length); $bw.Write([uint32]32)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$type)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$name)
    $bw.Write([uint32]0); $bw.Write([uint16]$mem); $bw.Write([uint16]0x0409)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    if ($data.Length) { $bw.Write($data) }
    $pad = (4 - ($data.Length % 4)) % 4; if ($pad) { $bw.Write((New-Object byte[] $pad)) }
}

$sizes = 48, 40, 32, 24, 20, 16
$icons = @(); $id = 1
foreach ($s in $sizes) { $icons += @{ Id = $id; S = $s; Data = (New-Dib $s) }; $id++ }

# RT_GROUP_ICON directory referencing each RT_ICON image.
$gms = New-Object System.IO.MemoryStream; $gw = New-Object System.IO.BinaryWriter($gms)
$gw.Write([uint16]0); $gw.Write([uint16]1); $gw.Write([uint16]$icons.Count)
foreach ($ic in $icons) {
    $gw.Write([byte]$ic.S); $gw.Write([byte]$ic.S); $gw.Write([byte]0); $gw.Write([byte]0)
    $gw.Write([uint16]1); $gw.Write([uint16]32)
    $gw.Write([uint32]$ic.Data.Length); $gw.Write([uint16]$ic.Id)
}
$gw.Flush(); $grp = $gms.ToArray()

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$out = New-Object System.IO.MemoryStream; $bw = New-Object System.IO.BinaryWriter($out)
Write-Res $bw 0 0 (New-Object byte[] 0) 0                       # 32-bit .res marker
foreach ($ic in $icons) { Write-Res $bw 3 $ic.Id $ic.Data 0x1010 }  # RT_ICON
Write-Res $bw 14 1 $grp 0x1030                                  # RT_GROUP_ICON (index 0)
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath (.res: $($icons.Count) icons)"
