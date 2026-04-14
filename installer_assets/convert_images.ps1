# Risknox Pulse - Installer Asset Generation Script
# Converts brand logo PNGs to BMP & ICO formats required by Inno Setup

Add-Type -AssemblyName System.Drawing

$agentDir = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $agentDir
$assetDir = $PSScriptRoot

# --- Source images ---
$verticalLogo = Join-Path $agentDir "Vertical-Logo_White-scaled-1324x1536.png"
$smallIcon    = Join-Path "$projectRoot\frontend\src\assets\images" "logo-sm.png"

# Verify source files exist
foreach ($src in @($verticalLogo, $smallIcon)) {
    if (-not (Test-Path $src)) {
        Write-Error "Source image not found: $src"
        exit 1
    }
}

# ----- Wizard Image (410x797 BMP, high-res for modern wizard) -----
# Large sidebar image for Inno Setup's modern wizard style.
# Uses the vertical Risknox brand logo centered on a dark navy background.
$wizWidth  = 410
$wizHeight = 797
$bgColor   = [System.Drawing.Color]::FromArgb(26, 26, 46)  # #1A1A2E

$wizBmp = New-Object System.Drawing.Bitmap($wizWidth, $wizHeight)
$g = [System.Drawing.Graphics]::FromImage($wizBmp)
$g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.Clear($bgColor)

# Draw the vertical logo centered, filling about 60% of the height
$srcImg = [System.Drawing.Image]::FromFile($verticalLogo)
$targetH = [int]($wizHeight * 0.55)
$scale = $targetH / $srcImg.Height
$drawW = [int]($srcImg.Width * $scale)
$drawH = $targetH
# Cap width to image width minus padding
if ($drawW -gt ($wizWidth - 60)) {
    $drawW = $wizWidth - 60
    $scale = $drawW / $srcImg.Width
    $drawH = [int]($srcImg.Height * $scale)
}
$drawX = [int](($wizWidth - $drawW) / 2)
$drawY = [int](($wizHeight - $drawH) / 2)
$g.DrawImage($srcImg, $drawX, $drawY, $drawW, $drawH)

$srcImg.Dispose()
$g.Dispose()
$wizBmp.Save("$assetDir\wizard_image.bmp", [System.Drawing.Imaging.ImageFormat]::Bmp)
$wizBmp.Dispose()
Write-Host "Wizard image (${wizWidth}x${wizHeight}) created"

# ----- Small Wizard Image (55x58 BMP) -----
$smWidth  = 55
$smHeight = 58

$smBmp = New-Object System.Drawing.Bitmap($smWidth, $smHeight)
$g = [System.Drawing.Graphics]::FromImage($smBmp)
$g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.Clear([System.Drawing.Color]::White)

$srcImg = [System.Drawing.Image]::FromFile($smallIcon)
$padding = 4
$maxW = $smWidth - ($padding * 2)
$maxH = $smHeight - ($padding * 2)
$scale = [Math]::Min($maxW / $srcImg.Width, $maxH / $srcImg.Height)
$drawW = [int]($srcImg.Width * $scale)
$drawH = [int]($srcImg.Height * $scale)
$drawX = [int](($smWidth - $drawW) / 2)
$drawY = [int](($smHeight - $drawH) / 2)
$g.DrawImage($srcImg, $drawX, $drawY, $drawW, $drawH)

$srcImg.Dispose()
$g.Dispose()
$smBmp.Save("$assetDir\small_wizard_image.bmp", [System.Drawing.Imaging.ImageFormat]::Bmp)
$smBmp.Dispose()
Write-Host "Small wizard image (${smWidth}x${smHeight}) created"

# ----- Application Icon (ICO) -----
$srcImg = [System.Drawing.Image]::FromFile($smallIcon)
$icon256 = New-Object System.Drawing.Bitmap($srcImg, 256, 256)
$iconHandle = $icon256.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($iconHandle)
$fs = [System.IO.File]::Create("$assetDir\risknox.ico")
$icon.Save($fs)
$fs.Close()
$icon.Dispose()
$icon256.Dispose()
$srcImg.Dispose()
Write-Host "Icon file (risknox.ico) created"

Write-Host ""
Write-Host "All Risknox Pulse installer assets generated successfully!" -ForegroundColor Green
