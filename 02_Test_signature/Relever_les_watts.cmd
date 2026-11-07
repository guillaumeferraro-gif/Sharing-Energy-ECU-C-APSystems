@echo off
setlocal
if not exist "%~dp0relever.py" goto incomplet
if not exist "%~dp0lecture.sh" goto incomplet
echo Essai des watts ECU : mesure reelle, +1 000 W pendant 90 s, puis mesure reelle.
echo Garde le zero export desactive et l'ESP32 selectionne pendant tout le releve.
echo Depuis la réseau local, hors maintenance ECU. Duree du releve : environ quatre minutes.
where py.exe >nul 2>nul
if errorlevel 1 goto wsl
py.exe -3 -c "import sys; sys.exit(sys.version_info < (3,8))" >nul 2>nul
if errorlevel 1 goto wsl
py.exe -3 -B -u "%~dp0relever.py"
goto fin
:wsl
where wsl.exe >nul 2>nul
if errorlevel 1 goto manque
wsl.exe -d Ubuntu --cd "%~dp0." --exec python3 -B -u relever.py
goto fin
:incomplet
echo Choisir Extraire tout et lancer Relever_les_watts.cmd dans le dossier extrait.
goto fin
:manque
echo Ce releve utilise Python 3 pour Windows ou Ubuntu sous WSL.
:fin
echo.
pause
