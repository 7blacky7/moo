# Hardwarefreie Vertragspruefung: kein WinRM, keine Firmware, keine Aufgabe.
$ErrorActionPreference='Stop'
. "$PSScriptRoot/n100-boot.ps1"
$UsbSerial='USBTEST'
$PartitionGuid='{11111111-2222-3333-4444-555555555555}'
$StickSha256='a'*64
$Eintrag='{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}'
$WaechterWarAn=$true
$SynapseGestoppt=$false
$script:enabled=$true
$script:sequence=''
$script:trace=[System.Collections.Generic.List[string]]::new()
$script:badHash=$false
$script:duplicate=$false
$script:wrongBus=$false
$script:shutdownFail=$false
$script:badStop=$false
$script:manager="Firmware Boot Manager`r`n---------------------`r`nidentifier {a5a30fa2-3d06-4e9f-b5f4-a01df9d1fcba}`r`ndisplayorder {9dea862c-5cdd-4e70-acc1-f32b344d4795}`r`ntimeout 1`r`n"
function Get-Disk {
    $d=[pscustomobject]@{BusType=$(if($script:wrongBus){'NVMe'}else{'USB'});SerialNumber='USBTEST';Number=9}
    $d
    if ($script:duplicate) {$d}
}
function Get-Partition { param($DiskNumber)
    if ($DiskNumber -ne 9) {throw 'Falsche Disk'}
    [pscustomobject]@{Guid=$PartitionGuid;AccessPaths=@('\\?\Volume{11111111-2222-3333-4444-555555555555}\')}
}
function Volume-Geraet { param($Pfad)
    if ($Pfad -notlike '\\?\Volume*') {throw 'Falscher Volume-Pfad'}
    '\Device\HarddiskVolume7'
}
function Get-FileHash { param($LiteralPath,$Algorithm)
    if ($Algorithm -ne 'SHA256' -or !$LiteralPath.EndsWith('EFI\BOOT\BOOTX64.EFI')) {throw 'Falscher Hashpfad'}
    [pscustomobject]@{Hash=$(if($script:badHash){'b'*64}else{$StickSha256})}
}
function Get-CimInstance { param($ClassName)
    if ($ClassName -ne 'Win32_OperatingSystem') {throw 'Falsche Klasse'}
    [pscustomobject]@{LastBootUpTime=[datetime]'2026-11-16T20:00:00Z'}
}
function Get-ScheduledTask { param($TaskName)
    if ($TaskName -ne 'Synapse-Waechter') {throw 'Falsche Aufgabe'}
    [pscustomobject]@{Settings=[pscustomobject]@{Enabled=$script:enabled}}
}
function Disable-ScheduledTask { param($TaskName) $script:trace.Add("disable $TaskName");$script:enabled=$false }
function Enable-ScheduledTask { param($TaskName) $script:trace.Add("enable $TaskName");$script:enabled=$true }
function Stop-ScheduledTask { param($TaskName)
    $script:trace.Add("stop $TaskName")
    if($script:badStop){throw 'Teilfehler nach Disable'}
}
function Start-ScheduledTask { param($TaskName) $script:trace.Add("start $TaskName") }
function Bcd-Lesen { param($Objekt)
    if($Objekt -eq 'firmware') {
        return "Firmware Application (101fffff)`r`nidentifier $Eintrag`r`ndevice partition=\Device\HarddiskVolume7`r`ndescription BELIEBIG`r`n"
    }
    if($Objekt -ne '{fwbootmgr}') {throw 'Falscher BCD-Lesepfad'}
    $text=$script:manager
    if($script:sequence){$text+="bootsequence $script:sequence`r`n"}
    return $text
}
function bcdedit.exe {
    $global:LASTEXITCODE=0
    if($args[0] -eq '/set' -and $args[1] -eq '{fwbootmgr}' -and $args[2] -eq 'bootsequence' -and $args.Count -eq 4) {
        $script:sequence=$args[3];$script:trace.Add("set $script:sequence");return
    }
    if($args[0] -eq '/deletevalue' -and $args[1] -eq '{fwbootmgr}' -and $args[2] -eq 'bootsequence' -and $args.Count -eq 3) {
        $script:sequence='';$script:trace.Add('delete bootsequence');return
    }
    throw "Verbotener BCD-Schreibbefehl: $args"
}
function shutdown.exe {
    if(($args -join ' ') -ne '/r /t 5') {throw 'Falscher Neustartbefehl'}
    $script:trace.Add('shutdown')
    $global:LASTEXITCODE=$(if($script:shutdownFail){1}else{0})
}
function Assert($ok,$text) { if(!$ok){throw "FAIL: $text"};Write-Output "PASS: $text" }
function Muss-Scheitern([scriptblock]$AktionTest,$text) {
    $fehlgeschlagen=$false
    try { & $AktionTest | Out-Null } catch {$fehlgeschlagen=$true}
    Assert $fehlgeschlagen $text
}
$script:trace.Clear()
$Aktion='pruefen'
$r=N100-Haupt | ConvertFrom-Json
Assert ($r.entry -eq $Eintrag -and $r.device -eq '\Device\HarddiskVolume7') 'USB-Geraetepfad bestimmt den Eintrag unabhaengig vom Namen'
Assert ($script:trace.Count -eq 0) 'Inventar ist rein lesend'
$script:duplicate=$true
Muss-Scheitern {Bestand} 'Mehrdeutige USB-Disk abgelehnt'
$script:duplicate=$false
$script:wrongBus=$true
Muss-Scheitern {Bestand} 'Interne NVMe statt USB abgelehnt'
$script:wrongBus=$false
$script:badHash=$true
Muss-Scheitern {Bestand} 'Falscher Stick-Loaderhash abgelehnt'
$script:badHash=$false
$script:sequence='{bbbbbbbb-cccc-dddd-eeee-ffffffffffff}'
Muss-Scheitern {Bestand} 'Vorhandene Einmal-Bootfolge nicht ueberschrieben'
$script:sequence=''
$Aktion='pausieren'
N100-Haupt
Assert (!$script:enabled -and $script:trace.Contains('stop Synapse-Waechter')) 'Waechter pausiert und laufende Instanz gestoppt'
$Aktion='booten'
N100-Haupt
Assert ($script:sequence -eq $Eintrag -and $script:trace.Contains('shutdown')) 'Genau ein einmaliger Firmware-Start mit Neustart'
Assert ($script:manager -notmatch 'bootsequence') 'Dauerhafte Firmware-Reihenfolge unveraendert'
$Aktion='zurueck'
N100-Haupt | Out-Null
Assert ($script:enabled -and !$script:sequence) 'Eigene Bootfolge und Waechter restauriert'
$script:enabled=$false
$script:sequence='{bbbbbbbb-cccc-dddd-eeee-ffffffffffff}'
Muss-Scheitern {N100-Haupt} 'Fremde Bootfolge bleibt erhalten'
Assert ($script:enabled -and $script:sequence -ne $Eintrag) 'Waechter auch bei unabhaengigem BCD-Fehler restauriert'
$script:sequence=''
$script:shutdownFail=$true
$Aktion='booten'
Muss-Scheitern {N100-Haupt} 'Abgelehnter Neustart wird als Fehler gemeldet'
Assert (!$script:sequence) 'BootNext bei Neustartfehler zurueckgenommen'
$script:shutdownFail=$false
$script:enabled=$true
$script:badStop=$true
$Aktion='pausieren'
Muss-Scheitern {N100-Haupt} 'Teilfehler nach erfolgreichem Disable sichtbar'
$script:badStop=$false
$Aktion='zurueck'
N100-Haupt | Out-Null
Assert $script:enabled 'Wiederherstellung nach Teilfehler'
$WaechterWarAn=$false
$script:enabled=$false
$script:trace.Clear()
N100-Haupt | Out-Null
Assert (!$script:enabled -and $script:trace.Count -eq 0) 'Urspruenglich deaktivierter Waechter bleibt deaktiviert'
$deutsch="Firmwareanwendung`r`nBezeichner $Eintrag`r`nGerät partition=\Device\HarddiskVolume7`r`n"
Assert (@(Firmware-Eintraege $deutsch).Count -eq 1) 'Deutsche Firmware-Ausgabe'
Assert ((Bootfolge "Startsequenz $Eintrag") -match [regex]::Escape($Eintrag)) 'Deutsche Startsequenz wird erkannt'
Write-Output 'N100-BOOT MOCK OK'
