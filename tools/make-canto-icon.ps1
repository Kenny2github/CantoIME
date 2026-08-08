# make-canto-icon.ps1
#
# Emits a linkable Win32 resource (.res) containing the 粵 (U+7CB5) branding
# icon in the exact format Windows' own IME icons use (verified by ripping
# Microsoft Quick from ResourceDll.dll):
#
#   * a GRAYSCALE tile filling the icon (light box, ~50% gray 1px border, dark
#     glyph), corners cut;
#   * an 8bpp (grayscale-palette) image AND a 32bpp copy per size;
#   * classic DIB (BITMAPINFOHEADER + pixels + AND mask).
#
# The shell recolors that grayscale tile to the taskbar theme (dark glyph on
# light, light strokes on dark) - a plain colored/transparent glyph is shown
# as-is. We build a .res (not an .rc) because rc.exe rejects hand-authored 8bpp
# and opaque DIB icons with RC2176 "old DIB"; a .res is linked verbatim, so the
# resource compiler never re-parses it. The .res carries one RT_GROUP_ICON
# (index 0, what DllRegisterServer registers) plus its RT_ICON images. Built by
# CMake on the Windows runner using Windows PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

# Grayscale tile -> gray value (0..255) per pixel, top-down, flat byte[].
function Get-GrayTile([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::White)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255,128,128,128),1)
    $g.DrawRectangle($pen, 0, 0, $size - 1, $size - 1); $pen.Dispose()

    $fs = [math]::Max(1, [math]::Round($size * 0.82))
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', $fs,
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags = [System.Drawing.StringFormatFlags]::NoClip
    $g.DrawString([string][char]0x7CB5, $font,
        [System.Drawing.Brushes]::Black,
        (New-Object System.Drawing.RectangleF(0,0,$size,$size)), $fmt)
    $font.Dispose(); $g.Dispose()

    $r  = New-Object System.Drawing.Rectangle(0,0,$size,$size)
    $bd = $bmp.LockBits($r, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($bd.Stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $buf, 0, $buf.Length)
    $st = $bd.Stride; $bmp.UnlockBits($bd); $bmp.Dispose()
    $gray = New-Object byte[] ($size * $size)
    for ($y=0; $y -lt $size; $y++) { for ($x=0; $x -lt $size; $x++) {
        $gray[$y*$size+$x] = $buf[$y*$st+$x*4+2] } }
    return $gray
}

function Is-Corner([int]$x,[int]$y,[int]$s) {
    return (($x -eq 0 -or $x -eq $s-1) -and ($y -eq 0 -or $y -eq $s-1))
}
function New-Header([int]$w,[int]$h,[int]$bpp) {
    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr,0)
    [BitConverter]::GetBytes([int32]$w).CopyTo($hdr,4)
    [BitConverter]::GetBytes([int32]($h*2)).CopyTo($hdr,8)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr,12)
    [BitConverter]::GetBytes([uint16]$bpp).CopyTo($hdr,14)
    return $hdr
}
# Bottom-up 1bpp AND mask, corners transparent.
function And-Mask([int]$size) {
    $stride = [int]([math]::Floor(($size+31)/32))*4
    $m = New-Object byte[] ($stride*$size)
    for ($y=0; $y -lt $size; $y++) { $row=$size-1-$y
        for ($x=0; $x -lt $size; $x++) { if (Is-Corner $x $y $size) {
            $bi=$row*$stride+[int]([math]::Floor($x/8))
            $m[$bi] = $m[$bi] -bor [byte](0x80 -shr ($x%8)) } } }
    return $m
}
function New-Dib8([int]$size,[byte[]]$gray) {
    $hdr = New-Header $size $size 8
    $pal = New-Object byte[] 1024
    for ($i=0;$i -lt 256;$i++){ $pal[$i*4]=$i;$pal[$i*4+1]=$i;$pal[$i*4+2]=$i }
    $rl = [int]([math]::Floor(($size*8+31)/32))*4
    $xor = New-Object byte[] ($rl*$size)
    for ($y=0;$y -lt $size;$y++){ $row=$size-1-$y
        for ($x=0;$x -lt $size;$x++){ $xor[$row*$rl+$x]=$gray[$y*$size+$x] } }
    $and = And-Mask $size
    return ($hdr + $pal + $xor + $and)
}
function New-Dib32([int]$size,[byte[]]$gray) {
    $hdr = New-Header $size $size 32
    $xor = New-Object byte[] ($size*$size*4)
    for ($y=0;$y -lt $size;$y++){ $row=$size-1-$y
        for ($x=0;$x -lt $size;$x++){ $v=$gray[$y*$size+$x]; $p=$row*$size*4+$x*4
            $xor[$p]=$v;$xor[$p+1]=$v;$xor[$p+2]=$v
            $xor[$p+3]=$(if (Is-Corner $x $y $size){0}else{255}) } }
    $and = And-Mask $size
    return ($hdr + $xor + $and)
}

# --- Build the icon images ---
$sizes = 48,40,32,24,20,16
$rtIcons = New-Object System.Collections.ArrayList  # @{ Id; W; H; Bpp; Data }
$id = 1
foreach ($s in $sizes) { $g=Get-GrayTile $s
    [void]$rtIcons.Add(@{Id=$id;W=$s;H=$s;Bpp=8; Data=(New-Dib8  $s $g)}); $id++ }
foreach ($s in $sizes) { $g=Get-GrayTile $s
    [void]$rtIcons.Add(@{Id=$id;W=$s;H=$s;Bpp=32;Data=(New-Dib32 $s $g)}); $id++ }

# --- GRPICONDIR (RT_GROUP_ICON data) ---
$gms = New-Object System.IO.MemoryStream; $gw = New-Object System.IO.BinaryWriter($gms)
$gw.Write([uint16]0); $gw.Write([uint16]1); $gw.Write([uint16]$rtIcons.Count)
foreach ($ic in $rtIcons) {
    $wb=[byte]($(if($ic.W -ge 256){0}else{$ic.W})); $hb=[byte]($(if($ic.H -ge 256){0}else{$ic.H}))
    $gw.Write($wb); $gw.Write($hb); $gw.Write([byte]0); $gw.Write([byte]0)
    $gw.Write([uint16]1); $gw.Write([uint16]$ic.Bpp)
    $gw.Write([uint32]$ic.Data.Length); $gw.Write([uint16]$ic.Id)
}
$gw.Flush(); $grpData = $gms.ToArray()

# --- Write the .res ---
$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$out = New-Object System.IO.MemoryStream; $bw = New-Object System.IO.BinaryWriter($out)
function Write-Res($bw,[int]$type,[int]$name,[byte[]]$data,[int]$mem) {
    $bw.Write([uint32]$data.Length)   # DataSize
    $bw.Write([uint32]32)             # HeaderSize (ordinal type+name)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$type)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$name)
    $bw.Write([uint32]0)              # DataVersion
    $bw.Write([uint16]$mem)           # MemoryFlags
    $bw.Write([uint16]0x0409)         # LanguageId (en-US)
    $bw.Write([uint32]0)              # Version
    $bw.Write([uint32]0)              # Characteristics
    if ($data.Length) { $bw.Write($data) }
    $pad = (4 - ($data.Length % 4)) % 4
    if ($pad) { $bw.Write((New-Object byte[] $pad)) }
}
Write-Res $bw 0 0 (New-Object byte[] 0) 0            # leading empty (32-bit .res marker)
foreach ($ic in $rtIcons) { Write-Res $bw 3 $ic.Id $ic.Data 0x1010 }  # RT_ICON
Write-Res $bw 14 1 $grpData 0x1030                    # RT_GROUP_ICON (index 0)
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath (.res: $($rtIcons.Count) RT_ICON + 1 RT_GROUP_ICON)"
