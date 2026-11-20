@echo off
setlocal
title ServoWebController - Flash connected board
echo Flash bootloader, then the latest locally built application.
echo Connect one board through ST-LINK before continuing.
echo Application: STM32CubeIDE\Debug\BleServoController.hex
echo No build is performed. Both images are verified after programming.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\flash.ps1" -BootloaderHex "%~dp0bootloader.hex"
set "flash_result=%ERRORLEVEL%"
echo.
if "%flash_result%"=="0" (
    echo SUCCESS: Bootloader and application verified. Board reset.
) else (
    echo FAILED: Flashing did not complete. See the error above.
)
pause
exit /b %flash_result%
