# test_all_policies.ps1
# Sends TCP policy commands to the Manager (port 1515) to test all policy types.
# Run from the Agent directory: .\tests\test_all_policies.ps1
#
# Usage:
#   .\tests\test_all_policies.ps1              # runs all tests
#   .\tests\test_all_policies.ps1 -Test web    # runs only web blocking tests

param(
    [string]$Test = "all",
    [string]$AgentId = "test-license-agent",
    [string]$ManagerHost = "127.0.0.1",
    [int]$Port = 1515
)

function Send-Policy {
    param([string]$Description, [string]$Body)

    Write-Host "`n[$Description]" -ForegroundColor Cyan
    Write-Host "  Payload: $Body" -ForegroundColor DarkGray

    try {
        $tcp    = New-Object System.Net.Sockets.TcpClient($ManagerHost, $Port)
        $stream = $tcp.GetStream()
        $bytes  = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $stream.Close()
        $tcp.Close()
        Write-Host "  => Sent OK" -ForegroundColor Green
    } catch {
        Write-Host "  => FAILED: $_" -ForegroundColor Red
    }

    Start-Sleep -Milliseconds 800
}

Write-Host "============================================" -ForegroundColor Yellow
Write-Host " ResolutePulse Policy Test Suite" -ForegroundColor Yellow
Write-Host " Target : $ManagerHost`:$Port" -ForegroundColor Yellow
Write-Host " AgentId: $AgentId" -ForegroundColor Yellow
Write-Host "============================================" -ForegroundColor Yellow

# ─────────────────────────────────────────────────────────────────────────────
# WEB BLOCKING
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "web") {
    Write-Host "`n=== WEB BLOCKING ===" -ForegroundColor Magenta

    Send-Policy "Block facebook.com" (@{
        agent_id    = $AgentId
        policy_type = "web_blocking"
        policy_data = @{ action = "block"; url = "facebook.com" }
    } | ConvertTo-Json -Compress)

    Send-Policy "Block amazon.com" (@{
        agent_id    = $AgentId
        policy_type = "web_blocking"
        policy_data = @{ action = "block"; url = "amazon.com" }
    } | ConvertTo-Json -Compress)

    Send-Policy "Unblock facebook.com" (@{
        agent_id    = $AgentId
        policy_type = "web_blocking"
        policy_data = @{ action = "unblock"; url = "facebook.com" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Check hosts file: Get-Content C:\Windows\System32\drivers\etc\hosts | Select-String ResolutePulse" -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# SOFTWARE BLOCKING
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "software") {
    Write-Host "`n=== SOFTWARE BLOCKING ===" -ForegroundColor Magenta

    Send-Policy "Block Notepad" (@{
        agent_id    = $AgentId
        policy_type = "software_blocking"
        policy_data = @{ action = "block"; name = "Notepad"; executable = "notepad.exe" }
    } | ConvertTo-Json -Compress)

    Send-Policy "Block Calculator" (@{
        agent_id    = $AgentId
        policy_type = "software_blocking"
        policy_data = @{ action = "block"; name = "Calculator"; executable = "calc.exe" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Try opening notepad.exe - it should be blocked." -ForegroundColor DarkYellow
    Start-Sleep -Seconds 3

    Send-Policy "Unblock Notepad" (@{
        agent_id    = $AgentId
        policy_type = "software_blocking"
        policy_data = @{ action = "unblock"; name = "Notepad"; executable = "notepad.exe" }
    } | ConvertTo-Json -Compress)

    Send-Policy "Unblock Calculator" (@{
        agent_id    = $AgentId
        policy_type = "software_blocking"
        policy_data = @{ action = "unblock"; name = "Calculator"; executable = "calc.exe" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Check registry: reg query `"HKLM\SOFTWARE\Policies\Microsoft\Windows\Explorer\DisallowRun`"" -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# PATCH MANAGEMENT
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "patch") {
    Write-Host "`n=== PATCH MANAGEMENT ===" -ForegroundColor Magenta

    Send-Policy "Trigger patch scan (on-demand)" (@{
        agent_id    = $AgentId
        policy_type = "patch"
        policy_data = @{ action = "scan" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Patch scan runs in rp-patch.exe subprocess. Check agent.log for results:" -ForegroundColor DarkYellow
    Write-Host "     Get-Content 'C:\Program Files\Risknox Pulse\agent.log' -Tail 30 | Select-String patch" -ForegroundColor DarkGray
    Write-Host "  >> After scan, copy an UpdateID from the log and run the install test manually:" -ForegroundColor DarkYellow
    Write-Host @"
     `$body = '{"agent_id":"$AgentId","policy_type":"patch","policy_data":{"action":"install","update_ids":["PASTE-UPDATE-ID-HERE"]}}'
     `$tcp = New-Object System.Net.Sockets.TcpClient("$ManagerHost", $Port)
     `$stream = `$tcp.GetStream()
     `$bytes = [System.Text.Encoding]::UTF8.GetBytes(`$body)
     `$stream.Write(`$bytes, 0, `$bytes.Length)
     `$stream.Close(); `$tcp.Close()
"@ -ForegroundColor DarkGray
}

# ─────────────────────────────────────────────────────────────────────────────
# ANTIVIRUS SCAN
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "av") {
    Write-Host "`n=== ANTIVIRUS SCAN ===" -ForegroundColor Magenta

    Send-Policy "Quick scan (C:\Users)" (@{
        agent_id    = $AgentId
        policy_type = "antivirus"
        policy_data = @{ action = "quick_scan"; path = "C:\Users" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> AV scan runs in rp-antivirus.exe subprocess (ClamAV). Check agent.log:" -ForegroundColor DarkYellow
    Write-Host "     Get-Content 'C:\Program Files\Risknox Pulse\agent.log' -Tail 30 | Select-String -Pattern 'av|clam|antivirus'" -ForegroundColor DarkGray
    Write-Host "  >> Full system scan command (slow!):" -ForegroundColor DarkYellow
    Write-Host @"
     `$body = '{"agent_id":"$AgentId","policy_type":"antivirus","policy_data":{"action":"full_scan","path":"C:\\"}}'
     `$tcp = New-Object System.Net.Sockets.TcpClient("$ManagerHost", $Port)
     `$stream = `$tcp.GetStream()
     `$bytes = [System.Text.Encoding]::UTF8.GetBytes(`$body)
     `$stream.Write(`$bytes, 0, `$bytes.Length)
     `$stream.Close(); `$tcp.Close()
"@ -ForegroundColor DarkGray
}

# ─────────────────────────────────────────────────────────────────────────────
# STATUS REQUEST
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "status") {
    Write-Host "`n=== STATUS REQUEST ===" -ForegroundColor Magenta

    Send-Policy "Request module status" (@{
        agent_id    = $AgentId
        policy_type = "status_request"
        policy_data = @{}
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Agent will query each worker subprocess and send a status report to the Manager." -ForegroundColor DarkYellow
    Write-Host "  >> Check Manager terminal output for 'STATUS_REPORT received'." -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# VERIFY SUBPROCESS STATE
# ─────────────────────────────────────────────────────────────────────────────
Write-Host "`n=== VERIFY SUBPROCESSES ARE RUNNING ===" -ForegroundColor Magenta
Write-Host "  Checking for rp-webblock.exe and rp-softblock.exe in Task Manager..." -ForegroundColor Cyan

$workers = @("rp-webblock", "rp-softblock")
foreach ($w in $workers) {
    $proc = Get-Process -Name $w -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "  [OK] $w.exe  PID=$($proc.Id)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $w.exe not found - check agent.log for spawn errors" -ForegroundColor Red
    }
}

Write-Host "`n============================================" -ForegroundColor Yellow
Write-Host " Test run complete." -ForegroundColor Yellow
Write-Host " Log file: Get-Content 'C:\Program Files\Risknox Pulse\agent.log' -Tail 50" -ForegroundColor Yellow
Write-Host "============================================" -ForegroundColor Yellow
