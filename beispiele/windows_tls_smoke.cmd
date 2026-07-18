@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Native Windows TLS integration gate.
rem Usage: beispiele\windows_tls_smoke.cmd [schannel^|mbedtls]

set "BACKEND=%~1"
if "!BACKEND!"=="" set "BACKEND=schannel"
if /I "!BACKEND!"=="schannel" goto backend_ok
if /I "!BACKEND!"=="mbedtls" goto backend_ok
echo ERROR: backend must be schannel or mbedtls
exit /b 2
:backend_ok

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
call "!ROOT!\scripts\windows_tls_build.cmd" "!BACKEND!"
if errorlevel 1 exit /b !ERRORLEVEL!

if not defined MOO_WIN_TOOLS set "MOO_WIN_TOOLS=C:\moo-tools"
if not defined MOO_WIN_VCVARS set "MOO_WIN_VCVARS=C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined CARGO_HOME set "CARGO_HOME=!MOO_WIN_TOOLS!\cargo"
if not defined RUSTUP_HOME set "RUSTUP_HOME=%USERPROFILE%\.rustup"
if not defined MOO_WIN_LLVM_ROOT set "MOO_WIN_LLVM_ROOT=!MOO_WIN_TOOLS!\llvm18\Library"

call "!MOO_WIN_VCVARS!" >NUL
if errorlevel 1 exit /b 3
set "PATH=!CARGO_HOME!\bin;!MOO_WIN_LLVM_ROOT!\bin;!PATH!"
set "INCLUDE=!MOO_WIN_LLVM_ROOT!\include;!INCLUDE!"
set "LIB=!MOO_WIN_LLVM_ROOT!\lib;!LIB!"
set "LLVM_SYS_181_PREFIX=!MOO_WIN_LLVM_ROOT!"
set "LIBCLANG_PATH=!MOO_WIN_LLVM_ROOT!\bin"
set "MOO_CLANG=!MOO_WIN_LLVM_ROOT!\bin\clang.exe"
for /f "delims=" %%I in ('rustc --print sysroot') do set "RUST_SYSROOT=%%I"
if not defined MOO_LLD set "MOO_LLD=!RUST_SYSROOT!\lib\rustlib\x86_64-pc-windows-msvc\bin\gcc-ld\lld-link.exe"

set "SMOKE_STEM=%TEMP%\moo_windows_tls_!BACKEND!_!RANDOM!_!RANDOM!"
set "SMOKE_EXE=!SMOKE_STEM!.exe"
set "SMOKE_LOG=!SMOKE_STEM!.log"
pushd "!ROOT!"
compiler\target\release\moo-compiler.exe compile beispiele\windows_tls_smoke.moos -o "!SMOKE_EXE!"
set "RC=!ERRORLEVEL!"
popd
if not "!RC!"=="0" exit /b !RC!

call :run_mode https "WINDOWS-TLS-HTTPS PASS"
if errorlevel 1 exit /b !ERRORLEVEL!
call :run_mode reject "WINDOWS-TLS-SELF-SIGNED-REJECT PASS"
if errorlevel 1 exit /b !ERRORLEVEL!
call :run_mode starttls "WINDOWS-TLS-STARTTLS PASS"
if errorlevel 1 exit /b !ERRORLEVEL!
call :run_mode timeout "WINDOWS-TLS-TIMEOUT PASS"
if errorlevel 1 exit /b !ERRORLEVEL!

echo WINDOWS-TLS-SMOKE PASS backend=!BACKEND!
del /Q "!SMOKE_EXE!" "!SMOKE_LOG!" >NUL 2>&1
exit /b 0

:run_mode
set "MOO_TLS_TEST_MODE=%~1"
"!SMOKE_EXE!" >"!SMOKE_LOG!" 2>&1
set "MODE_RC=!ERRORLEVEL!"
type "!SMOKE_LOG!"
if not "!MODE_RC!"=="0" exit /b !MODE_RC!
findstr /X /C:"%~2" "!SMOKE_LOG!" >NUL
if errorlevel 1 (
    echo ERROR: missing marker %~2
    exit /b 20
)
exit /b 0
