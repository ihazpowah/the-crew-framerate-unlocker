@echo off
if defined THECREW_PRIVATE_BUILD goto :private_build_ready
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0invoke_private_build.ps1" -ScriptName "%~nx0"
exit /b %errorlevel%
:private_build_ready
setlocal EnableExtensions
pushd "%~dp0"
if errorlevel 1 exit /b 1
for %%I in ("%CD%\..\..") do set "ROOT=%%~fI"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT%."
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin AdvancedGraphics -Phase Preflight
if errorlevel 1 goto :fail
call "%~dp0setup_msvc.bat"
if errorlevel 1 goto :fail

set "OUTDIR=%ROOT%\build\AdvancedGraphics"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if errorlevel 1 goto :fail
set "MH=%ROOT%\MinHook"
if defined MINHOOK_ROOT set "MH=%MINHOOK_ROOT%"
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -Plugin AdvancedGraphics -Phase Dependency -ProjectRoot "%ROOT%" -MinHookRoot "%MH%"
if errorlevel 1 goto :fail
for %%F in (buffer hook trampoline) do (
  cl /nologo /O2 /MT /GS /c /TC /I"%MH%\include" /I"%MH%\src" /Fo:"%OUTDIR%\%%F.obj" "%MH%\src\%%F.c"
  if errorlevel 1 goto :fail
)
cl /nologo /O2 /MT /GS /c /TC /Fo:"%OUTDIR%\hde64.obj" "%MH%\src\hde\hde64.c"
if errorlevel 1 goto :fail

cl /nologo /O2 /MT /GS /EHsc /std:c++17 /I"%MH%\include" /Fo:"%OUTDIR%\AdvancedGraphicsTests.obj" /Fe:"%OUTDIR%\AdvancedGraphicsTests.exe" "%ROOT%\tests\AdvancedGraphicsTests.cpp" /link "%OUTDIR%\buffer.obj" "%OUTDIR%\hook.obj" "%OUTDIR%\trampoline.obj" "%OUTDIR%\hde64.obj"
if errorlevel 1 goto :fail
"%OUTDIR%\AdvancedGraphicsTests.exe"
if not "%errorlevel%"=="0" goto :fail
rc /nologo /d BUILD_ADVANCED_GRAPHICS /fo "%OUTDIR%\TheCrewFramerateUnlocker.res" "%ROOT%\source\TheCrewFramerateUnlocker.rc"
if errorlevel 1 goto :fail
cl /nologo /O2 /MT /GS /EHsc /std:c++17 /I"%MH%\include" /LD /Fo:"%OUTDIR%\AdvancedGraphics.obj" "%ROOT%\source\AdvancedGraphics\TheCrew.AdvancedGraphics.cpp" /link "%OUTDIR%\TheCrewFramerateUnlocker.res" "%OUTDIR%\buffer.obj" "%OUTDIR%\hook.obj" "%OUTDIR%\trampoline.obj" "%OUTDIR%\hde64.obj" /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /OUT:"%OUTDIR%\TheCrew.AdvancedGraphics.asi"
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin AdvancedGraphics -Phase Built
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin AdvancedGraphics -Phase Deploy
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0clean_build.ps1" -ProjectRoot "%ROOT%" -Plugin AdvancedGraphics
if errorlevel 1 goto :fail

echo ADVANCED GRAPHICS BUILD CHECK: PASS
popd
exit /b 0
:fail
echo ADVANCED GRAPHICS BUILD CHECK: FAIL
popd
exit /b 1
