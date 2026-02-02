# Cleanup obsolete files from agent directory
# Run this after confirming you don't need these files

Write-Host "=== ResolutePulse Agent Cleanup ===" -ForegroundColor Cyan
Write-Host ""

$filesToDelete = @(
    # Old Python server (replaced by Fluent Bit)
    "mock_server.py",
    
    # Old log files
    "events.log",
    "events.json",
    "build_debug.log",
    "build_fim.log",
    "build_fim2.log",
    "build_fim3.log",
    
    # Old test files (HTTP era)
    "test-event.json",
    "test-direct.json",
    "test_tcp.cpp",
    
    # Old documentation (superseded)
    "python-fluent-setup.md",
    "filebeat.yml",
    "update-filebeat.ps1"
)

$deletedCount = 0
$notFoundCount = 0

foreach ($file in $filesToDelete) {
    if (Test-Path $file) {
        Write-Host "[DELETE] $file" -ForegroundColor Yellow
        Remove-Item $file -Force
        $deletedCount++
    } else {
        Write-Host "[SKIP] $file (not found)" -ForegroundColor Gray
        $notFoundCount++
    }
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Deleted: $deletedCount files" -ForegroundColor Green
Write-Host "Not found: $notFoundCount files" -ForegroundColor Gray

# Optional: Clean build artifacts
Write-Host ""
$cleanBuild = Read-Host "Clean build directory? (y/N)"
if ($cleanBuild -eq 'y' -or $cleanBuild -eq 'Y') {
    if (Test-Path "build") {
        Write-Host "[DELETE] build/ directory" -ForegroundColor Yellow
        Remove-Item -Recurse -Force build
        Write-Host "Build directory cleaned" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Cleanup complete!" -ForegroundColor Green
