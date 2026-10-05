@echo off
REM Double-click launcher: hosts the server on this PC (if it isn't running yet)
REM and starts 2 Dolphins into patched SMG1. For more local players run:
REM   powershell -ExecutionPolicy Bypass -File start-galaxy.ps1 -Players 3
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-galaxy.ps1"
echo.
pause
