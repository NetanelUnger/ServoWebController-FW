@echo off
call "%~dp0run.bat" build %*
exit /b %ERRORLEVEL%
