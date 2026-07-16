function ConvertTo-P016WindowsArgument([string]$Value) {
    if ($null -eq $Value -or $Value.Length -eq 0) { return '""' }
    if ($Value -notmatch '[\s"]') { return $Value }
    $builder=New-Object Text.StringBuilder
    [void]$builder.Append([char]34); $slashes=0
    foreach($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { $slashes++; continue }
        if ($character -eq [char]34) {
            if ($slashes -gt 0) { [void]$builder.Append(([string][char]92) * ($slashes * 2)) }
            [void]$builder.Append([char]92); [void]$builder.Append([char]34); $slashes=0; continue
        }
        if ($slashes -gt 0) { [void]$builder.Append(([string][char]92) * $slashes); $slashes=0 }
        [void]$builder.Append($character)
    }
    if ($slashes -gt 0) { [void]$builder.Append(([string][char]92) * ($slashes * 2)) }
    [void]$builder.Append([char]34)
    return $builder.ToString()
}
function ConvertTo-P016WindowsArguments([string[]]$Values) {
    if ($null -eq $Values -or $Values.Count -eq 0) { return '' }
    return (($Values | ForEach-Object { ConvertTo-P016WindowsArgument $_ }) -join ' ')
}
function Invoke-P016BoundedProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$FilePath,
        [string[]]$Args=@(),
        [ValidateRange(1,300000)][int]$TimeoutMs=30000,
        [hashtable]$EnvironmentOverrides=@{}
    )
    $ErrorActionPreference='Stop'
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$FilePath; $psi.Arguments=ConvertTo-P016WindowsArguments $Args
    $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    foreach($name in $EnvironmentOverrides.Keys) {
        $value=$EnvironmentOverrides[$name]
        if ($null -eq $value) { [void]$psi.EnvironmentVariables.Remove([string]$name) }
        else { $psi.EnvironmentVariables[[string]$name]=[string]$value }
    }
    $process=New-Object Diagnostics.Process; $process.StartInfo=$psi
    try { $started=$process.Start() }
    catch {
        $reason=$_.Exception.Message; $process.Dispose()
        return [pscustomobject]@{ Started=$false; TimedOut=$false; Reaped=$true; ExitCode=$null; Stdout=''; Stderr=''; StartError=$reason }
    }
    if (-not $started) {
        $process.Dispose()
        return [pscustomobject]@{ Started=$false; TimedOut=$false; Reaped=$true; ExitCode=$null; Stdout=''; Stderr=''; StartError='START_RETURNED_FALSE' }
    }
    $stdoutTask=$process.StandardOutput.ReadToEndAsync()
    $stderrTask=$process.StandardError.ReadToEndAsync()
    $timedOut=-not $process.WaitForExit($TimeoutMs); $reaped=$true
    if ($timedOut) {
        try { $process.Kill() } catch { }
        $reaped=$process.WaitForExit($TimeoutMs)
    } else { $process.WaitForExit() }
    $stdout=''; $stderr=''; $exitCode=$null
    if ($reaped) {
        $stdoutTask.Wait(); $stderrTask.Wait()
        $stdout=$stdoutTask.Result; $stderr=$stderrTask.Result; $exitCode=$process.ExitCode
    }
    $process.Dispose()
    return [pscustomobject]@{ Started=$true; TimedOut=$timedOut; Reaped=$reaped; ExitCode=$exitCode; Stdout=$stdout; Stderr=$stderr; StartError=$null }
}
function Get-P016EnvironmentSnapshot([string[]]$Names) {
    $ErrorActionPreference='Stop'; $environment=[Environment]::GetEnvironmentVariables('Process'); $snapshot=@{}
    foreach($name in $Names) {
        $snapshot[$name]=[pscustomobject]@{
            Exists=$environment.Contains($name)
            Value=[Environment]::GetEnvironmentVariable($name,'Process')
        }
    }
    return $snapshot
}
function Set-P016EmptyProcessEnvironment([string]$Name) {
    $ErrorActionPreference='Stop'
    if (-not ('P016.NativeEnvironment' -as [type])) {
        $source=@'
using System.Runtime.InteropServices; namespace P016 { public static class NativeEnvironment { [DllImport("kernel32.dll",EntryPoint="SetEnvironmentVariableW",CharSet=CharSet.Unicode,SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)] public static extern bool SetEnvironmentVariableW(string name,string value); } }
'@
        $null=Add-Type -TypeDefinition $source -Language CSharp
    }
    if (-not [P016.NativeEnvironment]::SetEnvironmentVariableW($Name,[string]::Empty)) { throw ('EMPTY_ENV_SET_FAILED_'+[Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
}
function Restore-P016EnvironmentSnapshot([hashtable]$Snapshot) {
    $ErrorActionPreference='Stop'
    foreach($name in $Snapshot.Keys) {
        $entry=$Snapshot[$name]
        if (-not $entry.Exists) { [Environment]::SetEnvironmentVariable($name,$null,'Process') }
        elseif ([string]$entry.Value -ceq '') { Set-P016EmptyProcessEnvironment $name }
        else { [Environment]::SetEnvironmentVariable($name,[string]$entry.Value,'Process') }
    }
}
function Test-P016EnvironmentSnapshot([hashtable]$Snapshot) {
    $ErrorActionPreference='Stop'; $environment=[Environment]::GetEnvironmentVariables('Process')
    foreach($name in $Snapshot.Keys) {
        $entry=$Snapshot[$name]
        if ($environment.Contains($name) -ne $entry.Exists) { return $false }
        if ($entry.Exists -and [Environment]::GetEnvironmentVariable($name,'Process') -cne [string]$entry.Value) { return $false }
    }
    return $true
}
function Get-P016Sha256([string]$Path) {
    $ErrorActionPreference='Stop'; return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function New-P016Sha256Manifest([string[]]$Paths) {
    $ErrorActionPreference='Stop'; $entries=@(); $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
    foreach($path in $Paths) {
        if (-not [IO.Path]::IsPathRooted($path)) { throw 'MANIFEST_PATH_NOT_ABSOLUTE' }
        $full=[IO.Path]::GetFullPath($path)
        if (-not (Test-Path -LiteralPath $full)) { throw 'MANIFEST_FILE_NOT_REGULAR' }
        $item=Get-Item -LiteralPath $full
        if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists($full)) { throw 'MANIFEST_FILE_NOT_REGULAR' }
        $entries += [pscustomobject]@{ Path=$full; Bytes=[int64]$item.Length; Hash=(Get-P016Sha256 $full) }
    }
    return [pscustomobject]@{ Entries=$entries }
}
function Test-P016Sha256Manifest($Manifest) {
    $ErrorActionPreference='Stop'; $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
    foreach($entry in $Manifest.Entries) {
        if (-not [IO.Path]::IsPathRooted([string]$entry.Path)) { return [pscustomobject]@{ Valid=$false; Reason='PATH_NOT_ABSOLUTE'; Path=$entry.Path } }
        if (-not (Test-Path -LiteralPath $entry.Path)) { return [pscustomobject]@{ Valid=$false; Reason='FILE_MISSING'; Path=$entry.Path } }
        $item=Get-Item -LiteralPath $entry.Path
        if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists([string]$entry.Path)) { return [pscustomobject]@{ Valid=$false; Reason='FILE_NOT_REGULAR'; Path=$entry.Path } }
        if ([int64]$item.Length -ne [int64]$entry.Bytes) { return [pscustomobject]@{ Valid=$false; Reason='BYTE_LENGTH_DRIFT'; Path=$entry.Path } }
        if ((Get-P016Sha256 $entry.Path) -cne [string]$entry.Hash) { return [pscustomobject]@{ Valid=$false; Reason='SHA256_DRIFT'; Path=$entry.Path } }
    }
    return [pscustomobject]@{ Valid=$true; Reason='OK'; Path=$null }
}
if ($MyInvocation.InvocationName -eq '.') { return }
Write-Output 'P016 WINDOWS LINKER HELPERS INFRA_NOGO: reason=DOT_SOURCE_REQUIRED'
exit 2