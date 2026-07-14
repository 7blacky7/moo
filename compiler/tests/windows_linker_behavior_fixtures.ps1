function Get-P016WindowsLinkerHelperPath {
    $ErrorActionPreference='Stop'
    $path=Join-Path $PSScriptRoot 'windows_linker_behavior_helpers.ps1'
    if (-not [IO.Path]::IsPathRooted($path) -or -not (Test-Path -LiteralPath $path)) { throw 'HELPER_MISSING' }
    $item=Get-Item -LiteralPath $path
    $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
    if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists($path)) { throw 'HELPER_NOT_REGULAR' }
    $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -cne 'ab65a9e2670eed4c2edb3fa87852b15b0706c2ae27731eaffdc4e9ce11eeac50') { throw 'HELPER_SHA_DRIFT' }
    return [IO.Path]::GetFullPath($path)
}

function Get-P016PeMachine([string]$Path) {
    $ErrorActionPreference='Stop'
    $stream=[IO.File]::OpenRead($Path); $reader=New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'FIXTURE_MZ_INVALID' }
        $stream.Position=0x3c; $offset=$reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length-6)) { throw 'FIXTURE_PE_OFFSET_INVALID' }
        $stream.Position=$offset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw 'FIXTURE_PE_SIGNATURE_INVALID' }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Assert-P016RegularX64Fixture([string]$Path) {
    $ErrorActionPreference='Stop'
    if (-not [IO.Path]::IsPathRooted($Path) -or -not (Test-Path -LiteralPath $Path)) { throw 'FIXTURE_MISSING' }
    $item=Get-Item -LiteralPath $Path
    $bad=[IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint
    if (($item.Attributes -band $bad) -ne 0 -or -not [IO.File]::Exists($Path)) { throw 'FIXTURE_NOT_REGULAR' }
    if ((Get-P016PeMachine $Path) -ne 0x8664) { throw 'FIXTURE_MACHINE_NOT_X64' }
}

function New-P016ManagedFixture([string]$Path, [string]$Source) {
    $ErrorActionPreference='Stop'
    try {
        $compilerParameters=New-Object System.CodeDom.Compiler.CompilerParameters
        $compilerParameters.GenerateExecutable=$true;$compilerParameters.GenerateInMemory=$false;$compilerParameters.OutputAssembly=$Path;$compilerParameters.CompilerOptions='/platform:x64 /optimize+'
        $null=Add-Type -TypeDefinition $Source -Language CSharp -CompilerParameters $compilerParameters
    } catch { throw 'FIXTURE_BUILD_FAILED' }
    Assert-P016RegularX64Fixture $Path
}

function New-P016WindowsLinkerFixtures {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][ValidateNotNullOrEmpty()][string]$Root)
    $ErrorActionPreference='Stop'
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'WINDOWS_REQUIRED' }
    if (-not [IO.Path]::IsPathRooted($Root)) { throw 'ROOT_NOT_ABSOLUTE' }
    $Root=[IO.Path]::GetFullPath($Root)
    if (Test-Path -LiteralPath $Root) { throw 'ROOT_MUST_BE_ABSENT' }
    if (-not (Get-Command Add-Type -CommandType Cmdlet -ErrorAction SilentlyContinue)) { throw 'ADDTYPE_REQUIRED' }
    $helperPath=Get-P016WindowsLinkerHelperPath
    try { . $helperPath } catch { throw 'HELPER_IMPORT_FAILED' }
    foreach($api in @('Invoke-P016BoundedProcess','Get-P016Sha256','New-P016Sha256Manifest','Test-P016Sha256Manifest')) {
        if (-not (Get-Command $api -CommandType Function -ErrorAction SilentlyContinue)) { throw 'HELPER_API_MISSING' }
    }
    try { [void][IO.Directory]::CreateDirectory($Root) } catch { throw 'ROOT_CREATE_FAILED' }
    $suffix=[guid]::NewGuid().ToString('N')
    $clangPath=Join-Path $Root 'clang.exe'; $lldPath=Join-Path $Root 'lld-link.exe'; $payloadPath=Join-Path $Root 'payload.exe'
    $clangSource=@"
using System; using System.IO; using System.Text; using System.Collections.Generic; using System.Diagnostics;
namespace P016Fixtures$suffix {
 public static class Clang {
  public static int Main(string[] args) {
   string temp=null;
   try {
    string mode=Environment.GetEnvironmentVariable("MOO_FAKE_MODE");
    string payload=Environment.GetEnvironmentVariable("MOO_FAKE_PAYLOAD");
    string dir=Environment.GetEnvironmentVariable("MOO_FAKE_ARGV_DIR");
    if(String.IsNullOrEmpty(dir) || !Path.IsPathRooted(dir) || !Directory.Exists(dir)) return 41;
    FileAttributes da=File.GetAttributes(dir); if((da & FileAttributes.ReparsePoint)!=0) return 41;
    string id=Process.GetCurrentProcess().Id.ToString()+"-"+Guid.NewGuid().ToString("N");
    string log=Path.Combine(dir,"argv-"+id+".log"); temp=log+".tmp";
    UTF8Encoding utf8=new UTF8Encoding(false,true); List<string> lines=new List<string>();
    foreach(string token in args) { byte[] bytes=utf8.GetBytes(token); lines.Add(bytes.Length.ToString()+":"+Convert.ToBase64String(bytes)); }
    try { using(FileStream fs=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) using(StreamWriter sw=new StreamWriter(fs,utf8)) foreach(string line in lines) sw.WriteLine(line); }
    catch(IOException) { return 42; }
    try { File.Move(temp,log); temp=null; } catch(IOException) { return 43; }
    int outputSwitchCount=0;
    for(int i=0;i<args.Length;i++) if(String.Equals(args[i],"-o",StringComparison.Ordinal)) outputSwitchCount++;
    if(outputSwitchCount!=1) return 44;
    string output=null;
    for(int i=0;i<args.Length;i++) if(args[i]=="-o") { if(output!=null || i+1>=args.Length || String.IsNullOrEmpty(args[i+1])) return 44; output=args[++i]; }
    if(output==null) return 44;
    if(mode!="COPY_RC0") if(mode!="COPY_RC23") if(mode!="NOOUTPUT_RC0") return 45;
    if(String.IsNullOrEmpty(payload) || !Path.IsPathRooted(payload) || !File.Exists(payload)) return 46;
    FileAttributes pa=File.GetAttributes(payload); if((pa & (FileAttributes.Directory|FileAttributes.ReparsePoint))!=0) return 46;
    if(File.Exists(output) || Directory.Exists(output)) return 47;
    if(mode=="COPY_RC0" || mode=="COPY_RC23") File.Copy(payload,output,false);
    if(mode=="COPY_RC23") return 23; return 0;
   } catch { return 49; } finally { if(temp!=null) try { File.Delete(temp); } catch { } }
  }
 }
}
"@
    $lldSource=@"
namespace P016Fixtures$suffix { public static class Lld { public static int Main(string[] args) { return 0; } } }
"@
    $payloadSource=@"
using System; using System.IO; using System.Text;
namespace P016Fixtures$suffix {
 public static class Payload {
  public static int Main(string[] args) {
   string temp=null;
   try {
    string sentinel=Environment.GetEnvironmentVariable("MOO_FAKE_SENTINEL");
    if(String.IsNullOrEmpty(sentinel) || !Path.IsPathRooted(sentinel)) return 61;
    if(File.Exists(sentinel) || Directory.Exists(sentinel)) return 62;
    temp=sentinel+"."+Guid.NewGuid().ToString("N")+".tmp";
    byte[] data=new UTF8Encoding(false).GetBytes("PAYLOAD_EXECUTED\n");
    try { using(FileStream fs=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) fs.Write(data,0,data.Length); }
    catch(IOException) { return 63; }
    try { File.Move(temp,sentinel); temp=null; } catch(IOException) { return 64; }
    return 0;
   } catch { return 69; } finally { if(temp!=null) try { File.Delete(temp); } catch { } }
  }
 }
}
"@
    New-P016ManagedFixture $clangPath $clangSource
    New-P016ManagedFixture $lldPath $lldSource
    New-P016ManagedFixture $payloadPath $payloadSource
    $manifest=New-P016Sha256Manifest @($clangPath,$lldPath,$payloadPath)
    $check=Test-P016Sha256Manifest $manifest
    if (-not $check.Valid) { throw 'FIXTURE_MANIFEST_DRIFT' }
    $clangHash=[string]$manifest.Entries[0].Hash; $lldHash=[string]$manifest.Entries[1].Hash; $payloadHash=[string]$manifest.Entries[2].Hash
    if ($clangHash -ceq $lldHash -or $clangHash -ceq $payloadHash -or $lldHash -ceq $payloadHash) { throw 'FIXTURE_HASH_COLLISION' }
    return [pscustomobject]@{ Root=$Root; ClangPath=$clangPath; LldPath=$lldPath; PayloadPath=$payloadPath; Manifest=$manifest; ClangHash=$clangHash; LldHash=$lldHash; PayloadHash=$payloadHash }
}

if ($MyInvocation.InvocationName -eq '.') { return }
Write-Output 'P016 WINDOWS LINKER FIXTURES INFRA_NOGO: reason=DOT_SOURCE_REQUIRED'
exit 2