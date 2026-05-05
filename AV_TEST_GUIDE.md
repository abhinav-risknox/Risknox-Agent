# Test Guide: AV Database Management Commands

## Prerequisites
- Agent must be built and running: `ResolutePulse.exe`
- Manager must be running to receive MODULE_COMMAND_RESULT responses
- Ensure port 1514 is open on localhost (default manager port)

## Quick Test Commands

### Option 1: Full Script (Recommended)
```powershell
# Query database version/metadata
.\test_av_commands.ps1 -Verb av_version -Host 127.0.0.1 -Port 1514 -AgentId test-license-agent

# Trigger database update
.\test_av_commands.ps1 -Verb av_update -Host 127.0.0.1 -Port 1514 -AgentId test-license-agent
```

### Option 2: Copy-Paste One-Liners

#### Query AV Database Metadata (av_version):
```powershell
$cmdId='av-version-' + [guid]::NewGuid().ToString().Substring(0,8); $now=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'); $b=@{commandId=$cmdId;agentId='test-license-agent';verb='av_version';params=@{};timestamp=$now;_msgType='module_command'} | ConvertTo-Json -Compress; $h=@(82,80,76,83,1,0x70,0,0) + ([BitConverter]::GetBytes($b.Length) | %{[Array]::Reverse($_); $_}); $t=New-Object Net.Sockets.TcpClient('127.0.0.1',1514); $s=$t.GetStream(); $d=[Text.Encoding]::UTF8.GetBytes($b); $s.Write($h,0,$h.Length); $s.Write($d,0,$d.Length); $s.Close(); $t.Close(); Write-Host "Sent: MODULE_COMMAND verb=av_version (commandId: $cmdId)"
```

#### Trigger ClamAV Update (av_update):
```powershell
$cmdId='av-update-' + [guid]::NewGuid().ToString().Substring(0,8); $now=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'); $b=@{commandId=$cmdId;agentId='test-license-agent';verb='av_update';params=@{};timestamp=$now;_msgType='module_command'} | ConvertTo-Json -Compress; $h=@(82,80,76,83,1,0x70,0,0) + ([BitConverter]::GetBytes($b.Length) | %{[Array]::Reverse($_); $_}); $t=New-Object Net.Sockets.TcpClient('127.0.0.1',1514); $s=$t.GetStream(); $d=[Text.Encoding]::UTF8.GetBytes($b); $s.Write($h,0,$h.Length); $s.Write($d,0,$d.Length); $s.Close(); $t.Close(); Write-Host "Sent: MODULE_COMMAND verb=av_update (commandId: $cmdId)"
```

## Expected Behavior

### av_version (Query Database Metadata)
**What it does:**
- Spawns rp-antivirus.exe on-demand
- Sends `{"action":"database_info"}` over named pipe
- Inspects ClamAV database directory for .cvd and .cld files
- Returns per-file metadata: sizes (bytes + MB), modification timestamps (epoch + ISO 8601)
- Calculates totals: total size, latest modified timestamp

**Response payload in MODULE_COMMAND_RESULT.output:**
```json
{
  "type": "complete",
  "action": "database_info",
  "success": true,
  "database": {
    "databaseDir": "C:\\...\\vendor\\clamav\\database",
    "files": [
      {
        "name": "main.cvd",
        "path": "C:\\...\\main.cvd",
        "sizeBytes": 123456789,
        "sizeMb": 117.74,
        "lastModifiedEpoch": 1777200000,
        "lastModified": "2026-04-26T10:20:00Z"
      },
      ...
    ],
    "filesFound": 4,
    "filesExpected": 6,
    "totalSizeBytes": 234567890,
    "totalSizeMb": 223.74,
    "latestModifiedEpoch": 1777286400,
    "latestModified": "2026-04-27T10:20:00Z"
  }
}
```

### av_update (Trigger Database Update)
**What it does:**
- Spawns rp-antivirus.exe on-demand
- Sends `{"action":"update_definitions"}` over named pipe
- Runs freshclam.exe to update ClamAV virus signatures
- Waits up to 10 minutes for freshclam to complete
- Returns success/failure status

**Response payload in MODULE_COMMAND_RESULT.output:**
```json
{
  "type": "complete",
  "action": "update_definitions",
  "success": true
}
```

## Monitoring Results

1. **Check Agent Logs:**
   ```
   c:\Users\User\Desktop\Agent\build\agent.log (if enabled)
   ```

2. **Check Manager Audit Trail:**
   Look for entries with:
   - `verb: "av_update"` or `verb: "av_version"`
   - `status: "success"` or `status: "failed"`
   - `commandId` matching what was sent

3. **Via Named Pipe:**
   If you want to test the pipe directly (without MODULE_COMMAND wrapper):
   ```powershell
   # This would require launching rp-antivirus.exe manually with a custom pipe listener
   ```

## Payload Flow (av_version example)

```
Manager Console
     ↓ (sends MODULE_COMMAND)
Agent (main thread)
     ↓ (ModuleController.handleAvVersion)
ModuleController
     ↓ (runAntivirusAction)
WorkerManager
     ↓ (spawnWorker + streamEvents)
rp-antivirus.exe (on-demand)
     ↓ (receives action: "database_info" over named pipe)
AntivirusWorker::getDatabaseInfo()
     ↓ (inspects std::filesystem, collects metadata)
Named Pipe
     ↓ (sends back complete event with database info)
Agent Management Loop
     ↓ (serializes to MODULE_COMMAND_RESULT)
TLS Sender
     ↓ (sends to Manager via mTLS)
Manager
     ↓ (stores in MODULE_COMMAND audit trail)
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "cannot connect" | Agent not running | Run ResolutePulse.exe |
| "Permission denied" | Port 1514 in use or firewall | Check netstat, allow port 1514 |
| Timeout (no response) | Manager not connected | Ensure Manager.exe is running |
| av_update takes >10 min | freshclam blocked/slow | Check freshclam logs in clamav/ dir |
| av_version returns error | Database dir missing | Verify vendor/clamav/database exists |

## Files Modified for This Feature

- [src/antivirus/AntivirusWorker.h](src/antivirus/AntivirusWorker.h) - Added getDatabaseInfo()
- [src/antivirus/AntivirusWorker.cpp](src/antivirus/AntivirusWorker.cpp) - Implemented database inspection + new database_info action
- [src/policy/ModuleController.h](src/policy/ModuleController.h) - Added av_update + av_version verb handlers
