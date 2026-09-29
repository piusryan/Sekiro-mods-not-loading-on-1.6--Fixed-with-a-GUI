@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: Mod Engine 1.6 Fixer – GUI exe build script
:: Output: src\fixer_gui\ModEngineFixer.exe
:: Requires: Visual Studio 2022 Build Tools  (C++ workload)
:: ============================================================

:: --- locate vcvars64.bat via vswhere ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (
        `"%VSWHERE%" -products * -latest -format value -property installationPath 2^>nul`
    ) do set "VS_ROOT=%%i"
)

:: fallback: well-known paths
if not defined VS_ROOT (
    for %%e in (BuildTools Community Professional Enterprise) do (
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%e"
        )
        if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
        )
    )
)

if not defined VS_ROOT (
    echo.
    echo  [ERROR] Visual Studio 2022 Build Tools not found.
    echo.
    echo  Install with:
    echo    winget install Microsoft.VisualStudio.2022.BuildTools ^
    echo      --override "--quiet --wait --norestart ^
    echo      --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    echo.
    exit /b 1
)

set "VCVARS=%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
echo [build] Using: %VCVARS%
call "%VCVARS%" >nul 2>&1

:: --- paths ---
set "THIS_DIR=%~dp0"
set "THIS_DIR=%THIS_DIR:~0,-1%"
set "REPO_ROOT=%THIS_DIR%\..\..\"
set "OBJ=%THIS_DIR%\obj"

if not exist "%OBJ%" mkdir "%OBJ%"

:: --- compile resource (icon + background image + version) ---
echo [build] Compiling resources ...
rc.exe /nologo /fo "%OBJ%\fixer.res" "%THIS_DIR%\fixer.rc"
if errorlevel 1 (
    echo  [ERROR] rc.exe failed.
    exit /b 1
)

:: --- compile C++ sources ---
echo [build] Compiling sources ...

cl.exe /nologo ^
    /O2 /MT /EHsc /std:c++17 /GS- ^
    /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS ^
    /W3 /WX- ^
    /I"%THIS_DIR%" ^
    /Fo"%OBJ%\\" ^
    "%THIS_DIR%\core.cpp" ^
    "%THIS_DIR%\gfx.cpp" ^
    "%THIS_DIR%\main.cpp" ^
    /link ^
    /SUBSYSTEM:WINDOWS ^
    /MACHINE:X64 ^
    /MANIFEST:EMBED /MANIFESTINPUT:"%THIS_DIR%\app.manifest" ^
    /OUT:"%THIS_DIR%\ModEngineFixer.exe" ^
    "%OBJ%\fixer.res" ^
    d2d1.lib dwrite.lib windowscodecs.lib ^
    ole32.lib oleaut32.lib ^
    shell32.lib shlwapi.lib advapi32.lib ^
    comctl32.lib dwmapi.lib comdlg32.lib ^
    user32.lib gdi32.lib kernel32.lib

if errorlevel 1 (
    echo.
    echo  [ERROR] Compilation failed.
    exit /b 1
)

echo.
echo  [OK] ModEngineFixer.exe built successfully.
echo       %THIS_DIR%\ModEngineFixer.exe
echo.
endlocal
