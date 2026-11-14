# Moo-Testspiegel auf dem Windows-N100 anwenden (aufgerufen von smb_upload.py --spiegeln).
# EINWEG: Linux/Synapse ist die einzige Quelle; hier wird nur geprueft und uebernommen.
# NIE Dateien aus diesem Spiegel zurueckkopieren oder hier editieren: Fixes nur in
# Synapse, danach erneut spiegeln (Userentscheid 08.11.2026, #37353).
# Exitcodes: 0 ok, 3 Windows-Aenderung am Spiegel, 4 tar, 5 Hash im Staging,
#            6 robocopy, 7 Spiegel gesperrt, 8 Hash im Ziel.
param([string]$Tar, [string]$Ziel, [switch]$Erzwingen)
$ErrorActionPreference = 'Stop'
$neu = "$Ziel.neu"
$sperre = "$Ziel.spiegel.lock"
function Pruefsumme([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }

if ((Test-Path -LiteralPath $sperre) -and ((Get-Item -LiteralPath $sperre).LastWriteTime -gt (Get-Date).AddHours(-2))) {
    "MOO_SPIEGEL_GESPERRT $sperre"; exit 7
}
Set-Content -LiteralPath $sperre -Value (Get-Date -Format o)
try {
    # 1. Seit dem letzten Spiegel auf Windows veraenderte Dateien sind ein Befund.
    $alt = @{}
    $altManifest = Join-Path $Ziel '.moo-spiegel\manifest.json'
    if (Test-Path -LiteralPath $altManifest) {
        foreach ($e in (Get-Content -Raw -Encoding UTF8 -LiteralPath $altManifest | ConvertFrom-Json).dateien) { $alt[$e.pfad] = $e.sha256 }
        $geaendert = @($alt.Keys | Where-Object {
            $p = Join-Path $Ziel $_
            (Test-Path -LiteralPath $p) -and ((Pruefsumme $p) -ne $alt[$_])
        })
        if ($geaendert.Count -gt 0) {
            "MOO_SPIEGEL_WINDOWS_GEAENDERT anzahl=$($geaendert.Count)"
            $geaendert | Sort-Object | Select-Object -First 20 | ForEach-Object { "  $_" }
            if (-not $Erzwingen) { exit 3 }
        }
    }

    # 2. Entpacken in ein Staging-Verzeichnis und jede Datei pruefen.
    if (Test-Path -LiteralPath $neu) { Remove-Item -LiteralPath $neu -Recurse -Force }
    New-Item -ItemType Directory -Path $neu | Out-Null
    & tar.exe -xf $Tar -C $neu
    if ($LASTEXITCODE -ne 0) { "MOO_SPIEGEL_TAR_FEHLER $LASTEXITCODE"; exit 4 }
    $m = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $neu '.moo-spiegel\manifest.json') | ConvertFrom-Json
    $fehler = 0
    foreach ($e in $m.dateien) {
        $p = Join-Path $neu $e.pfad
        if (-not (Test-Path -LiteralPath $p) -or ((Pruefsumme $p) -ne $e.sha256)) {
            $fehler++; if ($fehler -le 20) { "  STAGING_FALSCH $($e.pfad)" }
        }
    }
    if ($fehler -gt 0) { "MOO_SPIEGEL_HASH_FEHLER staging=$fehler"; exit 5 }

    # 3. Aus dem Manifest gefallene Spiegeldateien entfernen; fremde Test-Ausgaben bleiben.
    $neuSet = @{}
    foreach ($e in $m.dateien) { $neuSet[$e.pfad] = $true }
    $entfernt = 0
    foreach ($k in $alt.Keys) {
        if (-not $neuSet.ContainsKey($k)) {
            $p = Join-Path $Ziel $k
            if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; $entfernt++ }
        }
    }

    # 4. Uebernehmen (immer kopieren, auch gleiche/geaenderte) und im Ziel erneut pruefen.
    & robocopy.exe $neu $Ziel /E /IS /IT /COPY:DT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { "MOO_SPIEGEL_ROBOCOPY_FEHLER $LASTEXITCODE"; exit 6 }
    $fehler = 0
    foreach ($e in $m.dateien) {
        $p = Join-Path $Ziel $e.pfad
        if (-not (Test-Path -LiteralPath $p) -or ((Pruefsumme $p) -ne $e.sha256)) {
            $fehler++; if ($fehler -le 20) { "  ZIEL_FALSCH $($e.pfad)" }
        }
    }
    if ($fehler -gt 0) { "MOO_SPIEGEL_HASH_FEHLER ziel=$fehler"; exit 8 }
    Remove-Item -LiteralPath $neu -Recurse -Force
    Remove-Item -LiteralPath $Tar -Force
    # Lesbarer Quellstand fuer Testbelege (nicht Teil des Manifests).
    Set-Content -LiteralPath (Join-Path $Ziel 'MANIFEST') -Encoding ASCII -Value @(
        "stand=$($m.stand)", "manifest=$($m.manifest_id)", "erzeugt=$($m.erzeugt)",
        "dateien=$($m.dateien.Count)", "quelle=$($m.quelle)", "gespiegelt=$(Get-Date -Format o)")
    "MOO_SPIEGEL_OK dateien=$($m.dateien.Count) entfernt=$entfernt ziel=$Ziel stand=$($m.stand) manifest=$($m.manifest_id)"
    exit 0
}
finally {
    Remove-Item -LiteralPath $sperre -Force -ErrorAction SilentlyContinue
}
