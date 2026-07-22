param(
    [string]$Output = "build/RisknoxPulse_Setup_v1.0.0.msi"
)

# Ensure WiX is installed
if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    Write-Host "WiX Toolset v4 is not installed. Installing..." -ForegroundColor Yellow
    dotnet tool install --global wix --version 4.0.5
}

Write-Host "Building MSI package using WiX..." -ForegroundColor Cyan
wix build installer.wxs -o $Output

if ($LASTEXITCODE -eq 0) {
    Write-Host "Successfully built MSI: $Output" -ForegroundColor Green
} else {
    Write-Host "Failed to build MSI." -ForegroundColor Red
}
