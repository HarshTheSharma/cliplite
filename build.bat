@echo off
setlocal

:: ── ClipLite build script ─────────────────────────────────────────────────────
:: Prerequisites:
::   - Visual Studio 2022 (MSVC or clang-cl) with C++ Desktop workload
::   - CMake 3.20+  in PATH
::   - Ninja         in PATH  (bundled with VS, or install from ninja-build.org)
::   - WTL 10 extracted to  third_party\wtl\   (see third_party\README.md)

if not exist "third_party\wtl\Include\atlapp.h" (
    echo.
    echo ERROR: WTL headers not found at third_party\wtl\Include\atlapp.h
    echo        Download WTL 10 from https://sourceforge.net/projects/wtl/
    echo        and extract so that atlapp.h is at third_party\wtl\Include\atlapp.h
    echo.
    exit /b 1
)

:: Initialise the VS environment (adjust year / edition as needed)
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (
        `%VSWHERE% -latest -property installationPath`
    ) do set VSDIR=%%i
)

if defined VSDIR (
    call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    echo WARNING: Could not locate Visual Studio via vswhere. Make sure vcvars64 is set.
)

:: Configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 ( echo CMake configure failed & exit /b 1 )

:: Build
cmake --build build --config Release
if errorlevel 1 ( echo Build failed & exit /b 1 )

echo.
echo Build complete: build\cliplite.exe
endlocal
