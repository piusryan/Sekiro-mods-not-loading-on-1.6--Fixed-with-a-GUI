@echo off
setlocal

set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

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

echo === exit code %errorlevel% ===
if exist "build\dinput8_patched.dll" (
   for %%A in ("build\dinput8_patched.dll") do echo OUTPUT: %%~fA  %%~zA bytes
   dumpbin /exports "build\dinput8_patched.dll" | findstr /i "DirectInput8Create"
) else (
   echo BUILD FAILED
)
endlocal
