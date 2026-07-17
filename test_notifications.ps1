# ============================================================
#  Notification Pipeline Test Script
#  Run from the project root.
#  Usage:  .\test_notifications.ps1 [-BuildDir "C:\...\build"] [-Step 1|2|3|4]
# ============================================================
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "build"),
    [int]$Step = 0
)

$notifier = Join-Path $BuildDir "ThreatNotification.exe"
$avExe    = Join-Path $BuildDir "rp-antivirus.exe"
$pipeName = "rp-antivirus-test-$([guid]::NewGuid().ToString('N').Substring(0,8))"

function Banner($msg) {
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  $msg" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan
}

# ─────────────────────────────────────────────────────────────
# STEP 1 - Does ThreatNotification.exe exist?
# ─────────────────────────────────────────────────────────────
Banner "STEP 1 - Check ThreatNotification.exe exists"
if (Test-Path $notifier) {
    Write-Host "  FOUND: $notifier" -ForegroundColor Green
} else {
    Write-Host "  MISSING: $notifier" -ForegroundColor Red
    Write-Host "  FIX: Build ThreatNotification (dotnet publish) and copy .exe to build\" -ForegroundColor Yellow
    exit 1
}

if ($Step -eq 1) { exit 0 }
Write-Host "`n[PRESS ENTER to run STEP 2 - clean-scan popup]" -ForegroundColor Yellow
Read-Host | Out-Null

# ─────────────────────────────────────────────────────────────
# STEP 2 - Fire the clean-scan (safe-mode) popup directly
#          This is what shows after a USB/download scan with NO threats
# ─────────────────────────────────────────────────────────────
Banner "STEP 2 - Clean-scan popup (--mode safe)"
Write-Host "  Launching ThreatNotification.exe in safe mode..."
Write-Host "  --> You should see a GREEN 'Download Verified Safe' toast bottom-right"

$proc = Start-Process -FilePath $notifier `
    -ArgumentList @('--mode', 'safe', '--file', 'test_document.pdf', '--timeout', '8') `
    -PassThru -WindowStyle Normal

$proc.WaitForExit(12000) | Out-Null
$color = if ($proc.ExitCode -eq 3) { "Green" } else { "Yellow" }
Write-Host "  Exit code: $($proc.ExitCode)  (3 = DISMISSED/TIMEOUT is expected)" -ForegroundColor $color

if ($Step -eq 2) { exit 0 }
Write-Host "`n[PRESS ENTER to run STEP 3 - threat popup]" -ForegroundColor Yellow
Read-Host | Out-Null

# ─────────────────────────────────────────────────────────────
# STEP 3 - Fire a threat popup directly
#          This is what shows when a virus is detected
# ─────────────────────────────────────────────────────────────
Banner "STEP 3 - Threat-detected popup (--mode threat)"
Write-Host "  Launching ThreatNotification.exe in threat mode..."
Write-Host "  --> You should see a RED 'Threat Blocked' toast bottom-right"

$proc = Start-Process -FilePath $notifier `
    -ArgumentList @('--file', 'invoice.exe', '--threat', 'Win.Test.EICAR', '--path', 'C:\Users\Test\Downloads\invoice.exe', '--severity', 'high', '--timeout', '10') `
    -PassThru -WindowStyle Normal

$proc.WaitForExit(15000) | Out-Null
Write-Host "  Exit code: $($proc.ExitCode)  (0=Quarantine, 1=Ignore, 3=Dismissed, 4=AutoQuarantine)" -ForegroundColor Cyan

if ($Step -eq 3) { exit 0 }
Write-Host "`n[PRESS ENTER to run STEP 4 - full pipe scan test]" -ForegroundColor Yellow
Read-Host | Out-Null

# ─────────────────────────────────────────────────────────────
# STEP 4 - Directly talk to rp-antivirus.exe over a named pipe
#          Simulates exactly what PolicyManager does for USB/download scans
# ─────────────────────────────────────────────────────────────
Banner "STEP 4 - Direct rp-antivirus.exe pipe test"

if (-not (Test-Path $avExe)) {
    Write-Host "  MISSING: $avExe" -ForegroundColor Red
    exit 1
}

$scanPath = $env:TEMP
Write-Host "  Spawning: $avExe --pipe $pipeName"
Write-Host "  Scan path: $scanPath"
Write-Host "  Waiting for pipe server..."

$avProc = Start-Process -FilePath $avExe `
    -ArgumentList @('--pipe', $pipeName) `
    -PassThru -WindowStyle Hidden

Start-Sleep -Milliseconds 1500

$pipeClient = New-Object System.IO.Pipes.NamedPipeClientStream(".", $pipeName,
    [System.IO.Pipes.PipeDirection]::InOut,
    [System.IO.Pipes.PipeOptions]::None)

try {
    $pipeClient.Connect(5000)
    Write-Host "  Connected to pipe." -ForegroundColor Green

    $cmd      = @{ action = "quick_scan"; path = $scanPath } | ConvertTo-Json -Compress
    $cmdBytes = [System.Text.Encoding]::UTF8.GetBytes($cmd)
    $lenBytes = [BitConverter]::GetBytes([uint32]$cmdBytes.Length)
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($lenBytes) }

    $pipeClient.Write($lenBytes, 0, 4)
    $pipeClient.Write($cmdBytes, 0, $cmdBytes.Length)
    $pipeClient.Flush()

    Write-Host "  Command sent. Reading events (may take 30-60s)..."
    Write-Host "  --> Watch for a GREEN or RED toast popup bottom-right"

    $reader  = New-Object System.IO.BinaryReader($pipeClient)
    $timeout = [System.Diagnostics.Stopwatch]::StartNew()

    while ($timeout.Elapsed.TotalSeconds -lt 180) {
        try {
            $lenBuf = $reader.ReadBytes(4)
            if ($lenBuf.Length -lt 4) { break }
            if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($lenBuf) }
            $msgLen = [BitConverter]::ToUInt32($lenBuf, 0)
            $msgBuf = $reader.ReadBytes($msgLen)
            $event  = [System.Text.Encoding]::UTF8.GetString($msgBuf) | ConvertFrom-Json

            $type  = $event.type
            $color = switch ($type) {
                "threat"   { "Red"   }
                "complete" { "Green" }
                "error"    { "Red"   }
                "progress" { "Gray"  }
                default    { "White" }
            }
            Write-Host "  EVENT [$type]: $($event | ConvertTo-Json -Compress)" -ForegroundColor $color

            if ($type -eq "complete" -or $type -eq "error") { break }
        } catch {
            break
        }
    }
} catch {
    Write-Host "  ERROR connecting to pipe: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "  POSSIBLE CAUSES:" -ForegroundColor Yellow
    Write-Host "    - rp-antivirus.exe crashed immediately (missing ClamAV DB or certs dir)" -ForegroundColor Yellow
    Write-Host "    - Check: $BuildDir\clamav\database exists" -ForegroundColor Yellow
    Write-Host "    - Check: $BuildDir\clamav\certs exists" -ForegroundColor Yellow
} finally {
    $pipeClient.Dispose()
    if (-not $avProc.HasExited) { $avProc.Kill() }
}

Banner "DONE"
Write-Host @"

  RESULTS SUMMARY:
    Step 2 (clean popup)  - should have shown GREEN toast
    Step 3 (threat popup) - should have shown RED toast
    Step 4 (full pipe)    - should have shown GREEN toast after scan

  LOG FILES:
    AV scan log:   C:\ProgramData\Risknox Pulse\antivirus\clamscan.log
    Threat log:    C:\ProgramData\Risknox Pulse\logs\threat_actions.log

  IF Step 2/3 worked but Step 4 did NOT show popup:
    - rp-antivirus.exe is not finding ThreatNotification.exe next to itself
    - Make sure ThreatNotification.exe is in the same folder as rp-antivirus.exe ($BuildDir)

  IF Step 2/3 did NOT show popup:
    - ThreatNotification.exe itself has a startup error
    - Try running it manually: & '$notifier' --mode safe --file test.pdf --timeout 8

"@ -ForegroundColor Cyan
