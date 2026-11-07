@echo off
setlocal
if not exist "%~dp0relever.py" goto incomplet
if not exist "%~dp0lecture.sh" goto incomplet
if not exist "%~dp0bridage.py" goto incomplet
if not exist "%~dp0onduleurs.py" goto incomplet
echo Test des micro-onduleurs, en plein soleil. Duree : environ cinq minutes.
echo Activer le zero export a 0 W et selectionner l'ESP32 dans l'ECU.
echo Attendre la stabilisation pres de 0 W, avec au moins 1 kW de production supplementaire disponible.
echo L'ESP32 simulera +1 000 W pendant 90 s. Injection reelle attendue : environ 1 kW.
echo Lancer sur le reseau de la maison, hors maintenance de l'ECU.
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
