@echo off
setlocal
where py.exe >nul 2>nul
if errorlevel 1 goto manque
py.exe -3 -B "%~dp0Personnaliser.py" %*
goto fin
:manque
echo Installer Python 3 pour Windows, ou utiliser python3 Personnaliser.py sous Linux ou macOS.
:fin
echo.
pause
