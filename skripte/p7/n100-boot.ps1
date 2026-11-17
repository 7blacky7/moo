# P7-T25. Nur der Koordinator fuehrt Aktionen ausser "pruefen" aus.
# Einmaliger Firmware-Start: {fwbootmgr} bootsequence, niemals displayorder.
[CmdletBinding()]
param(
    [ValidateSet('pruefen','pausieren','booten','zurueck')][string]$Aktion='pruefen',
    [string]$UsbSerial,
    [string]$PartitionGuid,
    [string]$StickSha256,
    [string]$RueckfallSha256,
    [string]$Eintrag,
    [bool]$WaechterWarAn=$true,
    [bool]$SynapseGestoppt=$false
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'

function Bcd-Lesen([string]$Objekt) {
    $text = & bcdedit.exe /enum $Objekt /v 2>&1
    if ($LASTEXITCODE -ne 0) { throw "bcdedit enum fehlgeschlagen: $text" }
    return ($text -join [Environment]::NewLine)
}
function Bootfolge([string]$Text) {
    # Englische/deutsche BCDEdit-Feldnamen; unerkannte Ausgabe wird abgelehnt.
    $treffer=[regex]::Match($Text,'(?im)^\s*(?:bootsequence|Startsequenz)\s+([^\r\n]+)(?:\r?\n\s{20,}(\{[0-9a-f-]+\}))*')
    if (!$treffer.Success) { return '' }
    return (($treffer.Value -replace '\s+',' ').Trim())
}
function Firmware-Eintraege([string]$Text) {
    foreach ($block in ($Text -split '(?:\r?\n){2,}')) {
        $id=[regex]::Match($block,'(?im)^\s*(?:identifier|Bezeichner)\s+(\{[0-9a-f-]{36}\})\s*$')
        $device=[regex]::Match($block,'(?im)^\s*(?:device|Ger.t)\s+partition=(\\Device\\HarddiskVolume[0-9]+)\s*$')
        $path=[regex]::Match($block,'(?im)^\s*(?:path|Pfad)\s+(\\[^\r\n]+)\s*$')
        if ($id.Success -and $device.Success) {
            [pscustomobject]@{ id=$id.Groups[1].Value; device=$device.Groups[1].Value; path=$path.Groups[1].Value.Trim() }
        }
    }
}
function Volume-Geraet([string]$Pfad) {
    # QueryDosDevice liest den NT-Geraetepfad der eindeutig identifizierten ESP.
    # Keine Laufwerksbuchstaben zuweisen und keine ESP mounten.
    if (!('MooN100DosDevice' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class MooN100DosDevice {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern uint QueryDosDevice(string name, StringBuilder target, int max);
}
'@
    }
    if ($Pfad -notmatch '^\\\\\?\\(Volume\{[0-9a-f-]+\})\\$') { throw 'Ungueltiger Volume-GUID-Pfad' }
    $puffer=New-Object System.Text.StringBuilder 4096
    if ([MooN100DosDevice]::QueryDosDevice($Matches[1],$puffer,$puffer.Capacity) -eq 0) {
        throw 'QueryDosDevice konnte die USB-ESP nicht zuordnen'
    }
    return $puffer.ToString().Split([char]0)[0]
}
function Bestand {
    if ($UsbSerial -notmatch '^[a-zA-Z0-9_-]+$' -or $StickSha256 -notmatch '^[0-9a-fA-F]{64}$' -or $RueckfallSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'USB-Seriennummer und erwarteter SHA256 sind Pflicht'
    }
    $disks=@(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and $_.SerialNumber.Trim() -eq $UsbSerial })
    if ($disks.Count -ne 1) { throw 'USB-Datentraeger nicht eindeutig' }
    $parts=@(Get-Partition -DiskNumber $disks[0].Number | Where-Object { $_.Guid -eq $PartitionGuid })
    if ($parts.Count -ne 1) { throw 'Erwartete GPT-Partition nicht eindeutig auf dem USB-Datentraeger' }
    $volumes=@($parts[0].AccessPaths | Where-Object { $_ -match '^\\\\\?\\Volume\{' })
    if ($volumes.Count -ne 1) { throw 'Keine eindeutige USB-Volume-GUID' }
    $device=Volume-Geraet $volumes[0]
    $hash=(Get-FileHash -LiteralPath ($volumes[0]+'EFI\BOOT\BOOTX64.EFI') -Algorithm SHA256).Hash
    if ($hash -ne $StickSha256) { throw 'Stick-Loader hat nicht den erwarteten SHA256' }
    $fallHash=(Get-FileHash -LiteralPath ($volumes[0]+'EFI\MOO\KERN.ELF') -Algorithm SHA256).Hash
    if ($fallHash -ne $RueckfallSha256) { throw 'Lokaler Rueckfallkern fehlt oder hat nicht den freigegebenen SHA256' }
    $firmware=Bcd-Lesen 'firmware'
    $entries=@(Firmware-Eintraege $firmware | Where-Object { $_.device -eq $device })
    if ($entries.Count -ne 1) { throw 'Kein eindeutiger Firmware-Eintrag fuer genau diese USB-ESP' }
    if ($entries[0].path -ine '\EFI\BOOT\BOOTX64.EFI') {
        throw 'EFI-Dateipfad des USB-Eintrags ist nicht exakt belegt (erwartet \EFI\BOOT\BOOTX64.EFI); kein Lauf'
    }
    $manager=Bcd-Lesen '{fwbootmgr}'
    if ($manager -notmatch '(?im)^\s*(?:displayorder|Anzeigereihenfolge)\s+') {
        throw 'Firmware-Manager-Ausgabe nicht erkannt'
    }
    if ((Bootfolge $manager) -ne '') { throw 'Vorhandene einmalige Bootfolge: nicht ueberschreiben' }
    $task=Get-ScheduledTask -TaskName 'Synapse-Waechter'
    [pscustomobject]@{
        entry=$entries[0].id; device=$device; efi_path=$entries[0].path; partition_guid=$parts[0].Guid
        usb_serial=$UsbSerial; stick_sha256=$hash; rueckfall_sha256=$fallHash
        watcher_enabled=[bool]$task.Settings.Enabled; manager=$manager
        last_boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
    }
}
function N100-Haupt {
if ($Aktion -eq 'pruefen') {
    Bestand | ConvertTo-Json -Compress
    return
}
if ($Eintrag -notmatch '^\{[0-9a-fA-F-]{36}\}$') { throw 'Ungueltiger Firmware-Eintrag' }
if ($Aktion -eq 'pausieren') {
    $b=Bestand
    if ($b.entry -ne $Eintrag -or $b.watcher_enabled -ne $WaechterWarAn) { throw 'Bestand seit Snapshot veraendert' }
    if ($WaechterWarAn) {
        Disable-ScheduledTask -TaskName 'Synapse-Waechter' | Out-Null
        # Ein gerade laufender Waechter darf WSL nicht wieder starten.
        Stop-ScheduledTask -TaskName 'Synapse-Waechter'
    }
    return
}
if ($Aktion -eq 'booten') {
    # Nach dem Stoppen von Synapse noch einmal Stick und freien Einmal-Slot pruefen.
    $b=Bestand
    if ($b.entry -ne $Eintrag) { throw 'Firmware-Eintrag hat sich geaendert' }
    & bcdedit.exe /set '{fwbootmgr}' bootsequence $Eintrag | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Einmalige Bootfolge konnte nicht gesetzt werden' }
    try {
        $danach=Bcd-Lesen '{fwbootmgr}'
        $folge=Bootfolge $danach
        if ($folge -notmatch [regex]::Escape($Eintrag) -or
            ([regex]::Matches($folge,'\{[0-9a-f-]{36}\}')).Count -ne 1) {
            throw 'Einmalige Bootfolge nicht exakt bestaetigt'
        }
        # Abgesehen vom einmaligen Element muss der Manager identisch bleiben.
        $ohne=[regex]::Replace($danach,'(?im)^\s*(?:bootsequence|Startsequenz)\s+[^\r\n]+\r?\n?','')
        if (($ohne -replace '\s','') -ne ($b.manager -replace '\s','')) {
            throw 'Firmware-Manager unerwartet geaendert'
        }
        & shutdown.exe /r /t 5
        if ($LASTEXITCODE -ne 0) { throw 'Neustart wurde nicht angenommen' }
    } catch {
        & bcdedit.exe /deletevalue '{fwbootmgr}' bootsequence | Out-Null
        throw
    }
    return
}
if ($Aktion -eq 'zurueck') {
    $restoreError=$null
    try {
    $manager=Bcd-Lesen '{fwbootmgr}'
    $folge=Bootfolge $manager
    if ($folge -ne '') {
        if ($folge -notmatch [regex]::Escape($Eintrag) -or
            ([regex]::Matches($folge,'\{[0-9a-f-]{36}\}')).Count -ne 1) {
            throw 'Fremde Einmal-Bootfolge gefunden; nichts geloescht'
        }
        & bcdedit.exe /deletevalue '{fwbootmgr}' bootsequence | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Eigene Einmal-Bootfolge nicht entfernt' }
    }
    } catch { $restoreError=$_ }
    # Die unabhaengige Waechter-Restaurierung auch bei BCD-Teilfehler ausfuehren.
    if ($WaechterWarAn) {
        Enable-ScheduledTask -TaskName 'Synapse-Waechter' | Out-Null
        if ($SynapseGestoppt) { Start-ScheduledTask -TaskName 'Synapse-Waechter' }
    } elseif ($SynapseGestoppt) {
        Start-ScheduledTask -TaskName 'Synapse-WSL'
    }
    if ($restoreError) { throw $restoreError }
    [pscustomobject]@{ restored=$true; watcher_enabled=[bool](Get-ScheduledTask -TaskName 'Synapse-Waechter').Settings.Enabled } | ConvertTo-Json -Compress
}

}
# Dot-Sourcing laedt nur die Funktionen fuer hardwarefreie Pruefungen.
if ($MyInvocation.InvocationName -ne '.') { N100-Haupt }
