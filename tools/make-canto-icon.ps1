# make-canto-icon.ps1
#
# Generates the 粵 (U+7CB5) branding icon: a Windows-8-style black glyph on a
# white box with a 1px 50%-black border, sized ~94% (matching Microsoft Quick's
# 速), at a few sizes, 32bpp.
#
# Emitted as a linkable .res (not an .rc) because rc.exe rejects an opaque DIB
# icon (RC2176 "old DIB"), and the box must be a classic DIB - the modern input
# indicator ignores PNG-encoded icons. Built as a DIB by hand because
# Bitmap.GetHicon mangles the alpha channel.
#
# Note: a third-party IME's brand icon can't be theme-inverted in the dark
# taskbar the way the built-in IMEs are - that's done by Microsoft's IME host
# and gated on being a system IME (proven). The 中/英 mode icon is the one that
# themes live (LangBarButton.cpp). Built by CMake on the Windows runner.
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
# The glyph alone (black on transparent) at a font size, on an NxN canvas.
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
# One 32bpp DIB: white box, 50% gray border, black glyph fit to ~94% and centered.
function New-Dib([int]$size) {
    $g1=Render-Glyph ($size*3) $size            # measure ink at em=size
    $ink=Get-Ink $g1; $g1.Dispose()
    $g2=Render-Glyph ($size*3) ($size*$size/[math]::Max($ink.W,$ink.H))  # scale ink -> full tile
    $ink2=Get-Ink $g2

    $tile=New-Object System.Drawing.Bitmap($size,$size,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $tg=[System.Drawing.Graphics]::FromImage($tile)
    $tg.SmoothingMode=[System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $tg.Clear([System.Drawing.Color]::White)
    $pen=New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(128,0,0,0),1)
    $tg.DrawRectangle($pen,0,0,$size-1,$size-1); $pen.Dispose()
    $dx=[int]([math]::Round(($size-$ink2.W)/2-$ink2.X))
    $dy=[int]([math]::Round(($size-$ink2.H)/2-$ink2.Y))
    $tg.DrawImage($g2,$dx,$dy); $tg.Dispose(); $g2.Dispose()

    $bd=$tile.LockBits((New-Object System.Drawing.Rectangle(0,0,$size,$size)),
        [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf=New-Object byte[] ($bd.Stride*$size)
    [System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0,$buf,0,$buf.Length)
    $st=$bd.Stride; $tile.UnlockBits($bd); $tile.Dispose()

    $hdr=New-Object byte[] 40
    [BitConverter]::GetBytes([uint32]40).CopyTo($hdr,0)
    [BitConverter]::GetBytes([int32]$size).CopyTo($hdr,4)
    [BitConverter]::GetBytes([int32]($size*2)).CopyTo($hdr,8)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr,12)
    [BitConverter]::GetBytes([uint16]32).CopyTo($hdr,14)
    $xor=New-Object byte[] ($size*$size*4)
    for($y=0;$y -lt $size;$y++){ [Array]::Copy($buf,$y*$st,$xor,($size-1-$y)*$size*4,$size*4) }
    $and=New-Object byte[] ([int]([math]::Floor(($size+31)/32))*4*$size)   # zero
    return ($hdr+$xor+$and)
}
# One Win32 resource entry (.res): ordinal type + name, then padded data.
function Write-Res($bw,[int]$type,[int]$name,[byte[]]$data,[int]$mem) {
    $bw.Write([uint32]$data.Length); $bw.Write([uint32]32)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$type)
    $bw.Write([uint16]0xFFFF); $bw.Write([uint16]$name)
    $bw.Write([uint32]0); $bw.Write([uint16]$mem); $bw.Write([uint16]0x0409)
    $bw.Write([uint32]0); $bw.Write([uint32]0)
    if($data.Length){ $bw.Write($data) }
    $p=(4-($data.Length%4))%4; if($p){ $bw.Write((New-Object byte[] $p)) }
}

$sizes=48,40,32,24,20,16
$icons=@(); $id=1
foreach($s in $sizes){ $icons+=@{Id=$id;S=$s;Data=(New-Dib $s)}; $id++ }

$gms=New-Object System.IO.MemoryStream; $gw=New-Object System.IO.BinaryWriter($gms)
$gw.Write([uint16]0); $gw.Write([uint16]1); $gw.Write([uint16]$icons.Count)
foreach($ic in $icons){
    $gw.Write([byte]$ic.S); $gw.Write([byte]$ic.S); $gw.Write([byte]0); $gw.Write([byte]0)
    $gw.Write([uint16]1); $gw.Write([uint16]32)
    $gw.Write([uint32]$ic.Data.Length); $gw.Write([uint16]$ic.Id) }
$gw.Flush(); $grp=$gms.ToArray()

$dir=[System.IO.Path]::GetDirectoryName($OutPath)
if($dir -and -not(Test-Path $dir)){ New-Item -ItemType Directory -Force -Path $dir|Out-Null }
$out=New-Object System.IO.MemoryStream; $bw=New-Object System.IO.BinaryWriter($out)
Write-Res $bw 0 0 (New-Object byte[] 0) 0
foreach($ic in $icons){ Write-Res $bw 3 $ic.Id $ic.Data 0x1010 }
Write-Res $bw 14 1 $grp 0x1030
$bw.Flush()
[System.IO.File]::WriteAllBytes($OutPath,$out.ToArray())
Write-Host "Wrote $OutPath (.res: $($icons.Count) icons)"
