@echo off
rem Starts DrakengardPC. Run build.bat once first.
rem Extra options are passed to the game, e.g.  play.bat --max-seconds 60
setlocal EnableExtensions
cd /d "%~dp0"

if not exist "build\game\DrakengardPC.exe" (
    echo  The game is not built yet. Drag your "Drakengard (USA).iso" onto build.bat first.
    pause
    exit /b 1
)
if not exist "work\iso_path.txt" (
    echo  The disc image location is unknown. Run build.bat with your ISO again.
    pause
    exit /b 1
)
set /p ISO=<"work\iso_path.txt"
if not exist "%ISO%" (
    echo  Cannot find the disc image: "%ISO%"
    echo  If you moved it, run build.bat with the new location.
    pause
    exit /b 1
)

rem The game uses the C++ runtime and FFmpeg DLLs from MSYS2 CLANG64.
set "CLANG64="
if defined MSYS2_CLANG64_BIN if exist "%MSYS2_CLANG64_BIN%\libc++.dll" set "CLANG64=%MSYS2_CLANG64_BIN%"
if not defined CLANG64 if exist "C:\msys64\clang64\bin\libc++.dll" set "CLANG64=C:\msys64\clang64\bin"
if not defined CLANG64 if exist "D:\msys64\clang64\bin\libc++.dll" set "CLANG64=D:\msys64\clang64\bin"
if defined CLANG64 set "PATH=%CLANG64%;%PATH%"

"build\game\DrakengardPC.exe" --iso "%ISO%" --data work %*
if errorlevel 1 (
    echo.
    echo  The game stopped with an error. See "Troubleshooting" in README.md.
    pause
)
