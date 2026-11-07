@echo off
cd /d "%~dp0"
if not exist "%~dp0Recuperer_ECU.ps1" (
  echo Extraire tout le ZIP avant de lancer ce fichier. Le fichier PS1 est absent.
  pause
  exit /b 1
)
if not exist "%~dp0ECU_C_recuperation_v3_3.tar.bz2" (
  echo Paquet de recuperation absent. Extraire tout le ZIP dans un nouveau dossier.
  pause
  exit /b 1
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Recuperer_ECU.ps1" -Reprendre
echo.
pause
