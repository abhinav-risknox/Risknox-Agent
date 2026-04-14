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
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
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
Name: "core"; Description: "Risknox Pulse Agent Service"; Types: full custom
Name: "ui"; Description: "Pulse Monitor Dashboard"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for Pulse Monitor"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; Core Service Executable
Source: "build\ResolutePulse.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: core

; Monitor UI Executables & Dependencies
Source: "src\gui\RiskNoXMonitor\bin\Release\net10.0-windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: ui

; Configuration File
Source: "config.json"; DestDir: "{app}"; Flags: ignoreversion; Components: core

; Branding Icon
Source: "installer_assets\risknox.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\risknox.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"; IconFilename: "{app}\risknox.ico"
Name: "{autodesktop}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\risknox.ico"
Name: "{autostartup}\Pulse Monitor"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\risknox.ico"

[Run]
; Install the ResolutePulse Windows Service
Filename: "{app}\ResolutePulse.exe"; Parameters: "--install"; StatusMsg: "Installing Risknox Pulse Agent Service..."; Flags: runhidden; Components: core

; Launch the Desktop Monitor UI after installation finishes
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Pulse Monitor Dashboard"; Flags: postinstall nowait skipifsilent shellexec

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
