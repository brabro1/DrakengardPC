@echo off
rem DrakengardPC one-time setup: builds the port from YOUR OWN copy of Drakengard (USA).
rem Usage: drag "Drakengard (USA).iso" onto this file, or run:  build.bat "C:\path\to\Drakengard (USA).iso"
rem Everything it creates stays in this folder (build\ and work\). Your ISO is only read.
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo  ===== DrakengardPC setup =====
echo.

set "ISO=%~1"
if "%ISO%"=="" if exist "work\iso_path.txt" set /p ISO=<"work\iso_path.txt"
if "%ISO%"=="" (
    echo  Drag your "Drakengard (USA).iso" file onto build.bat, or run:
    echo      build.bat "C:\path\to\Drakengard (USA).iso"
    goto :fail
)
if not exist "%ISO%" (
    echo  Cannot find the disc image: "%ISO%"
    goto :fail
)

call :find_tools || goto :fail

if not exist "external\parallel-gs\CMakeLists.txt" (
    if not exist ".git" (
        echo  This folder is not a git clone, so the GPU renderer source cannot be fetched.
        echo  Download DrakengardPC with:  git clone --recursive https://github.com/brabro1/DrakengardPC.git
        goto :fail
    )
    echo  [1/5] Fetching the GPU renderer source ^(git submodule^)...
    git submodule update --init --recursive || goto :fail
) else (
    echo  [1/5] GPU renderer source present.
)

echo  [2/5] Building the recompiler...
if not exist "build\ps2recomp\build.ninja" (
    cmake -S external/PS2Recomp -B build/ps2recomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1 -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_SCCACHE=OFF -DFETCHCONTENT_BASE_DIR=build/_deps || goto :fail
)
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer || goto :fail

echo  [3/5] Translating the game from your disc ^(checks that it is Drakengard USA v1.00^)...
%PYTHON% -I tools/recomp.py "%ISO%" || goto :fail
if not exist "work" mkdir work
> "work\iso_path.txt" echo %ISO%

echo  [4/5] Configuring the game build...
if not exist "build\game\build.ninja" (
    cmake -S . -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" -DFETCHCONTENT_BASE_DIR=build/_deps || goto :fail
)

echo  [5/5] Compiling the game ^(the first time takes 10-20 minutes^)...
cmake --build build/game --target DrakengardPC || goto :fail

echo.
echo  Done. Double-click play.bat to start the game.
echo.
pause
exit /b 0

:find_tools
rem MSYS2 CLANG64 compilers and DLLs: MSYS2_CLANG64_BIN, or the usual install folders.
set "CLANG64="
if defined MSYS2_CLANG64_BIN if exist "%MSYS2_CLANG64_BIN%\clang++.exe" set "CLANG64=%MSYS2_CLANG64_BIN%"
if not defined CLANG64 if exist "C:\msys64\clang64\bin\clang++.exe" set "CLANG64=C:\msys64\clang64\bin"
if not defined CLANG64 if exist "D:\msys64\clang64\bin\clang++.exe" set "CLANG64=D:\msys64\clang64\bin"
if not defined CLANG64 (
    echo  MSYS2 with the CLANG64 tools was not found.
    echo  Install MSYS2 from https://www.msys2.org and run the package command from the README,
    echo  or set MSYS2_CLANG64_BIN to your msys64\clang64\bin folder.
    exit /b 1
)
for %%T in (cmake.exe ninja.exe) do if not exist "%CLANG64%\%%T" (
    echo  %%T is missing from %CLANG64%. Run the package command from the README in MSYS2.
    exit /b 1
)
set "PATH=%CLANG64%;%PATH%"
set "PYTHON="
where python >nul 2>nul && set "PYTHON=python"
if not defined PYTHON where py >nul 2>nul && set "PYTHON=py -3"
if not defined PYTHON (
    echo  Python 3 was not found. Install it from https://www.python.org ^(tick "Add python.exe to PATH"^).
    exit /b 1
)
where git >nul 2>nul || (
    echo  Git was not found. Install Git for Windows from https://git-scm.com.
    exit /b 1
)
echo  Using tools from %CLANG64%
exit /b 0

:fail
echo.
echo  Setup stopped. See the messages above, and "Troubleshooting" in README.md.
echo.
pause
exit /b 1
