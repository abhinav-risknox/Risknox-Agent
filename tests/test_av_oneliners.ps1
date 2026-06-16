# One-liner test commands for av_update and av_version
# Copy and paste these directly into PowerShell

# ─────────────────────────────────────────────────────────────────
# TEST 1: Query AV Database Version/Metadata
# ─────────────────────────────────────────────────────────────────
$cmdId='av-version-' + [guid]::NewGuid().ToString().Substring(0,8); $now=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'); $b=@{commandId=$cmdId;agentId='test-license-agent';verb='av_version';params=@{};timestamp=$now;_msgType='module_command'} | ConvertTo-Json -Compress; $h=@(82,80,76,83,1,0x70,0,0) + ([BitConverter]::GetBytes($b.Length) | %{[Array]::Reverse($_); $_}); $t=New-Object Net.Sockets.TcpClient('127.0.0.1',1514); $s=$t.GetStream(); $d=[Text.Encoding]::UTF8.GetBytes($b); $s.Write($h,0,$h.Length); $s.Write($d,0,$d.Length); $s.Close(); $t.Close(); Write-Host "Sent: MODULE_COMMAND verb=av_version (commandId: $cmdId)"

# ─────────────────────────────────────────────────────────────────
# TEST 2: Trigger Fresh ClamAV Database Update
# ─────────────────────────────────────────────────────────────────
$cmdId='av-update-' + [guid]::NewGuid().ToString().Substring(0,8); $now=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'); $b=@{commandId=$cmdId;agentId='test-license-agent';verb='av_update';params=@{};timestamp=$now;_msgType='module_command'} | ConvertTo-Json -Compress; $h=@(82,80,76,83,1,0x70,0,0) + ([BitConverter]::GetBytes($b.Length) | %{[Array]::Reverse($_); $_}); $t=New-Object Net.Sockets.TcpClient('127.0.0.1',1514); $s=$t.GetStream(); $d=[Text.Encoding]::UTF8.GetBytes($b); $s.Write($h,0,$h.Length); $s.Write($d,0,$d.Length); $s.Close(); $t.Close(); Write-Host "Sent: MODULE_COMMAND verb=av_update (commandId: $cmdId)"

# ─────────────────────────────────────────────────────────────────
# SCRIPT: More readable version with error handling
# ─────────────────────────────────────────────────────────────────
# PowerShell .\test_av_commands.ps1 -Verb av_version
# PowerShell .\test_av_commands.ps1 -Verb av_update
