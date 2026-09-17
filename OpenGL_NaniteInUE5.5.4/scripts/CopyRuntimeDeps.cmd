@echo off
setlocal EnableExtensions

REM Post-Build: deploy glew32.dll and Res/ to output directory.
REM %1 = OutDir (with trailing backslash)
REM %2 = ProjectDir (with trailing backslash)
REM %3 = Platform (x64 or Win32)

set "OUTDIR=%~1"
set "PROJECTDIR=%~2"
set "PLATFORM=%~3"

if "%OUTDIR%"=="" goto :usage
if "%PROJECTDIR%"=="" goto :usage
if "%PLATFORM%"=="" goto :usage

if /I "%PLATFORM%"=="Win32" (
    set "GLEW_BIN=%PROJECTDIR%ThirdParty\GLEW\bin\x86"
) else (
    set "GLEW_BIN=%PROJECTDIR%ThirdParty\GLEW\bin\%PLATFORM%"
)

set "GLEW_DLL=%GLEW_BIN%\glew32.dll"
set "GLEW_DEST=%OUTDIR%glew32.dll"

if not exist "%GLEW_DLL%" (
    echo [Deploy] ERROR: missing source %GLEW_DLL%
    exit /b 1
)

if not exist "%GLEW_DEST%" (
    copy /Y "%GLEW_DLL%" "%GLEW_DEST%" >nul
    echo [Deploy] Copied glew32.dll -^> %OUTDIR%
) else (
    echo [Deploy] OK: glew32.dll already in output.
)

if not exist "%PROJECTDIR%Res" (
    echo [Deploy] ERROR: missing Res folder at %PROJECTDIR%Res
    exit /b 1
)

xcopy /E /I /Y /Q "%PROJECTDIR%Res" "%OUTDIR%Res\" >nul
echo [Deploy] Synced Res -^> %OUTDIR%Res\

exit /b 0

:usage
echo Usage: CopyRuntimeDeps.cmd OutDir ProjectDir Platform
exit /b 1
