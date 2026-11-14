# Einmalige Windows-Konfiguration fuer grosse Linux->Windows-Testdateien.
# Ausgefuehrt ueber die vorhandene Synapse-WinRM-Sitzung, nicht am Bildschirm.
$ErrorActionPreference = 'Stop'
$ordner = 'D:\MooTest'
$freigabe = 'MooTest'
$nutzer = "$env:COMPUTERNAME\MoTest"

[IO.Directory]::CreateDirectory($ordner) | Out-Null
$dienst = Get-Service -Name LanmanServer -ErrorAction Stop
if ($dienst.Status -ne 'Running') {
    Start-Service -Name LanmanServer -ErrorAction Stop
}

$vorhanden = Get-SmbShare -Name $freigabe -ErrorAction SilentlyContinue
if ($null -eq $vorhanden) {
    New-SmbShare -Name $freigabe -Path $ordner -FullAccess $nutzer -ErrorAction Stop | Out-Null
} elseif ($vorhanden.Path -ne $ordner) {
    throw "Freigabe $freigabe zeigt auf $($vorhanden.Path), erwartet $ordner"
}

$regel = Get-NetFirewallRule -Name 'MooTest-SMB-Direct' -ErrorAction SilentlyContinue
if ($null -eq $regel) {
    New-NetFirewallRule -Name 'MooTest-SMB-Direct' -DisplayName 'Moo Testkabel SMB' `
        -Direction Inbound -Action Allow -Profile Any -Protocol TCP -LocalPort 445 `
        -LocalAddress '192.168.77.2' -RemoteAddress '192.168.77.1' -ErrorAction Stop | Out-Null
}

$freigabestatus = Get-SmbShare -Name $freigabe -ErrorAction Stop
Write-Output "MOO_SMB_READY: $($freigabestatus.Name) -> $($freigabestatus.Path)"
Get-SmbShareAccess -Name $freigabe | Select-Object AccountName,AccessControlType,AccessRight
