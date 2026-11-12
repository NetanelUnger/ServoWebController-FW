@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build_ota.ps1" %*
set "OTA_EXIT=%ERRORLEVEL%"
if not "%OTA_EXIT%"=="0" echo OTA BUILD FAILED. See the log above.
if /I not "%OTA_NO_PAUSE%"=="1" pause
exit /b %OTA_EXIT%
