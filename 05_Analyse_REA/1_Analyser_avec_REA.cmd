@echo off
setlocal DisableDelayedExpansion
set "ACC_KIT_DIR=%~dp0."
if not exist "%~dp0preparer_rea.py" goto incomplete
where wsl.exe >nul 2>nul
if errorlevel 1 goto missing
echo Analyse de la copie v3.3 sur le PC. Aucun contact avec les appareils.
if not "%~1"=="" goto dropped
if exist "%~dp0ecu_etendu_v3_3.tar.gz" goto local
powershell.exe -NoProfile -STA -Command "Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.OpenFileDialog; $d.Title='Choisir l extraction etendue v3.3 (.tar.gz)'; $d.Filter='Archives du firmware (*.tar.gz;*.tgz)|*.tar.gz;*.tgz|Tous les fichiers|*.*'; if($d.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK){exit 3}; & wsl.exe -d Ubuntu --cd $env:ACC_KIT_DIR --exec python3 -u preparer_rea.py $d.FileName; exit $LASTEXITCODE"
goto finish
:local
wsl.exe -d Ubuntu --cd "%ACC_KIT_DIR%" --exec python3 -u preparer_rea.py "%~dp0ecu_etendu_v3_3.tar.gz"
goto finish
:dropped
wsl.exe -d Ubuntu --cd "%ACC_KIT_DIR%" --exec python3 -u preparer_rea.py "%~f1"
goto finish
:incomplete
echo Utiliser Extraire tout avant de lancer ce fichier.
goto finish
:missing
echo La distribution Ubuntu de WSL est attendue sur ce PC.
:finish
echo.
pause
