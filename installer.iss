; Risknox Pulse - Installer Script
; Created with Inno Setup

#define MyAppName "Risknox Pulse"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Risknox"
#define MyAppURL "https://risknox.ai"
#define MyAppExeName "RisknoxMonitor.exe"
#define MyAppServiceName "ResolutePulse"

[Setup]
; Basic Information
AppId={{8F9A2B3C-4D5E-6F7A-8B9C-0D1E2F3A4B5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/support
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\Risknox Pulse
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=installer_output
OutputBaseFilename=RisknoxPulse_Setup_v{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableWelcomePage=no

; Branding & Visual Theme
SetupIconFile=installer_assets\risknox.ico
WizardImageFile=installer_assets\wizard_image.bmp
WizardSmallImageFile=installer_assets\small_wizard_image.bmp
WizardImageStretch=yes
WizardImageBackColor=$2E1A1A
UninstallDisplayIcon={app}\risknox.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full Installation (Recommended)"
Name: "custom"; Description: "Custom Installation"; Flags: iscustom

[Components]
Name: "core";      Description: "Risknox Pulse Agent Service (required)"; Types: full custom; Flags: fixed
Name: "ui";        Description: "Pulse Monitor Dashboard";               Types: full custom
Name: "antivirus"; Description: "Bundled ClamAV Antivirus Engine";        Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for Pulse Monitor"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; ── Core Agent ──────────────────────────────────────────────────────────────
Source: "build\ResolutePulse.exe";   DestDir: "{app}"; Flags: ignoreversion; Components: core

; Worker subprocess executables
Source: "build\rp-webblock.exe";     DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "build\rp-softblock.exe";    DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "build\rp-patch.exe";        DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "build\rp-antivirus.exe";    DestDir: "{app}"; Flags: ignoreversion; Components: antivirus

; OpenSSL Dependencies for Agent and Workers
Source: "vendor\clamav\libcrypto-3-x64.dll"; DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "vendor\clamav\libssl-3-x64.dll";    DestDir: "{app}"; Flags: ignoreversion; Components: core

; MSVC Runtimes required by OpenSSL DLLs
Source: "vendor\clamav\vcruntime140.dll";    DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "vendor\clamav\vcruntime140_1.dll";  DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "vendor\clamav\msvcp140.dll";        DestDir: "{app}"; Flags: ignoreversion; Components: core
Source: "vendor\clamav\msvcp140_1.dll";      DestDir: "{app}"; Flags: ignoreversion; Components: core

; Monitor UI Executables & Dependencies (Self-Contained)
Source: "src\gui\RiskNoXMonitor\bin\Release\net10.0-windows\win-x64\publish\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: ui

; Configuration File
Source: "config.json"; DestDir: "{app}"; Flags: ignoreversion; Components: core

; Branding Icon
Source: "installer_assets\risknox.ico"; DestDir: "{app}"; Flags: ignoreversion

; ── Bundled ClamAV Antivirus Engine ─────────────────────────────────────────
; Bundle the whole vetted ClamAV runtime so vendor updates cannot miss a DLL.
; This also provides the read-only fallback database under {app}\clamav\database.
Source: "vendor\clamav\*"; DestDir: "{app}\clamav"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: antivirus

; Update definitions wrapper script
Source: "installer_assets\update_definitions.bat"; DestDir: "{app}\clamav"; Flags: ignoreversion; Components: antivirus
Source: "installer_assets\freshclam.conf"; DestDir: "{app}\clamav"; Flags: ignoreversion; Components: antivirus

; Virus Databases (~107 MB total)
Source: "vendor\clamav\database\main.cvd";     DestDir: "{commonappdata}\Risknox Pulse\antivirus\database"; Flags: ignoreversion; Components: antivirus
Source: "vendor\clamav\database\daily.cvd";    DestDir: "{commonappdata}\Risknox Pulse\antivirus\database"; Flags: ignoreversion; Components: antivirus
Source: "vendor\clamav\database\bytecode.cvd"; DestDir: "{commonappdata}\Risknox Pulse\antivirus\database"; Flags: ignoreversion; Components: antivirus

[Icons]
Name: "{group}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\risknox.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"; IconFilename: "{app}\risknox.ico"
Name: "{autodesktop}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\risknox.ico"
Name: "{autostartup}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\risknox.ico"

[Run]
; Install the ResolutePulse Windows Service
Filename: "{app}\ResolutePulse.exe"; Parameters: "--install"; StatusMsg: "Installing Risknox Pulse Agent Service..."; Flags: runhidden; Components: core

; Schedule daily virus definition updates at 3:00 AM via Windows Task Scheduler.
; The wrapper .bat means /TR only needs a single quoted path - no nested args.
Filename: "schtasks.exe"; Parameters: "/Create /F /SC DAILY /TN RisknoxClamAVUpdate /TR ""{app}\clamav\update_definitions.bat"" /ST 03:00 /RL HIGHEST"; Flags: runhidden; StatusMsg: "Scheduling virus definition updates..."; Components: antivirus

; Launch the Desktop Monitor UI after installation finishes
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Pulse Monitor Dashboard"; Flags: postinstall nowait skipifsilent shellexec; Components: ui

[UninstallRun]
; Stop and remove the Windows Service during uninstallation
Filename: "net.exe"; Parameters: "stop {#MyAppServiceName}"; Flags: runhidden; RunOnceId: "StopService"
Filename: "{app}\ResolutePulse.exe"; Parameters: "--uninstall"; Flags: runhidden; RunOnceId: "RemoveService"
; Force kill any remaining worker processes to ensure clean uninstallation
Filename: "taskkill.exe"; Parameters: "/F /IM rp-webblock.exe /T"; Flags: runhidden; RunOnceId: "KillWebBlock"
Filename: "taskkill.exe"; Parameters: "/F /IM rp-softblock.exe /T"; Flags: runhidden; RunOnceId: "KillSoftBlock"
Filename: "taskkill.exe"; Parameters: "/F /IM rp-antivirus.exe /T"; Flags: runhidden; RunOnceId: "KillAV"
Filename: "taskkill.exe"; Parameters: "/F /IM rp-patch.exe /T"; Flags: runhidden; RunOnceId: "KillPatch"
Filename: "taskkill.exe"; Parameters: "/F /IM {#MyAppExeName} /T"; Flags: runhidden; RunOnceId: "KillMonitor"
; Remove the scheduled ClamAV update task
Filename: "schtasks.exe"; Parameters: "/Delete /F /TN ""RisknoxClamAVUpdate"""; Flags: runhidden; RunOnceId: "RemoveClamTask"; Components: antivirus

[UninstallDelete]
Type: files;          Name: "{app}\*.log"
Type: files;          Name: "{app}\*.db*"
Type: files;          Name: "{app}\status.json"
Type: files;          Name: "{app}\status.json.tmp"
Type: filesandordirs; Name: "{app}\certs"
Type: filesandordirs; Name: "{app}\logs"
Type: filesandordirs; Name: "{app}\clamav"
Type: filesandordirs; Name: "{app}\runtimes"
; Clean up ProgramData artifacts (event buffer DB, FIM DB, config)
Type: filesandordirs; Name: "{commonappdata}\Risknox Pulse"
Type: dirifempty;     Name: "{app}"
