# Windows PowerShell 5.1+. Only the fixed ECU address is contacted.
param([switch]$Reprendre)
$ErrorActionPreference = 'Stop'
$Base = 'http://IP_ECU'
$Token = '00000000000000000000000000000000'
$ExpectedPackageHash = '7e616a7271d66f26a45cbf06fa4904d51c4bef03f3af7c9ce975a820a6126e2e'
$Package = Join-Path $PSScriptRoot 'ECU_C_recuperation_v3_3.tar.bz2'
$Resource = "$Base/resources/acc_ecu_copy_$Token"
$OutputDir = Join-Path $PSScriptRoot ('retour_ECU_v3_3_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$null = New-Item -ItemType Directory -Path $OutputDir

function Get-HttpFile([string]$Url, [string]$Destination, [int]$Timeout = 15) {
    $rawStatus = & $Curl --noproxy '*' --silent --show-error --connect-timeout 5 `
        --max-time $Timeout --output $Destination --write-out '%{http_code}' $Url
    if ($LASTEXITCODE -ne 0) { throw "Erreur reseau curl ($LASTEXITCODE) pour $Url" }
    return [int](($rawStatus -join '').Trim())
}

function Save-InventoryReport {
    $temporary = Join-Path $OutputDir 'inventaire.part'
    $report = Join-Path $OutputDir 'inventaire.tsv'
    try {
        if ((Get-HttpFile "$Resource/inventaire.tsv" $temporary) -ne 200) { throw 'Inventaire des repertoires indisponible.' }
        $header = Get-Content -LiteralPath $temporary -TotalCount 1
        if ($header -ne "scope`tstate`tdirectories`tregular_files`tlinks`tadded_files") { throw 'En-tete du rapport de parcours inattendu.' }
        Move-Item -LiteralPath $temporary -Destination $report -Force
        return $report
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Confirm-ArchiveMembers([string]$Path, [int]$Count, [int]$Selected, [int]$Added) {
    $reader = Get-Command tar.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $reader) { return }
    $tarExe = $reader.Source
    $paths = @(& $tarExe -tf $Path)
    if ($LASTEXITCODE -ne 0 -or $paths.Count -ne $Count) { throw 'Le lecteur TAR Windows signale une archive incomplete.' }
    $members = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($name in $paths) {
        if ($name -notmatch '^[A-Za-z0-9_./+-]+$' -or $name -match '(^/|/$|//|(^|/)\.{1,2}(/|$))' -or -not $members.Add($name)) {
            throw "Entree TAR invalide ou en double : $name"
        }
    }
    foreach ($name in @('ajouts.txt','copied_paths.txt','inventaire.tsv','payload/etc/yuneng/version.conf','payload/home/applications/main.exe','payload/home/applications/meterManager','payload/home/applications/ipcTest')) {
        if (-not $members.Contains($name)) { throw "Entree TAR attendue absente : $name" }
    }
    $copied = @(& $tarExe -xOf $Path 'copied_paths.txt')
    if ($LASTEXITCODE -ne 0 -or $copied.Count -ne $Selected) { throw 'Liste des fichiers selectionnes incomplete.' }
    $selection = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($name in $copied) {
        if (-not $members.Contains($name) -or -not $selection.Add($name)) { throw "Fichier annonce absent ou en double : $name" }
    }
    $extras = @(& $tarExe -xOf $Path 'ajouts.txt')
    if ($LASTEXITCODE -ne 0 -or $extras.Count -ne $Added) { throw 'Liste des ajouts incomplete.' }
    foreach ($name in $extras) {
        if (-not $name.StartsWith('payload/') -or -not $selection.Remove($name)) { throw "Ajout annonce absent ou en double : $name" }
    }
    Write-Host ("Le lecteur TAR du PC a retrouve les {0} ajouts." -f $Added)
}

$terminalFailure = $false
$mayResume = $false
try {
    $Curl = (Get-Command curl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    Write-Host 'ECU cible : IP_ECU'
    Write-Host 'Recuperation v3.3 : programmes, bibliotheques, code web et demarrage.'
    Write-Host 'Copie temporaire accessible sur le reseau local pendant 15 minutes apres sa preparation.'
    Write-Host "Dossier de retour : $OutputDir"
    if ((Get-FileHash -LiteralPath $Package -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ExpectedPackageHash) {
        throw 'Empreinte du paquet incorrecte : aucun envoi effectue.'
    }
    $helper = Join-Path $PSScriptRoot 'sources\VerifierGzip.cs'
    if ((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'de84a69686a983ce22885b8e50ab897cff44e4ba293994e2a474bea01f1a148f') {
        throw 'Fichier de verification altere ou incomplet. Extraire de nouveau le kit.'
    }
    Add-Type -Path $helper -ErrorAction Stop
    [AccEcuGzip33]::SelfTest()
    $preflight = Join-Path $OutputDir 'page_upload.html'
    if ((Get-HttpFile "$Base/index.php/hidden/upload" $preflight) -ne 200) {
        throw 'La page Upload connue ne repond pas : aucun envoi effectue.'
    }
    $page = [IO.File]::ReadAllText($preflight)
    if ($page -notmatch 'ENERGY COMMUNICATION UNIT' -or $page -notmatch 'do_upload') {
        throw 'La page ne correspond pas a celle de cet ECU : aucun envoi effectue.'
    }
    $statusPath = Join-Path $OutputDir 'statut.txt'
    $oldStatus = Get-HttpFile "$Resource/status.txt" $statusPath
    if ($oldStatus -ne 404 -and $oldStatus -ne 200) {
        throw "Reponse inattendue avant envoi (HTTP $oldStatus). Aucun envoi effectue."
    }
    if ($Reprendre -and $oldStatus -eq 404) {
        throw 'Aucun resultat disponible a cette adresse. Le mode reprise ne renvoie jamais de script.'
    }
    if ($oldStatus -eq 404) {
        # This firmware's isexist_file reads the existence of exactly this one path.
        $guardPath = Join-Path $OutputDir 'controle_emplacement.txt'
        $guardCode = & $Curl --noproxy '*' --silent --show-error --connect-timeout 5 --max-time 15 --output $guardPath --write-out '%{http_code}' --data-urlencode 'src=/home/update_from_app/update_localweb/assist' "$Base/index.php/management/isexist_file"
        if ($LASTEXITCODE -ne 0 -or (($guardCode -join '').Trim()) -ne '200') {
            throw 'Impossible de verifier si une autre operation ECU est en cours. Envoi annule.'
        }
        $guard = [IO.File]::ReadAllText($guardPath) | ConvertFrom-Json
        if ($null -eq $guard -or $null -eq $guard.PSObject.Properties['value']) { throw 'Reponse de disponibilite inconnue. Envoi annule.' }
        if ([string]$guard.value -eq '0') { throw 'L emplacement assist est deja occupe sur le boitier. Attendre la fin de l autre operation. Envoi annule.' }
        if ([string]$guard.value -ne '-1') { throw 'Etat de l emplacement assist inconnu. Envoi annule.' }
        $mayResume = $true
        Write-Host 'Envoi unique du script de copie...'
        $bodyPath = Join-Path $OutputDir 'reponse_envoi.txt'
        $rawStatus = & $Curl --noproxy '*' --silent --show-error --connect-timeout 5 `
            --max-time 25 --header 'Expect:' --form "file=@$Package" `
            --output $bodyPath --write-out '%{http_code}' `
            "$Base/index.php/management/exec_upgrade_ecu_app"
        if ($LASTEXITCODE -ne 0) {
            throw 'Envoi interrompu ou reponse perdue. Le script peut avoir demarre. Utiliser Reprendre_telechargement.cmd, qui ne renvoie pas le paquet.'
        }
        $httpStatus = [int](($rawStatus -join '').Trim())
        if ($httpStatus -ne 200) {
            throw "Le serveur a repondu HTTP $httpStatus. Ne pas relancer ; conserver reponse_envoi.txt."
        }
        $reply = $null
        try { $reply = [IO.File]::ReadAllText($bodyPath) | ConvertFrom-Json } catch { $reply = $null }
        if ($null -ne $reply -and $reply.PSObject.Properties.Match('res').Count -gt 0 -and $reply.res -ne 0) {
            throw 'Le mecanisme de mise a jour a refuse le paquet. Conserver reponse_envoi.txt ; aucun autre envoi ne sera tente.'
        }
    } else {
        $mayResume = $true
        Write-Host 'Un statut existe deja : reprise sans nouvel envoi de script.'
    }
    Write-Host 'Suivi de la collecte sur le boitier...'
    $ready = $false
    $deadline = [DateTime]::UtcNow.AddMinutes(45)
    $nextProgress = [DateTime]::UtcNow
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    $lastProgress = ''
    $nextHeartbeat = [DateTime]::UtcNow
    while ([DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Seconds 3
        $code = Get-HttpFile "$Resource/status.txt" $statusPath
        if ($code -eq 404) { continue }
        if ($code -ne 200) { throw "Lecture du statut impossible (HTTP $code)." }
        $statusFields = @{}
        foreach ($line in [IO.File]::ReadAllLines($statusPath)) {
            $pair = $line -split '=', 2
            if ($pair.Length -eq 2) { $statusFields[$pair[0]] = $pair[1].Trim() }
        }
        if ($statusFields['TOKEN'] -ne $Token) { throw 'Marqueur de retour inattendu.' }
        if ($statusFields['STATE'] -eq 'FAILED') {
            $terminalFailure = $true
            try { $null = Save-InventoryReport } catch { Write-Host 'Consulter statut.txt pour le dernier repertoire atteint.' }
            Write-Host ("Parcours : {0} ; detail : {1} ; fichiers vus : {2} ; ajouts : {3}" -f `
                $statusFields['SCAN_SCOPE'], $statusFields['FAILED_PATH'], $statusFields['SCANNED_FILES'], $statusFields['ADDED_FILES'])
            if ($statusFields['WORK_FREE_KIB'] -match '^\d+$') {
                Write-Host ("Espace libre (Kio) : /tmp={0}, travail={1}, retour={2}; besoin retour={3}, travail={4}" -f `
                $statusFields['TMP_FREE_KIB'], $statusFields['WORK_FREE_KIB'], `
                $statusFields['WEB_FREE_KIB'], $statusFields['REQUIRED_WEB_KIB'], $statusFields['REQUIRED_WORK_KIB'])
            }
            throw ('Le script a demarre mais la copie a echoue : ' + $statusFields['REASON'])
        }
        if ($statusFields['STATE'] -eq 'READY') { $ready = $true; break }
        if ($statusFields['STATE'] -ne 'WORKING') { throw 'Etat de collecte inconnu.' }
        $phaseText = switch ($statusFields['REASON']) {
            'inventory' { 'Inventaire {0} | {1} examines, {2} ajouts' -f $statusFields['SCAN_SCOPE'],$statusFields['SCANNED_FILES'],$statusFields['ADDED_FILES'] }
            'verifying_snapshots' { 'Verification des copies | {0} / {1} fichiers' -f $statusFields['PHASE_DONE'],$statusFields['FILES'] }
            'preparing_headers' { 'Preparation des entrees de l archive' }
            'writing_archive' { 'Ecriture de l archive | {0} / {1} fichiers' -f $statusFields['PACKED_FILES'],$statusFields['PACK_TOTAL'] }
            'hashing_archive' { 'Calcul de l empreinte du TAR sur le boitier' }
            'compressing_for_crc' { 'Compression de secours pour le controle CRC32' }
            default { $statusFields['REASON'] }
        }
        if (($phaseText -ne $lastProgress -and [DateTime]::UtcNow -ge $nextProgress) -or [DateTime]::UtcNow -ge $nextHeartbeat) {
            Write-Host ('[{0:mm\:ss}] {1}' -f $elapsed.Elapsed,$phaseText)
            $lastProgress = $phaseText
            $nextProgress = [DateTime]::UtcNow.AddSeconds(12)
            $nextHeartbeat = [DateTime]::UtcNow.AddMinutes(2)
        }
    }
    if (-not $ready) {
        throw 'Aucun export confirme dans les 45 minutes. Conserver le dossier puis utiliser Reprendre_telechargement.cmd pour verifier sans nouvel envoi.'
    }
    $reportPath = Save-InventoryReport
    $rows = @(Import-Csv -LiteralPath $reportPath -Delimiter "`t")
    $expectedScopes = @('/home/applications','/home/local_web','/lib','/usr/lib','/bin','/sbin','/usr/bin','/usr/sbin','/usr/local/bin','/usr/local/sbin','/etc/init.d')
    $seen = @{}
    $added = 0
    $examined = 0
    foreach ($row in $rows) {
        if ($row.scope -notin $expectedScopes -or $seen.ContainsKey($row.scope) -or $row.state -notin @('visited','missing','symlink') -or
            $row.added_files -notmatch '^\d{1,5}$' -or $row.regular_files -notmatch '^\d{1,5}$' -or [int]$row.added_files -gt [int]$row.regular_files) {
            throw 'Rapport de parcours incomplet ou invalide.'
        }
        $seen[$row.scope] = $true
        $added += [int]$row.added_files
        $examined += [int]$row.regular_files
    }
    if ($seen.Count -ne $expectedScopes.Count -or $added -lt 1 -or $statusFields['ADDED_FILES'] -ne [string]$added -or $statusFields['SCANNED_FILES'] -ne [string]$examined) {
        throw 'Le parcours etendu ne confirme aucun ajout ou ses totaux different du statut. Conserver inventaire.tsv et statut.txt.'
    }
    if ($statusFields['PACKED_FILES'] -notmatch '^\d{1,5}$' -or
        $statusFields['PACKED_FILES'] -ne $statusFields['PACK_TOTAL'] -or
        $statusFields['FILES'] -notmatch '^\d{1,5}$' -or
        [int]$statusFields['FILES'] -lt $added -or
        [int]$statusFields['PACKED_FILES'] -lt [int]$statusFields['FILES']) {
        throw 'Inventaire et nombre de fichiers archives incoherents.'
    }
    if ($statusFields['BYTES'] -notmatch '^\d{1,9}$' -or [long]$statusFields['BYTES'] -gt 536870912) {
        throw 'Taille de transfert hors limite.'
    }
    $archive = Join-Path $OutputDir 'ecu_etendu_v3_3.tar.gz'
    $gzipPart = Join-Path $OutputDir 'ecu_etendu_v3_3.tar.gz.part'
    $isRaw = $statusFields['TRANSPORT'] -eq 'tar'
    if ($isRaw) {
        if ([long]$statusFields['BYTES'] -lt 1536 -or ([long]$statusFields['BYTES'] % 512) -ne 0) {
            throw 'Taille du TAR invalide.'
        }
        if ($statusFields['SOURCE_CHECK'] -in @('sha256sum','busybox_sha256','openssl_sha256') -and
            $statusFields['SHA256'] -match '^[a-f0-9]{64}$') { $algorithm = 'SHA256' }
        elseif ($statusFields['SOURCE_CHECK'] -eq 'md5sum' -and $statusFields['MD5'] -match '^[a-f0-9]{32}$') { $algorithm = 'MD5' }
        else { throw 'Empreinte de transfert du TAR absente ou invalide.' }
        $remoteName = 'ecu_etendu.tar'
        $download = Join-Path $OutputDir 'ecu_etendu_v3_3.tar.part'
    } elseif ($statusFields['TRANSPORT'] -eq 'gzip' -and $statusFields['SOURCE_CHECK'] -in @('cmp','cksum') -and
        $statusFields['GZIP_FOOTER'] -match '^[0-9a-f]{16}$' -and [long]$statusFields['BYTES'] -ge 18) {
        $remoteName = 'ecu_etendu.tar.gz'
        $download = $gzipPart
    } else { throw 'Format de transfert inattendu.' }
    Write-Host ('Archive disponible. Telechargement de {0:N1} Mio...' -f ([long]$statusFields['BYTES'] / 1MB))
    if ((Get-HttpFile "$Resource/$remoteName" $download 480) -ne 200) {
        throw 'Le telechargement a echoue.'
    }
    if ((Get-Item -LiteralPath $download).Length -ne [long]$statusFields['BYTES']) {
        throw 'Taille recue differente de celle mesuree sur le boitier.'
    }
    if ($isRaw) {
        $wireHash = (Get-FileHash -LiteralPath $download -Algorithm $algorithm).Hash.ToLowerInvariant()
        if ($wireHash -ne $statusFields[$algorithm]) { throw "Empreinte $algorithm de transfert incorrecte." }
        $rawTar = Join-Path $OutputDir 'ecu_etendu_v3_3.tar'
        Move-Item -LiteralPath $download -Destination $rawTar
        $localTarHash = $wireHash
        if ($algorithm -eq 'MD5') { $localTarHash = (Get-FileHash -LiteralPath $rawTar -Algorithm SHA256).Hash }
        Write-Host "TAR recu et verifie ($algorithm). Compression et relecture sur le PC..."
        Confirm-ArchiveMembers $rawTar ([int]$statusFields['PACKED_FILES']) ([int]$statusFields['FILES']) $added
        [AccEcuGzip33]::CompressVerifiedTar($rawTar, $gzipPart, $localTarHash)
    } else {
        $null = [AccEcuGzip33]::VerifyFile($gzipPart, $statusFields['GZIP_FOOTER'])
        Confirm-ArchiveMembers $gzipPart ([int]$statusFields['PACKED_FILES']) ([int]$statusFields['FILES']) $added
        Write-Host 'Transfert relu integralement. Longueur et CRC32 verifies.'
    }
    $finalHash = (Get-FileHash -LiteralPath $gzipPart -Algorithm SHA256).Hash.ToLowerInvariant()
    Move-Item -LiteralPath $gzipPart -Destination $archive
    [IO.File]::WriteAllText((Join-Path $OutputDir 'SHA256_local.txt'), "$finalHash  ecu_etendu_v3_3.tar.gz`r`n")
    if ($isRaw) { Remove-Item -LiteralPath $rawTar -ErrorAction SilentlyContinue }
    Write-Host ("Fichiers supplementaires dans l'archive : {0}." -f $added)
    Write-Host "RECUPERATION TERMINEE : $archive" -ForegroundColor Green
    Write-Host 'Joindre ecu_etendu_v3_3.tar.gz a la conversation.'
    exit 0
} catch {
    $message = $_.Exception.Message
    [IO.File]::WriteAllText((Join-Path $OutputDir 'erreur.txt'), $message)
    Write-Host "ARRET : $message" -ForegroundColor Yellow
    Write-Host "Conserver et joindre les fichiers texte de : $OutputDir"
    Write-Host 'Aucun autre point d''entree ne sera essaye automatiquement.'
    if ($terminalFailure) { Write-Host 'Collecte terminee en echec. Joindre statut.txt, erreur.txt et inventaire.tsv si disponible.' }
    elseif ($rawTar -and (Test-Path -LiteralPath $rawTar -PathType Leaf)) { Write-Host "Le TAR verifie est conserve sur le PC : $rawTar" }
    elseif ($mayResume) { Write-Host 'Reprendre_telechargement.cmd consulte le resultat disponible sans renvoyer de script.' }
    exit 1
}
