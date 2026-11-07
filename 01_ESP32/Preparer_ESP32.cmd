@echo off
setlocal DisableDelayedExpansion
set "ACC_ESSAI_DIR=%~dp0"
echo Preparation unique du nouveau sketch Arduino pour l'ESP32.
echo Dans la fenetre qui s'ouvre, choisis ton ancien fichier .ino v0.8.7.
powershell.exe -NoProfile -STA -Command ^
  "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.Windows.Forms;" ^
  "$pick=New-Object System.Windows.Forms.OpenFileDialog; $pick.Title='Choisir le sketch ESP32 v0.8.7 qui utilise deja le Wi-Fi'; $pick.Filter='Sketch Arduino (*.ino)|*.ino'; if($pick.ShowDialog() -ne 'OK'){exit 2};" ^
  "$src=[IO.File]::ReadAllText($pick.FileName); $lines=@('#pragma once'); foreach($name in @('WIFI_SSID','WIFI_PASSWORD')){" ^
  "$pattern='(?m)^[ \t]*const[ \t]+char[ \t]*\*[ \t]*'+$name+'[ \t]*=[ \t]*(\x22(?:\\.|[^\x22\\\r\n])+\x22)[ \t]*;'; $m=[regex]::Matches($src,$pattern); if($m.Count -ne 1){throw ('Valeur Wi-Fi absente ou ambigue : '+$name+'. Voir LIRE_MOI.txt.')};" ^
  "$lines+=('constexpr char '+$name+'[] = '+$m[0].Groups[1].Value+';')}; $dir=Join-Path $env:ACC_ESSAI_DIR 'ESP32_v088'; $dest=Join-Path $dir 'WifiLocal.h';" ^
  "$temp=Join-Path $dir ([IO.Path]::GetRandomFileName()); try{[IO.File]::WriteAllLines($temp,[string[]]$lines,[Text.UTF8Encoding]::new($false)); [IO.File]::Copy($temp,$dest,$true)}finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}; exit 0"
if errorlevel 1 goto termine
echo Le Wi-Fi est pret. Ouverture du sketch ESP32_v088.ino dans Arduino IDE.
echo Conserve tes reglages habituels, puis clique sur Verifier et Televerser.
start "" "%~dp0ESP32_v088\ESP32_v088.ino"
:termine
pause
