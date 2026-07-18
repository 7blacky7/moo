@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Proves the selected Windows TLS backend from the native static runtime.
rem Usage: scripts\windows_tls_backend_gate.cmd [schannel^|mbedtls]

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

if not defined MOO_WIN_VCVARS set "MOO_WIN_VCVARS=C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "!MOO_WIN_VCVARS!" >NUL
if errorlevel 1 exit /b 3

set "RUNTIME_LIB="
for /f "delims=" %%I in ('where /r "!ROOT!\compiler\target\release\build" moo_runtime.lib 2^>NUL') do set "RUNTIME_LIB=%%I"
if not defined RUNTIME_LIB (
    echo ERROR: moo_runtime.lib not found
    exit /b 4
)

set "SYMBOLS=%TEMP%\moo_tls_symbols_!BACKEND!_!RANDOM!_!RANDOM!.txt"
dumpbin /symbols "!RUNTIME_LIB!" >"!SYMBOLS!"
if errorlevel 1 exit /b 5

if /I "!BACKEND!"=="schannel" goto check_schannel
goto check_mbedtls

:check_schannel
call :must_have InitializeSecurityContextA
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_have EncryptMessage
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_have DecryptMessage
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_have CertVerifyCertificateChainPolicy
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_not_have mbedtls_ssl_handshake
if errorlevel 1 exit /b !ERRORLEVEL!
goto pass

:check_mbedtls
call :must_have mbedtls_ssl_handshake
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_have mbedtls_ssl_write
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_have mbedtls_ssl_read
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_not_have InitializeSecurityContextA
if errorlevel 1 exit /b !ERRORLEVEL!
call :must_not_have EncryptMessage
if errorlevel 1 exit /b !ERRORLEVEL!
goto pass

:must_have
findstr /C:"%~1" "!SYMBOLS!" >NUL
if errorlevel 1 (
    echo ERROR: expected symbol missing: %~1
    exit /b 10
)
exit /b 0

:must_not_have
findstr /C:"%~1" "!SYMBOLS!" >NUL
if not errorlevel 1 (
    echo ERROR: forbidden symbol present: %~1
    exit /b 11
)
exit /b 0

:pass
echo WINDOWS-TLS-BACKEND-GATE PASS backend=!BACKEND!
del /Q "!SYMBOLS!" >NUL 2>&1
exit /b 0
