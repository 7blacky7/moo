$ErrorActionPreference = 'Stop'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Root = [IO.Path]::GetFullPath((Get-Location).Path)
$RootPrefix = $Root.TrimEnd('\') + '\'
$LogPath = [IO.Path]::Combine($Root, 'vm-verify.txt')
$Lines = New-Object 'System.Collections.Generic.List[string]'
function Finish-Fail([string]$Message) {
    $script:Lines.Add("FAIL $Message")
    [IO.File]::WriteAllLines($script:LogPath, $script:Lines, $script:Utf8NoBom)
    Write-Error $Message
    exit 20
}
function Sha256-Bytes([byte[]]$Bytes) {
    $Sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($Sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $Sha.Dispose() }
}
$Expected = @(
'RUN_I1_WINDOWS.cmd',
'VERIFY_I1_WINDOWS.ps1',
'compiler/runtime/moo_compositor_animation.c',
'compiler/runtime/moo_compositor_animation.h',
'compiler/runtime/moo_compositor_core.c',
'compiler/runtime/moo_compositor_core.h',
'compiler/runtime/moo_compositor_effects_cpu.c',
'compiler/runtime/moo_compositor_effects_cpu.h',
'compiler/runtime/moo_compositor_effects_damage.c',
'compiler/runtime/moo_compositor_effects_damage.h',
'compiler/runtime/moo_compositor_effects_gpu.c',
'compiler/runtime/moo_compositor_effects_gpu.h',
'compiler/runtime/moo_compositor_effects_math.c',
'compiler/runtime/moo_compositor_effects_math.h',
'compiler/runtime/moo_compositor_effects_protocol.h',
'compiler/runtime/moo_compositor_effects_state.c',
'compiler/runtime/moo_compositor_effects_state.h',
'compiler/runtime/moo_compositor_protocol.h',
'compiler/runtime/moo_compositor_raster.c',
'compiler/runtime/tests/test_effects_integration.c',
'scripts/effects-test-runner.sh'
)
try {
    $ManifestPath = [IO.Path]::Combine($Root, 'SHA256SUMS')
    $MarkerPath = [IO.Path]::Combine($Root, 'I1-SNAPSHOT.txt')
    if (!(Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { Finish-Fail 'missing SHA256SUMS' }
    if (!(Test-Path -LiteralPath $MarkerPath -PathType Leaf)) { Finish-Fail 'missing marker' }
    $ManifestBytes = [IO.File]::ReadAllBytes($ManifestPath)
    $SnapshotHash = Sha256-Bytes $ManifestBytes
    $Text = [Text.Encoding]::UTF8.GetString($ManifestBytes)
    if ($Text.Contains([char]13) -or !$Text.EndsWith([string][char]10) -or ($ManifestBytes.Length -ge 3 -and $ManifestBytes[0] -eq 0xEF -and $ManifestBytes[1] -eq 0xBB -and $ManifestBytes[2] -eq 0xBF)) { Finish-Fail 'manifest encoding/newline' }
    $ManifestLines = $Text.Substring(0, $Text.Length - 1).Split([char]10)
    if ($ManifestLines.Count -ne 21) { Finish-Fail 'manifest count' }
    $Seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    for ($i = 0; $i -lt 21; $i++) {
        $Line = $ManifestLines[$i]
        if ($Line -cnotmatch '^(?<hash>[0-9a-f]{64})  (?<path>[^\r\n]+)$') { Finish-Fail "manifest syntax $i" }
        $Rel = $Matches.path
        $WantHash = $Matches.hash
        if ($Rel.Contains('\') -or $Rel.StartsWith('/') -or $Rel -match '^[A-Za-z]:' ) { Finish-Fail "rooted path $Rel" }
        $Parts = $Rel.Split('/')
        if ($Parts.Count -eq 0 -or ($Parts | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -ne 0) { Finish-Fail "traversal path $Rel" }
        if (!$Seen.Add($Rel)) { Finish-Fail "duplicate path $Rel" }
        if ($Rel -cne $Expected[$i]) { Finish-Fail "membership/order $Rel" }
        $NativeRel = $Rel.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $Full = [IO.Path]::GetFullPath([IO.Path]::Combine($Root, $NativeRel))
        if (!$Full.StartsWith($RootPrefix, [StringComparison]::OrdinalIgnoreCase)) { Finish-Fail "escaped path $Rel" }
        if (!(Test-Path -LiteralPath $Full -PathType Leaf)) { Finish-Fail "missing $Rel" }
        $Info = Get-Item -LiteralPath $Full
        if (($Info.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Finish-Fail "reparse $Rel" }
        $GotHash = (Get-FileHash -LiteralPath $Full -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($GotHash -cne $WantHash) { Finish-Fail "hash $Rel" }
        $Lines.Add("OK $GotHash  $Rel")
    }
    $Allowed = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($P in $Expected) { [void]$Allowed.Add($P) }
    [void]$Allowed.Add('SHA256SUMS')
    [void]$Allowed.Add('I1-SNAPSHOT.txt')
    $Before = @(Get-ChildItem -LiteralPath $Root -File -Recurse)
    if ($Before.Count -ne 23) { Finish-Fail "prebuild file count $($Before.Count)" }
    foreach ($File in $Before) {
        if (($File.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { Finish-Fail "reparse payload $($File.FullName)" }
        $Rel = $File.FullName.Substring($RootPrefix.Length).Replace('\', '/')
        if (!$Allowed.Contains($Rel)) { Finish-Fail "extra payload $Rel" }
    }
    $QaSource = [IO.File]::ReadAllText([IO.Path]::Combine($Root, 'compiler\runtime\tests\test_effects_integration.c'))
    if ($QaSource -notmatch '(?m)^#define QA_I1_BIND_REAL 1\r?$') { Finish-Fail 'QA_I1_BIND_REAL is not 1' }
    $MarkerLines = [IO.File]::ReadAllLines($MarkerPath)
    $ExpectedMarker = @(
      'schema=p016-i1-snapshot-v1',
      "snapshot_sha256=$SnapshotHash",
      "manifest_sha256=$SnapshotHash",
      'qa_bind_real=1',
      'expected_integration_rc=0',
      'expected_integration_marker=P016-O5 I1 INTEGRATION PASS'
    )
    if ($MarkerLines.Count -ne $ExpectedMarker.Count) { Finish-Fail 'marker count' }
    for ($i = 0; $i -lt $ExpectedMarker.Count; $i++) {
        if ($MarkerLines[$i] -cne $ExpectedMarker[$i]) { Finish-Fail "marker line $i" }
    }
    $Lines.Insert(0, "SNAPSHOT_SHA256 $SnapshotHash")
    $Lines.Add('VERIFIED count=21')
    [IO.File]::WriteAllLines($LogPath, $Lines, $Utf8NoBom)
    exit 0
}
catch { Finish-Fail $_.Exception.Message }
