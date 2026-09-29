@echo off
REM Builds ui\fixer.py into a single clickable ModEngineFixer.exe
REM in the repository root. Run it from a normal user terminal, not as admin.
REM
REM   build_exe.cmd
REM
REM The exe lands in the repo root on purpose: when frozen, the app locates
REM its src\, docs\ and in_gameadd\ folders relative to its own location,
REM so it must sit next to them.

setlocal
cd /d "%~dp0"

echo === installing PyInstaller ===
python -m pip install --upgrade pyinstaller || exit /b 1

echo === cleaning previous build ===
if exist build_exe rmdir /s /q build_exe
if exist ModEngineFixer.exe del /q ModEngineFixer.exe

echo === building ===
python -m PyInstaller ^
   --noconfirm ^
   --clean ^
   --onefile ^
   --windowed ^
   --name ModEngineFixer ^
   --distpath "%CD%" ^
   --workpath "%CD%\build_exe\work" ^
   --specpath "%CD%\build_exe" ^
   --exclude-module numpy ^
   --exclude-module matplotlib ^
   --exclude-module pytest ^
   --exclude-module PyQt5 ^
   --exclude-module PySide2 ^
   "%CD%\ui\fixer.py"

if errorlevel 1 (
   echo BUILD FAILED
   exit /b 1
)

echo === done ===
if exist ModEngineFixer.exe (
   for %%A in (ModEngineFixer.exe) do echo built: %%~fA  %%~zA bytes
   echo.
   echo Put this next to src\ and docs\. Double-click to run.
) else (
   echo output not found
   exit /b 1
)
endlocal
