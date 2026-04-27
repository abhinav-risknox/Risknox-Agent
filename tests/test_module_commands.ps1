# test_module_commands.ps1
# Sends TCP module control commands to the Manager ingest socket (127.0.0.1:1515).
# Tests all MODULE_COMMAND verbs introduced in the control framework.
#
# Usage:
#   .\tests\test_module_commands.ps1                   # all verbs
#   .\tests\test_module_commands.ps1 -Test diagnostics # single verb
#   .\tests\test_module_commands.ps1 -AgentId "my-agent" -Port 1515

param(
    [string]$Test        = "all",
    [string]$AgentId     = "test-license-agent",
    [string]$ManagerHost = "127.0.0.1",
    [int]$Port           = 1515,
    [switch]$Verbose
)

# ─── Helpers ──────────────────────────────────────────────────────────────────

function New-CommandId {
    param([string]$Verb)
    # Short deterministic ID: verb + epoch-ms
    $ts = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    return "$($AgentId.Substring(0, [Math]::Min(8,$AgentId.Length)))-$Verb-$ts"
}

function Send-Module {
    param(
        [string]$Description,
        [string]$Verb,
        [hashtable]$Params = @{},
        [string]$CommandId  = ""
    )

    if ($CommandId -eq "") { $CommandId = New-CommandId $Verb }

    $body = @{
        command_type = "module"
        agent_id     = $AgentId
        command_id   = $CommandId
        verb         = $Verb
        params       = $Params
    } | ConvertTo-Json -Compress -Depth 5

    Write-Host ""
    Write-Host "[$Description]" -ForegroundColor Cyan
    Write-Host "  Verb      : $Verb" -ForegroundColor DarkGray
    Write-Host "  CommandId : $CommandId" -ForegroundColor DarkGray
    if ($Verbose) {
        Write-Host "  Payload   : $body" -ForegroundColor DarkGray
    }

    try {
        $tcp    = New-Object System.Net.Sockets.TcpClient($ManagerHost, $Port)
        $stream = $tcp.GetStream()
        $bytes  = [System.Text.Encoding]::UTF8.GetBytes($body)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $stream.Close()
        $tcp.Close()
        Write-Host "  => Sent OK" -ForegroundColor Green
    }
    catch {
        Write-Host "  => FAILED: $_" -ForegroundColor Red
    }

    Start-Sleep -Milliseconds 600
}

function Send-Policy {
    param([string]$Description, [string]$Body)
    Write-Host ""
    Write-Host "[$Description]" -ForegroundColor Cyan
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
    }
    catch {
        Write-Host "  => FAILED: $_" -ForegroundColor Red
    }
    Start-Sleep -Milliseconds 600
}

# ─── Banner ───────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "=================================================" -ForegroundColor Yellow
Write-Host "  ResolutePulse  MODULE COMMAND  Test Suite" -ForegroundColor Yellow
Write-Host "  Target  : $ManagerHost`:$Port" -ForegroundColor Yellow
Write-Host "  AgentId : $AgentId" -ForegroundColor Yellow
Write-Host "  Filter  : $Test" -ForegroundColor Yellow
Write-Host "=================================================" -ForegroundColor Yellow
Write-Host ""
Write-Host "  All commands are routed via command_type='module'." -ForegroundColor DarkGray
Write-Host "  Watch the Manager log for MODULE_COMMAND dispatch," -ForegroundColor DarkGray
Write-Host "  and Agent log for MODULE_COMMAND_RESULT responses." -ForegroundColor DarkGray

# ─────────────────────────────────────────────────────────────────────────────
# 1. DIAGNOSTICS
#    Safe read-only probe — always run this first to confirm the channel works.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "diagnostics") {
    Write-Host "`n=== DIAGNOSTICS ===" -ForegroundColor Magenta

    Send-Module "Get live runtime counters" `
        -Verb "diagnostics"

    Write-Host "  >> Expected in Agent log:" -ForegroundColor DarkYellow
    Write-Host "       MODULE_COMMAND received: verb=diagnostics" -ForegroundColor DarkGray
    Write-Host "       MODULE_COMMAND_RESULT sent: verb=diagnostics status=success" -ForegroundColor DarkGray
    Write-Host "  >> Expected in Manager log:" -ForegroundColor DarkYellow
    Write-Host "       MODULE_COMMAND_RESULT: verb=diagnostics status=success" -ForegroundColor DarkGray
}

# ─────────────────────────────────────────────────────────────────────────────
# 2. STATUS REQUEST
#    Forces an immediate status.json flush + STATUS_REPORT over mTLS.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "status_request") {
    Write-Host "`n=== STATUS REQUEST ===" -ForegroundColor Magenta

    Send-Module "Force immediate status flush + STATUS_REPORT" `
        -Verb "status_request"

    Write-Host "  >> Expected in Manager log: 'Status report received: type=module_status'" -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# 3. COLLECTOR STOP / START
#    Pauses and resumes event collection + batch sender.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "collector") {
    Write-Host "`n=== COLLECTOR STOP / START ===" -ForegroundColor Magenta

    Send-Module "Stop event collection" -Verb "collector_stop"
    Write-Host "  >> Agent should pause event collection. No new events sent for ~5s." -ForegroundColor DarkYellow

    Start-Sleep -Seconds 3

    Send-Module "Resume event collection" -Verb "collector_start"
    Write-Host "  >> Agent should resume. Events flowing again." -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# 4. FIM STOP / START
#    Pauses and resumes File Integrity Monitoring.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "fim") {
    Write-Host "`n=== FIM STOP / START ===" -ForegroundColor Magenta

    Send-Module "Pause FIM monitoring" -Verb "fim_stop"
    Write-Host "  >> FIM watch should be suspended." -ForegroundColor DarkYellow

    Start-Sleep -Seconds 2

    Send-Module "Resume FIM monitoring" -Verb "fim_start"
    Write-Host "  >> FIM watching again." -ForegroundColor DarkYellow
}

# ─────────────────────────────────────────────────────────────────────────────
# 5. WORKER RESTART
#    Stops and re-spawns rp-webblock.exe + rp-softblock.exe.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "worker_restart") {
    Write-Host "`n=== WORKER RESTART ===" -ForegroundColor Magenta

    Write-Host "  Before:" -ForegroundColor DarkGray
    @("rp-webblock", "rp-softblock") | ForEach-Object {
        $p = Get-Process $_ -ErrorAction SilentlyContinue
        if ($p) { Write-Host "    [RUNNING] $_.exe  PID=$($p.Id)" -ForegroundColor Green }
        else     { Write-Host "    [STOPPED] $_.exe" -ForegroundColor Red }
    }

    Send-Module "Restart persistent worker subprocesses" -Verb "worker_restart"

    Start-Sleep -Seconds 3

    Write-Host "  After (new PIDs expected):" -ForegroundColor DarkGray
    @("rp-webblock", "rp-softblock") | ForEach-Object {
        $p = Get-Process $_ -ErrorAction SilentlyContinue
        if ($p) { Write-Host "    [RUNNING] $_.exe  PID=$($p.Id)" -ForegroundColor Green }
        else     { Write-Host "    [STOPPED] $_.exe - check agent.log" -ForegroundColor Red }
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# 6. CONFIG PUSH
#    Merges a new config section into config.json on the agent.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "config_push") {
    Write-Host "`n=== CONFIG PUSH ===" -ForegroundColor Magenta

    # 6a: Push a patch_management config change (disable auto_install)
    Send-Module "Disable patch auto-install" `
        -Verb "config_push" `
        -Params @{
            section = "patch_management"
            config  = @{
                enabled      = $true
                auto_scan    = $true
                auto_install = $false
                scan_interval_hours = 12
            }
        }
    Write-Host "  >> Check: Get-Content '$env:ProgramFiles\Risknox Pulse\config.json' | ConvertFrom-Json | Select -Expand patch_management" -ForegroundColor DarkYellow

    # 6b: Push a web_blocking config change (enable)
    Send-Module "Enable web blocking via config push" `
        -Verb "config_push" `
        -Params @{
            section = "web_blocking"
            config  = @{ enabled = $true }
        }

    # 6c: Push a software_blocking interval update
    Send-Module "Set software_blocking monitor interval to 500ms" `
        -Verb "config_push" `
        -Params @{
            section = "software_blocking"
            config  = @{ enabled = $true; monitor_interval_ms = 500 }
        }
}

# ─────────────────────────────────────────────────────────────────────────────
# 7. AGENT RESTART
#    Schedules a graceful restart. Run this last — agent will restart!
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "agent_restart") {
    Write-Host "`n=== AGENT RESTART ===" -ForegroundColor Magenta
    Write-Host "  WARNING: This will cause the agent service to restart!" -ForegroundColor Red

    $confirm = Read-Host "  Type YES to proceed"
    if ($confirm -ne "YES") {
        Write-Host "  Skipped." -ForegroundColor DarkGray
    } else {
        Send-Module "Graceful agent restart" -Verb "agent_restart"
        Write-Host "  >> Agent will exit its run() loop cleanly and restart." -ForegroundColor DarkYellow
        Write-Host "  >> Monitor with: Get-Service 'ResolutePulse' | Select Status" -ForegroundColor DarkGray
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# 8. OFFLINE QUEUE TEST
#    Send a module command for an agent that is NOT connected.
#    Should be stored in agent_commands with status='pending'.
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "offline") {
    Write-Host "`n=== OFFLINE QUEUE TEST ===" -ForegroundColor Magenta

    $offlineAgent = "offline-agent-does-not-exist"
    $cid = "offline-test-$(([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))"

    $body = @{
        command_type = "module"
        agent_id     = $offlineAgent
        command_id   = $cid
        verb         = "diagnostics"
        params       = @{}
    } | ConvertTo-Json -Compress

    Write-Host ""
    Write-Host "[Send MODULE_COMMAND to offline agent '$offlineAgent']" -ForegroundColor Cyan
    Write-Host "  CommandId : $cid" -ForegroundColor DarkGray

    try {
        $tcp    = New-Object System.Net.Sockets.TcpClient($ManagerHost, $Port)
        $stream = $tcp.GetStream()
        $bytes  = [System.Text.Encoding]::UTF8.GetBytes($body)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush(); $stream.Close(); $tcp.Close()
        Write-Host "  => Sent OK" -ForegroundColor Green
    }
    catch {
        Write-Host "  => FAILED: $_" -ForegroundColor Red
    }

    Write-Host "  >> Expected in Manager log: 'dispatchModuleCommand: agent $offlineAgent not connected - queuing'" -ForegroundColor DarkYellow
    Write-Host "  >> Verify DB row with command_class='module' and status='pending':" -ForegroundColor DarkYellow
    Write-Host "     psql -c `"SELECT id,agent_id,policy_type,status,command_class FROM agent_commands WHERE command_id='$cid'`"" -ForegroundColor DarkGray
}

# ─────────────────────────────────────────────────────────────────────────────
# 9. MIXED: Policy + Module in one run (regression: legacy path still works)
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "regression") {
    Write-Host "`n=== REGRESSION: Legacy policy still works alongside module commands ===" -ForegroundColor Magenta

    # Legacy policy command (no command_type field at all)
    Send-Policy "Legacy web_blocking block (no command_type field)" (@{
        agent_id    = $AgentId
        policy_type = "web_blocking"
        policy_data = @{ action = "block"; url = "test-regression.invalid" }
    } | ConvertTo-Json -Compress)

    Write-Host "  >> Check hosts file for 'test-regression.invalid'" -ForegroundColor DarkYellow

    # Immediately follow with a module command on the same socket
    Send-Module "Diagnostics right after policy (mixed interleave)" -Verb "diagnostics"

    Write-Host "  >> Both should succeed - policy via POLICY_UPDATE path, diagnostics via MODULE_COMMAND path" -ForegroundColor DarkYellow

    # Cleanup: unblock the test URL
    Send-Policy "Unblock regression test URL" (@{
        agent_id    = $AgentId
        policy_type = "web_blocking"
        policy_data = @{ action = "unblock"; url = "test-regression.invalid" }
    } | ConvertTo-Json -Compress)
}

# ─────────────────────────────────────────────────────────────────────────────
# 10. INVALID VERB (error path)
# ─────────────────────────────────────────────────────────────────────────────
if ($Test -in "all", "invalid") {
    Write-Host "`n=== INVALID VERB (error path) ===" -ForegroundColor Magenta

    Send-Module "Unknown verb should return status='unsupported'" `
        -Verb "this_verb_does_not_exist"

    Write-Host "  >> Expected in Agent log: `"ModuleController: unknown verb 'this_verb_does_not_exist'`"" -ForegroundColor DarkYellow
    Write-Host "  >> Expected result: status=unsupported in MODULE_COMMAND_RESULT" -ForegroundColor DarkYellow
}

# ─── Summary ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "=================================================" -ForegroundColor Yellow
Write-Host "  Test run complete." -ForegroundColor Yellow
Write-Host "  Agent log   : Get-Content '$env:ProgramFiles\Risknox Pulse\agent.log' -Tail 60" -ForegroundColor Yellow
Write-Host "  Manager log : check manager terminal / docker logs" -ForegroundColor Yellow
Write-Host "  DB audit    : psql -c `"SELECT id,agent_id,policy_type AS verb,status,ack_status,ack_at FROM agent_commands ORDER BY created_at DESC LIMIT 20`"" -ForegroundColor Yellow
Write-Host "=================================================" -ForegroundColor Yellow
