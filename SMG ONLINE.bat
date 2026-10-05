@echo off
REM Opens the SMG Online launcher window: pick your colour, then Join or Host.
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher.ps1"
