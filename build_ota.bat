@echo off
call "%~dp0python_ota\build_ota.bat" %*
exit /b %ERRORLEVEL%
