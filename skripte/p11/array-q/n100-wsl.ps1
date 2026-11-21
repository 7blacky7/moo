param([switch]$Worker, [switch]$Status)
$ErrorActionPreference = 'Stop'
$task = 'Moo-P11-T26-Messung'
$root = 'D:\MooTest'
$log = Join-Path $root 'p11-t26-wsl.log'
$rc = Join-Path $root 'p11-t26-wsl.rc'
if ($Worker) {
    # Bereits laufende Synapse-Distribution, kein Neustart und kein Service-Eingriff.
    $ErrorActionPreference = 'Continue'
    $text = & wsl.exe -d Synapse -- sh -lc 'set -eu; export LC_ALL=C; uname -a; getconf GNU_LIBC_VERSION; lscpu | head -n 22; taskset -c 2 /mnt/d/MooTest/moo-p11-gegen-c; taskset -c 2 /mnt/d/MooTest/moo-p11-faktor; taskset -c 2 /mnt/d/MooTest/moo-p11-simd-puffer' 2>&1
    $result = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    [IO.File]::WriteAllLines($log, [string[]]$text)
    [IO.File]::WriteAllText($rc, [string]$result)
    exit $result
}
if ($Status) {
    if (!(Test-Path -LiteralPath $rc)) {
        Write-Output 'MOO_T26_RUNNING'
        exit 0
    }
    Get-Content -LiteralPath $log
    $result = [int](Get-Content -LiteralPath $rc)
    $own = Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
    if ($own -and $own.State -ne 'Running') {
        Unregister-ScheduledTask -TaskName $task -Confirm:$false
    }
    Write-Output "MOO_T26_RC $result"
    exit $result
}
if (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue) {
    throw "Eigene Messaufgabe existiert bereits; zuerst -Status prüfen."
}
foreach ($file in @($log, $rc)) {
    if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
$principal = New-ScheduledTaskPrincipal -UserId "$env:COMPUTERNAME\MOTest" -LogonType Interactive -RunLevel Limited
$arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Worker'
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $arguments
$settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $task -Principal $principal -Action $action -Settings $settings | Out-Null
Start-ScheduledTask -TaskName $task
Write-Output 'MOO_T26_GESTARTET'
