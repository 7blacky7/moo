[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$SourceManifestPath,
    [Parameter(Mandatory=$true)][string]$EvidencePath,
    [Parameter(Mandatory=$true)][ValidateRange(1,300000)][int]$TimeoutMs
)
$ErrorActionPreference='Stop'

function Get-P016ProductionBoundSibling([string]$Name,[string]$ExpectedSha) {
    $path=Join-Path $PSScriptRoot $Name
    if (-not [IO.Path]::IsPathRooted($path) -or -not [IO.File]::Exists($path)) { throw 'INFRA_BOUND_SIBLING_MISSING' }
    $item=Get-Item -LiteralPath $path
    if (($item.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw 'INFRA_BOUND_SIBLING_NOT_REGULAR' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $ExpectedSha) { throw 'INFRA_BOUND_SIBLING_SHA_DRIFT' }
    return [IO.Path]::GetFullPath($path)
}
function Assert-P016ProductionFile([string]$Path,[string]$ExpectedSha,[string]$Token) {
    if ([string]::IsNullOrEmpty($Path) -or -not [IO.Path]::IsPathRooted($Path) -or -not [IO.File]::Exists($Path)) { throw ('INFRA_'+$Token+'_MISSING') }
    $item=Get-Item -LiteralPath $Path
    if (($item.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw ('INFRA_'+$Token+'_NOT_REGULAR') }
    if ((Get-P016Sha256 $Path) -cne $ExpectedSha) { throw ('INFRA_'+$Token+'_SHA_DRIFT') }
}
function Assert-P016ProductionSourceManifest {
    Assert-P016ProductionFile $SourceManifestPath $SourceManifestSha 'SOURCE_MANIFEST'
    $root=[IO.Path]::GetFullPath($RepoRoot)
    if (-not [IO.Directory]::Exists($root)) { throw 'INFRA_STAGED_ROOT_MISSING' }
    $rootItem=Get-Item -LiteralPath $root
    if (($rootItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_STAGED_ROOT_NOT_REGULAR' }
    $seen=@{}; $previous=$null
    foreach($line in [IO.File]::ReadAllLines($SourceManifestPath,(New-Object Text.UTF8Encoding($false,$true)))) {
        if ($line -cnotmatch '^([0-9a-f]{64})  ([^\r\n\t]+)$') { throw 'INFRA_SOURCE_MANIFEST_PARSE' }
        $hash=$Matches[1]; $relative=$Matches[2]
        if ([string]::IsNullOrEmpty($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains('\') -or $relative -match '(^|/)[.]{1,2}(/|$)' -or $relative -match '(^|/)target(/|$)') { throw 'INFRA_SOURCE_MANIFEST_PATH' }
        if ($null -ne $previous -and [string]::CompareOrdinal($previous,$relative) -ge 0) { throw 'INFRA_SOURCE_MANIFEST_ORDER' }; $previous=$relative
        $key=$relative; if ($seen.ContainsKey($key)) { throw 'INFRA_SOURCE_MANIFEST_DUPLICATE' }
        $full=[IO.Path]::GetFullPath((Join-Path $root $relative))
        if (-not $full.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'INFRA_SOURCE_MANIFEST_ESCAPE' }
        Assert-P016ProductionFile $full $hash 'SOURCE_ENTRY'
        $seen[$key]=$true
    }
    foreach($required in @('compiler/Cargo.toml','compiler/Cargo.lock','compiler/build.rs','compiler/src/main.rs','compiler/tests/windows_linker_behavior_helpers.ps1','compiler/tests/windows_linker_behavior_fixtures.ps1','compiler/tests/windows_linker_fixture_selftest.ps1','compiler/tests/windows_linker_production_gate.ps1','beispiele/tests/ui_moo_import_link_test.moos','stdlib/ui_moo.moos','stdlib/ui_moo_kern.moos','stdlib/ui_moo_host.moos','stdlib/ui_moo_surface.moos')) {
        if (-not $seen.ContainsKey($required)) { throw ('INFRA_SOURCE_MANIFEST_REQUIRED_'+($required -replace '[^A-Za-z0-9]','_')) }
    }
    foreach($directory in @('compiler/src','compiler/runtime','stdlib')) {
        foreach($file in [IO.Directory]::GetFiles((Join-Path $root $directory),'*',[IO.SearchOption]::AllDirectories)) {
            $relative=$file.Substring($root.Length+1).Replace('\','/')
            if (-not $seen.ContainsKey($relative)) { throw 'INFRA_SOURCE_MANIFEST_CLOSURE' }
        }
    }
    $cargoConfig=Join-Path $root '.cargo'
    if ([IO.Directory]::Exists($cargoConfig)) {
        foreach($file in [IO.Directory]::GetFiles($cargoConfig,'*',[IO.SearchOption]::AllDirectories)) {
            $relative=$file.Substring($root.Length+1).Replace('\','/')
            if (-not $seen.ContainsKey($relative)) { throw 'INFRA_SOURCE_MANIFEST_CARGO_CONFIG' }
        }
    }
}
function Assert-P016ExactProperties($Object,[string[]]$Names,[string]$Reason) {
    if ($null -eq $Object) { throw $Reason }; $properties=@($Object.PSObject.Properties)
    if ($properties.Count -ne $Names.Count) { throw $Reason }
    for($index=0;$index -lt $Names.Count;$index++) { if ([string]::CompareOrdinal([string]$properties[$index].Name,[string]$Names[$index]) -ne 0) { throw $Reason } }
}
function Assert-P016ToolchainEvidence($Evidence) {
    if ($null -eq $Evidence) { throw 'INFRA_TOOLCHAIN_EVIDENCE_NULL' }
    $names=@('cargo','rustc','llvm','clang','lld','rust_lld'); Assert-P016ExactProperties $Evidence $names 'INFRA_TOOLCHAIN_EVIDENCE_SCHEMA'
    foreach($name in $names) {
        $entry=$Evidence.PSObject.Properties[$name].Value
        Assert-P016ExactProperties $entry @('path','sha256','version') 'INFRA_TOOLCHAIN_EVIDENCE_SCHEMA'
        $path=[string]$entry.path; $sha=[string]$entry.sha256; $version=[string]$entry.version
        if ($sha -cnotmatch '^[0-9a-f]{64}$' -or [string]::IsNullOrWhiteSpace($version)) { throw 'INFRA_TOOLCHAIN_EVIDENCE_VALUE' }
        Assert-P016ProductionFile $path $sha ('TOOLCHAIN_'+$name.ToUpperInvariant())
    }
}
function Assert-P016ProductionEvidence($Evidence) {
    if ($null -eq $Evidence) { throw 'INFRA_EVIDENCE_NULL' }
    Assert-P016ExactProperties $Evidence @('schema','source_manifest_sha256','source_sha256','selftest_sha256','toolchain','toolchain_evidence_sha256','compiler','runtime','fixtures','totals','cases') 'INFRA_EVIDENCE_SCHEMA'
    Assert-P016ToolchainEvidence $Evidence.toolchain
    Assert-P016ExactProperties $Evidence.compiler @('path','sha256') 'INFRA_EVIDENCE_COMPILER_SCHEMA'
    Assert-P016ExactProperties $Evidence.runtime @('path','sha256') 'INFRA_EVIDENCE_RUNTIME_SCHEMA'
    Assert-P016ExactProperties $Evidence.fixtures @('clang','lld','payload') 'INFRA_EVIDENCE_FIXTURES_SCHEMA'
    Assert-P016ExactProperties $Evidence.totals @('compiler_starts','linker_starts','payload_exec','blocked') 'INFRA_EVIDENCE_TOTALS_SCHEMA'
    if ([string]$Evidence.schema -cne 'p016-windows-linker-production-v1' -or [string]$Evidence.source_manifest_sha256 -cne $SourceManifestSha -or [string]$Evidence.source_sha256 -cne $SourceSha -or [string]$Evidence.selftest_sha256 -cne 'c971c929e9048d8412ff8d4861b3af987f354bd4910bff77370af03d6eaeb7c4' -or [string]$Evidence.toolchain_evidence_sha256 -cne $ToolchainEvidenceSha) { throw 'INFRA_EVIDENCE_BINDING' }
    if ([string]$Evidence.compiler.path -cne $CompilerPath -or [string]$Evidence.compiler.sha256 -cne $CompilerSha -or [string]$Evidence.runtime.path -cne $RuntimePath -or [string]$Evidence.runtime.sha256 -cne $RuntimeSha) { throw 'INFRA_EVIDENCE_ARTIFACT' }
    if ([int]$Evidence.totals.compiler_starts -ne 3 -or [int]$Evidence.totals.linker_starts -ne 3 -or [int]$Evidence.totals.payload_exec -ne 1 -or [int]$Evidence.totals.blocked -ne 2) { throw 'INFRA_EVIDENCE_TOTALS' }
    $cases=@($Evidence.cases); if ($cases.Count -ne 3) { throw 'INFRA_EVIDENCE_CASES' }
    $expected=@(@('COPY0','COPY_RC0',0),@('COPY23','COPY_RC23',1),@('NOOUTPUT0','NOOUTPUT_RC0',1))
    for($i=0;$i -lt 3;$i++) {
        Assert-P016ExactProperties $cases[$i] @('name','mode','rc','stdout_sha256','stderr_sha256','stderr','argv','argv_log_sha256','output','output_hash','sentinel_hash') 'INFRA_EVIDENCE_CASE_SCHEMA'
        if ([string]$cases[$i].name -cne $expected[$i][0] -or [string]$cases[$i].mode -cne $expected[$i][1] -or [int]$cases[$i].rc -ne [int]$expected[$i][2] -or @($cases[$i].argv).Count -ne 22) { throw 'INFRA_EVIDENCE_CASE_BINDING' }
        foreach($hashName in @('stdout_sha256','stderr_sha256','argv_log_sha256')) { if ([string]$cases[$i].$hashName -cnotmatch '^[0-9a-f]{64}$') { throw 'INFRA_EVIDENCE_CASE_HASH' } }
    }
}
function Invoke-P016BoundedProcessAt {
    param([string]$FilePath,[string[]]$ArgumentList,[int]$Timeout,[hashtable]$EnvironmentOverrides,[string]$WorkingDirectory)
    $psi=New-Object Diagnostics.ProcessStartInfo
    $psi.FileName=$FilePath; $psi.Arguments=ConvertTo-P016WindowsArguments $ArgumentList
    $psi.WorkingDirectory=$WorkingDirectory; $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true
    $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
    foreach($name in $EnvironmentOverrides.Keys) {
        $value=$EnvironmentOverrides[$name]
        if ($null -eq $value) { [void]$psi.EnvironmentVariables.Remove([string]$name) }
        else { $psi.EnvironmentVariables[[string]$name]=[string]$value }
    }
    $process=New-Object Diagnostics.Process; $process.StartInfo=$psi
    try { $started=$process.Start() } catch { $errorText=$_.Exception.Message; $process.Dispose(); return [pscustomobject]@{Started=$false;TimedOut=$false;Reaped=$true;ExitCode=$null;Stdout='';Stderr='';StartError=$errorText} }
    if (-not $started) { $process.Dispose(); return [pscustomobject]@{Started=$false;TimedOut=$false;Reaped=$true;ExitCode=$null;Stdout='';Stderr='';StartError='START_RETURNED_FALSE'} }
    $stdoutTask=$process.StandardOutput.ReadToEndAsync(); $stderrTask=$process.StandardError.ReadToEndAsync()
    $timedOut=-not $process.WaitForExit($Timeout); $reaped=$true; $treeKillOk=$true
    if ($timedOut) {
        $treeKillOk=$false
        try {
            $killInfo=New-Object Diagnostics.ProcessStartInfo
            $killInfo.FileName=Join-Path $env:SystemRoot 'System32\taskkill.exe'
            $killInfo.Arguments='/PID '+$process.Id+' /T /F'; $killInfo.UseShellExecute=$false; $killInfo.CreateNoWindow=$true
            $killInfo.RedirectStandardOutput=$true; $killInfo.RedirectStandardError=$true
            $killer=New-Object Diagnostics.Process; $killer.StartInfo=$killInfo
            if ($killer.Start()) {
                $killStdout=$killer.StandardOutput.ReadToEndAsync(); $killStderr=$killer.StandardError.ReadToEndAsync()
                $killExited=$killer.WaitForExit($Timeout)
                if (-not $killExited) { try { $killer.Kill() } catch {}; $killExited=$killer.WaitForExit(5000) }
                $killOutDone=$false; $killErrDone=$false; if ($killExited) { $killOutDone=$killStdout.Wait(5000); $killErrDone=$killStderr.Wait(5000) }; $killDrained=$killOutDone -and $killErrDone
                $treeKillOk=$killDrained -and $killer.ExitCode -eq 0
            }
            $killer.Dispose()
        } catch { $treeKillOk=$false }
        $reaped=$process.WaitForExit($Timeout)
    } else { $process.WaitForExit() }
    $stdout=''; $stderr=''; $exitCode=$null; $drained=$false
    if ($reaped) { $stdoutDone=$stdoutTask.Wait(5000); $stderrDone=$stderrTask.Wait(5000); $drained=$stdoutDone -and $stderrDone }
    if ($reaped -and $drained) { $stdout=$stdoutTask.Result; $stderr=$stderrTask.Result; $exitCode=$process.ExitCode } else { $reaped=$false }
    $process.Dispose()
    $startError=$null; if (-not $reaped -or ($timedOut -and -not $treeKillOk)) { $startError='TREE_REAP_FAILED' }
    return [pscustomobject]@{Started=$true;TimedOut=$timedOut;Reaped=$reaped;ExitCode=$exitCode;Stdout=$stdout;Stderr=$stderr;StartError=$startError;TreeKillOk=$treeKillOk}
}
function Assert-P016OrdinalCount([string[]]$Values,[string]$Expected,[int]$Count,[string]$Token) {
    $actual=@($Values | Where-Object { [string]::Equals([string]$_,$Expected,[StringComparison]::Ordinal) }).Count
    Assert-P016SelfTestContract ($actual -eq $Count) $Token
}
function Get-P016StringSha256([string]$Value) {
    $algorithm=New-Object Security.Cryptography.SHA256Managed
    try { return [BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function ConvertTo-P016ProductionReason([string]$Message) {
    $token=(([string]$Message).ToUpperInvariant() -replace '^(FAIL_|INFRA_)','' -replace '[^A-Z0-9_]+','_').Trim('_')
    if ($token.Length -eq 0) { $token='UNKNOWN' }; if ($token.Length -gt 80) { $token=$token.Substring(0,80) }; return $token
}
function Invoke-P016WindowsLinkerProductionGate {
    $helper=Get-P016ProductionBoundSibling 'windows_linker_behavior_helpers.ps1' 'ab65a9e2670eed4c2edb3fa87852b15b0706c2ae27731eaffdc4e9ce11eeac50'
    $fixturesScript=Get-P016ProductionBoundSibling 'windows_linker_behavior_fixtures.ps1' '2b7d211736f415d0c6e8d3427e9a5fa5424105fc1cab6cd60f7a65dea57f33e1'
    $selftest=Get-P016ProductionBoundSibling 'windows_linker_fixture_selftest.ps1' 'c971c929e9048d8412ff8d4861b3af987f354bd4910bff77370af03d6eaeb7c4'
    try { . $helper; . $fixturesScript; . $selftest } catch { throw 'INFRA_IMPORT_FAILED' }
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or $PSVersionTable.PSVersion.ToString() -notlike '5.1*') { throw 'INFRA_WINDOWS_PS51_REQUIRED' }
    foreach($path in @($CompilerPath,$RuntimePath,$SourcePath,$SourceManifestPath)) {
        if (-not [IO.Path]::IsPathRooted($path) -or -not [IO.File]::Exists($path)) { throw 'INFRA_INPUT_PATH' }
    }
    $CompilerSha=Get-P016Sha256 $CompilerPath; $RuntimeSha=Get-P016Sha256 $RuntimePath
    $SourceSha=Get-P016Sha256 $SourcePath; $SourceManifestSha=Get-P016Sha256 $SourceManifestPath
    Assert-P016ProductionFile $CompilerPath $CompilerSha 'COMPILER'
    Assert-P016ProductionFile $RuntimePath $RuntimeSha 'RUNTIME'
    Assert-P016ProductionFile $SourcePath $SourceSha 'SOURCE'
    $rootFull=[IO.Path]::GetFullPath($RepoRoot)
    if (-not [IO.Directory]::Exists($rootFull) -or ((Get-Item -LiteralPath $rootFull).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_STAGED_ROOT' }
    $expectedSource=[IO.Path]::GetFullPath((Join-Path $rootFull 'beispiele/tests/ui_moo_import_link_test.moos'))
    if (-not [string]::Equals([IO.Path]::GetFullPath($SourcePath),$expectedSource,[StringComparison]::OrdinalIgnoreCase)) { throw 'INFRA_SOURCE_PATH' }
    $runRoot=[IO.Directory]::GetParent($rootFull).FullName
    if ([IO.Path]::GetFileName($rootFull) -cne 'source') { throw 'INFRA_STAGED_ROOT_NAME' }
    $targetRoot=[IO.Path]::GetFullPath((Join-Path $runRoot 'target'))
    $tripleRoot=Join-Path $targetRoot 'x86_64-pc-windows-msvc'; $releaseRoot=Join-Path $tripleRoot 'release'; $buildRoot=Join-Path $releaseRoot 'build'
    foreach($directory in @($targetRoot,$tripleRoot,$releaseRoot,$buildRoot)) {
        if (-not [IO.Directory]::Exists($directory) -or ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_TARGET_DIRECTORY' }
    }
    $expectedCompiler=[IO.Path]::GetFullPath((Join-Path $releaseRoot 'moo-compiler.exe'))
    if (-not [string]::Equals([IO.Path]::GetFullPath($CompilerPath),$expectedCompiler,[StringComparison]::OrdinalIgnoreCase)) { throw 'INFRA_COMPILER_PATH' }
    if ((Get-P016PeMachine $CompilerPath) -ne 0x8664) { throw 'INFRA_COMPILER_MACHINE' }
    $runtimeFull=[IO.Path]::GetFullPath($RuntimePath); $buildPrefix=[IO.Path]::GetFullPath($buildRoot)+[IO.Path]::DirectorySeparatorChar
    if (-not $runtimeFull.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase) -or $runtimeFull.Substring($buildPrefix.Length) -cnotmatch '^moo-compiler-[^\\]+\\out\\moo_runtime[.]lib$') { throw 'INFRA_RUNTIME_PATH' }
    $expectedManifest=[IO.Path]::GetFullPath((Join-Path $runRoot 'source-sha256.txt'))
    if (-not [string]::Equals([IO.Path]::GetFullPath($SourceManifestPath),$expectedManifest,[StringComparison]::OrdinalIgnoreCase)) { throw 'INFRA_SOURCE_MANIFEST_PATH' }
    if (-not [IO.Path]::IsPathRooted($EvidencePath) -or (Test-Path -LiteralPath $EvidencePath)) { throw 'INFRA_EVIDENCE_PRECONDITION' }
    $evidenceParent=[IO.Path]::GetFullPath((Split-Path -Parent $EvidencePath)); $expectedEvidenceParent=[IO.Path]::GetFullPath((Join-Path $runRoot 'evidence'))
    if (-not [string]::Equals($evidenceParent,$expectedEvidenceParent,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($EvidencePath) -cne 'production-evidence.json') { throw 'INFRA_EVIDENCE_PATH' }
    if (-not [IO.Directory]::Exists($evidenceParent) -or ((Get-Item -LiteralPath $evidenceParent).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_EVIDENCE_PARENT' }
    $ToolchainEvidencePath=Join-Path $evidenceParent 'toolchain.json'
    $ToolchainEvidenceSha=Get-P016Sha256 $ToolchainEvidencePath
    Assert-P016ProductionFile $ToolchainEvidencePath $ToolchainEvidenceSha 'TOOLCHAIN_EVIDENCE'
    [byte[]]$toolchainRaw=[IO.File]::ReadAllBytes($ToolchainEvidencePath)
    if ($toolchainRaw.Length -ge 3 -and $toolchainRaw[0] -eq 0xef -and $toolchainRaw[1] -eq 0xbb -and $toolchainRaw[2] -eq 0xbf) { throw 'INFRA_TOOLCHAIN_EVIDENCE_BOM' }
    try { $toolchainText=(New-Object Text.UTF8Encoding($false,$true)).GetString($toolchainRaw); $toolchain=$toolchainText | ConvertFrom-Json } catch { throw 'INFRA_TOOLCHAIN_EVIDENCE_PARSE' }
    Assert-P016ToolchainEvidence $toolchain
    Assert-P016ProductionSourceManifest
    $powershell=Join-Path $PSHOME 'powershell.exe'
    $self=Invoke-P016BoundedProcessAt $powershell @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$selftest) $TimeoutMs @{} $RepoRoot
    Assert-P016SelfTestProcessInfra $self 'BOUND_SELFTEST'
    Assert-P016SelfTestContract ($self.ExitCode -eq 0) 'BOUND_SELFTEST_RC'
    Assert-P016SelfTestContract ([string]$self.Stderr -ceq '') 'BOUND_SELFTEST_STDERR'
    $selfMarker='P016 WINDOWS LINKER FIXTURE SELFTEST PASS'
    $selfLf=$selfMarker+[string][char]10; $selfCrlf=$selfMarker+[string][char]13+[char]10
    Assert-P016SelfTestContract ([string]$self.Stdout -ceq $selfLf -or [string]$self.Stdout -ceq $selfCrlf) 'BOUND_SELFTEST_MARKER'
    $owner=Join-Path ([IO.Path]::GetTempPath()) ('p016 production linker '+[guid]::NewGuid().ToString('N'))
    $ownerCreated=$false; $caseEvidence=New-Object 'System.Collections.Generic.List[object]'
    $envNames=@('MOO_CLANG','MOO_LLD','MOO_RUNTIME_LIB','MOO_QUIET','MOO_FAKE_MODE','MOO_FAKE_PAYLOAD','MOO_FAKE_ARGV_DIR','MOO_FAKE_SENTINEL','TEMP','TMP')
    $snapshot=Get-P016EnvironmentSnapshot $envNames
    try {
        if (-not [IO.Path]::IsPathRooted($owner) -or (Test-Path -LiteralPath $owner)) { throw 'INFRA_OWNER_PRECONDITION' }
        $null=New-Item -ItemType Directory -Path $owner -ErrorAction Stop; $ownerCreated=$true
        if (((Get-Item -LiteralPath $owner).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_OWNER_REPARSE' }
        $fixtures=New-P016WindowsLinkerFixtures -Root (Join-Path $owner 'fixtures')
        Assert-P016SelfTestManifest $fixtures.Manifest
        $definitions=@(
            [pscustomobject]@{Name='COPY0';Mode='COPY_RC0';Rc=0;Kind='EXECUTED'},
            [pscustomobject]@{Name='COPY23';Mode='COPY_RC23';Rc=1;Kind='COPIED'},
            [pscustomobject]@{Name='NOOUTPUT0';Mode='NOOUTPUT_RC0';Rc=1;Kind='ABSENT'}
        )
        foreach($definition in $definitions) {
            $caseRoot=Join-Path $owner $definition.Name; $temp=Join-Path $caseRoot 'temp'; $argv=Join-Path $caseRoot 'argv'
            $null=New-Item -ItemType Directory -Path $caseRoot -ErrorAction Stop
            $null=New-Item -ItemType Directory -Path $temp -ErrorAction Stop; $null=New-Item -ItemType Directory -Path $argv -ErrorAction Stop
            if (((Get-Item -LiteralPath $caseRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ((Get-Item -LiteralPath $temp).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ((Get-Item -LiteralPath $argv).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('INFRA_'+$definition.Name+'_DIRECTORY_REPARSE') }
            $caseSource=Join-Path $caseRoot 'ui_moo_import_link_test.moos'; [IO.File]::Copy($SourcePath,$caseSource,$false)
            Assert-P016ProductionFile $caseSource $SourceSha ($definition.Name+'_SOURCE')
            $object=[IO.Path]::ChangeExtension($caseSource,'o'); $sentinel=Join-Path $caseRoot 'payload sentinel.txt'
            if ((Test-Path -LiteralPath $object) -or (Test-Path -LiteralPath $sentinel)) { throw ('INFRA_'+$definition.Name+'_PRECONDITION') }
            $overrides=@{
                MOO_CLANG=$fixtures.ClangPath; MOO_LLD=$fixtures.LldPath; MOO_RUNTIME_LIB=$RuntimePath; MOO_QUIET='1'
                MOO_FAKE_MODE=$definition.Mode; MOO_FAKE_PAYLOAD=$fixtures.PayloadPath; MOO_FAKE_ARGV_DIR=$argv; MOO_FAKE_SENTINEL=$sentinel
                TEMP=$temp; TMP=$temp
            }
            $failure=$null; $itemEvidence=$null
            try {
                $result=Invoke-P016BoundedProcessAt $CompilerPath @('run',$caseSource) $TimeoutMs $overrides $RepoRoot
                Assert-P016SelfTestProcessInfra $result ('COMPILER_'+$definition.Name)
                $decoded=Read-P016StrictArgvLog $argv; $tokens=@($decoded.Tokens)
                Assert-P016SelfTestContract ($tokens.Count -eq 22) ($definition.Name+'_ARGV_COUNT')
                Assert-P016SelfTestContract ($tokens[2] -ceq '-o') ($definition.Name+'_OUTPUT_SWITCH')
                $output=[string]$tokens[3]
                Assert-P016SelfTestContract ([IO.Path]::IsPathRooted($output)) ($definition.Name+'_OUTPUT_ABSOLUTE')
                Assert-P016SelfTestContract ([string]::Equals([IO.Path]::GetFullPath((Split-Path -Parent $output)),[IO.Path]::GetFullPath($temp),[StringComparison]::OrdinalIgnoreCase)) ($definition.Name+'_OUTPUT_OWNER')
                Assert-P016SelfTestContract ([IO.Path]::GetFileName($output) -cmatch '^moo_tmp_binary_[1-9][0-9]*[.]exe$') ($definition.Name+'_OUTPUT_NAME')
                $expected=@($object,$RuntimePath,'-o',$output,'-fms-runtime-lib=dll','-Wl,/NODEFAULTLIB:libcmt','-llibcurl','-lsqlite3','-lws2_32','-lbcrypt','-lmfplat','-lmf','-lmfreadwrite','-lmfuuid','-lole32','-loleaut32','-luuid',('--ld-path='+$fixtures.LldPath),'-lmsvcrt','-lvcruntime','-lucrt','-lkernel32')
                for($i=0;$i -lt $expected.Count;$i++) { Assert-P016SelfTestContract ([string]::Equals([string]$tokens[$i],[string]$expected[$i],[StringComparison]::Ordinal)) ($definition.Name+'_ARGV_'+$i) }
                Assert-P016OrdinalCount $tokens '--ld-path' 0 ($definition.Name+'_BARE_LD_PATH')
                Assert-P016OrdinalCount $tokens '-fuse-ld=lld' 0 ($definition.Name+'_FUSE_LLD')
                foreach($token in $tokens) { if ([IO.Path]::GetFileName([string]$token) -ieq 'link.exe') { throw ('FAIL_'+$definition.Name+'_LINK_EXE') } }
                Assert-P016SelfTestContract ($result.ExitCode -eq $definition.Rc) ($definition.Name+'_RC')
                Assert-P016SelfTestContract ([string]$result.Stdout -ceq '') ($definition.Name+'_STDOUT')
                $errorText='Fehler: Linken fehlgeschlagen oder Ausgabedatei fehlt'
                if ($definition.Rc -eq 0) { Assert-P016SelfTestContract ([string]$result.Stderr -ceq '') ($definition.Name+'_STDERR') }
                else {
                    $stderrLf=$errorText+[string][char]10; $stderrCrlf=$errorText+[string][char]13+[char]10
                    Assert-P016SelfTestContract ([string]$result.Stderr -ceq $stderrLf -or [string]$result.Stderr -ceq $stderrCrlf) ($definition.Name+'_STDERR')
                }
                Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $object)) ($definition.Name+'_OBJECT_CLEANUP')
                $outputHash=$null; $sentinelHash=$null
                if ($definition.Kind -ceq 'EXECUTED') {
                    Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $output)) 'COPY0_OUTPUT_CLEANUP'
                    Assert-P016SelfTestContract ([IO.File]::Exists($sentinel)) 'COPY0_SENTINEL_MISSING'
                    if (((Get-Item -LiteralPath $sentinel).Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw 'INFRA_COPY0_SENTINEL_NOT_REGULAR' }
                    [byte[]]$actual=[IO.File]::ReadAllBytes($sentinel); [byte[]]$expectedSentinel=[Text.Encoding]::ASCII.GetBytes('PAYLOAD_EXECUTED')+[byte]10
                    Assert-P016SelfTestContract ([Convert]::ToBase64String($actual) -ceq [Convert]::ToBase64String($expectedSentinel)) 'COPY0_SENTINEL_BYTES'
                    $sentinelHash=Get-P016Sha256 $sentinel
                } elseif($definition.Kind -ceq 'COPIED') {
                    Assert-P016SelfTestContract ([IO.File]::Exists($output)) 'COPY23_OUTPUT_MISSING'
                    Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $sentinel)) 'COPY23_SENTINEL'
                    Assert-P016ProductionFile $output ([string]$fixtures.PayloadHash) 'COPY23_OUTPUT'
                    $outputHash=Get-P016Sha256 $output
                } else {
                    Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $output)) 'NOOUTPUT0_OUTPUT'
                    Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $sentinel)) 'NOOUTPUT0_SENTINEL'
                }
                $itemEvidence=[ordered]@{name=$definition.Name;mode=$definition.Mode;rc=$result.ExitCode;stdout_sha256=(Get-P016StringSha256 $result.Stdout);stderr_sha256=(Get-P016StringSha256 $result.Stderr);stderr=$result.Stderr;argv=$tokens;argv_log_sha256=(Get-P016Sha256 $decoded.LogPath);output=$output;output_hash=$outputHash;sentinel_hash=$sentinelHash}
            } catch { $failure=$_ }
            Assert-P016SelfTestManifest $fixtures.Manifest; Assert-P016ProductionSourceManifest
            if ($null -ne $failure) { throw $failure }; $null=$caseEvidence.Add($itemEvidence)
        }
        Assert-P016SelfTestContract ($caseEvidence.Count -eq 3) 'CASE_EVIDENCE_COUNT'
        $document=[ordered]@{
            schema='p016-windows-linker-production-v1';source_manifest_sha256=$SourceManifestSha;source_sha256=$SourceSha
            selftest_sha256='c971c929e9048d8412ff8d4861b3af987f354bd4910bff77370af03d6eaeb7c4'
            toolchain=$toolchain;toolchain_evidence_sha256=$ToolchainEvidenceSha
            compiler=[ordered]@{path=$CompilerPath;sha256=$CompilerSha};runtime=[ordered]@{path=$RuntimePath;sha256=$RuntimeSha}
            fixtures=[ordered]@{clang=$fixtures.ClangHash;lld=$fixtures.LldHash;payload=$fixtures.PayloadHash}
            totals=[ordered]@{compiler_starts=3;linker_starts=3;payload_exec=1;blocked=2};cases=$caseEvidence.ToArray()
        }
    } finally {
        $final=New-Object 'System.Collections.Generic.List[string]'
        try { if (-not (Test-P016EnvironmentSnapshot $snapshot)) { $final.Add('ENV_DRIFT') } } catch { $final.Add('ENV_VERIFY') }
        if ($ownerCreated) { try { [IO.Directory]::Delete($owner,$true) } catch { $final.Add('CLEANUP') }; if (Test-Path -LiteralPath $owner) { $final.Add('CLEANUP_DRIFT') } }
        if ($final.Count -gt 0) { throw ('INFRA_FINALIZE_'+($final -join '_')) }
    }
    Assert-P016ProductionSourceManifest
    $json=$document | ConvertTo-Json -Depth 8 -Compress
    $utf8=New-Object Text.UTF8Encoding($false,$true); $temporary=$EvidencePath+'.tmp'
    if (Test-Path -LiteralPath $temporary) { throw 'INFRA_EVIDENCE_TMP_EXISTS' }
    try { [IO.File]::WriteAllText($temporary,$json,$utf8); [IO.File]::Move($temporary,$EvidencePath) }
    catch { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }; throw 'INFRA_EVIDENCE_WRITE' }
    [byte[]]$finalRaw=[IO.File]::ReadAllBytes($EvidencePath)
    if ($finalRaw.Length -ge 3 -and $finalRaw[0] -eq 0xef -and $finalRaw[1] -eq 0xbb -and $finalRaw[2] -eq 0xbf) { throw 'INFRA_EVIDENCE_BOM' }
    try { $finalText=(New-Object Text.UTF8Encoding($false,$true)).GetString($finalRaw); $finalDocument=$finalText | ConvertFrom-Json } catch { throw 'INFRA_EVIDENCE_PARSE' }
    Assert-P016ProductionEvidence $finalDocument
    $EvidenceSha=Get-P016Sha256 $EvidencePath
    Assert-P016ProductionFile $EvidencePath $EvidenceSha 'EVIDENCE'
    if ((Get-P016Sha256 $EvidencePath) -cne $EvidenceSha) { throw 'INFRA_EVIDENCE_SHA_DRIFT' }
    return [pscustomobject]@{Document=$finalDocument;CompilerSha=$CompilerSha;RuntimeSha=$RuntimeSha;EvidenceSha=$EvidenceSha}
}
if ($MyInvocation.InvocationName -eq '.') { return }
try {
    $gate=Invoke-P016WindowsLinkerProductionGate
    Write-Output ('P016 WINDOWS LINKER PRODUCTION GATE PASS: cases=3 compiler_sha256='+$gate.CompilerSha+' runtime_sha256='+$gate.RuntimeSha+' evidence_sha256='+$gate.EvidenceSha)
    exit 0
} catch {
    $raw=[string]$_.Exception.Message; $token=ConvertTo-P016ProductionReason $raw
    if ($raw.StartsWith('FAIL_')) { Write-Output ('P016 WINDOWS LINKER PRODUCTION GATE FAIL: reason='+$token); exit 1 }
    Write-Output ('P016 WINDOWS LINKER PRODUCTION GATE INFRA_NOGO: reason='+$token); exit 2
}
