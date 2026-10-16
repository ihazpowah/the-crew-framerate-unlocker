@echo off
if defined THECREW_PRIVATE_BUILD goto :private_build_ready
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0invoke_private_build.ps1" -ScriptName "%~nx0"
exit /b %errorlevel%
:private_build_ready
setlocal EnableExtensions
title Build, test and deploy The Crew Framerate Unlocker x64
pushd "%~dp0"
if errorlevel 1 exit /b 1
for %%I in ("%CD%\..\..") do set "ROOT=%%~fI"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT%."
set "OUTDIR=%ROOT%\build\FramerateUnlocker"
set "OUTASI=%OUTDIR%\TheCrew.FramerateUnlocker.asi"
set "DESTASI=C:\Games\The Crew\scripts\TheCrew.FramerateUnlocker.asi"
set "CHECKDIR=%OUTDIR%\checks"
set "STAGE=preflight"

echo.
echo The Crew Framerate Unlocker - build, regression checks and deployment
echo =================================
echo.

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_TheCrew.FramerateUnlocker.ps1" -Phase Preflight -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail
set "STAGE=toolchain"

rem ---------------------------------------------------------------------------
rem Locate Visual Studio x64 build tools.
rem ---------------------------------------------------------------------------
call "%~dp0setup_msvc.bat"
if errorlevel 1 goto :fail

where cl.exe >nul 2>&1
if errorlevel 1 (
  echo ERROR: cl.exe not found after vcvars64.bat.
  goto :fail
)

rem ---------------------------------------------------------------------------
rem Set project paths.
rem ---------------------------------------------------------------------------
set "SRC=%ROOT%\source\FramerateUnlocker\TheCrew.FramerateUnlocker.cpp"

rem ---------------------------------------------------------------------------
rem Check TheCrewFramerateUnlocker source.
rem ---------------------------------------------------------------------------
if not exist "%SRC%" (
  echo ERROR: Source file is missing:
  echo   %SRC%
  goto :fail
)

rem ---------------------------------------------------------------------------
rem Locate MinHook source.
rem
rem Supported:
rem   MINHOOK_ROOT environment variable
rem   %ROOT%\MinHook\
rem
rem Dependency sources are local, explicit and checked against release provenance.
rem ---------------------------------------------------------------------------
set "MH="
if defined MINHOOK_ROOT (
  if not exist "%MINHOOK_ROOT%\include\MinHook.h" (
    echo ERROR: MINHOOK_ROOT does not contain MinHook sources.
    goto :fail
  )
  set "MH=%MINHOOK_ROOT%"
)
if not defined MH if exist "%ROOT%\MinHook\include\MinHook.h" set "MH=%ROOT%\MinHook"
if not defined MH (
  echo ERROR: MinHook sources are required locally in %ROOT%\MinHook or MINHOOK_ROOT.
  echo No dependency is downloaded automatically.
  goto :fail
)
set "STAGE=dependency-provenance"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_TheCrew.FramerateUnlocker.ps1" -Phase Dependency -ProjectRoot "%ROOT%" -MinHookRoot "%MH%"
if errorlevel 1 goto :fail

echo Using MinHook:
echo   %MH%
echo.

rem ---------------------------------------------------------------------------
rem Verify expected MinHook source layout.
rem ---------------------------------------------------------------------------
if not exist "%MH%\include\MinHook.h" (
  echo ERROR: %MH%\include\MinHook.h is missing.
  goto :fail
)
if not exist "%MH%\src\buffer.c" (
  echo ERROR: %MH%\src\buffer.c is missing.
  goto :fail
)
if not exist "%MH%\src\hook.c" (
  echo ERROR: %MH%\src\hook.c is missing.
  goto :fail
)
if not exist "%MH%\src\trampoline.c" (
  echo ERROR: %MH%\src\trampoline.c is missing.
  goto :fail
)
if not exist "%MH%\src\hde\hde64.c" (
  echo ERROR: %MH%\src\hde\hde64.c is missing.
  goto :fail
)

rem ---------------------------------------------------------------------------
rem Build MinHook objects first as C.
rem ---------------------------------------------------------------------------
set "STAGE=minhook"
echo Compiling MinHook...
echo.

cl /nologo /c /O2 /MT /GS ^
  /I"%MH%\include" ^
  /I"%MH%\src" ^
  /I"%MH%\src\hde" ^
  "%MH%\src\buffer.c" ^
  "%MH%\src\hook.c" ^
  "%MH%\src\trampoline.c" ^
  "%MH%\src\hde\hde64.c"

if errorlevel 1 goto :fail

rem ---------------------------------------------------------------------------
rem Run the supported regression executables against the same MinHook objects.
rem No deployment occurs unless every configured executable compiles and passes.
rem ---------------------------------------------------------------------------
set "STAGE=regression-checks"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%ROOT%\tests\BuildCleanupTests.ps1" -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if errorlevel 1 goto :fail
if not exist "%CHECKDIR%" mkdir "%CHECKDIR%"
if errorlevel 1 goto :fail
for %%P in (A B) do (
  cl /nologo /O2 /MT /GS /EHsc /std:c++17 /LD /I"%MH%\include" /Fo:"%CHECKDIR%\NativeHookChain%%P.obj" "%ROOT%\tests\NativeHookChainFixture.cpp" /link "buffer.obj" "hook.obj" "trampoline.obj" "hde64.obj" /OUT:"%CHECKDIR%\NativeHookChain%%P.dll"
  if errorlevel 1 goto :fail
)
call :run_check ShadowCameraTests
if errorlevel 1 goto :fail
call :run_check ScreenSpaceFlareTests
if errorlevel 1 goto :fail
call :run_check SceneRenderAnimationTests
if errorlevel 1 goto :fail
call :run_check MatrixInterpolationTests
if errorlevel 1 goto :fail
call :run_check RepeatedSceneUploadTests
if errorlevel 1 goto :fail
call :run_check HookSafetyTests
if errorlevel 1 goto :fail

rem ---------------------------------------------------------------------------
rem Build TheCrewFramerateUnlocker ASI and link the freshly compiled MinHook objects.
rem Output goes to ..\FramerateUnlocker\
rem ---------------------------------------------------------------------------
set "STAGE=compile-link"

echo.
echo Compiling TheCrewFramerateUnlocker integrated ASI...
echo.

del /q "%OUTDIR%\TheCrew.FramerateUnlocker.asi" "FramerateUnlocker.obj" 2>nul
del /q "%OUTDIR%\TheCrew.FramerateUnlocker.pdb" "%OUTDIR%\TheCrew.FramerateUnlocker.compile.pdb" 2>nul

cl /nologo /c /O2 /MT /GS /EHsc /std:c++17 /Zi  ^
  /Fd:"%OUTDIR%\TheCrew.FramerateUnlocker.compile.pdb" ^
  /I"%MH%\include" ^
  /Fo:"FramerateUnlocker.obj" ^
  "%SRC%"

if errorlevel 1 goto :fail

rc /nologo /fo "TheCrewFramerateUnlocker.res" "%ROOT%\source\TheCrewFramerateUnlocker.rc"
if errorlevel 1 goto :fail

link /nologo /DLL /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT ^
  /DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO ^
  /PDB:"%OUTDIR%\TheCrew.FramerateUnlocker.pdb" ^
  /PDBALTPATH:TheCrew.FramerateUnlocker.pdb ^
  /OUT:"%OUTDIR%\TheCrew.FramerateUnlocker.asi" ^
  "FramerateUnlocker.obj" "TheCrewFramerateUnlocker.res" ^
  "buffer.obj" ^
  "hook.obj" ^
  "trampoline.obj" ^
  "hde64.obj" ^
   d3d11.lib ^
   winmm.lib ^
   user32.lib

if errorlevel 1 goto :fail

if not exist "%OUTDIR%\TheCrew.FramerateUnlocker.asi" (
  echo ERROR: linker returned success but ASI output is missing.
  goto :fail
)

del /q "FramerateUnlocker.obj" "buffer.obj" "hook.obj" "trampoline.obj" "hde64.obj" "%OUTDIR%\TheCrew.FramerateUnlocker.lib" "%OUTDIR%\TheCrew.FramerateUnlocker.exp" 2>nul

set "STAGE=artifact-check"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_TheCrew.FramerateUnlocker.ps1" -Phase Built -ProjectRoot "%ROOT%" -MinHookRoot "%MH%"
if errorlevel 1 goto :fail

rem Recheck game state immediately before deployment, then copy ONLY this ASI.
rem Installed DAT settings remain untouched.
set "STAGE=deployment"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_TheCrew.FramerateUnlocker.ps1" -Phase Preflight -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail
copy /y "%OUTASI%" "%DESTASI%" >nul
if errorlevel 1 goto :fail

set "STAGE=advanced-graphics-build"
call "%~dp0build_TheCrew.AdvancedGraphics.bat"
if errorlevel 1 goto :fail
set "STAGE=skip-intro-build"
call "%~dp0build_TheCrew.SkipIntro.bat"
if errorlevel 1 goto :fail
set "STAGE=xinput-proxy-build"
call "%~dp0build_xinput1_3.bat"
if errorlevel 1 goto :fail

set "STAGE=deployment-check"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_TheCrew.FramerateUnlocker.ps1" -Phase Deployed -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail

echo.
set "STAGE=cleanup"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0clean_build.ps1" -ProjectRoot "%ROOT%" -Plugin All
if errorlevel 1 goto :fail

echo BUILD CHECK: PASS
echo Regression checks: ShadowCameraTests, ScreenSpaceFlareTests, SceneRenderAnimationTests, MatrixInterpolationTests, RepeatedSceneUploadTests, HookSafetyTests
echo Built and deployed: %DESTASI%
echo.
echo [OK] Build and deploy complete.
echo IMPORTANT: If the game is running, fully close it (check Task Manager for TheCrew.exe) before relaunching.
echo.
set /p "_exit=Press Enter to exit..."
popd
exit /b 0

:run_check
echo.
echo Running %~1...
cl /nologo /O2 /MT /GS /EHsc /std:c++17 /I"%MH%\include" ^
  /Fo:"%CHECKDIR%\%~1.obj" /Fe:"%CHECKDIR%\%~1.exe" ^
  "%ROOT%\tests\%~1.cpp" /link ^
  "%CD%\buffer.obj" "%CD%\hook.obj" "%CD%\trampoline.obj" "%CD%\hde64.obj" ^
  d3d11.lib winmm.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
"%CHECKDIR%\%~1.exe"
if not "%errorlevel%"=="0" (
  echo Regression executable failed: %~1 exit=%errorlevel%
  exit /b 1
)
del /q "%CHECKDIR%\%~1.obj" 2>nul
exit /b 0

:fail
echo.
echo BUILD CHECK: FAIL - stage=%STAGE%
echo.
set /p "_exit=Press Enter to exit..."
popd
exit /b 1
