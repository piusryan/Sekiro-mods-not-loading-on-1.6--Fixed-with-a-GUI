"""Core logic for the Mod Engine 1.6 Fixer GUI.

Kept separate from the tkinter widgets so it can be unit-tested and reused by
the command line front end.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ARCHIVE_EXTENSIONS = (".dcx", ".tpf")

# Sekiro 1.02 / 1.03, the only builds stock Mod Engine accepts.
LEGACY_SIZES = {
    65682008: "1.02",
    65682312: "1.02 (unpacked)",
    65688152: "1.03",
}

BACKUP_SUFFIX = ".modengine-0.1.16.orig.bak"
BUILT_DLL_NAME = "dinput8_patched.dll"
INSTALLED_DLL_NAME = "dinput8.dll"
LOAD_LOG = "modengine_load.log"


# --------------------------------------------------------------------------
# results
# --------------------------------------------------------------------------
@dataclass
class BuildResult:
    ok: bool
    log: str
    dll_path: Path | None = None


@dataclass
class InstallResult:
    ok: bool
    message: str
    backup: Path | None = None


@dataclass
class GameStatus:
    game_dir: Path | None = None
    ok: bool = False
    problem: str = ""
    exe: Path | None = None
    exe_size: int = 0
    version: str = ""
    version_supported: bool | None = None
    dll: Path | None = None
    dll_size: int = 0
    dll_is_patched: bool = False
    dll_is_stale: bool = False
    backup: Path | None = None
    ini_ok: bool = False
    notes: list[str] = field(default_factory=list)


@dataclass
class Report:
    hook_line: str = ""
    problem: str = ""
    available: list[str] = field(default_factory=list)
    served: list[str] = field(default_factory=list)

    @property
    def missing(self) -> list[str]:
        return sorted(set(self.available) - set(self.served))

    @property
    def ok(self) -> bool:
        return bool(self.hook_line) and not self.problem


# --------------------------------------------------------------------------
# toolchain discovery
# --------------------------------------------------------------------------
def find_vswhere() -> Path | None:
    for candidate in (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio/Installer/vswhere.exe",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio/Installer/vswhere.exe",
    ):
        if candidate.is_file():
            return candidate
    return None


def find_msvc() -> Path | None:
    """Return the directory containing vcvars64.bat, or None."""
    vswhere = find_vswhere()
    roots: list[Path] = []
    if vswhere:
        try:
            out = subprocess.run(
                [str(vswhere), "-products", "*", "-latest", "-format", "value",
                 "-property", "installationPath"],
                capture_output=True, text=True, timeout=30,
            )
            if out.returncode == 0:
                for line in out.stdout.splitlines():
                    line = line.strip()
                    if line:
                        roots.append(Path(line))
        except (OSError, subprocess.SubprocessError):
            pass

    roots += [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio/2022/BuildTools",
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio/2022/Community",
        Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "Microsoft Visual Studio/2022/Community",
    ]

    for root in roots:
        vc = root / "VC/Auxiliary/Build/vcvars64.bat"
        if vc.is_file():
            return vc
    return None


def find_windows_sdk() -> Path | None:
    root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) \
        / "Windows Kits/10/Include"
    if not root.is_dir():
        return None
    versions = sorted(
        (p for p in root.iterdir() if p.is_dir() and p.name[0].isdigit()),
        key=lambda p: [int(x) for x in re.findall(r"\d+", p.name) or [0]],
    )
    return versions[-1] if versions else None


def toolchain_status() -> tuple[bool, str]:
    msvc = find_msvc()
    sdk = find_windows_sdk()
    if msvc and sdk:
        tools = next(iter(sorted(
            (msvc.parents[2] / "Tools/MSVC").glob("*"),
            key=lambda p: p.name, reverse=True)), None) if (msvc.parents[2] / "Tools/MSVC").is_dir() else None
        label = f"MSVC {tools.name}" if tools else "MSVC"
        return True, f"Ready - {label}, SDK {sdk.name}"
    missing = []
    if not msvc:
        missing.append("Visual Studio 2022 Build Tools (C++ workload)")
    if not sdk:
        missing.append("Windows 10/11 SDK")
    return False, "Missing: " + ", ".join(missing)


# --------------------------------------------------------------------------
# game inspection
# --------------------------------------------------------------------------
def _read_ini_bool(path: Path, section: str, key: str, default: bool) -> bool:
    # Deliberately not configparser: modengine.ini is Windows-encoding and may
    # use ; comments and loose keys, and we never write it back from the GUI.
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return default
    current = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].strip().lower()
            continue
        if "=" not in line:
            continue
        name, _, value = line.partition("=")
        if current == section.lower() and name.strip().lower() == key.lower():
            return value.strip().strip('"').lower() in {"1", "true", "yes", "on"}
    return default


def get_mods_dir(game_dir: Path) -> Path:
    override = "\\mods"
    ini = game_dir / "modengine.ini"
    if ini.is_file():
        try:
            text = ini.read_text(encoding="utf-8", errors="replace")
            match = re.search(
                r"^\s*modOverrideDirectory\s*=\s*(.+?)\s*$",
                text, re.MULTILINE | re.IGNORECASE)
            if match:
                override = match.group(1).strip().strip('"')
        except OSError:
            pass
    return game_dir / override.lstrip("\\/").replace("\\", os.sep)


def inspect_game(game_dir: Path) -> GameStatus:
    st = GameStatus(game_dir=game_dir)

    if not game_dir or not game_dir.is_dir():
        st.problem = "Folder does not exist."
        return st

    exe = game_dir / "sekiro.exe"
    if not exe.is_file():
        st.problem = "No sekiro.exe here. Pick the folder that contains it."
        return st

    st.exe = exe
    try:
        st.exe_size = exe.stat().st_size
    except OSError:
        st.problem = "Could not read sekiro.exe."
        return st

    st.version = LEGACY_SIZES.get(st.exe_size, "unknown / newer than 1.03")
    st.version_supported = st.exe_size in LEGACY_SIZES

    dll = game_dir / INSTALLED_DLL_NAME
    if not dll.is_file():
        st.problem = f"No {INSTALLED_DLL_NAME} in this folder yet."
    else:
        st.dll = dll
        st.dll_size = dll.stat().st_size
        st.dll_is_patched = st.dll_size < 400_000
        st.dll_is_stale = st.dll_size > 500_000
        if st.dll_is_stale:
            st.notes.append(
                f"{INSTALLED_DLL_NAME} is {st.dll_size:,} bytes, which looks like "
                "stock Mod Engine 0.1.16. Stock silently loads nothing on 1.6."
            )

    backup = game_dir / (INSTALLED_DLL_NAME + BACKUP_SUFFIX)
    if backup.is_file():
        st.backup = backup

    ini = game_dir / "modengine.ini"
    st.ini_ok = ini.is_file()
    if not st.ini_ok:
        st.notes.append("No modengine.ini. One will be created on install.")
    else:
        if not _read_ini_bool(ini, "files", "useModOverrideDirectory", True):
            st.notes.append("modengine.ini: useModOverrideDirectory is 0 — mods will not load.")
        if not ini.is_file() or not get_mods_dir(game_dir).is_dir():
            st.notes.append(f"Override folder missing: {get_mods_dir(game_dir)}")

    mods = get_mods_dir(game_dir)
    if mods.is_dir():
        archives = [p for p in mods.rglob("*")
                    if p.is_file() and p.suffix.lower() in ARCHIVE_EXTENSIONS]
        st.notes.append(f"{len(archives)} archive file(s) in {mods.name}\\")

    st.ok = dll.is_file()
    return st


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------
def scan_available(game_dir: Path) -> list[str]:
    mods = get_mods_dir(game_dir)
    if not mods.is_dir():
        return []
    out = []
    for path in mods.rglob("*"):
        if path.is_file() and path.suffix.lower() in ARCHIVE_EXTENSIONS:
            out.append(path.relative_to(mods).as_posix())
    return sorted(out)


def build_report(game_dir: Path) -> Report:
    report = Report(available=scan_available(game_dir))
    log = game_dir / LOAD_LOG
    if not log.is_file():
        report.problem = (
            f"No {LOAD_LOG}. Play the game once, or use Install if the DLL is missing."
        )
        return report

    mods = get_mods_dir(game_dir)
    text = log.read_text(encoding="utf-8", errors="replace")
    served: set[str] = set()

    for line in text.splitlines():
        if "[OVERRIDE OK ]" in line:
            path = line.split("[OVERRIDE OK ]", 1)[1].strip()
            p = Path(path)
            try:
                rel = p.relative_to(mods)
            except ValueError:
                rel = p
            served.add(rel.as_posix())
        elif "AOB scan hooking archive function" in line and not report.hook_line:
            report.hook_line = line.strip()
        elif "FATAL" in line and not report.problem:
            report.problem = line.strip()

    report.served = sorted(served)
    if not report.hook_line and not report.problem:
        report.problem = (
            "Log has no hook line. The DLL loaded but never installed its hooks — "
            "see docs/TROUBLESHOOTING.md."
        )
    return report


# --------------------------------------------------------------------------
# build / install
# --------------------------------------------------------------------------
def repo_root() -> Path:
    """Locate the repository that ships alongside this code.

    When frozen into an exe, __file__ points into PyInstaller's temp
    extraction folder, so the exe has to be the anchor instead. We keep the
    exe in the repo root so both cases resolve to the same place.
    """
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[2]


REPO_ROOT = repo_root()
SRC_DIR = REPO_ROOT / "src"
BUILD_CMD = SRC_DIR / "build.cmd"

DEFAULT_INI = """; Mod Engine configuration
; Generated by the Mod Engine 1.6 Fixer GUI.
; See docs/ for what each key does.

[misc]
skipLogos=1
chainDInput8DLLPath=""

[files]
loadUXMFiles=0
useModOverrideDirectory=1
modOverrideDirectory=\\mods
cacheFilePaths=0

[debug]
showDebugLog=1
"""


def build_dll(callback=None) -> BuildResult:
    """Compile the patched DLL. Returns the combined compiler output."""
    if not BUILD_CMD.is_file():
        return BuildResult(False, f"build.cmd not found at {BUILD_CMD}")

    vcvars = find_msvc()
    if not vcvars:
        return BuildResult(
            False,
            "Visual Studio 2022 Build Tools not found.\n\n"
            "Install it with:\n"
            '  winget install Microsoft.VisualStudio.2022.BuildTools '
            '--override "--quiet --wait --norestart '
            '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"',
        )

    cmd = f'call "{vcvars}" >nul && call build.cmd'
    proc = subprocess.Popen(
        cmd, cwd=str(SRC_DIR), shell=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="replace",
    )
    lines: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        lines.append(line.rstrip())
        if callback:
            callback(line.rstrip())
    proc.wait()

    built = SRC_DIR / "build" / BUILT_DLL_NAME
    ok = proc.returncode == 0 and built.is_file()
    return BuildResult(ok, "\n".join(lines), built if built.is_file() else None)


def install_dll(game_dir: Path, dll_path: Path | None = None) -> InstallResult:
    if dll_path is None:
        dll_path = SRC_DIR / "build" / BUILT_DLL_NAME
    if not dll_path.is_file():
        return InstallResult(False, "DLL not built yet. Press Build first.")

    game_dir.mkdir(parents=True, exist_ok=True)
    target = game_dir / INSTALLED_DLL_NAME
    backup = game_dir / (INSTALLED_DLL_NAME + BACKUP_SUFFIX)

    import shutil
    messages = []
    if target.is_file() and not backup.is_file():
        shutil.copy2(target, backup)
        messages.append(f"Backed up original to {backup.name}")
    elif target.is_file():
        messages.append(f"Backup already exists ({backup.name}), left alone")

    try:
        shutil.copy2(dll_path, target)
    except OSError as exc:
        return InstallResult(False, f"Could not write {target}: {exc}")

    messages.append(f"Installed {INSTALLED_DLL_NAME} ({target.stat().st_size:,} bytes)")

    ini = game_dir / "modengine.ini"
    if not ini.is_file():
        ini.write_text(DEFAULT_INI, encoding="utf-8")
        messages.append("Created modengine.ini")

    mods = get_mods_dir(game_dir)
    if not mods.is_dir():
        mods.mkdir(parents=True, exist_ok=True)
        messages.append(f"Created {mods.name}\\ folder")

    return InstallResult(True, "\n".join(messages), backup if backup.is_file() else None)


def launch_game(game_dir: Path) -> None:
    """Launch sekiro.exe with the working directory set to the game folder.

    Mod Engine resolves the override folder against the process working
    directory, so this is not optional.
    """
    subprocess.Popen([str(game_dir / "sekiro.exe")], cwd=str(game_dir))


if __name__ == "__main__":
    print(f"msvc   : {find_msvc()}")
    print(f"sdk    : {find_windows_sdk()}")
    print(f"status : {toolchain_status()}")
