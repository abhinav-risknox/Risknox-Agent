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
    - PowerShell: 4103, 4104
    - WMI: 5857
    - FIM: File create/modify/delete
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
    # 8. FIM Events
    # ---------------------------------------------------------
    Write-Host "  -> FIM File Activity..."
    $fimFile = "C:\Windows\System32\drivers\rp_fim_test.txt"
    "Test FIM content $counter" | Out-File -FilePath $fimFile -Encoding utf8
    Start-Sleep -Milliseconds 300
    Remove-Item -Path $fimFile -Force

    $counter++
    Write-Host "Sleeping for 5 seconds...`n"
    Start-Sleep -Seconds 5
}
