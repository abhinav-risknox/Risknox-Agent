@echo off
REM Runs freshclam with the bundled config to update virus definitions.
REM Called by the Windows Task Scheduler daily at 03:00.
set APP_DIR=%~dp0
"%APP_DIR%freshclam.exe" --config-file="%APP_DIR%freshclam.conf"
