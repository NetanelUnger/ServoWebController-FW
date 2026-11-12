@echo off
setlocal
set "PYTHONUTF8=1"
set "PYTHONUNBUFFERED=1"
rem Prefer standard CPython 3.13, then the default Python 3 installation.
py -3.13 -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>&1
if not errorlevel 1 goto py313
py -3 -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>&1
if not errorlevel 1 goto py3
python -c "import sys; sys.exit(sys.version_info < (3,10))" >nul 2>&1
if not errorlevel 1 goto python
echo Python 3.10 or newer is required. Install Python 3.13 x64 with pip and the Python launcher:
echo https://www.python.org/downloads/windows/
set "OTA_EXIT=1"
goto finish
:py313
py -3.13 "%~dp0bootstrap.py" %*
goto result
:py3
py -3 "%~dp0bootstrap.py" %*
goto result
:python
python "%~dp0bootstrap.py" %*
:result
set "OTA_EXIT=%ERRORLEVEL%"
:finish
if not "%OTA_EXIT%"=="0" echo OTA command failed. See the error above.
if /I not "%OTA_NO_PAUSE%"=="1" pause
exit /b %OTA_EXIT%
