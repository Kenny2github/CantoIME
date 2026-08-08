# make-canto-icon.ps1
#
# Renders the 粵 (U+7CB5) glyph to a multi-size .ico so the DLL can register it
# as the input-method profile icon (what the language indicator shows, like
# Bopomofo's ㄅ). The glyph fills the canvas at each size so it stays readable
# when the indicator scales it down. Run at build time by CMake on the Windows
# runner; uses Windows PowerShell 5.1's System.Drawing. Entries are PNG-encoded
# (supported by Windows Vista+).
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

function Render-Png([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size,
           [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    # Font em close to the full box: a CJK glyph then fills ~90% of the icon.
    $fs = [math]::Max(1, [math]::Round($size * 0.92))
    $font = New-Object System.Drawing.Font('Microsoft JhengHei', $fs,
            [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment     = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $fmt.FormatFlags   = [System.Drawing.StringFormatFlags]::NoClip
    $rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)

    $g.DrawString([string][char]0x7CB5, $font, [System.Drawing.Brushes]::Black, $rect, $fmt)
    $g.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return ,$stream.ToArray()
}

$sizes = 16, 20, 24, 32, 48
$images = @()
foreach ($s in $sizes) { $images += ,(Render-Png $s) }

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

# Assemble the .ico (ICONDIR + ICONDIRENTRYs + PNG payloads).
$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter($out)
$bw.Write([UInt16]0)              # reserved
$bw.Write([UInt16]1)              # type = icon
$bw.Write([UInt16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $data = $images[$i]
    $dim = [Byte]($(if ($s -ge 256) { 0 } else { $s }))
    $bw.Write($dim); $bw.Write($dim)  # width, height
    $bw.Write([Byte]0)                # color count
    $bw.Write([Byte]0)                # reserved
    $bw.Write([UInt16]1)              # planes
    $bw.Write([UInt16]32)             # bit count
    $bw.Write([UInt32]$data.Length)   # bytes in resource
    $bw.Write([UInt32]$offset)        # image offset
    $offset += $data.Length
}
foreach ($data in $images) { $bw.Write($data) }
$bw.Flush()

[System.IO.File]::WriteAllBytes($OutPath, $out.ToArray())
Write-Host "Wrote $OutPath ($($sizes.Count) sizes)"
