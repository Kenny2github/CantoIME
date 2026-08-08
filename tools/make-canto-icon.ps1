# make-canto-icon.ps1
#
# Emits a linkable Win32 resource (.res) with the 粵 (U+7CB5) branding icon in
# the format Windows' own IME icons use (ripped from Microsoft Quick): a
# grayscale tile - light box, ~50% gray 1px border, dark glyph - shipped as an
# 8bpp (grayscale-palette) image AND a 32bpp copy per size, classic DIB.
#
# NOTE: a third-party IME cannot get this icon theme-inverted in the dark
# taskbar the way the built-in IMEs do - that inversion is done by Microsoft's
# IME host/shell and is gated on being a system IME (proven: pointing CantoIME
# at Quick's own icon still renders un-inverted). So this is a theme-NEUTRAL
# brand: a rounded grayscale tile that reads on both light and dark. The live-
# theming that IS ours is the 中/英 mode icon (LangBarButton.cpp).
#
# We emit a .res (not an .rc) because rc.exe rejects hand-authored 8bpp / opaque
# DIB icons (RC2176); a .res is linked verbatim. The glyph is auto-fit to ~94%
# of the tile to match Quick's 速. Built by CMake on the Windows runner with
# Windows PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing
$Glyph = [string][char]0x7CB5   # 粵

# Ink bounding box (alpha > 32) of a bitmap.
function Get-Ink([System.Drawing.Bitmap]$bmp) {
    $w=$bmp.Width; $h=$bmp.Height
    $bd=$bmp.LockBits((New-Object System.Drawing.Rectangle(0,0,$w,$h)),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf=New-Object byte[] ($bd.Stride*$h)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0,$buf,0,$buf.Length)
    $st=$bd.Stride; $bmp.UnlockBits($bd)
    $minx=$w;$miny=$h;$maxx=-1;$maxy=-1
    for($y=0;$y -lt $h;$y++){ for($x=0;$x -lt $w;$x++){
        if($buf[$y*$st+$x*4+3] -gt 32){
            if($x -lt $minx){$minx=$x}; if($x -gt $maxx){$maxx=$x}
            if($y -lt $miny){$miny=$y}; if($y -gt $maxy){$maxy=$y} } } }
    return @{ X=$minx; Y=$miny; W=($maxx-$minx+1); H=($maxy-$miny+1) }
}
# Render the glyph alone (black on transparent) at a given font px on an NxN canvas.
function Render-Glyph([int]$canvas,[double]$fontpx) {
    $b=New-Object System.Drawing.Bitmap($canvas,$canvas,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g=[System.Drawing.Graphics]::FromImage($b)
    $g.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint=[System.Drawing.Text.TextRenderingHint]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $f=New-Object System.Drawing.Font('Microsoft JhengHei',$fontpx,
        [System.Drawing.FontStyle]::Regular,[System.Drawing.GraphicsUnit]::Pixel)
    $fmt=New-Object System.Drawing.StringFormat
    $fmt.Alignment=[System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment=[System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags=[System.Drawing.StringFormatFlags]::NoClip
    $g.DrawString($Glyph,$f,[System.Drawing.Brushes]::Black,
        (New-Object System.Drawing.RectangleF(0,0,$canvas,$canvas)),$fmt)
    $f.Dispose(); $g.Dispose(); return $b
}
# Grayscale tile with the glyph scaled to fill ~(size-2)px (1px border margin),
# centered, on a white box with a 50% gray border. Returns gray[y*size+x].
function Get-GrayTile([int]$size) {
    # Fit the glyph's full (anti-aliased) extent to the tile; the solid strokes
    # then land at ~94%, matching Quick's 速 (measured 30/32).
    $target = $size
    $g1 = Render-Glyph ($size*3) $size          # measure ink at em=size
    $ink = Get-Ink $g1; $g1.Dispose()
    $scale = $target / [math]::Max($ink.W, $ink.H)
    $g2 = Render-Glyph ($size*3) ($size*$scale)  # final glyph, auto-fit
    $ink2 = Get-Ink $g2

    $tile=New-Object System.Drawing.Bitmap($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $tg=[System.Drawing.Graphics]::FromImage($tile)
    $tg.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $tg.Clear([System.Drawing.Color]::White)
    $pen=New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255,128,128,128),1)
    $tg.DrawRectangle($pen,0,0,$size-1,$size-1); $pen.Dispose()
    $dx=[int]([math]::Round(($size-$ink2.W)/2 - $ink2.X))
    $dy=[int]([math]::Round(($size-$ink2.H)/2 - $ink2.Y))
    $tg.DrawImage($g2,$dx,$dy)
    $tg.Dispose(); $g2.Dispose()

    $r=New-Object System.Drawing.Rectangle(0,0,$size,$size)
    $bd=$tile.LockBits($r,[System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf=New-Object byte[] ($bd.Stride*$size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0,$buf,0,$buf.Length)
    $st=$bd.Stride; $tile.UnlockBits($bd); $tile.Dispose()
    $gray=New-Object byte[] ($size*$size)
    for($y=0;$y -lt $size;$y++){ for($x=0;$x -lt $size;$x++){
        $gray[$y*$size+$x]=$buf[$y*$st+$x*4+2] } }
    return $gray
}

# Rounded-corner cut (transparent) for a softer tile.
function Is-Cut([int]$x,[int]$y,[int]$s) {
    $r=[math]::Max(1,[int]([math]::Round($s/7)))
    if($x -ge $r -and $x -lt $s-$r){return $false}
    if($y -ge $r -and $y -lt $s-$r){return $false}
    $cx=$(if($x -lt $r){$r}else{$s-$r}); $cy=$(if($y -lt $r){$r}else{$s-$r})
    $dx=($x+0.5)-$cx; $dy=($y+0.5)-$cy
    return (($dx*$dx+$dy*$dy) -gt ($r*$r))
}
function New-Header([int]$w,[int]$h,[int]$bpp) {
    $hdr=New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr,0)
    [BitConverter]::GetBytes([int32]$w).CopyTo($hdr,4)
    [BitConverter]::GetBytes([int32]($h*2)).CopyTo($hdr,8)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr,12)
    [BitConverter]::GetBytes([uint16]$bpp).CopyTo($hdr,14); return $hdr
}
function And-Mask([int]$size) {
    $stride=[int]([math]::Floor(($size+31)/32))*4
    $m=New-Object byte[] ($stride*$size)
    for($y=0;$y -lt $size;$y++){ $row=$size-1-$y
        for($x=0;$x -lt $size;$x++){ if(Is-Cut $x $y $size){
            $bi=$row*$stride+[int]([math]::Floor($x/8))
            $m[$bi]=$m[$bi] -bor [byte](0x80 -shr ($x%8)) } } }
    return $m
}
function New-Dib8([int]$size,[byte[]]$gray) {
    $hdr=New-Header $size $size 8
    $pal=New-Object byte[] 1024
    for($i=0;$i -lt 256;$i++){ $pal[$i*4]=$i;$pal[$i*4+1]=$i;$pal[$i*4+2]=$i }
    $rl=[int]([math]::Floor(($size*8+31)/32))*4
    $xor=New-Object byte[] ($rl*$size)
    for($y=0;$y -lt $size;$y++){ $row=$size-1-$y
        for($x=0;$x -lt $size;$x++){ $xor[$row*$rl+$x]=$gray[$y*$size+$x] } }
    return ($hdr + $pal + $xor + (And-Mask $size))
}
function New-Dib32([int]$size,[byte[]]$gray) {
    $hdr=New-Header $size $size 32
    $xor=New-Object byte[] ($size*$size*4)
    for($y=0;$y -lt $size;$y++){ $row=$size-1-$y
        for($x=0;$x -lt $size;$x++){ $v=$gray[$y*$size+$x]; $p=$row*$size*4+$x*4
            $xor[$p]=$v;$xor[$p+1]=$v;$xor[$p+2]=$v
            $xor[$p+3]=$(if(Is-Cut $x $y $size){0}else{255}) } }
    return ($hdr + $xor + (And-Mask $size))
}

$sizes = 48,40,32,24,20,16
$rt = New-Object System.Collections.ArrayList; $id=1
foreach($s in $sizes){ $g=Get-GrayTile $s
    [void]$rt.Add(@{Id=$id;W=$s;H=$s;Bpp=8; Data=(New-Dib8  $s $g)}); $id++ }
foreach($s in $sizes){ $g=Get-GrayTile $s
    [void]$rt.Add(@{Id=$id;W=$s;H=$s;Bpp=32;Data=(New-Dib32 $s $g)}); $id++ }

$gms=New-Object System.IO.MemoryStream; $gw=New-Object System.IO.BinaryWriter($gms)
$gw.Write([uint16]0);$gw.Write([uint16]1);$gw.Write([uint16]$rt.Count)
foreach($ic in $rt){
    $wb=[byte]($(if($ic.W -ge 256){0}else{$ic.W})); $hb=[byte]($(if($ic.H -ge 256){0}else{$ic.H}))
    $gw.Write($wb);$gw.Write($hb);$gw.Write([byte]0);$gw.Write([byte]0)
    $gw.Write([uint16]1);$gw.Write([uint16]$ic.Bpp)
    $gw.Write([uint32]$ic.Data.Length);$gw.Write([uint16]$ic.Id) }
$gw.Flush(); $grp=$gms.ToArray()

$dir=[System.IO.Path]::GetDirectoryName($OutPath)
if($dir -and -not(Test-Path $dir)){ New-Item -ItemType Directory -Force -Path $dir|Out-Null }
$out=New-Object System.IO.MemoryStream; $bw=New-Object System.IO.BinaryWriter($out)
function Write-Res($bw,[int]$type,[int]$name,[byte[]]$data,[int]$mem){
    $bw.Write([uint32]$data.Length);$bw.Write([uint32]32)
    $bw.Write([uint16]0xFFFF);$bw.Write([uint16]$type)
    $bw.Write([uint16]0xFFFF);$bw.Write([uint16]$name)
    $bw.Write([uint32]0);$bw.Write([uint16]$mem);$bw.Write([uint16]0x0409)
    $bw.Write([uint32]0);$bw.Write([uint32]0)
    if($data.Length){$bw.Write($data)}
    $pad=(4-($data.Length%4))%4; if($pad){$bw.Write((New-Object byte[] $pad))} }
Write-Res $bw 0 0 (New-Object byte[] 0) 0
foreach($ic in $rt){ Write-Res $bw 3 $ic.Id $ic.Data 0x1010 }
Write-Res $bw 14 1 $grp 0x1030
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath,$out.ToArray())
Write-Host "Wrote $OutPath (.res: $($rt.Count) RT_ICON + 1 RT_GROUP_ICON)"
