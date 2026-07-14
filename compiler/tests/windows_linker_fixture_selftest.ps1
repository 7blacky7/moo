function Get-P016SelfTestBoundSibling([string]$Name, [string]$ExpectedSha) {
    $ErrorActionPreference='Stop'
    $path=Join-Path $PSScriptRoot $Name
    if (-not [IO.Path]::IsPathRooted($path) -or -not (Test-Path -LiteralPath $path)) { throw 'INFRA_SIBLING_MISSING' }
    $item=Get-Item -LiteralPath $path
    $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
    if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists($path)) { throw 'INFRA_SIBLING_NOT_REGULAR' }
    $actual=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $ExpectedSha) { throw 'INFRA_SIBLING_SHA_DRIFT' }
    return [IO.Path]::GetFullPath($path)
}
function ConvertTo-P016SelfTestReason([string]$Message) {
    $token=($Message -replace '^(FAIL_|INFRA_)','' -replace '[^A-Za-z0-9_]+','_').Trim('_')
    if ($token.Length -eq 0) { $token='UNKNOWN' }
    if ($token.Length -gt 80) { $token=$token.Substring(0,80) }
    return $token
}
function Assert-P016SelfTestContract([bool]$Condition, [string]$Token) {
    if (-not $Condition) { throw ('FAIL_'+$Token) }
}
function Assert-P016SelfTestProcessInfra($Result, [string]$Name) {
    if ($null -eq $Result) { throw ('INFRA_'+$Name+'_EVIDENCE_MISSING') }
    foreach($property in @('Started','TimedOut','Reaped')) {
        if ($null -eq $Result.PSObject.Properties[$property] -or $Result.$property -isnot [bool]) { throw ('INFRA_'+$Name+'_EVIDENCE_MALFORMED') }
    }
    if (-not $Result.Started) { throw ('INFRA_'+$Name+'_START_FAILED') }
    if ($Result.TimedOut) { throw ('INFRA_'+$Name+'_TIMEOUT') }
    if (-not $Result.Reaped) { throw ('INFRA_'+$Name+'_UNREAPED') }
    if ($null -eq $Result.PSObject.Properties['ExitCode'] -or $Result.ExitCode -isnot [int]) { throw ('INFRA_'+$Name+'_EVIDENCE_MALFORMED') }
    foreach($property in @('Stdout','Stderr')) {
        if ($null -eq $Result.PSObject.Properties[$property] -or $null -eq $Result.$property -or $Result.$property -isnot [string]) { throw ('INFRA_'+$Name+'_EVIDENCE_MALFORMED') }
    }
}
function Assert-P016SelfTestManifest($Manifest) {
    if ($null -eq $Manifest -or $null -eq $Manifest.Entries -or $Manifest.Entries.Count -ne 3) { throw 'INFRA_MANIFEST_MALFORMED' }
    $check=Test-P016Sha256Manifest $Manifest
    if ($null -eq $check -or $null -eq $check.Valid) { throw 'INFRA_MANIFEST_EVIDENCE_MALFORMED' }
    if (-not $check.Valid) { throw ('INFRA_MANIFEST_'+[string]$check.Reason) }
}
function Read-P016StrictArgvLog([string]$Directory) {
    $ErrorActionPreference='Stop'
    try {
        if ([string]::IsNullOrEmpty($Directory) -or -not [IO.Path]::IsPathRooted($Directory) -or -not [IO.Directory]::Exists($Directory)) { throw 'INFRA_ARGV_DIR_INVALID' }
        $dirItem=Get-Item -LiteralPath $Directory
        if (($dirItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or ($dirItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_ARGV_DIR_NOT_REGULAR' }
        $entries=@([IO.Directory]::GetFileSystemEntries($Directory))
        if ($entries.Count -ne 1 -or [IO.Path]::GetFileName($entries[0]) -notlike 'argv-*.log') { throw 'INFRA_ARGV_ENTRY_SET' }
        $log=[IO.Path]::GetFullPath($entries[0]); $logItem=Get-Item -LiteralPath $log
        if (-not [IO.File]::Exists($log) -or ($logItem.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw 'INFRA_ARGV_LOG_NOT_REGULAR' }
        [byte[]]$raw=[IO.File]::ReadAllBytes($log)
        if ($raw.Length -ge 3 -and $raw[0] -eq 0xef -and $raw[1] -eq 0xbb -and $raw[2] -eq 0xbf) { throw 'INFRA_ARGV_BOM' }
        $utf8=New-Object 'System.Text.UTF8Encoding' ($false,$true)
        $text=$utf8.GetString($raw); $crlf=[string][char]13+[char]10
        if ($text.Length -eq 0 -or -not $text.EndsWith($crlf,[StringComparison]::Ordinal)) { throw 'INFRA_ARGV_FRAMING' }
        $records=$text.Split([string[]]@($crlf),[StringSplitOptions]::None)
        if ($records.Count -lt 2 -or $records[$records.Count-1] -cne '') { throw 'INFRA_ARGV_FRAMING' }
        $tokens=New-Object 'System.Collections.Generic.List[string]'
        for($i=0; $i -lt $records.Count-1; $i++) {
            $record=$records[$i]
            if ($record.Length -eq 0 -or $record.IndexOf([char]13) -ge 0 -or $record.IndexOf([char]10) -ge 0 -or $record -cnotmatch '^(0|[1-9][0-9]*):([A-Za-z0-9+/]*={0,2})$') { throw 'INFRA_ARGV_RECORD' }
            [uint64]$declared=0
            if (-not [uint64]::TryParse($Matches[1],[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$declared)) { throw 'INFRA_ARGV_LENGTH' }
            try { [byte[]]$bytes=[Convert]::FromBase64String($Matches[2]) } catch { throw 'INFRA_ARGV_BASE64' }
            if ([Convert]::ToBase64String($bytes) -cne $Matches[2] -or [uint64]$bytes.Length -ne $declared) { throw 'INFRA_ARGV_CANONICAL' }
            try { $token=$utf8.GetString($bytes); [byte[]]$round=$utf8.GetBytes($token) } catch { throw 'INFRA_ARGV_UTF8' }
            $same=$round.Length -eq $bytes.Length
            if ($same) { for($j=0; $j -lt $bytes.Length; $j++) { if ($round[$j] -ne $bytes[$j]) { $same=$false; break } } }
            if (-not $same) { throw 'INFRA_ARGV_UTF8_ROUNDTRIP' }
            $null=$tokens.Add($token)
        }
        return [pscustomobject]@{ LogPath=$log; Tokens=$tokens.ToArray() }
    } catch {
        if ([string]$_.Exception.Message -like 'INFRA_*') { throw }
        throw 'INFRA_ARGV_EVIDENCE_MALFORMED'
    }
}
function Invoke-P016WindowsLinkerFixtureSelfTest {
    [CmdletBinding()]
    param([ValidateRange(1,300000)][int]$TimeoutMs=30000)
    $ErrorActionPreference='Stop'
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'INFRA_WINDOWS_REQUIRED' }
    if ($PSVersionTable.PSVersion.Major -ne 5 -or $PSVersionTable.PSVersion.Minor -ne 1) { throw 'INFRA_POWERSHELL_5_1_REQUIRED' }
    if (-not (Get-Command Add-Type -CommandType Cmdlet -ErrorAction SilentlyContinue)) { throw 'INFRA_ADDTYPE_REQUIRED' }
    $helper=Get-P016SelfTestBoundSibling 'windows_linker_behavior_helpers.ps1' 'ab65a9e2670eed4c2edb3fa87852b15b0706c2ae27731eaffdc4e9ce11eeac50'
    $builder=Get-P016SelfTestBoundSibling 'windows_linker_behavior_fixtures.ps1' 'f69f166ffb53b107251a8c2e9df3077521e7a0977ba66cdc2760acb9df28c5af'
    try { . $helper; . $builder } catch { throw 'INFRA_IMPORT_FAILED' }
    foreach($api in @('Invoke-P016BoundedProcess','Get-P016EnvironmentSnapshot','Restore-P016EnvironmentSnapshot','Test-P016EnvironmentSnapshot','Get-P016Sha256','Test-P016Sha256Manifest','New-P016WindowsLinkerFixtures')) {
        if (-not (Get-Command $api -CommandType Function -ErrorAction SilentlyContinue)) { throw 'INFRA_API_MISSING' }
    }
    $names=@('MOO_FAKE_MODE','MOO_FAKE_PAYLOAD','MOO_FAKE_ARGV_DIR','MOO_FAKE_SENTINEL')
    $snapshot=Get-P016EnvironmentSnapshot $names
    $parent=Join-Path ([IO.Path]::GetTempPath()) ('p016 linker selftest owner '+[guid]::NewGuid().ToString('N'))
    $root=Join-Path $parent 'fixture child with spaces'
    $rootOwned=$false; $evidence=$null
    try {
        if (-not [IO.Path]::IsPathRooted($parent) -or $parent -notmatch ' ' -or (Test-Path -LiteralPath $parent)) { throw 'INFRA_PARENT_PRECONDITION' }
        try { $createdParent=New-Item -ItemType Directory -Path $parent -ErrorAction Stop } catch { throw 'INFRA_PARENT_CREATE' }
        if ([string]$createdParent.FullName -cne [IO.Path]::GetFullPath($parent)) { throw 'INFRA_PARENT_CREATE_DRIFT' }
        $parentItem=Get-Item -LiteralPath $parent
        $parentBad=[IO.FileAttributes]::ReparsePoint
        if (-not [IO.Directory]::Exists($parent) -or ($parentItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or ($parentItem.Attributes -band $parentBad) -ne 0) { throw 'INFRA_PARENT_NOT_OWNED_REGULAR' }
        $rootOwned=$true
        if (-not [IO.Path]::IsPathRooted($root) -or $root -notmatch ' ' -or (Test-Path -LiteralPath $root)) { throw 'INFRA_ROOT_NOT_ABSENT' }
        try { $fixtures=New-P016WindowsLinkerFixtures -Root $root } catch { throw 'INFRA_BUILD_FAILED' }
        if ($null -eq $fixtures -or $null -eq $fixtures.Manifest) { throw 'INFRA_FIXTURE_EVIDENCE_MALFORMED' }
        $requestedRoot=[IO.Path]::GetFullPath($root)
        $returnedRootText=[string]$fixtures.Root
        if ([string]::IsNullOrEmpty($returnedRootText) -or -not [IO.Path]::IsPathRooted($returnedRootText)) { throw 'INFRA_FIXTURE_ROOT_INVALID' }
        $returnedRoot=[IO.Path]::GetFullPath($returnedRootText)
        if (-not [string]::Equals($returnedRoot,$requestedRoot,[StringComparison]::Ordinal)) { throw 'INFRA_FIXTURE_ROOT_DRIFT' }
        $returnedRootItem=Get-Item -LiteralPath $returnedRoot
        if (-not [IO.Directory]::Exists($returnedRoot) -or ($returnedRootItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or ($returnedRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'INFRA_FIXTURE_ROOT_NOT_REGULAR' }
        $expectedPaths=@([IO.Path]::GetFullPath((Join-Path $requestedRoot 'clang.exe')),[IO.Path]::GetFullPath((Join-Path $requestedRoot 'lld-link.exe')),[IO.Path]::GetFullPath((Join-Path $requestedRoot 'payload.exe')))
        $paths=@([string]$fixtures.ClangPath,[string]$fixtures.LldPath,[string]$fixtures.PayloadPath)
        if ($paths.Count -ne 3) { throw 'INFRA_FIXTURE_EVIDENCE_MALFORMED' }
        $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
        for($i=0; $i -lt 3; $i++) {
            if ([string]::IsNullOrEmpty($paths[$i]) -or -not [IO.Path]::IsPathRooted($paths[$i])) { throw 'INFRA_FIXTURE_PATH_INVALID' }
            $path=[IO.Path]::GetFullPath($paths[$i]); $paths[$i]=$path
            if (-not [string]::Equals($path,$expectedPaths[$i],[StringComparison]::Ordinal)) { throw 'INFRA_FIXTURE_PATH_DRIFT' }
            if (-not (Test-Path -LiteralPath $path)) { throw 'INFRA_FIXTURE_PATH_INVALID' }
            $item=Get-Item -LiteralPath $path
            if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists($path)) { throw 'INFRA_FIXTURE_NOT_REGULAR' }
            if ([string]$fixtures.Manifest.Entries[$i].Path -cne $path) { throw 'INFRA_FIXTURE_MANIFEST_PATH_DRIFT' }
        }
        Assert-P016SelfTestManifest $fixtures.Manifest
        if ([string]$fixtures.ClangHash -cne (Get-P016Sha256 $paths[0]) -or [string]$fixtures.LldHash -cne (Get-P016Sha256 $paths[1]) -or [string]$fixtures.PayloadHash -cne (Get-P016Sha256 $paths[2])) { throw 'INFRA_FIXTURE_HASH_EVIDENCE_DRIFT' }
        $lld=Invoke-P016BoundedProcess -FilePath $fixtures.LldPath -Args @() -TimeoutMs $TimeoutMs -EnvironmentOverrides @{}
        Assert-P016SelfTestProcessInfra $lld 'LLD'
        Assert-P016SelfTestContract ($lld.ExitCode -eq 0) 'LLD_RC'
        Assert-P016SelfTestContract ([string]$lld.Stdout -ceq '') 'LLD_STDOUT'
        Assert-P016SelfTestContract ([string]$lld.Stderr -ceq '') 'LLD_STDERR'
        Assert-P016SelfTestManifest $fixtures.Manifest
        $sentinel=Join-Path $root 'payload sentinel.txt'
        if (-not [IO.Path]::IsPathRooted($sentinel) -or (Test-Path -LiteralPath $sentinel)) { throw 'INFRA_SENTINEL_PRECONDITION' }
        $payload=Invoke-P016BoundedProcess -FilePath $fixtures.PayloadPath -Args @() -TimeoutMs $TimeoutMs -EnvironmentOverrides @{'MOO_FAKE_SENTINEL'=$sentinel}
        Assert-P016SelfTestProcessInfra $payload 'PAYLOAD'
        Assert-P016SelfTestContract ($payload.ExitCode -eq 0) 'PAYLOAD_RC'
        Assert-P016SelfTestContract ([string]$payload.Stdout -ceq '') 'PAYLOAD_STDOUT'
        Assert-P016SelfTestContract ([string]$payload.Stderr -ceq '') 'PAYLOAD_STDERR'
        Assert-P016SelfTestContract ([IO.File]::Exists($sentinel)) 'SENTINEL_MISSING'
        $sentinelItem=Get-Item -LiteralPath $sentinel
        if (($sentinelItem.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw 'INFRA_SENTINEL_NOT_REGULAR' }
        $actual=[IO.File]::ReadAllBytes($sentinel)
        [byte[]]$expected=[Text.Encoding]::ASCII.GetBytes('PAYLOAD_EXECUTED') + [byte]10
        $same=$actual.Length -eq $expected.Length
        if ($same) { for($i=0; $i -lt $expected.Length; $i++) { if ($actual[$i] -ne $expected[$i]) { $same=$false; break } } }
        Assert-P016SelfTestContract $same 'SENTINEL_BYTES'
        Assert-P016SelfTestManifest $fixtures.Manifest
        $caseDefinitions=@(
            [pscustomobject]@{Name='C1';Mode='COPY_RC0';ExpectedRc=0;OutputKind='COPY'},
            [pscustomobject]@{Name='C2';Mode='COPY_RC23';ExpectedRc=23;OutputKind='COPY'},
            [pscustomobject]@{Name='C3';Mode='NOOUTPUT_RC0';ExpectedRc=0;OutputKind='ABSENT'},
            [pscustomobject]@{Name='C4a';Mode='COPY_RC0';ExpectedRc=44;OutputKind='ABSENT'},
            [pscustomobject]@{Name='C4b';Mode='COPY_RC0';ExpectedRc=44;OutputKind='ABSENT'}
        )
        $clangCases=New-Object 'System.Collections.Generic.List[object]'
        foreach($case in $caseDefinitions) {
            $caseDir=Join-Path $root ('clang '+$case.Name); $argvDir=Join-Path $caseDir 'argv'
            $output=Join-Path $caseDir 'output with spaces.exe'; $caseSentinel=Join-Path $caseDir 'must stay absent.txt'
            if (-not [IO.Path]::IsPathRooted($output) -or -not [IO.Path]::IsPathRooted($caseSentinel) -or (Test-Path -LiteralPath $caseDir)) { throw ('INFRA_'+$case.Name+'_PRECONDITION') }
            $null=New-Item -ItemType Directory -Path $argvDir -ErrorAction Stop
            $caseDirItem=Get-Item -LiteralPath $caseDir; $argvDirItem=Get-Item -LiteralPath $argvDir
            if (($caseDirItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ($argvDirItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('INFRA_'+$case.Name+'_DIR_REPARSE') }
            $payloadOverride=$fixtures.PayloadPath
            switch($case.Name) {
                'C1' { $caseArgs=@('','spaced token','embedded"quote','trailing\','-o',$output) }
                'C2' { $caseArgs=@('copy23.c','-o',$output) }
                'C3' { $caseArgs=@('nooutput.c','-o',$output) }
                'C4a' { $caseArgs=@('-o','-o'); $payloadOverride=Join-Path $caseDir 'absolute absent payload.exe' }
                'C4b' { $caseArgs=@('missing-switch.c') }
            }
            if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $caseSentinel) -or -not [IO.Path]::IsPathRooted($payloadOverride)) { throw ('INFRA_'+$case.Name+'_PATH_PRECONDITION') }
            if ($case.Name -ceq 'C4a' -and (Test-Path -LiteralPath $payloadOverride)) { throw 'INFRA_C4a_PAYLOAD_PRECONDITION' }
            $caseFailure=$null; $caseEvidence=$null
            try {
                $result=Invoke-P016BoundedProcess -FilePath $fixtures.ClangPath -Args $caseArgs -TimeoutMs $TimeoutMs -EnvironmentOverrides @{'MOO_FAKE_MODE'=$case.Mode;'MOO_FAKE_PAYLOAD'=$payloadOverride;'MOO_FAKE_ARGV_DIR'=$argvDir;'MOO_FAKE_SENTINEL'=$caseSentinel}
                Assert-P016SelfTestProcessInfra $result ('CLANG_'+$case.Name)
                $decoded=Read-P016StrictArgvLog $argvDir; $actualArgs=@($decoded.Tokens)
                Assert-P016SelfTestContract ($actualArgs.Count -eq $caseArgs.Count) ($case.Name+'_ARGV_COUNT')
                for($i=0; $i -lt $caseArgs.Count; $i++) { Assert-P016SelfTestContract ([string]::Equals([string]$actualArgs[$i],[string]$caseArgs[$i],[StringComparison]::Ordinal)) ($case.Name+'_ARGV_'+$i) }
                Assert-P016SelfTestContract ($result.ExitCode -eq $case.ExpectedRc) ($case.Name+'_RC')
                Assert-P016SelfTestContract ([string]$result.Stdout -ceq '') ($case.Name+'_STDOUT')
                Assert-P016SelfTestContract ([string]$result.Stderr -ceq '') ($case.Name+'_STDERR')
                Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $caseSentinel)) ($case.Name+'_SENTINEL')
                if ($case.Name -ceq 'C4a') { Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $payloadOverride)) 'C4a_PAYLOAD_SIDE_EFFECT' }
                $outputHash=$null
                if ($case.OutputKind -ceq 'COPY') {
                    Assert-P016SelfTestContract ([IO.File]::Exists($output)) ($case.Name+'_OUTPUT_MISSING')
                    $outputItem=Get-Item -LiteralPath $output
                    if (($outputItem.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -ne 0) { throw ('INFRA_'+$case.Name+'_OUTPUT_NOT_REGULAR') }
                    $outputHash=Get-P016Sha256 $output
                    Assert-P016SelfTestContract ([string]$outputHash -ceq [string]$fixtures.PayloadHash) ($case.Name+'_OUTPUT_HASH')
                } else { Assert-P016SelfTestContract (-not (Test-Path -LiteralPath $output)) ($case.Name+'_OUTPUT_SIDE_EFFECT') }
                $caseEvidence=[pscustomobject]@{Name=$case.Name;Rc=$result.ExitCode;DecodedArgv=$actualArgs;LogPath=$decoded.LogPath;OutputHash=$outputHash;OutputAbsent=(-not (Test-Path -LiteralPath $output))}
            } catch { $caseFailure=$_ }
            Assert-P016SelfTestManifest $fixtures.Manifest
            if ($null -ne $caseFailure) { throw $caseFailure }
            $null=$clangCases.Add($caseEvidence)
        }
        $evidence=[pscustomobject]@{ Root=$root; ClangPath=$fixtures.ClangPath; LldPath=$fixtures.LldPath; PayloadPath=$fixtures.PayloadPath; Manifest=$fixtures.Manifest; ClangHash=$fixtures.ClangHash; LldHash=$fixtures.LldHash; PayloadHash=$fixtures.PayloadHash; Lld=$lld; Payload=$payload; SentinelPath=$sentinel; SentinelBytes=$actual.Length; ClangCases=$clangCases.ToArray() }
    } finally {
        $finalize=New-Object 'System.Collections.Generic.List[string]'
        try { Restore-P016EnvironmentSnapshot $snapshot } catch { $finalize.Add('RESTORE_EXCEPTION') }
        try { if (-not (Test-P016EnvironmentSnapshot $snapshot)) { $finalize.Add('RESTORE_DRIFT') } } catch { $finalize.Add('RESTORE_VERIFY') }
        if ($rootOwned) {
            try {
                if (-not [IO.Directory]::Exists($parent)) { $finalize.Add('OWNED_PARENT_MISSING') }
                else {
                    $ownedItem=Get-Item -LiteralPath $parent
                    if (($ownedItem.Attributes -band [IO.FileAttributes]::Directory) -eq 0 -or ($ownedItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { $finalize.Add('OWNERSHIP_DRIFT') }
                    else { [IO.Directory]::Delete($parent,$true) }
                }
            } catch { $finalize.Add('CLEANUP_EXCEPTION') }
            if (Test-Path -LiteralPath $parent) { $finalize.Add('CLEANUP_DRIFT') }
        }
        if ($finalize.Count -gt 0) { throw ('INFRA_FINALIZE_'+($finalize -join '_')) }
    }
    if ($null -eq $evidence) { throw 'INFRA_EVIDENCE_MISSING' }
    return $evidence
}
if ($MyInvocation.InvocationName -eq '.') { return }
try {
    $null=Invoke-P016WindowsLinkerFixtureSelfTest
    Write-Output 'P016 WINDOWS LINKER FIXTURE SELFTEST PASS'
    exit 0
} catch {
    $raw=[string]$_.Exception.Message
    $token=ConvertTo-P016SelfTestReason $raw
    if ($raw.StartsWith('FAIL_')) {
        Write-Output ('P016 WINDOWS LINKER FIXTURE SELFTEST FAIL: reason='+$token)
        exit 1
    }
    Write-Output ('P016 WINDOWS LINKER FIXTURE SELFTEST INFRA_NOGO: reason='+$token)
    exit 2
}