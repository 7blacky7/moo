@echo off
setlocal
set "MOO_TMP=%CD%\.tmp"
if not exist "%MOO_TMP%" mkdir "%MOO_TMP%"
if not exist "%MOO_TMP%" exit /b 126
set "TMP=%MOO_TMP%"
set "TEMP=%MOO_TMP%"
set CLANG=C:\moo-tools\llvm18\Library\bin\clang.exe
if not exist "%CLANG%" (
  >version.rc echo 127
  exit /b 127
)
"%CLANG%" --version > clang-version.txt 2>&1
set VERSION_RC=%ERRORLEVEL%
>version.rc echo %VERSION_RC%
if not "%VERSION_RC%"=="0" exit /b %VERSION_RC%
findstr /b /c:"clang version 18." clang-version.txt >nul
set LLVM18_RC=%ERRORLEVEL%
if not "%LLVM18_RC%"=="0" (
  >version.rc echo 18
  exit /b 18
)
"%CLANG%" -std=c11 -Wall -Wextra -Werror -pedantic -Icompiler\runtime compiler\runtime\tests\test_effects_integration.c compiler\runtime\moo_compositor_core.c compiler\runtime\moo_compositor_raster.c compiler\runtime\moo_compositor_effects_state.c compiler\runtime\moo_compositor_animation.c compiler\runtime\moo_compositor_effects_math.c compiler\runtime\moo_compositor_effects_cpu.c compiler\runtime\moo_compositor_effects_damage.c compiler\runtime\moo_compositor_effects_gpu.c -o test_effects_integration.exe > build.log 2>&1
set BUILD_RC=%ERRORLEVEL%
>build.rc echo %BUILD_RC%
if not "%BUILD_RC%"=="0" exit /b %BUILD_RC%
.\test_effects_integration.exe > run.log 2>&1
set RUN_RC=%ERRORLEVEL%
>run.rc echo %RUN_RC%
if not "%RUN_RC%"=="0" exit /b %RUN_RC%
findstr /x /c:"P016-O5 I1 INTEGRATION PASS" run.log >nul
set PASS_RC=%ERRORLEVEL%
if not "%PASS_RC%"=="0" (
  >run.rc echo 10
  exit /b 10
)
findstr /c:"FAIL " /c:" INTEGRATION RED:" run.log >nul
set BAD_RC=%ERRORLEVEL%
if "%BAD_RC%"=="0" (
  >run.rc echo 11
  exit /b 11
)
findstr /b /c:"P016-O5 I1 INTEGRATION GREEN: " run.log > green-summary.txt
set GREEN_RC=%ERRORLEVEL%
if not "%GREEN_RC%"=="0" (
  >run.rc echo 12
  exit /b 12
)
exit /b 0
