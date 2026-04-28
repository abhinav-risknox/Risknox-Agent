# Test script for new AV MODULE_COMMAND verbs: av_update and av_version
# Sends properly formatted MODULE_COMMAND messages to the agent

param(
    [string]$Host = "127.0.0.1",
    [int]$Port = 1514,
    [string]$AgentId = "test-license-agent",
    [string]$Verb = "av_version"  # av_version or av_update
)

function New-ModuleCommand {
    param(
        [string]$CommandId = [guid]::NewGuid().ToString(),
        [string]$AgentId = "test-license-agent",
        [string]$Verb = "av_version",
        [hashtable]$Params = @{}
    )

    $now = [System.DateTime]::UtcNow
    $timestamp = $now.ToString("yyyy-MM-ddTHH:mm:ssZ")

    # Build the MODULE_COMMAND JSON payload
    $cmd = @{
        commandId = $CommandId
        agentId = $AgentId
        verb = $Verb
        params = $Params
        timestamp = $timestamp
        _msgType = "module_command"
    }

    $cmdJson = $cmd | ConvertTo-Json -Compress
    return $cmdJson
}

function New-MessageHeader {
    param(
        [byte]$MessageType = 0x70,  # MODULE_COMMAND
        [int]$PayloadLength = 0
    )

    $header = New-Object byte[] 12
    
    # Magic: RPLS
    $header[0] = [byte]'R'
    $header[1] = [byte]'P'
    $header[2] = [byte]'L'
    $header[3] = [byte]'S'
    
    # Version
    $header[4] = 1
    
    # Type (0x70 = MODULE_COMMAND)
    $header[5] = $MessageType
    
    # Reserved (big-endian)
    $header[6] = 0
    $header[7] = 0
    
    # Payload length (big-endian)
    $payloadLenBytes = [BitConverter]::GetBytes([int]$PayloadLength)
    if ([BitConverter]::IsLittleEndian) {
        [Array]::Reverse($payloadLenBytes)
    }
    $header[8] = $payloadLenBytes[0]
    $header[9] = $payloadLenBytes[1]
    $header[10] = $payloadLenBytes[2]
    $header[11] = $payloadLenBytes[3]
    
    return $header
}

function Send-ModuleCommand {
    param(
        [string]$Host = "127.0.0.1",
        [int]$Port = 1514,
        [string]$CommandJson,
        [int]$TimeoutSec = 10
    )

    try {
        $client = New-Object System.Net.Sockets.TcpClient
        $client.SendTimeout = $TimeoutSec * 1000
        $client.ReceiveTimeout = $TimeoutSec * 1000
        
        Write-Host "Connecting to $Host`:$Port..." -ForegroundColor Cyan
        $client.Connect($Host, $Port)
        
        $stream = $client.GetStream()
        
        # Encode JSON payload
        $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes($CommandJson)
        $payloadLength = $payloadBytes.Length
        
        # Build header
        $header = New-MessageHeader -MessageType 0x70 -PayloadLength $payloadLength
        
        # Send header + payload
        Write-Host "Sending MESSAGE_HEADER (12 bytes) + payload ($payloadLength bytes)..." -ForegroundColor Cyan
        $stream.Write($header, 0, $header.Length)
        $stream.Write($payloadBytes, 0, $payloadBytes.Length)
        $stream.Flush()
        
        Write-Host "Message sent successfully!" -ForegroundColor Green
        Write-Host "Payload: $CommandJson" -ForegroundColor Gray
        
        # Try to read response (non-blocking)
        if ($stream.DataAvailable) {
            $buffer = New-Object byte[] 1024
            $bytesRead = $stream.Read($buffer, 0, $buffer.Length)
            $response = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $bytesRead)
            Write-Host "Response: $response" -ForegroundColor Yellow
        }
        
        $stream.Close()
        $client.Close()
        
    } catch {
        Write-Host "Error: $_" -ForegroundColor Red
        Write-Host "Note: Ensure Agent is running and listening on $Host`:$Port" -ForegroundColor Yellow
    }
}

# ─────────────────────────────────────────────────────────────────
# Main: Send the requested command
# ─────────────────────────────────────────────────────────────────

$cmdId = [guid]::NewGuid().ToString()

if ($Verb -eq "av_update") {
    Write-Host "`n=== TEST: Force ClamAV Database Update ===" -ForegroundColor Cyan
    $cmd = New-ModuleCommand -CommandId $cmdId -AgentId $AgentId -Verb "av_update" -Params @{}
    Send-ModuleCommand -Host $Host -Port $Port -CommandJson $cmd -TimeoutSec 600
    Write-Host "Monitor Manager logs for MODULE_COMMAND_RESULT with verb=av_update`n" -ForegroundColor Cyan
    
} elseif ($Verb -eq "av_version") {
    Write-Host "`n=== TEST: Query ClamAV Database Metadata ===" -ForegroundColor Cyan
    $cmd = New-ModuleCommand -CommandId $cmdId -AgentId $AgentId -Verb "av_version" -Params @{}
    Send-ModuleCommand -Host $Host -Port $Port -CommandJson $cmd -TimeoutSec 30
    Write-Host "Expect response with database file metadata (sizes, timestamps, etc.)`n" -ForegroundColor Cyan
    
} else {
    Write-Host "Unknown verb: $Verb. Use 'av_update' or 'av_version'" -ForegroundColor Red
}

Write-Host "Command ID for audit trail: $cmdId" -ForegroundColor Gray
