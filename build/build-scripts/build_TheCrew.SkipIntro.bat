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
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin SkipIntro -Phase Preflight
if errorlevel 1 goto :fail
call "%~dp0setup_msvc.bat"
if errorlevel 1 goto :fail

set "OUTDIR=%ROOT%\build\SkipIntro"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if errorlevel 1 goto :fail
cl /nologo /O2 /MT /GS /EHsc /std:c++17 /Fo:"%OUTDIR%\SkipIntroTests.obj" /Fe:"%OUTDIR%\SkipIntroTests.exe" "%ROOT%\tests\SkipIntroTests.cpp"
if errorlevel 1 goto :fail
"%OUTDIR%\SkipIntroTests.exe"
if not "%errorlevel%"=="0" goto :fail
rc /nologo /d BUILD_SKIP_INTRO /fo "%OUTDIR%\TheCrewFramerateUnlocker.res" "%ROOT%\source\TheCrewFramerateUnlocker.rc"
if errorlevel 1 goto :fail
cl /nologo /O2 /MT /GS /EHsc /std:c++17 /LD /Fo:"%OUTDIR%\SkipIntro.obj" "%ROOT%\source\SkipIntro\TheCrew.SkipIntro.cpp" /link "%OUTDIR%\TheCrewFramerateUnlocker.res" /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /OUT:"%OUTDIR%\TheCrew.SkipIntro.asi"
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin SkipIntro -Phase Built
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin SkipIntro -Phase Deploy
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0clean_build.ps1" -ProjectRoot "%ROOT%" -Plugin SkipIntro
if errorlevel 1 goto :fail

echo SKIP INTRO BUILD CHECK: PASS
popd
exit /b 0
:fail
echo SKIP INTRO BUILD CHECK: FAIL
popd
exit /b 1
