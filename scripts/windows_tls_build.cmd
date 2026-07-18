@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Native Windows TLS build for moo (MSVC + LLVM 18).
rem Usage: scripts\windows_tls_build.cmd [mbedtls^|schannel]

set "BACKEND=%~1"
if "%BACKEND%"=="" set "BACKEND=mbedtls"
if /I "!BACKEND!"=="mbedtls" goto backend_ok
if /I "!BACKEND!"=="schannel" goto backend_ok
echo ERROR: backend must be mbedtls or schannel
exit /b 2
:backend_ok

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if not defined MOO_WIN_TOOLS set "MOO_WIN_TOOLS=C:\moo-tools"
if not defined MOO_WIN_VCVARS set "MOO_WIN_VCVARS=C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined CARGO_HOME set "CARGO_HOME=!MOO_WIN_TOOLS!\cargo"
if not defined RUSTUP_HOME set "RUSTUP_HOME=%USERPROFILE%\.rustup"
if not defined MOO_WIN_LLVM_ROOT set "MOO_WIN_LLVM_ROOT=!MOO_WIN_TOOLS!\llvm18\Library"

if not exist "!MOO_WIN_VCVARS!" (
    echo ERROR: vcvars64.bat not found: !MOO_WIN_VCVARS!
    exit /b 3
)
if not exist "!CARGO_HOME!\bin\cargo.exe" (
    echo ERROR: cargo.exe not found below !CARGO_HOME!
    exit /b 3
)
if not exist "!MOO_WIN_LLVM_ROOT!\bin\llvm-config.exe" (
    echo ERROR: llvm-config.exe not found below !MOO_WIN_LLVM_ROOT!
    exit /b 3
)
if not exist "!MOO_WIN_LLVM_ROOT!\include\sqlite3.h" (
    echo ERROR: sqlite3.h not found below !MOO_WIN_LLVM_ROOT!
    exit /b 3
)
if not exist "!MOO_WIN_LLVM_ROOT!\lib\sqlite3.lib" (
    echo ERROR: sqlite3.lib not found below !MOO_WIN_LLVM_ROOT!
    exit /b 3
)

call "!MOO_WIN_VCVARS!" >NUL
if errorlevel 1 exit /b 4

set "PATH=!CARGO_HOME!\bin;!MOO_WIN_LLVM_ROOT!\bin;!PATH!"
set "INCLUDE=!MOO_WIN_LLVM_ROOT!\include;!INCLUDE!"
set "LIB=!MOO_WIN_LLVM_ROOT!\lib;!LIB!"
set "LLVM_SYS_181_PREFIX=!MOO_WIN_LLVM_ROOT!"
set "LIBCLANG_PATH=!MOO_WIN_LLVM_ROOT!\bin"
set "MOO_CLANG=!MOO_WIN_LLVM_ROOT!\bin\clang.exe"

for /f "delims=" %%I in ('rustc --print sysroot') do set "RUST_SYSROOT=%%I"
if not defined MOO_LLD set "MOO_LLD=!RUST_SYSROOT!\lib\rustlib\x86_64-pc-windows-msvc\bin\gcc-ld\lld-link.exe"
if not exist "!MOO_CLANG!" (
    echo ERROR: clang.exe not found: !MOO_CLANG!
    exit /b 5
)
if not exist "!MOO_LLD!" (
    echo ERROR: lld-link.exe not found: !MOO_LLD!
    exit /b 5
)

set "MOO_TLS_BACKEND=!BACKEND!"
echo WINDOWS_TLS_BUILD backend=!MOO_TLS_BACKEND!
echo WINDOWS_TLS_BUILD root=!ROOT!
echo WINDOWS_TLS_BUILD rustc=
rustc --version
echo WINDOWS_TLS_BUILD cargo=
cargo --version
echo WINDOWS_TLS_BUILD llvm=
llvm-config --version
echo WINDOWS_TLS_BUILD clang=!MOO_CLANG!
echo WINDOWS_TLS_BUILD lld=!MOO_LLD!

pushd "!ROOT!\compiler"
cargo build --release --no-default-features
set "RC=!ERRORLEVEL!"
popd
if not "!RC!"=="0" exit /b !RC!

echo WINDOWS_TLS_BUILD PASS backend=!MOO_TLS_BACKEND!
exit /b 0
