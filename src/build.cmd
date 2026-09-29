@echo off
setlocal

REM The Fixer GUI/CLI runs the matching vcvars64.bat before calling this
REM script, so cl.exe is already on PATH and there is nothing to set up.
REM When run by hand, cl.exe is absent and we locate vcvars64.bat ourselves.
set "CL_ON_PATH="
for %%C in (cl.exe) do if not defined CL_ON_PATH if exist "%%~$PATH:C" set "CL_ON_PATH=1"
if defined CL_ON_PATH goto :have_cl

call :find_vcvars
if not defined VCVARS64 goto :no_msvc
call "%VCVARS64%" >nul || goto :no_msvc

:have_cl
cd /d "%~dp0"

if not exist build mkdir build

echo === building ===
cl /nologo /LD /O2 /MT /EHsc /std:c++17 /GS- /DUNICODE /D_UNICODE ^
   /I"." /I"MinHook\include" /I"dinput8" ^
   /Fo"build\\" /Fe"build\dinput8_patched.dll" ^
   dllmain.cpp ^
   ModLoader.cpp ^
   AOBScanner.cpp ^
   Game.cpp ^
   ModEngineLog.cpp ^
   "dinput8\dinputWrapper.cpp" ^
   "MinHook\src\buffer.c" ^
   "MinHook\src\hook.c" ^
   "MinHook\src\trampoline.c" ^
   "MinHook\src\HDE\hde64.c" ^
   /link /DEF:"dinput8\dinput8.def" /MACHINE:X64 psapi.lib shlwapi.lib advapi32.lib

set "BUILD_RC=%errorlevel%"
echo === exit code %BUILD_RC% ===
if exist "build\dinput8_patched.dll" (
   for %%A in ("build\dinput8_patched.dll") do echo OUTPUT: %%~fA  %%~zA bytes
   dumpbin /exports "build\dinput8_patched.dll" | findstr /i "DirectInput8Create"
) else (
   echo BUILD FAILED
)
goto :done

:no_msvc
echo.
echo ERROR: no Visual Studio 2022 C++ toolchain was found.
echo.
echo The Fixer normally finds it by itself. If you reached this by running
echo build.cmd by hand, install the build tools with:
echo.
echo   winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
exit /b 1

REM ---------------------------------------------------------------------------
REM :find_vcvars sets VCVARS64 to a vcvars64.bat, or leaves it unset.
REM Prefers vswhere, so any VS edition and install location is found, then
REM falls back to the standard 2022 paths.
REM ---------------------------------------------------------------------------
:find_vcvars
if defined VCVARS64 goto :eof
set "PFX86=%ProgramFiles(x86)%"
set "PFX64=%ProgramFiles%"
set "VSWHERE=%PFX86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%PFX64%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" call :probe_vswhere
if defined VCVARS64 goto :eof
for %%E in (Community BuildTools Enterprise Professional) do call :probe_root "%PFX86%\Microsoft Visual Studio\2022\%%E"
if defined VCVARS64 goto :eof
for %%E in (Community BuildTools Enterprise Professional) do call :probe_root "%PFX64%\Microsoft Visual Studio\2022\%%E"
goto :eof

:probe_vswhere
if defined VCVARS64 goto :eof
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -products * -latest -format value -property installationPath 2^>nul`) do call :probe_root "%%I"
goto :eof

:probe_root
if defined VCVARS64 goto :eof
if exist "%~1\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS64=%~1\VC\Auxiliary\Build\vcvars64.bat"
goto :eof

:done
endlocal & exit /b %BUILD_RC%
