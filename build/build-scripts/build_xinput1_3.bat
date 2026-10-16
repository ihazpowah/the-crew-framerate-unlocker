@echo off
if defined THECREW_PRIVATE_BUILD goto :private_build_ready
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0invoke_private_build.ps1" -ScriptName "%~nx0"
exit /b %errorlevel%
:private_build_ready
setlocal EnableExtensions
title Build TheCrewFramerateUnlocker XInput 1.3 wrapper x64 - scripts ASI
pushd "%~dp0"

call "%~dp0setup_msvc.bat"
if errorlevel 1 goto :fail

set "ROOT=%CD%\..\.."
set "SRC=%ROOT%\source\xinput1_3_wrapper"

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin XInput -Phase Preflight
if errorlevel 1 goto :fail

if not exist "%SRC%\xinput1_3_wrapper.cpp" (
  echo ERROR: xinput1_3_wrapper.cpp missing from:
  echo   %SRC%
  goto :fail
)

if not exist "%SRC%\xinput1_3_wrapper.def" (
  echo ERROR: xinput1_3_wrapper.def missing from:
  echo   %SRC%
  goto :fail
)

set "OUTDIR=%CD%\..\xinput1_3_wrapper"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if errorlevel 1 goto :fail

echo Running production XInput loader regression checks...
cl /nologo /O2 /MT /GS /EHsc /std:c++17 ^
  /Fo:"%OUTDIR%\XInputProxyTests.obj" /Fe:"%OUTDIR%\XInputProxyTests.exe" ^
  "%ROOT%\tests\XInputProxyTests.cpp"
if errorlevel 1 goto :fail
"%OUTDIR%\XInputProxyTests.exe"
if not "%errorlevel%"=="0" goto :fail

rc /nologo /d BUILD_PROXY /fo "%OUTDIR%\TheCrewFramerateUnlocker.res" "%ROOT%\source\TheCrewFramerateUnlocker.rc"
if errorlevel 1 goto :fail

del /q "%OUTDIR%\xinput1_3.dll" xinput1_3_wrapper.obj xinput1_3.lib xinput1_3.exp 2>nul

echo.
echo Building XInput proxy that loads ASI from .\scripts\ ...
echo.

cl /nologo /O2 /MT /GS /EHsc /std:c++17 /LD ^
  /Fo:"xinput1_3_wrapper.obj" ^
  "%SRC%\xinput1_3_wrapper.cpp" ^
  /link "%OUTDIR%\TheCrewFramerateUnlocker.res" /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT ^
  /DEF:"%SRC%\xinput1_3_wrapper.def" /OUT:"%OUTDIR%\xinput1_3.dll"

if errorlevel 1 goto :fail
if not exist "%OUTDIR%\xinput1_3.dll" goto :fail

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_xinput1_3.ps1" -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%ROOT%\tests\ArtifactChecksTests.ps1" -ProjectRoot "%ROOT%"
if errorlevel 1 goto :fail

del /q xinput1_3_wrapper.obj xinput1_3_wrapper.lib xinput1_3_wrapper.exp 2>nul

powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0verify_optional_plugin.ps1" -ProjectRoot "%ROOT%" -Plugin XInput -Phase Deploy
if errorlevel 1 goto :fail

echo.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0clean_build.ps1" -ProjectRoot "%ROOT%" -Plugin XInput
if errorlevel 1 goto :fail

echo PROXY BUILD CHECK: PASS
echo Built:
echo   %OUTDIR%\xinput1_3.dll
echo.
popd
exit /b 0

:fail
echo.
echo BUILD FAILED
echo.
popd
exit /b 1
