#Requires -RunAsAdministrator

<#
.SYNOPSIS
    Generates an extensive variety of Windows events configured in Risknox Pulse config.json.
.DESCRIPTION
    Continuously loops and triggers specific actions to generate targeted Event IDs:
    - Process Tracking: 4688, 4689
    - Logon: 4625
    - User/Group Mgmt: 4720, 4722, 4724, 4725, 4726, 4732, 4733, 4738
    - Scheduled Tasks: 4698, 4699
    - Services: 7045, 7036, 7040
    - PowerShell: 4103, 4104 (including suspicious payloads)
    - WMI: 5857
    - Suspicious Execution: certutil, whoami, vssadmin (Sigma rule triggers)
    - Network Shares: 5142 (net share)
    - FIM: File create/modify/delete
    - Sysmon (if installed): 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 25, 26
    - Windows Firewall: 2004, 2006, 5156, 5157 (Application, Action, Protocol, DestAddress)
    - Object Access Audit: 4663 (AccessMask, AccessList, ObjectType)
    - DNS Client: 3008 (QueryName, QueryResults)
    - File Share Access: 5140, 5145 (ShareName, RelativeTargetName, AccessMask)
    - NTLM Auth: 8001, 8002, 8003 (source/destination hostname, IP)
    - Kerberos: 4768, 4769, 4771 (TicketEncryptionType, TicketOptions, FailureCode)
    - Audit Policy Changes: 4719 (AuditPolicyChanges)
    - Named Pipes: CreateFile on pipes (PipeName)
    - Registry Activity: reg add/delete (TargetObject, Details)
    - Network Connections: outbound connections (DestinationHostname, DestAddress, DestinationPort)
    - Process Access: OpenProcess (SourceImage, TargetImage, GrantedAccess, CallTrace)
    - Certificate / Code Signing: sigcheck (Signature, SignatureStatus, Signed)
    - Windows Defender: 1116, 1117 detection events
#>

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host "   ResolutePulse Extended Event Generator" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# 1. Enable Required Audit Policies
Write-Host "`n[+] Configuring Windows Audit Policies..." -ForegroundColor Yellow
cmd.exe /c "auditpol /set /subcategory:`"Process Creation`" /success:enable /failure:enable" | Out-Null
cmd.exe /c "auditpol /set /subcategory:`"Logon`" /success:enable /failure:enable" | Out-Null
cmd.exe /c "auditpol /set /subcategory:`"User Account Management`" /success:enable /failure:enable" | Out-Null
cmd.exe /c "auditpol /set /subcategory:`"Security Group Management`" /success:enable /failure:enable" | Out-Null

# Enable PowerShell Script Block and Module Logging
$psLogPath = "HKLM:\Software\Policies\Microsoft\Windows\PowerShell\ScriptBlockLogging"
if (!(Test-Path $psLogPath)) { New-Item -Path $psLogPath -Force | Out-Null }
Set-ItemProperty -Path $psLogPath -Name "EnableScriptBlockLogging" -Value 1 -Force

$psModPath = "HKLM:\Software\Policies\Microsoft\Windows\PowerShell\ModuleLogging"
if (!(Test-Path $psModPath)) { New-Item -Path $psModPath -Force | Out-Null }
Set-ItemProperty -Path $psModPath -Name "EnableModuleLogging" -Value 1 -Force

Write-Host "[+] Audit Policies enabled successfully." -ForegroundColor Green
Write-Host "`n[+] Starting continuous event generation. Press Ctrl+C to stop.`n" -ForegroundColor Yellow

$counter = 1
while ($true) {
    Write-Host "--- Generating Event Batch #$counter ---" -ForegroundColor DarkGray

    # ---------------------------------------------------------
    # 1. Process Tracking (4688, 4689)
    # ---------------------------------------------------------
    Write-Host "  -> Process Tracking (4688, 4689)..."
    $proc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c timeout /t 1 > NUL" -PassThru -WindowStyle Hidden
    $proc.WaitForExit()

    # ---------------------------------------------------------
    # 2. Failed Logon (4625)
    # ---------------------------------------------------------
    Write-Host "  -> Failed Logon (4625)..."
    cmd.exe /c "net use \\127.0.0.1\IPC$ /user:fake_hacker_user WrongPassword123! 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 3. User & Group Management (4720, 4722, 4724, 4725, 4726, 4732, 4733, 4738)
    # ---------------------------------------------------------
    Write-Host "  -> User & Group Mgmt (4720, 4722, 4724, 4725, 4726, 4732, 4733, 4738)..."
    $dummyUser = "rp_test_user_$($counter % 5)"
    
    cmd.exe /c "net user $dummyUser /delete 2>nul" | Out-Null                                       # Cleanup
    cmd.exe /c "net user $dummyUser `"RiskTest123!`" /add 2>nul" | Out-Null                      # 4720: Created
    cmd.exe /c "net user $dummyUser /active:no 2>nul" | Out-Null                                    # 4725: Disabled
    cmd.exe /c "net user $dummyUser /active:yes 2>nul" | Out-Null                                   # 4722: Enabled
    cmd.exe /c "net user $dummyUser `"RiskPass123!`" 2>nul" | Out-Null                        # 4724: Password Reset
    cmd.exe /c "net user $dummyUser /fullname:`"Test Account`" 2>nul" | Out-Null                    # 4738: Changed
    cmd.exe /c "net localgroup `"Guests`" $dummyUser /add 2>nul" | Out-Null                         # 4732: Added to group
    cmd.exe /c "net localgroup `"Guests`" $dummyUser /delete 2>nul" | Out-Null                      # 4733: Removed from group
    cmd.exe /c "net user $dummyUser /delete 2>nul" | Out-Null                                       # 4726: Deleted

    # ---------------------------------------------------------
    # 4. Scheduled Tasks (4698, 4699)
    # ---------------------------------------------------------
    Write-Host "  -> Scheduled Tasks (4698, 4699)..."
    $taskName = "ResolutePulse_DummyTask_$($counter % 5)"
    $action = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c echo test"
    $trigger = New-ScheduledTaskTrigger -AtStartup
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Force | Out-Null  # 4698: Task Created
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false | Out-Null                         # 4699: Task Deleted

    # ---------------------------------------------------------
    # 5. Services (7045, 7036, 7040)
    # ---------------------------------------------------------
    Write-Host "  -> Service Configuration (7045, 7036, 7040)..."
    $svcName = "RpDummyService_$($counter % 5)"
    cmd.exe /c "sc delete $svcName 2>nul" | Out-Null
    cmd.exe /c "sc create $svcName binPath= `"cmd.exe /c echo dummy`" start= demand 2>nul" | Out-Null # 7045: Service Installed
    cmd.exe /c "sc config $svcName start= auto 2>nul" | Out-Null                                      # 7040: Start type changed
    cmd.exe /c "sc start $svcName 2>nul" | Out-Null                                                   # 7036: State changed
    cmd.exe /c "sc delete $svcName 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 6. PowerShell (4103, 4104)
    # ---------------------------------------------------------
    Write-Host "  -> PowerShell Logging (4103, 4104)..."
    $psBlock = [scriptblock]::Create("Write-Output 'Dummy PS Script Execution'")
    $psBlock.Invoke() | Out-Null

    # ---------------------------------------------------------
    # 7. WMI Activity (5857)
    # ---------------------------------------------------------
    Write-Host "  -> WMI Activity (5857)..."
    Get-WmiObject -Class Win32_Process -Filter "Name='explorer.exe'" | Out-Null

    # ---------------------------------------------------------
    # 8. Suspicious Process Execution (Sigma Triggers)
    # ---------------------------------------------------------
    Write-Host "  -> Suspicious Process Execution (certutil, whoami, vssadmin)..."
    cmd.exe /c "certutil -hashfile C:\Windows\System32\cmd.exe SHA256 > nul 2>nul" | Out-Null
    cmd.exe /c "whoami /priv > nul" | Out-Null
    cmd.exe /c "vssadmin list shadows > nul 2>nul" | Out-Null
    cmd.exe /c "netsh advfirewall set allprofiles state off 2>nul" | Out-Null
    cmd.exe /c "netsh advfirewall set allprofiles state on 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 9. Suspicious PowerShell (Sigma Triggers)
    # ---------------------------------------------------------
    Write-Host "  -> Suspicious PowerShell Executions..."
    $suspiciousPs = [scriptblock]::Create("Invoke-Expression 'Write-Output ''Suspicious IEX'''")
    $suspiciousPs.Invoke() | Out-Null
    
    # ---------------------------------------------------------
    # 10. Network Share Creation (5142)
    # ---------------------------------------------------------
    Write-Host "  -> Network Share Creation (5142)..."
    $shareName = "RpFakeShare_$($counter % 5)"
    cmd.exe /c "net share $shareName=C:\Windows\Temp 2>nul" | Out-Null
    cmd.exe /c "net share $shareName /delete 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 11. FIM Events
    # ---------------------------------------------------------
    Write-Host "  -> FIM File Activity..."
    $fimFile = "C:\Windows\System32\drivers\rp_fim_test.txt"
    "Test FIM content $counter" | Out-File -FilePath $fimFile -Encoding utf8
    Start-Sleep -Milliseconds 300
    Remove-Item -Path $fimFile -Force

    # ---------------------------------------------------------
    # 12. Sysmon Process Creation (EID 1) — populates:
    #     Image, CommandLine, ParentImage, ParentCommandLine,
    #     CurrentDirectory, IntegrityLevel, Hashes, Hash,
    #     OriginalFileName, Company, Product, FileVersion,
    #     Description, ParentProcessId, ProcessId, LogonId,
    #     User (winlog.event_data.User/winlog.user.name)
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Process Triggers (EID 1 fields)..."
    # Spawn processes that Sysmon will log with full hashing, parent tracking
    $sysmonTriggers = @(
        "cmd.exe /c echo SysmonProcessTest_$counter",
        "powershell.exe -NoProfile -Command `"Write-Output 'sysmon_trigger_$counter'`"",
        "cmd.exe /c wmic process list brief > nul",
        "cmd.exe /c ipconfig /all > nul",
        "cmd.exe /c net session > nul 2>&1"
    )
    foreach ($cmdLine in $sysmonTriggers) {
        $p = Start-Process -FilePath "cmd.exe" -ArgumentList "/c $cmdLine" -PassThru -WindowStyle Hidden
        $p | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
        if (!$p.HasExited) { $p.Kill() }
    }

    # ---------------------------------------------------------
    # 13. Sysmon File Create / File Create Stream Hash (EID 11, 15) — populates:
    #     TargetFilename, CreationUtcTime, Image (creator process)
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon File Create (EID 11, 15 fields)..."
    $sysmonFileDir = "C:\Windows\Temp\rp_sysmon_test"
    if (!(Test-Path $sysmonFileDir)) { New-Item -ItemType Directory -Path $sysmonFileDir -Force | Out-Null }
    # Create files with various extensions that Sysmon rules care about
    @("test_binary.exe", "test_script.ps1", "test_dll.dll", "test_bat.bat", "test_hta.hta") | ForEach-Object {
        $fPath = Join-Path $sysmonFileDir $_
        "RisknoxPulse test content $counter" | Out-File -FilePath $fPath -Encoding utf8 -Force
    }
    # Create an Alternate Data Stream (triggers EID 15 - FileCreateStreamHash)
    $adsFile = Join-Path $sysmonFileDir "test_ads.txt"
    "Normal content" | Out-File -FilePath $adsFile -Encoding utf8 -Force
    cmd.exe /c "echo ADS_hidden_content > `"$adsFile`:Zone.Identifier`"" 2>$null
    Start-Sleep -Milliseconds 200
    Remove-Item -Path $sysmonFileDir -Recurse -Force -ErrorAction SilentlyContinue

    # ---------------------------------------------------------
    # 14. Sysmon Registry Events (EID 12, 13, 14) — populates:
    #     TargetObject, Details/Detail, EventType, NewValue, OldValue
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Registry Events (EID 12, 13, 14 fields)..."
    $regTestPath = "HKCU:\Software\RisknoxPulseTest"
    # EID 12: CreateKey / DeleteKey
    if (!(Test-Path $regTestPath)) { New-Item -Path $regTestPath -Force | Out-Null }
    # EID 13: SetValue
    Set-ItemProperty -Path $regTestPath -Name "TestValue_$($counter % 5)" -Value "TestData_$counter" -Force
    Set-ItemProperty -Path $regTestPath -Name "RunTest" -Value "C:\Windows\Temp\malware_sim.exe" -Force
    # EID 14: RenameKey (simulate by creating and removing)
    $regRenameSrc = "$regTestPath\RenameSource_$($counter % 3)"
    if (!(Test-Path $regRenameSrc)) { New-Item -Path $regRenameSrc -Force | Out-Null }
    Remove-Item -Path $regRenameSrc -Force -ErrorAction SilentlyContinue
    # Suspicious autorun registry keys (triggers many Sigma registry_set rules)
    $autorunPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
    Set-ItemProperty -Path $autorunPath -Name "RpTestAutorun_$($counter % 3)" -Value "cmd.exe /c echo autorun_test" -Force
    Start-Sleep -Milliseconds 100
    Remove-ItemProperty -Path $autorunPath -Name "RpTestAutorun_$($counter % 3)" -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $regTestPath -Recurse -Force -ErrorAction SilentlyContinue

    # ---------------------------------------------------------
    # 15. Sysmon Network Connection (EID 3) — populates:
    #     DestinationHostname, DestinationPort, DestinationIsIpv6,
    #     DestAddress, SourceHostname, Protocol, Initiated,
    #     SourceImage (process making connection), source.ip/port,
    #     destination.ip/port, Image
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Network Connection (EID 3 fields)..."
    # Generate outbound connections that Sysmon will log
    try {
        $tcpClient = New-Object System.Net.Sockets.TcpClient
        $tcpClient.ConnectAsync("127.0.0.1", 445).Wait(1000) | Out-Null
        $tcpClient.Close()
    } catch { }
    try {
        $tcpClient2 = New-Object System.Net.Sockets.TcpClient
        $tcpClient2.ConnectAsync("127.0.0.1", 135).Wait(1000) | Out-Null
        $tcpClient2.Close()
    } catch { }
    # HTTP connection attempt (will be logged even if it fails)
    try {
        [System.Net.WebRequest]::Create("http://127.0.0.1:8080/rp_test").GetResponse() | Out-Null
    } catch { }

    # ---------------------------------------------------------
    # 16. Sysmon DNS Query (EID 22) — populates:
    #     QueryName, QueryResults, QueryStatus, Image, ProcessId
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon DNS Query (EID 22 fields)..."
    @("example.com", "risknox-pulse-test.local", "evil-c2-sim.test", "update.microsoft.com") | ForEach-Object {
        try { [System.Net.Dns]::GetHostAddresses($_) | Out-Null } catch { }
    }

    # ---------------------------------------------------------
    # 17. Sysmon Named Pipe (EID 17, 18) — populates:
    #     PipeName, Image, EventType (CreatePipe/ConnectPipe)
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Named Pipe (EID 17, 18 fields)..."
    $pipeName = "RisknoxPulseTestPipe_$($counter % 5)"
    try {
        $pipeServer = New-Object System.IO.Pipes.NamedPipeServerStream($pipeName, [System.IO.Pipes.PipeDirection]::InOut, 1, [System.IO.Pipes.PipeTransmissionMode]::Byte, [System.IO.Pipes.PipeOptions]::Asynchronous)
        Start-Sleep -Milliseconds 200
        $pipeServer.Dispose()
    } catch { }

    # ---------------------------------------------------------
    # 18. Sysmon WMI Events (EID 19, 20, 21) — populates:
    #     Name, EventType (WmiBindingEvent, WmiConsumerEvent, WmiFilterEvent),
    #     Destination, Query, Consumer, Filter
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon WMI Subscription Persistence (EID 19, 20, 21 fields)..."
    $wmiFilterName = "RisknoxPulseTestFilter_$($counter % 3)"
    $wmiConsumerName = "RisknoxPulseTestConsumer_$($counter % 3)"
    try {
        # Create WMI event filter (EID 19)
        $filterQuery = "SELECT * FROM __InstanceModificationEvent WITHIN 60 WHERE TargetInstance ISA 'Win32_PerfFormattedData_PerfOS_System'"
        $wmiFilter = Set-WmiInstance -Namespace "root\subscription" -Class "__EventFilter" -Arguments @{
            Name           = $wmiFilterName
            EventNamespace = "root\cimv2"
            QueryLanguage  = "WQL"
            Query          = $filterQuery
        } -ErrorAction SilentlyContinue

        # Create CommandLine consumer (EID 20)
        $wmiConsumer = Set-WmiInstance -Namespace "root\subscription" -Class "CommandLineEventConsumer" -Arguments @{
            Name               = $wmiConsumerName
            CommandLineTemplate = "cmd.exe /c echo wmi_persistence_test"
        } -ErrorAction SilentlyContinue

        # Create binding (EID 21)
        if ($wmiFilter -and $wmiConsumer) {
            Set-WmiInstance -Namespace "root\subscription" -Class "__FilterToConsumerBinding" -Arguments @{
                Filter   = $wmiFilter
                Consumer = $wmiConsumer
            } -ErrorAction SilentlyContinue | Out-Null
        }

        # Cleanup
        Start-Sleep -Milliseconds 300
        Get-WmiObject -Namespace "root\subscription" -Class "__FilterToConsumerBinding" -ErrorAction SilentlyContinue |
            Where-Object { $_.Filter -like "*$wmiFilterName*" } | Remove-WmiObject -ErrorAction SilentlyContinue
        Get-WmiObject -Namespace "root\subscription" -Class "CommandLineEventConsumer" -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -eq $wmiConsumerName } | Remove-WmiObject -ErrorAction SilentlyContinue
        Get-WmiObject -Namespace "root\subscription" -Class "__EventFilter" -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -eq $wmiFilterName } | Remove-WmiObject -ErrorAction SilentlyContinue
    } catch { }

    # ---------------------------------------------------------
    # 19. Sysmon Process Access (EID 10) — populates:
    #     SourceImage, TargetImage, GrantedAccess, CallTrace,
    #     SourceProcessId, TargetProcessId
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Process Access (EID 10 fields)..."
    # Reading processes triggers Sysmon EID 10 if configured
    try {
        $explorerProc = Get-Process -Name "explorer" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($explorerProc) {
            # Accessing process handle triggers Sysmon process access logging
            $explorerProc.Handle | Out-Null
        }
    } catch { }
    # Also trigger via tasklist which accesses multiple processes
    cmd.exe /c "tasklist /v > nul 2>&1" | Out-Null

    # ---------------------------------------------------------
    # 20. Sysmon Image Load / Driver Load (EID 6, 7) — populates:
    #     ImageLoaded, Signature, SignatureStatus, Signed,
    #     Image (loading process), Hashes, Company, Description,
    #     FileVersion, OriginalFileName, Product
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Image/Driver Load (EID 6, 7 fields)..."
    # Loading DLLs via rundll32 triggers EID 7 (Image Load)
    cmd.exe /c "rundll32.exe shell32.dll,Control_RunDLL > nul 2>&1" | Out-Null
    # Trigger system DLL loading
    powershell.exe -NoProfile -Command "[System.Reflection.Assembly]::LoadWithPartialName('System.Management') | Out-Null" 2>$null | Out-Null

    # ---------------------------------------------------------
    # 21. Windows Firewall Events (2004, 2006, 5156, 5157) — populates:
    #     Application, ApplicationPath, Action, Protocol,
    #     DestAddress, DestinationPort, RemoteAddress, RemoteName,
    #     SourceAddress, SourcePort, Direction, FilterOrigin, LayerRTID
    # ---------------------------------------------------------
    Write-Host "  -> Windows Firewall Events (2004, 5156, 5157 fields)..."
    $fwRuleName = "RisknoxPulse_TestRule_$($counter % 5)"
    # Create firewall rule (generates EID 2004)
    cmd.exe /c "netsh advfirewall firewall delete rule name=`"$fwRuleName`" > nul 2>&1"
    cmd.exe /c "netsh advfirewall firewall add rule name=`"$fwRuleName`" dir=in action=block protocol=tcp localport=65533 > nul 2>&1"
    Start-Sleep -Milliseconds 100
    # Modify the rule (generates EID 2006)
    cmd.exe /c "netsh advfirewall firewall set rule name=`"$fwRuleName`" new action=allow > nul 2>&1"
    # Delete rule (cleanup)
    cmd.exe /c "netsh advfirewall firewall delete rule name=`"$fwRuleName`" > nul 2>&1"

    # ---------------------------------------------------------
    # 22. Object Access Audit (4663, 4656, 4660) — populates:
    #     AccessMask, AccessList, ObjectType, ObjectName,
    #     ObjectServer, ProcessName, HandleId
    # ---------------------------------------------------------
    Write-Host "  -> Object Access Audit (4663, 4656 fields)..."
    # Enable object access auditing
    cmd.exe /c "auditpol /set /subcategory:`"File System`" /success:enable /failure:enable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"Registry`" /success:enable /failure:enable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"SAM`" /success:enable /failure:enable > nul 2>&1"
    # Create and access a SACL-audited file
    $auditFile = "C:\Windows\Temp\rp_audit_test_$($counter % 5).txt"
    "Audit test content $counter" | Out-File -FilePath $auditFile -Encoding utf8 -Force
    # Read it back to generate access events
    Get-Content -Path $auditFile -ErrorAction SilentlyContinue | Out-Null
    Remove-Item -Path $auditFile -Force -ErrorAction SilentlyContinue

    # ---------------------------------------------------------
    # 23. File Share Access (5140, 5145) — populates:
    #     ShareName, ShareLocalPath, RelativeTargetName,
    #     AccessMask, AccessList, IpAddress, IpPort
    # ---------------------------------------------------------
    Write-Host "  -> File Share Access (5140, 5145 fields)..."
    cmd.exe /c "auditpol /set /subcategory:`"File Share`" /success:enable /failure:enable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"Detailed File Share`" /success:enable /failure:enable > nul 2>&1"
    # Access admin shares to generate 5140/5145
    cmd.exe /c "dir \\127.0.0.1\C$ > nul 2>&1" | Out-Null
    cmd.exe /c "dir \\127.0.0.1\ADMIN$ > nul 2>&1" | Out-Null

    # ---------------------------------------------------------
    # 24. Kerberos Events (4768, 4769, 4771) — populates:
    #     TicketEncryptionType, TicketOptions, ServiceName,
    #     TargetUserName, TargetDomainName, FailureCode,
    #     IpAddress, IpPort, Status, PreAuthType
    # ---------------------------------------------------------
    Write-Host "  -> Kerberos Events (4768, 4769, 4771 fields)..."
    cmd.exe /c "auditpol /set /subcategory:`"Kerberos Authentication Service`" /success:enable /failure:enable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"Kerberos Service Ticket Operations`" /success:enable /failure:enable > nul 2>&1"
    # Trigger Kerberos by accessing domain resources (generates 4768/4769 on domain, 4771 on failure)
    cmd.exe /c "klist purge > nul 2>&1"
    cmd.exe /c "net use \\%COMPUTERNAME%\IPC$ 2> nul" | Out-Null

    # ---------------------------------------------------------
    # 25. Audit Policy Change (4719) — populates:
    #     AuditPolicyChanges, AuditSourceName, SubjectUserSid,
    #     SubjectUserName, SubjectDomainName, SubjectLogonId
    # ---------------------------------------------------------
    Write-Host "  -> Audit Policy Change (4719 fields)..."
    # Toggle an audit policy to generate 4719
    cmd.exe /c "auditpol /set /subcategory:`"Other Object Access Events`" /success:enable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"Other Object Access Events`" /success:disable > nul 2>&1"
    cmd.exe /c "auditpol /set /subcategory:`"Other Object Access Events`" /success:enable > nul 2>&1"

    # ---------------------------------------------------------
    # 26. Suspicious Sysmon Triggers (various EIDs) — populates:
    #     Image, OriginalFileName, Hashes, CommandLine,
    #     ParentImage, ParentCommandLine, CurrentDirectory
    #     These spawn well-known LOLBin commands that Sigma rules detect
    # ---------------------------------------------------------
    Write-Host "  -> Suspicious LOLBin Triggers (Sysmon EID 1 Sigma matches)..."
    # LOLBAS invocations — safe variants that still log as process creation with these Images
    cmd.exe /c "mshta.exe about:blank 2>nul" | Out-Null
    cmd.exe /c "regsvr32.exe /s /n /u scrobj.dll 2>nul" | Out-Null
    cmd.exe /c "cscript.exe //nologo //H:cscript 2>nul" | Out-Null
    cmd.exe /c "wscript.exe //nologo //H:wscript 2>nul" | Out-Null
    cmd.exe /c "bitsadmin.exe /info /verbose 2>nul" | Out-Null
    cmd.exe /c "msiexec.exe /? 2>nul" | Out-Null
    cmd.exe /c "schtasks.exe /query /tn `"RpNonExistent`" 2>nul" | Out-Null
    cmd.exe /c "wmic.exe process get name,processid /format:csv > nul 2>&1" | Out-Null
    cmd.exe /c "nltest.exe /dclist: 2>nul" | Out-Null
    cmd.exe /c "dsquery.exe * -limit 1 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 27. Suspicious PowerShell with Detection Fields — populates:
    #     ScriptBlockText, ScriptBlockId, HostApplication,
    #     EngineVersion, HostVersion, CommandLine, ContextInfo,
    #     Path, Payload, Message, ScriptBlockLogging
    # ---------------------------------------------------------
    Write-Host "  -> Suspicious PowerShell Detection Payloads (4104 fields)..."
    # Execute script blocks that trigger 4104 script block logging
    # NOTE: Offensive tool names and IEX+DownloadString removed to avoid Defender blocking the script
    $suspBlocks = @(
        '[System.Reflection.Assembly]::LoadWithPartialName("System.Runtime")',
        'Get-WmiObject Win32_ShadowCopy',
        'Get-MpPreference | Select-Object -Property ExclusionPath',
        '[Convert]::FromBase64String("dGVzdA==") | ForEach-Object { $_ }',
        'Get-Process | Where-Object { $_.ProcessName -eq "lsass" } | Select-Object Id',
        'Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\SecurityProviders\WDigest"',
        'Get-NetTCPConnection | Where-Object { $_.State -eq "Established" }',
        'Get-ScheduledTask | Where-Object { $_.State -eq "Running" }'
    )
    foreach ($block in $suspBlocks) {
        try {
            $sb = [scriptblock]::Create("try { $block } catch { }")
            $sb.Invoke() | Out-Null
        } catch { }
    }

    # ---------------------------------------------------------
    # 28. Sysmon File Delete (EID 23, 26) / Process Tampering (EID 25) — populates:
    #     TargetFilename, Image, IsExecutable, Archived
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon File Delete & Process Tampering (EID 23, 25, 26 fields)..."
    $deletionTargets = @(
        "C:\Windows\Temp\rp_delete_test_$($counter % 5).exe",
        "C:\Windows\Temp\rp_delete_test_$($counter % 5).dll",
        "C:\Windows\Temp\rp_delete_test_$($counter % 5).log"
    )
    foreach ($dt in $deletionTargets) {
        "MZ_fake_binary_content_$counter" | Out-File -FilePath $dt -Encoding utf8 -Force
        Start-Sleep -Milliseconds 50
        Remove-Item -Path $dt -Force -ErrorAction SilentlyContinue
    }

    # ---------------------------------------------------------
    # 29. Sysmon Clipboard (EID 24) / FileBlockExecutable — populates:
    #     Image, Archived, ClientInfo
    # ---------------------------------------------------------
    Write-Host "  -> Clipboard & Process Integrity (EID 24 fields)..."
    # Set clipboard content to trigger Sysmon EID 24 (if clipboard monitoring enabled)
    try {
        Set-Clipboard -Value "RisknoxPulse clipboard test $counter" -ErrorAction SilentlyContinue
    } catch { }

    # ---------------------------------------------------------
    # 30. Code Signing / Certificate Verification — populates:
    #     Signature, SignatureStatus, Signed, CertThumbprint,
    #     Imphash, Company, Product, FileVersion, Description
    # ---------------------------------------------------------
    Write-Host "  -> Code Signing Verification (signature fields)..."
    # Check authenticode signatures on system binaries (generates signing-related fields in Sysmon)
    try {
        Get-AuthenticodeSignature "C:\Windows\System32\cmd.exe" | Out-Null
        Get-AuthenticodeSignature "C:\Windows\System32\notepad.exe" | Out-Null
        Get-AuthenticodeSignature "C:\Windows\System32\svchost.exe" | Out-Null
    } catch { }

    # ---------------------------------------------------------
    # 31. Windows Defender / Antimalware Events (1116, 1117) — populates:
    #     Threat Name, Threat ID, Detection Time, Detection User,
    #     Action Name, Action ID, Severity Name, Process Name,
    #     Path, Remediation User, Product Name, Product Version,
    #     Engine Version, Security intelligence Version
    # ---------------------------------------------------------
    Write-Host "  -> Windows Defender Log Query (1116, 1117 fields)..."
    # Query Defender event logs to exercise the Defender provider (generates log read events)
    # NOTE: EICAR test string removed — it causes Defender to block the entire script file
    try {
        Get-WinEvent -LogName "Microsoft-Windows-Windows Defender/Operational" -MaxEvents 5 -ErrorAction SilentlyContinue | Out-Null
    } catch { }
    # Trigger a Defender scan on a safe path to generate Defender activity events
    try {
        Start-MpScan -ScanType QuickScan -ScanPath "C:\Windows\Temp" -ErrorAction SilentlyContinue
    } catch { }
    # Query threat detection history
    try {
        Get-MpThreatDetection -ErrorAction SilentlyContinue | Out-Null
        Get-MpThreat -ErrorAction SilentlyContinue | Out-Null
    } catch { }

    # ---------------------------------------------------------
    # 32. NTLM Authentication Events (8001, 8002, 8003) — populates:
    #     SourceHostname, DestinationHostname, DomainName,
    #     UserName, SourceAddress, destination.ip, source.ip
    # ---------------------------------------------------------
    Write-Host "  -> NTLM Authentication (8001, 8002, 8003 fields)..."
    # Enable NTLM auditing
    cmd.exe /c "auditpol /set /subcategory:`"NTLM`" /success:enable /failure:enable > nul 2>&1" 2>$null
    # Trigger NTLM auth by connecting to local resources
    cmd.exe /c "net use \\127.0.0.1\IPC$ 2>nul" | Out-Null
    cmd.exe /c "net use \\127.0.0.1\IPC$ /delete 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 33. Sysmon Process Terminate (EID 5) — populates:
    #     Image, ProcessId, UtcTime
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Process Terminate (EID 5 fields)..."
    $killProc = Start-Process -FilePath "cmd.exe" -ArgumentList "/c ping -n 2 127.0.0.1 > nul" -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 500
    if (!$killProc.HasExited) {
        $killProc.Kill()
    }

    # ---------------------------------------------------------
    # 34. Sysmon CreateRemoteThread (EID 8) — populates:
    #     SourceImage, TargetImage, StartAddress, StartModule,
    #     StartFunction, NewThreadId, SourceProcessId, TargetProcessId
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon CreateRemoteThread triggers (EID 8 fields)..."
    # rundll32 with thread injection patterns are commonly logged
    cmd.exe /c "rundll32.exe advpack.dll,LaunchINFSection 2>nul" | Out-Null

    # ---------------------------------------------------------
    # 35. Sysmon File Creation Time Changed (EID 2) — populates:
    #     TargetFilename, CreationUtcTime, PreviousCreationUtcTime, Image
    # ---------------------------------------------------------
    Write-Host "  -> Sysmon Timestomping Detection (EID 2 fields)..."
    $timestompFile = "C:\Windows\Temp\rp_timestomp_test_$($counter % 3).txt"
    "Timestomp test $counter" | Out-File -FilePath $timestompFile -Encoding utf8 -Force
    try {
        # Modify creation time (triggers Sysmon EID 2 — FileCreateTime changed)
        $item = Get-Item $timestompFile
        $item.CreationTime = (Get-Date).AddYears(-2)
    } catch { }
    Remove-Item -Path $timestompFile -Force -ErrorAction SilentlyContinue

    $counter++
    Write-Host "Sleeping for 5 seconds...`n"
    Start-Sleep -Seconds 5
}

