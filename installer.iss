; RiskNoX Security Agent - Installer Script
; Created with Inno Setup

#define MyAppName "RiskNoX Security Agent"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "RiskNoX Security"
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
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\RiskNoX Security Agent
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=installer_output
OutputBaseFilename=RiskNoX_Installer_v{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full Installation (Recommended)"
Name: "custom"; Description: "Custom Installation"; Flags: iscustom

[Components]
Name: "core"; Description: "Core Security Agent Service"; Types: full custom
Name: "ui"; Description: "Monitor Dashboard UI"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop icon for Monitor Dashboard"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; Core Service Executable
Source: "build\ResolutePulse.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: core

; Monitor UI Executables & Dependencies
Source: "src\gui\RiskNoXMonitor\bin\Release\net10.0-windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: ui

; Configuration File
Source: "config.json"; DestDir: "{app}"; Flags: ignoreversion; Components: core

[Icons]
Name: "{group}\RiskNoX Monitor"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\RiskNoX Monitor"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{autostartup}\RiskNoX Monitor"; Filename: "{app}\{#MyAppExeName}"

[Run]
; Install the ResolutePulse Windows Service
Filename: "{app}\ResolutePulse.exe"; Parameters: "--install"; StatusMsg: "Installing Windows Service..."; Flags: runhidden; Components: core

; Launch the Desktop Monitor UI after installation finishes
Filename: "{app}\{#MyAppExeName}"; Description: "Launch RiskNoX Monitor Dashboard"; Flags: postinstall nowait skipifsilent shellexec

[UninstallRun]
; Stop and remove the Windows Service during uninstallation
Filename: "net.exe"; Parameters: "stop {#MyAppServiceName}"; Flags: runhidden; RunOnceId: "StopService"
Filename: "{app}\ResolutePulse.exe"; Parameters: "--uninstall"; Flags: runhidden; RunOnceId: "RemoveService"

[UninstallDelete]
Type: files; Name: "{app}\*.log"
Type: files; Name: "{app}\*.db*"
Type: filesandordirs; Name: "{app}\certs"
Type: filesandordirs; Name: "{app}\logs"
Type: dirifempty; Name: "{app}"
