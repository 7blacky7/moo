#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINVM_HOST="${MOO_WINVM_HOST:-192.168.50.246}"
WINVM_USER="${MOO_WINVM_USER:-pro}"
WINVM_KEY="${MOO_WINVM_KEY:-/tmp/moo-winvm-ed25519}"
BUILD_TIMEOUT_MS="${MOO_WINVM_LINKER_BUILD_TIMEOUT_MS:-900000}"
CASE_TIMEOUT_MS="${MOO_WINVM_LINKER_CASE_TIMEOUT_MS:-30000}"
infra(){ printf 'P016 WINDOWS LINKER VM GATE INFRA_NOGO: reason=%s VM_STAYS_ON=1 artifacts=%s\n' "$1" "${OUT:-unavailable}" >&2; exit 2; }
[[ $# -le 1 ]] || { OUT="${TMPDIR:-/tmp}/moo-p016-linker-invalid"; infra ARGUMENT_COUNT; }
if [[ $# -eq 1 ]]; then OUT="$1"; [[ "$OUT" = /* && ! -e "$OUT" ]] || infra OUT_PRECONDITION; mkdir -p "$OUT" || infra OUT_CREATE
else OUT="$(mktemp -d "${TMPDIR:-/tmp}/moo-p016-windows-linker.XXXXXX")" || exit 2; fi
for t in ssh scp sha256sum tar find sort xargs iconv base64 tr grep wc date awk mktemp cut sed; do command -v "$t" >/dev/null 2>&1 || infra "MISSING_TOOL_${t//[^A-Za-z0-9]/_}"; done
[[ -r "$WINVM_KEY" ]] || infra WINVM_KEY
for n in "$BUILD_TIMEOUT_MS" "$CASE_TIMEOUT_MS"; do [[ "$n" =~ ^[1-9][0-9]*$ ]] || infra TIMEOUT_VALUE; done
required=(
  compiler/Cargo.toml compiler/Cargo.lock compiler/build.rs compiler/src/main.rs
  compiler/tests/windows_linker_behavior_helpers.ps1
  compiler/tests/windows_linker_behavior_fixtures.ps1
  compiler/tests/windows_linker_fixture_selftest.ps1
  compiler/tests/windows_linker_production_gate.ps1
  beispiele/tests/ui_moo_import_link_test.moos
)
for optional in Cargo.toml Cargo.lock; do [[ ! -e "$ROOT/$optional" || ( -f "$ROOT/$optional" && ! -L "$ROOT/$optional" ) ]] || infra ROOT_CARGO_TYPE; [[ ! -f "$ROOT/$optional" ]] || required+=("$optional"); done
for p in "${required[@]}"; do [[ -f "$ROOT/$p" && ! -L "$ROOT/$p" ]] || infra REQUIRED_SOURCE; done
for d in compiler/src compiler/runtime stdlib; do [[ -d "$ROOT/$d" && ! -L "$ROOT/$d" ]] || infra SOURCE_DIRECTORY; done
[[ -z "$(find "$ROOT/compiler/src" "$ROOT/compiler/runtime" "$ROOT/stdlib" -type l -print -quit)" ]] || infra SOURCE_SYMLINK
if [[ -e "$ROOT/.cargo" ]]; then [[ -d "$ROOT/.cargo" && ! -L "$ROOT/.cargo" ]] || infra CARGO_CONFIG_DIRECTORY; [[ -z "$(find "$ROOT/.cargo" -type l -print -quit)" ]] || infra CARGO_CONFIG_SYMLINK; fi
file_list="$OUT/source-files.nul"
( cd "$ROOT"; { printf '%s\0' "${required[@]}"; find compiler/src compiler/runtime stdlib -type f -print0; [[ ! -d .cargo ]] || find .cargo -type f -print0; } | sort -zu >"$file_list" ) || infra SOURCE_ENUMERATION
count=0
while IFS= read -r -d '' p; do
  [[ -n "$p" && "$p" != /* && "$p" != *\\* && "$p" != *:* && "$p" != *$'\n'* && "$p" != *$'\r'* && "$p" != *$'\t'* ]] || infra SOURCE_PATH
  [[ "/$p/" != *"/../"* && "/$p/" != *"/./"* && "/$p/" != *"/target/"* ]] || infra SOURCE_SEGMENT
  [[ -f "$ROOT/$p" && ! -L "$ROOT/$p" ]] || infra SOURCE_TYPE; count=$((count+1))
done <"$file_list"
[[ $count -gt 100 ]] || infra SOURCE_COUNT
source_manifest="$OUT/source-sha256.txt"; archive="$OUT/source.tar.gz"
( cd "$ROOT"; xargs -0 sha256sum <"$file_list" >"$source_manifest"; sha256sum -c "$source_manifest" >"$OUT/source-check-before-transfer.txt"; tar --null --no-recursion -T "$file_list" -czf "$archive" ) || infra SOURCE_PACKAGE
manifest_sha="$(sha256sum "$source_manifest"|awk '{print $1}')"; archive_sha="$(sha256sum "$archive"|awk '{print $1}')"
[[ "$manifest_sha" =~ ^[0-9a-f]{64}$ && "$archive_sha" =~ ^[0-9a-f]{64}$ ]] || infra LOCAL_HASH
run_id="p016-linker-${manifest_sha:0:16}-$(date -u +%Y%m%dT%H%M%SZ)-$$"
remote_root_scp="C:/moo-runs/$run_id"; remote_root_ps="C:\\moo-runs\\$run_id"; win_target="$WINVM_USER@$WINVM_HOST"
remote_root_re='^C:\\moo-runs\\p016-linker-[0-9a-f]{16}-[0-9]{8}T[0-9]{6}Z-[0-9]+$'
[[ "$remote_root_ps" =~ $remote_root_re ]] || infra REMOTE_ROOT_PATTERN
SSH=(ssh -T -F /dev/null -i "$WINVM_KEY" -o BatchMode=yes -o StrictHostKeyChecking=no -o WarnWeakCrypto=no -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3)
SCP=(scp -F /dev/null -i "$WINVM_KEY" -o BatchMode=yes -o StrictHostKeyChecking=no -o WarnWeakCrypto=no -o ConnectTimeout=10)
root_b64="$(printf '%s' "$remote_root_ps"|iconv -f UTF-8 -t UTF-16LE|base64|tr -d '\r\n')"
root_create="$OUT/root-create.ps1"
printf "\$ErrorActionPreference='Stop';\$ProgressPreference='SilentlyContinue'\n\
\$Root=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String('%s'))\nif(Test-Path -LiteralPath \$Root){exit 91}\n[IO.Directory]::CreateDirectory(\$Root)|Out-Null\nif((Get-Item -LiteralPath \$Root).Attributes -band [IO.FileAttributes]::ReparsePoint){exit 92}\n" "$root_b64" >"$root_create"
root_create_b64="$(iconv -f UTF-8 -t UTF-16LE "$root_create"|base64|tr -d '\r\n')"
"${SSH[@]}" "$win_target" "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $root_create_b64" >"$OUT/create.stdout" 2>"$OUT/create.stderr" || infra REMOTE_ROOT
"${SCP[@]}" "$archive" "$win_target:$remote_root_scp/source.tar.gz" || infra ARCHIVE_TRANSFER
"${SCP[@]}" "$source_manifest" "$win_target:$remote_root_scp/source-sha256.txt" || infra MANIFEST_TRANSFER
controller="$OUT/remote-controller.ps1"
cat >"$controller" <<'POWERSHELL'
param(
  [Parameter(Mandatory=$true)][string]$Root,
  [Parameter(Mandatory=$true)][string]$ManifestSha,
  [Parameter(Mandatory=$true)][string]$ArchiveSha,
  [Parameter(Mandatory=$true)][int]$BuildTimeoutMs,
  [Parameter(Mandatory=$true)][int]$CaseTimeoutMs
)
$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; $utf8=New-Object Text.UTF8Encoding($false,$true)
function Hash ([string]$p){(Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant()}
function Regular([string]$p){if ( -not [IO.File]::Exists($p)){return $false};$i=Get-Item -LiteralPath $p;return ($i.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0}
function Directory([string]$p){
  if ( -not [IO.Directory]::Exists($p)){return $false};$i=Get-Item -LiteralPath $p
  return ($i.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -and ($i.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0
}
function ConvertArg([string]$v){
  if ($null -eq $v -or $v.Length -eq 0){return '""'};if ($v -notmatch '[\s"]'){return $v}
  $b=New-Object Text.StringBuilder;[void]$b.Append([char]34);$slashes=0
  foreach ($c in $v.ToCharArray()){
    if ($c -eq [char]92){$slashes++;continue}
    if ($c -eq [char]34){if ($slashes -gt 0){[void]$b.Append(([string][char]92)*($slashes*2))};[void]$b.Append([char]92);[void]$b.Append([char]34);$slashes=0;continue}
    if ($slashes -gt 0){[void]$b.Append(([string][char]92)*$slashes);$slashes=0}
    [void]$b.Append($c)
  }
  if ($slashes -gt 0){[void]$b.Append(([string][char]92)*($slashes*2))};[void]$b.Append([char]34);$b.ToString()
}
function ConvertArgs([string[]]$v){if ($null -eq $v -or $v.Count -eq 0){return ''};(($v|ForEach-Object{ConvertArg $_}) -join ' ')}
function StopTree([int]$ProcessId){
  $q=New-Object Diagnostics.ProcessStartInfo
  $q.FileName=Join-Path $env:SystemRoot 'System32\taskkill.exe'
  $q.Arguments=ConvertArgs @('/PID',[string]$ProcessId,'/T','/F')
  $q.UseShellExecute=$false;$q.CreateNoWindow=$true
  $q.RedirectStandardOutput=$true;$q.RedirectStandardError=$true
  $p=New-Object Diagnostics.Process;$p.StartInfo=$q;try{if ( -not $p.Start()){return $false}}catch{return $false};$o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync()
  if ( -not $p.WaitForExit(15000)){try{$p.Kill()}catch{};try{$p.WaitForExit(5000)}catch{};$p.Dispose();return $false}
  $outDone=$o.Wait(5000);$errDone=$e.Wait(5000);$drained=$outDone -and $errDone;$ok=$drained -and $p.ExitCode -eq 0;$p.Dispose();$ok
}
function Run([string]$File,[string[]]$ArgumentList,[string]$Cwd,[int]$Timeout,[hashtable]$Environment){
  $q=New-Object Diagnostics.ProcessStartInfo;$q.FileName=$File;$q.Arguments=ConvertArgs $ArgumentList;$q.WorkingDirectory=$Cwd;$q.UseShellExecute=$false;$q.CreateNoWindow=$true;$q.RedirectStandardOutput=$true;$q.RedirectStandardError=$true
  foreach ($n in $Environment.Keys){$v=$Environment[$n];if ($null -eq $v){[void]$q.EnvironmentVariables.Remove([string]$n)}else{$q.EnvironmentVariables[[string]$n]=[string]$v}}
  $p=New-Object Diagnostics.Process;$p.StartInfo=$q;try{$started=$p.Start()}catch{return[pscustomobject]@{Started=$false;TimedOut=$false;Reaped=$true;ExitCode=$null;Stdout='';Stderr=''}}
  if ( -not $started){$p.Dispose();return[pscustomobject]@{Started=$false;TimedOut=$false;Reaped=$true;ExitCode=$null;Stdout='';Stderr=''}}
  $o=$p.StandardOutput.ReadToEndAsync();$e=$p.StandardError.ReadToEndAsync();$timedOut= -not $p.WaitForExit($Timeout);$tree=$true
  if ($timedOut){$tree=StopTree $p.Id;$reaped=$p.WaitForExit(15000)}else{$p.WaitForExit();$reaped=$true}
  $drained=$false;if ($reaped){$outDone=$o.Wait(5000);$errDone=$e.Wait(5000);$drained=$outDone -and $errDone}
  $complete=$reaped -and $tree -and $drained;$rc=if ($complete){$p.ExitCode}else{$null};$stdout=if ($complete){$o.Result}else{''};$stderr=if ($complete){$e.Result}else{''};$p.Dispose()
  [pscustomobject]@{Started=$true;TimedOut=$timedOut;Reaped=$complete;ExitCode=$rc;Stdout=$stdout;Stderr=$stderr}
}
function RequireRun($r,[string]$n){if ( -not $r.Started -or $r.TimedOut -or -not $r.Reaped -or $null -eq $r.ExitCode){throw ('RUN_'+$n)}}
function PeMachine([string]$p){
  $s=[IO.File]::Open($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try{$r=New-Object IO.BinaryReader($s);if ($r.ReadUInt16() -ne 0x5a4d){throw 'PE_DOS'};$s.Position=0x3c;$off=$r.ReadUInt32()
    if ($off -lt 64 -or $off -gt $s.Length-6){throw 'PE_OFFSET'};$s.Position=$off
    if ($r.ReadUInt32() -ne 0x00004550){throw 'PE_SIGNATURE'};$r.ReadUInt16()
  }finally{$s.Dispose()}
}
function CheckSources([string]$Source,[string]$Manifest){
  $root=[IO.Path]::GetFullPath($Source);if ( -not (Directory $root)){throw 'SOURCE_ROOT'};$seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase);$previous=$null
  foreach ($line in [IO.File]::ReadAllLines($Manifest,$utf8)){if ($line -cnotmatch '^([0-9a-f]{64})  ([^\r\n\t\\:]+)$'){throw 'MANIFEST_FORMAT'};$expected=$Matches[1];$relative=$Matches[2]
    $bad=@($relative.Split('/')|Where-Object{$_ -eq '' -or $_ -eq '.' -or $_ -eq '..'});if ([IO.Path]::IsPathRooted($relative) -or $bad.Count -ne 0){throw 'MANIFEST_PATH'}
    if ($null -ne $previous -and [string]::CompareOrdinal($previous,$relative) -ge 0){throw 'MANIFEST_ORDER'};$previous=$relative;if ( -not $seen.Add($relative)){throw 'MANIFEST_DUPLICATE'}
    $p=[IO.Path]::GetFullPath((Join-Path $root $relative));if ( -not $p.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or -not (Regular $p) -or (Hash $p) -cne $expected){throw 'SOURCE_DRIFT'}
  }
  $actual=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
  foreach ($i in Get-ChildItem -LiteralPath $root -Recurse -Force){
    if ($i.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'SOURCE_REPARSE'}
    if ( -not $i.PSIsContainer){$relative=$i.FullName.Substring($root.Length+1).Replace('\','/');if ( -not $actual.Add($relative)){throw 'SOURCE_ACTUAL_DUPLICATE'}}
  }
  if ($actual.Count -ne $seen.Count){throw 'SOURCE_SET'};foreach ($relative in $actual){if ( -not $seen.Contains($relative)){throw 'SOURCE_EXTRA'}}
}
function VersionEntry([string]$n,[string]$p,[string[]]$a,[string]$cwd,[hashtable]$env){
  if ( -not (Regular $p)){throw ('VERSION_TOOL_'+$n)};$before=Hash $p;$r=Run $p $a $cwd 30000 $env;RequireRun $r ('VERSION_'+$n)
  if ($r.ExitCode -ne 0){throw ('VERSION_RC_'+$n)};$v=($r.Stdout+$r.Stderr).Trim().Replace(([string][char]13+[char]10),[string][char]10)
  if ([string]::IsNullOrWhiteSpace($v)){throw ('VERSION_EMPTY_'+$n)}
  if ((Hash $p) -cne $before){throw ('VERSION_SHA_'+$n)};[ordered]@{path=$p;sha256=$before;version=$v}
}
function WriteText([string]$p,[string]$v){
  $t=$p+'.tmp';if ((Test-Path -LiteralPath $p) -or (Test-Path -LiteralPath $t)){throw 'TEXT_PRECONDITION'}
  [IO.File]::WriteAllText($t,$v,$utf8);[IO.File]::Move($t,$p);if ( -not (Regular $p)){throw 'TEXT_FINAL'}
}
function WriteJson([string]$p,$v){
  WriteText $p ($v|ConvertTo-Json -Compress -Depth 6);[byte[]]$raw=[IO.File]::ReadAllBytes($p)
  if ($raw.Length -ge 3 -and $raw[0] -eq 0xef -and $raw[1] -eq 0xbb -and $raw[2] -eq 0xbf){throw 'JSON_BOM'}
  try{$null=$utf8.GetString($raw)|ConvertFrom-Json}catch{throw 'JSON_REREAD'}
  $h=Hash $p;if ((Hash $p) -cne $h){throw 'JSON_SHA_DRIFT'};$h
}
function WriteEvidence([string]$Path,[string]$Dir,[string[]]$Names,[bool]$RequireAll){
  $selected=New-Object 'Collections.Generic.List[string]';foreach ($n in $Names){$p=Join-Path $Dir $n;if (Regular $p){$selected.Add($n)}elseif ($RequireAll){throw 'EVIDENCE_MISSING'}}
  $a=$selected.ToArray();[Array]::Sort($a,[StringComparer]::Ordinal)
  $lines=@($a|ForEach-Object{(Hash (Join-Path $Dir $_))+'  '+$_})
  if ($lines.Count -eq 0){throw 'EVIDENCE_EMPTY'}
  WriteText $Path (($lines -join [string][char]10)+[string][char]10)
}
$evidenceNames=@('build.stderr','build.stdout','production-evidence.json','production.exit','production.stderr','production.stdout','toolchain.json');$failureEvidenceNames=$evidenceNames+@('tar.exit','tar.stderr','tar.stdout')
try{
  if ( -not (Directory $Root)){throw 'ROOT_INVALID'};$archive=Join-Path $Root 'source.tar.gz';$manifest=Join-Path $Root 'source-sha256.txt'
  if ( -not (Regular $archive) -or -not (Regular $manifest) -or (Hash $archive) -cne $ArchiveSha -or (Hash $manifest) -cne $ManifestSha){throw 'TRANSFER_HASH'}
  $source=Join-Path $Root 'source';$targetDir=Join-Path $Root 'target';$evidence=Join-Path $Root 'evidence'
  foreach ($p in @($source,$targetDir,$evidence)){if (Test-Path -LiteralPath $p){throw 'OWNED_PRECONDITION'};[void](New-Item -ItemType Directory -Path $p);if ( -not (Directory $p)){throw 'OWNED_DIRECTORY'}}
  $tar=Run 'tar.exe' @('-xzf',$archive,'-C',$source) $Root 120000 @{};RequireRun $tar 'TAR'
  if ($tar.ExitCode -ne 0){WriteText (Join-Path $evidence 'tar.stdout') $tar.Stdout;WriteText (Join-Path $evidence 'tar.stderr') $tar.Stderr;WriteText (Join-Path $evidence 'tar.exit') ([string]$tar.ExitCode+[string][char]10)
    throw 'TAR_EXIT'
  };CheckSources $source $manifest
  if ((Hash (Join-Path $source 'compiler\src\main.rs')) -cne 'a3c4785403a89dab6d4faffe11fb27cc07bde6ef2528a71d07c8368e90f25330'){throw 'MAIN_SHA'}
  $rustRoot=[IO.Path]::GetFullPath('C:\moo-tools\rustup\toolchains\stable-x86_64-pc-windows-msvc');$llvmRoot=[IO.Path]::GetFullPath('C:\moo-tools\llvm18\Library');$cargoHome=[IO.Path]::GetFullPath('C:\moo-tools\cargo')
  $sdkRoot=[IO.Path]::GetFullPath('C:\Program Files (x86)\Windows Kits\10');$sdkVer='10.0.19041.0';$msvcRoot=[IO.Path]::GetFullPath('C:\BuildTools\VC\Tools\MSVC\14.44.35207')
  if ( -not (Directory $sdkRoot)){throw 'ENV_ROOT_SDK'};if ( -not (Directory $msvcRoot)){throw 'ENV_ROOT_MSVC'}
  function EnvPin([string]$k,[string]$root,[string]$rel,[string]$sha){$p=[IO.Path]::GetFullPath((Join-Path $root $rel))
    if ( -not $p.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($p) -cne [IO.Path]::GetFileName($rel) -or -not (Regular $p)){throw ('ENV_PATH_'+$k)};if ((Hash $p) -cne $sha){throw ('ENV_ID_'+$k)};$d=[IO.Directory]::GetParent($p)
    while ($null -ne $d -and -not [string]::Equals($d.FullName,$root,[StringComparison]::OrdinalIgnoreCase)){if ( -not (Directory $d.FullName)){throw ('ENV_PATH_'+$k)};$d=$d.Parent};if ($null -eq $d -or -not (Directory $d.FullName)){throw ('ENV_PATH_'+$k)}
  }
  $envPins=[ordered]@{SDK_STDLIB_H=@($sdkRoot,('Include\'+$sdkVer+'\ucrt\stdlib.h'),'1576fdb40eb225dff53489db2fd52e41c7bb24f1e621e4a95c2c7f4a7216949c');SDK_INTTYPES_H=@($sdkRoot,('Include\'+$sdkVer+'\ucrt\inttypes.h'),'615bcc64d5945d41c67608ad674e4ade389d90d4d68268370cf8b914f13dabac')}
  $envPins.SDK_WINDOWS_H=@($sdkRoot,('Include\'+$sdkVer+'\um\windows.h'),'65ccd069fe343519a7ec0698ccaf5a6f0388f1af180b959b8271902b983cb099');$envPins.SDK_UCRT_LIB=@($sdkRoot,('Lib\'+$sdkVer+'\ucrt\x64\ucrt.lib'),'7ef4eac926bf597d2f243f16cdfed7e0db22cb3ca34a1d7e088a84c994a03d66')
  $envPins.SDK_KERNEL32_LIB=@($sdkRoot,('Lib\'+$sdkVer+'\um\x64\kernel32.lib'),'d9810feacbb1150584cdd484ea1ce60b4e6b3ddec3b8afa505c0ae47330027e2');$envPins.MSVC_VCRUNTIME_H=@($msvcRoot,'include\vcruntime.h','a2fe9117dc2640e243f91fa94e1d05fc35083218c2a67a446db9e34cece779b5')
  $envPins.MSVC_VCRUNTIME_LIB=@($msvcRoot,'lib\x64\vcruntime.lib','1acbd98b43a4be8373e25c65dcae4412f36f6111d6b64e47e4a2a97a1d16b27b');$envPins.MSVC_MSVCRT_LIB=@($msvcRoot,'lib\x64\msvcrt.lib','07670951a39739b6d1b50421ae61707ef23c8dff50cdf35f99408711478f3a7c')
  $envPins.MSVC_LIBCMT_LIB=@($msvcRoot,'lib\x64\libcmt.lib','8fe40dd342b90b35d62b8f5d55b4e92df7785c122976618365cfe3b014cc857a');$envPins.MSVC_CL_EXE=@($msvcRoot,'bin\Hostx64\x64\cl.exe','88c8344236a27a6e727e0a8edc49aaa2690bdc7a9464b9d18cc7abe70a9f1c0d')
  $envPins.MSVC_LINK_EXE=@($msvcRoot,'bin\Hostx64\x64\link.exe','ca11e6c45debd34bf652dfe984c5360a531a005ed78bf72852330c9c2590cf0d');$envPins.MSVC_LIB_EXE=@($msvcRoot,'bin\Hostx64\x64\lib.exe','3d694c782f93e998fec5db0c2df78153565da23a028ee4618561fa0c33408489')
  foreach ($k in $envPins.Keys){$s=$envPins[$k];EnvPin $k ([string]$s[0]) ([string]$s[1]) ([string]$s[2])}
  $msvcInc=Join-Path $msvcRoot 'include';$msvcLib=Join-Path $msvcRoot 'lib\x64';$msvcBin=Join-Path $msvcRoot 'bin\Hostx64\x64';$sdkInc=Join-Path $sdkRoot ('Include\'+$sdkVer);$sdkLib=Join-Path $sdkRoot ('Lib\'+$sdkVer)
  $paths=[ordered]@{cargo=@($rustRoot,'bin\cargo.exe','cargo.exe','3cd119fe81dfedb9dce4573696bf65058f16b57c9e5babe415b71624315cbb7d');rustc=@($rustRoot,'bin\rustc.exe','rustc.exe','6d1c5543ed3a45cfbc1c1332d42d6550d883c14d3c2e323427e631c331cebeeb')}
  $paths.llvm=@($llvmRoot,'bin\llvm-config.exe','llvm-config.exe','59bd65b0fa8f70f7db747668f831067e99ded2118ff6e07da66f410dcaa2f53a');$paths.clang=@($llvmRoot,'bin\clang.exe','clang.exe','b6c738e3563522656db7f9bb93c467c47176a243c8931d428a7c6c0e7427390b')
  $paths.lld=@($rustRoot,'lib\rustlib\x86_64-pc-windows-msvc\bin\gcc-ld\lld-link.exe','lld-link.exe','0999f1ce175b9a34aa39029fc95df06aa2d1f80f93842668d31e66a9e8014fe7');$selected=[ordered]@{}
  $paths.rustLld=@($rustRoot,'lib\rustlib\x86_64-pc-windows-msvc\bin\rust-lld.exe','rust-lld.exe','55bd23cee94c73c87de51106f60b63ad8981390aa7079c435c27fb5b04e283ad')
  foreach ($n in $paths.Keys){$s=$paths[$n];$base=[IO.Path]::GetFullPath([string]$s[0]);$p=[IO.Path]::GetFullPath((Join-Path $base ([string]$s[1])))
    if ( -not (Directory $base) -or -not $p.StartsWith($base+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($p) -cne [string]$s[2] -or -not (Regular $p)){throw ('TOOLCHAIN_PATH_'+$n)}
    if ((PeMachine $p) -ne 0x8664 -or (Hash $p) -cne [string]$s[3]){throw ('TOOLCHAIN_ID_'+$n)};$d=[IO.Directory]::GetParent($p)
    while ($null -ne $d -and -not [string]::Equals($d.FullName,$base,[StringComparison]::OrdinalIgnoreCase)){if ( -not (Directory $d.FullName)){throw ('TOOLCHAIN_PARENT_'+$n)};$d=$d.Parent}
    if ($null -eq $d -or -not (Directory $d.FullName)){throw ('TOOLCHAIN_CONTAINMENT_'+$n)};$selected[$n]=$p
  };$cargo=$selected.cargo;$rustc=$selected.rustc;$llvmConfig=$selected.llvm;$clang=$selected.clang;$lld=$selected.lld;$rustBin=Split-Path -Parent $cargo;$llvmBin=Split-Path -Parent $clang;$lldDir=Split-Path -Parent $lld
  $system32=[IO.Path]::GetFullPath('C:\Windows\System32');if ( -not (Directory $system32) -or -not (Directory $cargoHome)){throw 'CHILD_ROOT_PATH'};$child=@{
    CARGO_TARGET_DIR=$targetDir;CARGO_HOME=$cargoHome;CARGO_NET_OFFLINE='true';CARGO_BUILD_TARGET=$null;RUSTFLAGS=$null;CARGO_ENCODED_RUSTFLAGS=$null;RUSTC=$rustc;RUSTC_WRAPPER=$null;RUSTC_WORKSPACE_WRAPPER=$null
    CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=$selected.rustLld;CC=$clang;CC_x86_64_pc_windows_msvc=$clang;LLVM_SYS_181_PREFIX=$llvmRoot;LIBCLANG_PATH=$llvmBin;CFLAGS=('-I'+(Join-Path $llvmRoot 'include'))
    INCLUDE=($msvcInc+';'+(Join-Path $sdkInc 'ucrt')+';'+(Join-Path $sdkInc 'shared')+';'+(Join-Path $sdkInc 'um'));LIB=($msvcLib+';'+(Join-Path $sdkLib 'ucrt\x64')+';'+(Join-Path $sdkLib 'um\x64'));PATH=($rustBin+';'+$llvmBin+';'+$lldDir+';'+$msvcBin+';'+$system32)
    WindowsSdkDir=($sdkRoot+'\');WindowsSDKVersion=($sdkVer+'\');UniversalCRTSdkDir=($sdkRoot+'\');UCRTVersion=$sdkVer;VCToolsInstallDir=($msvcRoot+'\');VCToolsVersion='14.44.35207';LIBPATH=$null;CL=$null;_CL_=$null;LINK=$null;_LINK_=$null;CXXFLAGS=$null
    CFLAGS_x86_64_pc_windows_msvc=$null;CXXFLAGS_x86_64_pc_windows_msvc=$null;'CFLAGS_x86_64-pc-windows-msvc'=$null;'CXXFLAGS_x86_64-pc-windows-msvc'=$null
  };$toolchain=[ordered]@{cargo=(VersionEntry 'CARGO' $cargo @('--version') $source $child);rustc=(VersionEntry 'RUSTC' $rustc @('--version') $source $child)
    llvm=(VersionEntry 'LLVM' $llvmConfig @('--version') $source $child);clang=(VersionEntry 'CLANG' $clang @('--version') $source $child)
    lld=(VersionEntry 'LLD' $lld @('--version') $source $child);rust_lld=(VersionEntry 'RUST_LLD' $selected.rustLld @('-flavor','link','--version') $source $child)};$expected=[ordered]@{cargo='cargo 1.97.0 (c980f4866 2026-06-30)';rustc='rustc 1.97.0 (2d8144b78 2026-07-07)'
    llvm='18.1.8';clang=('clang version 18.1.8'+[char]10+'Target: x86_64-pc-windows-msvc'+[char]10+'Thread model: posix'+[char]10+'InstalledDir: '+$llvmBin)
    lld='LLD 22.1.6 (https://github.com/rust-lang/llvm-project.git 08c84e69a84d95936296dfcab0e38b34100725d5)';rust_lld='LLD 22.1.6 (https://github.com/rust-lang/llvm-project.git 08c84e69a84d95936296dfcab0e38b34100725d5)'}
  foreach ($n in $expected.Keys){if ([string]$toolchain[$n].version -cne [string]$expected[$n]){throw ('TOOLCHAIN_VERSION_'+$n)}};[void](WriteJson (Join-Path $evidence 'toolchain.json') $toolchain);CheckSources $source $manifest
  $build=Run $cargo @('build','--manifest-path',(Join-Path $source 'compiler\Cargo.toml'),'--locked','--offline','--release','--target','x86_64-pc-windows-msvc','--no-default-features') $source $BuildTimeoutMs $child
  WriteText (Join-Path $evidence 'build.stdout') $build.Stdout;WriteText (Join-Path $evidence 'build.stderr') $build.Stderr;RequireRun $build 'BUILD';if ($build.ExitCode -ne 0){throw 'BUILD_EXIT'};CheckSources $source $manifest
  $release=Join-Path $targetDir 'x86_64-pc-windows-msvc\release';$compiler=Join-Path $release 'moo-compiler.exe';if ( -not (Regular $compiler) -or (PeMachine $compiler) -ne 0x8664){throw 'COMPILER_ARTIFACT'};$candidates=New-Object 'Collections.Generic.List[string]'
  $buildRoot=Join-Path $release 'build';foreach ($d in Get-ChildItem -LiteralPath $buildRoot -Directory -Force){if ($d.Name -like 'moo-compiler-*'){
    if ($d.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'RUNTIME_REPARSE'};$p=Join-Path $d.FullName 'out\moo_runtime.lib';if (Test-Path -LiteralPath $p -PathType Leaf){$candidates.Add($p)}
  }};if ($candidates.Count -ne 1 -or -not (Regular $candidates[0])){throw 'RUNTIME_COUNT'};$runtime=[IO.Path]::GetFullPath($candidates[0])
  $runner=Join-Path $source 'compiler\tests\windows_linker_production_gate.ps1';$main=Join-Path $source 'beispiele\tests\ui_moo_import_link_test.moos';$production=Join-Path $evidence 'production-evidence.json'
  $gateTimeout=[int][Math]::Min([long][int]::MaxValue,([long]$CaseTimeoutMs*5))
  $gateArgs=@(
    '-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$runner
    '-RepoRoot',$source,'-CompilerPath',$compiler,'-RuntimePath',$runtime,'-SourcePath',$main
    '-SourceManifestPath',$manifest,'-EvidencePath',$production,'-TimeoutMs',[string]$CaseTimeoutMs
  )
  $gate=Run (Join-Path $PSHOME 'powershell.exe') $gateArgs $source $gateTimeout $child
  WriteText (Join-Path $evidence 'production.stdout') $gate.Stdout
  WriteText (Join-Path $evidence 'production.stderr') $gate.Stderr
  WriteText (Join-Path $evidence 'production.exit') ([string]$gate.ExitCode+[string][char]10)
  RequireRun $gate 'PRODUCTION';CheckSources $source $manifest
  if ($gate.Stderr -match 'P016 WINDOWS LINKER PRODUCTION GATE'){throw 'MARKER_STREAM'}
  $marker=$gate.Stdout.TrimEnd([char]13,[char]10)
  if ($gate.Stdout -cne ($marker+[string][char]10) -and $gate.Stdout -cne ($marker+[string][char]13+[char]10)){throw 'MARKER_CARDINALITY'}
  $pass=[regex]::Match($marker,'^P016 WINDOWS LINKER PRODUCTION GATE PASS: cases=3 compiler_sha256=([0-9a-f]{64}) runtime_sha256=([0-9a-f]{64}) evidence_sha256=([0-9a-f]{64})$')
  if ($gate.ExitCode -eq 0 -and $pass.Success -and $gate.Stderr.Length -eq 0){
    if ( -not (Regular $production) -or (Hash $compiler) -cne $pass.Groups[1].Value -or (Hash $runtime) -cne $pass.Groups[2].Value -or (Hash $production) -cne $pass.Groups[3].Value){throw 'PASS_BINDING'}
    [byte[]]$raw=[IO.File]::ReadAllBytes($production)
    if ($raw.Length -ge 3 -and $raw[0] -eq 239 -and $raw[1] -eq 187 -and $raw[2] -eq 191){throw 'PRODUCTION_BOM'}
    try{$doc=(New-Object Text.UTF8Encoding($false,$true)).GetString($raw)|ConvertFrom-Json}catch{throw 'PRODUCTION_JSON'}
    if ([string]$doc.schema -cne 'p016-windows-linker-production-v1' -or [int]$doc.totals.compiler_starts -ne 3 -or [int]$doc.totals.linker_starts -ne 3){throw 'PRODUCTION_SCHEMA'}
    if ([int]$doc.totals.payload_exec -ne 1 -or [int]$doc.totals.blocked -ne 2){throw 'PRODUCTION_SCHEMA'}
    WriteEvidence (Join-Path $evidence 'remote-evidence-sha256.txt') $evidence $evidenceNames $true;$result=0
  }elseif ($gate.ExitCode -eq 1 -and $marker -cmatch '^P016 WINDOWS LINKER PRODUCTION GATE FAIL: reason=[A-Z0-9_]+$'){
    try{WriteEvidence (Join-Path $evidence 'failure-evidence.sha256') $evidence $failureEvidenceNames $false}catch{}
    $result=1
  }elseif ($gate.ExitCode -eq 2 -and $marker -cmatch '^P016 WINDOWS LINKER PRODUCTION GATE INFRA_NOGO: reason=[A-Z0-9_]+$'){
    try{WriteEvidence (Join-Path $evidence 'failure-evidence.sha256') $evidence $failureEvidenceNames $false}catch{}
    $result=2
  }else{throw 'RC_MARKER_MISMATCH'}
  [Console]::Out.WriteLine($marker);exit $result
}catch{
  $raw=[string]$_.Exception.Message;if ($raw -cmatch '^[A-Z0-9_]{1,80}$'){$reason=$raw}else{$reason='CONTROLLER_ERROR'}
  try{
    if (Test-Path -LiteralPath $evidence -PathType Container){WriteEvidence (Join-Path $evidence 'failure-evidence.sha256') $evidence $failureEvidenceNames $false}
  }catch{[Console]::Error.WriteLine('failure evidence collection unavailable')}
  [Console]::Out.WriteLine('P016 WINDOWS LINKER PRODUCTION GATE INFRA_NOGO: reason='+$reason);exit 2
}
POWERSHELL
controller_sha="$(sha256sum "$controller"|awk '{print $1}')"
"${SCP[@]}" "$controller" "$win_target:$remote_root_scp/remote-controller.ps1" || infra CONTROLLER_TRANSFER
launcher="$OUT/remote-launcher.ps1"
printf '%s\n' \
  "\$ErrorActionPreference='Stop';\$ProgressPreference='SilentlyContinue';Set-StrictMode -Version Latest" \
  "try{" \
  "  \$root='$remote_root_ps';\$c=Join-Path \$root 'remote-controller.ps1'" \
  "  if ((Get-FileHash -LiteralPath \$c -Algorithm SHA256).Hash.ToLowerInvariant() -cne '$controller_sha'){throw 'CONTROLLER_HASH'}" \
  "  \$global:LASTEXITCODE=\$null" \
  "  & \$c -Root \$root -ManifestSha '$manifest_sha' -ArchiveSha '$archive_sha' -BuildTimeoutMs $BUILD_TIMEOUT_MS -CaseTimeoutMs $CASE_TIMEOUT_MS" \
  "  if (\$null -eq \$LASTEXITCODE -or \$LASTEXITCODE -isnot [int]){throw 'CONTROLLER_RC_INVALID'}" \
  "  \$controllerRc=[int]\$LASTEXITCODE" \
  "  if (\$controllerRc -lt 0 -or \$controllerRc -gt 255){throw 'CONTROLLER_RC_INVALID'}" \
  "  exit \$controllerRc" \
  "}catch{[Console]::Error.WriteLine('WINDOWS_LINKER_LAUNCHER_ERROR');exit 2}" >"$launcher"
if grep -q "$(printf '\015')" "$launcher";then infra LAUNCHER_CR;fi
launcher_sha="$(sha256sum "$launcher"|awk '{print $1}')";controller_b64="$(iconv -f UTF-8 -t UTF-16LE "$launcher"|base64|tr -d '\r\n')"
verifier_template="$OUT/evidence-verifier.template.ps1"
cat >"$verifier_template" <<'POWERSHELL'
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';Set-StrictMode -Version Latest
function FRegular ([string]$p){
  if ( -not (Test-Path -LiteralPath $p -PathType Leaf)){return $false}
  $i=Get-Item -LiteralPath $p -Force
  return ($i.Attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0
}
function FHash ([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$dir=Join-Path $Root 'evidence';$manifest=Join-Path $dir $ManifestName
if ( -not (Test-Path -LiteralPath $dir -PathType Container) -or (Get-Item -LiteralPath $dir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'DIR_INVALID'}
if ( -not (FRegular $manifest)){throw 'MANIFEST_INVALID'};[byte[]]$bytes=[IO.File]::ReadAllBytes($manifest)
if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191){throw 'MANIFEST_BOM'}
$lines=[IO.File]::ReadAllLines($manifest,(New-Object Text.UTF8Encoding($false,$true)));if ($lines.Count -lt 1){throw 'MANIFEST_EMPTY'}
$allowed=@('build.stderr','build.stdout','production-evidence.json','production.exit','production.stderr','production.stdout','toolchain.json');if ($ManifestName -ceq 'failure-evidence.sha256'){$allowed+=@('tar.exit','tar.stderr','tar.stdout')};$seen=@{};$previous=$null
foreach ($line in $lines){
  if ($line -cnotmatch '^([0-9a-f]{64})  ([A-Za-z0-9.-]+)$'){throw 'MANIFEST_SYNTAX'}
  $sha=$Matches[1];$name=$Matches[2]
  if ($allowed -cnotcontains $name -or $seen.ContainsKey($name)){throw 'MANIFEST_NAME'}
  if ($null -ne $previous -and [string]::CompareOrdinal($previous,$name) -ge 0){throw 'MANIFEST_ORDER'}
  $seen[$name]=$true;$previous=$name;$p=Join-Path $dir $name
  if ( -not (FRegular $p) -or (FHash $p) -cne $sha){throw 'EVIDENCE_HASH'}
}
if ($ManifestName -ceq 'remote-evidence-sha256.txt'){
  $production=Join-Path $dir 'production-evidence.json'
  if ((FHash $production) -cne $ExpectedEvidence){throw 'PRODUCTION_HASH'}
  $doc=[IO.File]::ReadAllText($production,(New-Object Text.UTF8Encoding($false,$true)))|ConvertFrom-Json
  if ([string]$doc.compiler.sha256 -cne $ExpectedCompiler -or [string]$doc.runtime.sha256 -cne $ExpectedRuntime){throw 'PRODUCTION_BINDING'}
}
[Console]::Out.WriteLine((FHash $manifest))
POWERSHELL
build_verifier(){
  local manifest_name=$1 expected_compiler=$2 expected_runtime=$3 expected_evidence=$4 script="$OUT/evidence-verifier.ps1"
  case "$manifest_name" in remote-evidence-sha256.txt|failure-evidence.sha256);;*) return 1;; esac
  for value in "$expected_compiler" "$expected_runtime" "$expected_evidence";do [[ -z "$value" || "$value" =~ ^[0-9a-f]{64}$ ]] || return 1;done
  { printf "\$Root=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String('%s'))\n\$ManifestName='%s'\n\$ExpectedCompiler='%s'\n\$ExpectedRuntime='%s'\n\$ExpectedEvidence='%s'\n" "$root_b64" "$manifest_name" "$expected_compiler" "$expected_runtime" "$expected_evidence"; cat "$verifier_template"; } >"$script" || return 1
  iconv -f UTF-8 -t UTF-16LE "$script"|base64|tr -d '\r\n'
}
collect_evidence(){
  local manifest_name=$1 dst=$2 required_count=$3 expected_compiler=${4-} expected_runtime=${5-} expected_evidence=${6-} b64 vo="$OUT/evidence-verify.out" ve="$OUT/evidence-verify.err" vrc manifest_sha part line sha name actual previous=
  local -a allowed=(build.stderr build.stdout production-evidence.json production.exit production.stderr production.stdout toolchain.json);[[ "$manifest_name" != failure-evidence.sha256 ]] || allowed+=(tar.exit tar.stderr tar.stdout)
  local -A seen=()
  [[ ! -e "$dst" ]] || return 1;mkdir "$dst" || return 1;b64="$(build_verifier "$manifest_name" "$expected_compiler" "$expected_runtime" "$expected_evidence")" || return 1
  set +e;"${SSH[@]}" "$win_target" "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $b64" >"$vo" 2>"$ve";vrc=$?;set -e
  mapfile -t verify_lines <"$vo";[[ ${#verify_lines[@]} -eq 1 ]] || return 1;verify_lines[0]="$(printf '%s' "${verify_lines[0]}"|sed 's/\r$//')";[[ $vrc -eq 0 && ! -s "$ve" && ${verify_lines[0]} =~ ^[0-9a-f]{64}$ ]] || return 1
  manifest_sha="${verify_lines[0]}";part="$dst/$manifest_name.part"
  "${SCP[@]}" "$win_target:$remote_root_scp/evidence/$manifest_name" "$part" || return 1
  [[ -f "$part" && ! -L "$part" && "$(sha256sum "$part"|cut -d' ' -f1)" == "$manifest_sha" ]] || return 1;mv "$part" "$dst/$manifest_name" || return 1
  while IFS= read -r line || [[ -n "$line" ]];do
    [[ "$line" =~ ^([0-9a-f]{64})\ \ ([A-Za-z0-9.-]+)$ ]] || return 1;sha="${BASH_REMATCH[1]}";name="${BASH_REMATCH[2]}"
    case "$name" in build.stderr|build.stdout|production-evidence.json|production.exit|production.stderr|production.stdout|toolchain.json);;tar.exit|tar.stderr|tar.stdout)[[ "$manifest_name" == failure-evidence.sha256 ]]||return 1;;*)return 1;;esac
    [[ -z "${seen[$name]+x}" && ( -z "$previous" || "$previous" < "$name" ) ]] || return 1;seen[$name]=1;previous="$name";part="$dst/$name.part"
    "${SCP[@]}" "$win_target:$remote_root_scp/evidence/$name" "$part" || return 1
    [[ -f "$part" && ! -L "$part" ]] || return 1;actual="$(sha256sum "$part"|cut -d' ' -f1)" || return 1;[[ "$actual" == "$sha" ]] || return 1;mv "$part" "$dst/$name" || return 1
  done <"$dst/$manifest_name"
  [[ "${#seen[@]}" -eq "$required_count" || "$required_count" -eq 0 ]] || return 1
  : >"$dst/absent-evidence.txt";for name in "${allowed[@]}";do [[ -n "${seen[$name]+x}" ]] || printf '%s\n' "$name" >>"$dst/absent-evidence.txt";done
}
printf 'target=%s\nremote_root=%s\nsource_manifest_sha256=%s\narchive_sha256=%s\ncontroller_sha256=%s\nlauncher_sha256=%s\nroot_create_sha256=%s\nbuild_timeout_ms=%s\ncase_timeout_ms=%s\n' \
  "$win_target" "$remote_root_ps" "$manifest_sha" "$archive_sha" "$controller_sha" "$launcher_sha" "$(sha256sum "$root_create"|cut -d' ' -f1)" "$BUILD_TIMEOUT_MS" "$CASE_TIMEOUT_MS" >"$OUT/command.txt"
remote_stdout_raw="$OUT/remote.stdout.raw";remote_stderr_raw="$OUT/remote.stderr.raw"
set +e
"${SSH[@]}" "$win_target" "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $controller_b64" >"$remote_stdout_raw" 2>"$remote_stderr_raw"
remote_rc=$?
set -e
result_rc=$remote_rc
sed 's/\r$//' <"$remote_stdout_raw" >"$OUT/remote.stdout" || result_rc=2
sed 's/\r$//' <"$remote_stderr_raw" >"$OUT/remote.stderr" || result_rc=2
mapfile -t marker_lines <"$OUT/remote.stdout";marker="${marker_lines[0]-}";marker_count="${#marker_lines[@]}";stderr_text="$(<"$OUT/remote.stderr")"
pass_re='^P016 WINDOWS LINKER PRODUCTION GATE PASS: cases=3 compiler_sha256=([0-9a-f]{64}) runtime_sha256=([0-9a-f]{64}) evidence_sha256=([0-9a-f]{64})$'
marker_valid=0
if [[ $remote_rc -eq 0 && $marker_count -eq 1 && -z "$stderr_text" && "$marker" =~ $pass_re ]];then
  compiler_sha="${BASH_REMATCH[1]}";runtime_sha="${BASH_REMATCH[2]}";evidence_sha="${BASH_REMATCH[3]}";marker_valid=1
elif [[ $remote_rc -eq 1 && $marker_count -eq 1 && "$marker" =~ ^P016\ WINDOWS\ LINKER\ PRODUCTION\ GATE\ FAIL:\ reason=[A-Z0-9_]+$ && ! "$stderr_text" =~ P016\ WINDOWS\ LINKER\ PRODUCTION\ GATE ]];then marker_valid=1
elif [[ $remote_rc -eq 2 && $marker_count -eq 1 && "$marker" =~ ^P016\ WINDOWS\ LINKER\ PRODUCTION\ GATE\ INFRA_NOGO:\ reason=[A-Z0-9_]+$ && ! "$stderr_text" =~ P016\ WINDOWS\ LINKER\ PRODUCTION\ GATE ]];then marker_valid=1
else result_rc=2
fi
if [[ $result_rc -eq 0 ]];then
  collect_evidence remote-evidence-sha256.txt "$OUT/remote-evidence" 7 "$compiler_sha" "$runtime_sha" "$evidence_sha" || result_rc=2
  [[ $result_rc -ne 0 || "$(sha256sum "$OUT/remote-evidence/production-evidence.json"|cut -d' ' -f1)" == "$evidence_sha" ]] || result_rc=2
  [[ $result_rc -ne 0 ]] || (cd "$ROOT" && sha256sum -c "$source_manifest" >"$OUT/source-check-before-final.txt") || result_rc=2
fi
if [[ $result_rc -ne 0 ]];then
  if ! collect_evidence failure-evidence.sha256 "$OUT/failure-evidence" 0;then
    if [[ $remote_rc -eq 0 && $marker_valid -eq 1 ]];then collect_evidence remote-evidence-sha256.txt "$OUT/reclassified-evidence" 7 "$compiler_sha" "$runtime_sha" "$evidence_sha" || printf 'P016 WINDOWS LINKER VM GATE DIAGNOSTIC: failure_evidence_unavailable=1 remote_rc=%s result_rc=%s VM_STAYS_ON=1\n' "$remote_rc" "$result_rc" >&2
    else printf 'P016 WINDOWS LINKER VM GATE DIAGNOSTIC: failure_evidence_unavailable=1 remote_rc=%s result_rc=%s VM_STAYS_ON=1\n' "$remote_rc" "$result_rc" >&2;fi
  fi
  if [[ $marker_valid -eq 1 && $result_rc -eq $remote_rc && ( $remote_rc -eq 1 || $remote_rc -eq 2 ) ]];then printf '%s\n' "$marker";exit "$result_rc";fi
  infra OUTER_CLASSIFICATION
fi
sha256sum "$source_manifest" "$archive" "$controller" "$launcher" "$root_create" "$OUT/command.txt" "$remote_stdout_raw" "$remote_stderr_raw" \
  "$OUT/remote-evidence/remote-evidence-sha256.txt" "$OUT/remote-evidence/production-evidence.json" "$OUT/remote-evidence/toolchain.json" \
  "$OUT/source-check-before-transfer.txt" "$OUT/source-check-before-final.txt" "$ROOT/scripts/windows_linker_behavior_gate.sh" \
  "$ROOT/compiler/tests/windows_linker_production_gate.ps1" "$ROOT/.mise.toml" >"$OUT/final-evidence-sha256.txt" || infra FINAL_EVIDENCE
printf 'P016 WINDOWS LINKER VM GATE PASS: cases=3 source_manifest_sha256=%s compiler_sha256=%s runtime_sha256=%s evidence_sha256=%s VM_STAYS_ON=1 artifacts=%s\n' "$manifest_sha" "$compiler_sha" "$runtime_sha" "$evidence_sha" "$OUT"
