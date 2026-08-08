# make-canto-icon.ps1
#
# Renders the 粵 (U+7CB5) glyph to an .ico so the DLL can register it as the
# input-method profile icon (what the language indicator shows, like Bopomofo's
# ㄅ). Run at build time by CMake on the Windows runner; uses Windows
# PowerShell 5.1's System.Drawing.
param([Parameter(Mandatory = $true)][string]$OutPath)

Add-Type -AssemblyName System.Drawing

$size = 32
$bmp  = New-Object System.Drawing.Bitmap($size, $size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::Transparent)

# Prefer a Traditional Chinese face; System.Drawing substitutes if absent.
$font = New-Object System.Drawing.Font('Microsoft JhengHei', 24,
        [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment     = [System.Drawing.StringAlignment]::Center
$fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
$rect  = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
$glyph = [string][char]0x7CB5   # 粵

$g.DrawString($glyph, $font, [System.Drawing.Brushes]::Black, $rect, $fmt)
$g.Dispose()

$dir = [System.IO.Path]::GetDirectoryName($OutPath)
if ($dir -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

$hicon = $bmp.GetHicon()
$icon  = [System.Drawing.Icon]::FromHandle($hicon)
$fs = [System.IO.File]::Open($OutPath, [System.IO.FileMode]::Create)
$icon.Save($fs)
$fs.Close()
$icon.Dispose()
$bmp.Dispose()

Write-Host "Wrote $OutPath"
