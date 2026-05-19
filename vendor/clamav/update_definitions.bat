@echo off
REM Runs freshclam with the bundled config to update virus definitions.
REM Writable database is stored in %ProgramData% for service compatibility.
set APP_DIR=%~dp0
if not exist "%ProgramData%\Risknox Pulse\antivirus" mkdir "%ProgramData%\Risknox Pulse\antivirus"
if not exist "%ProgramData%\Risknox Pulse\antivirus\database" mkdir "%ProgramData%\Risknox Pulse\antivirus\database"
"%APP_DIR%freshclam.exe" --config-file="%APP_DIR%freshclam.conf" --datadir="%ProgramData%\Risknox Pulse\antivirus\database" --cvdcertsdir="%APP_DIR%certs" --log="%ProgramData%\Risknox Pulse\antivirus\freshclam.log"
