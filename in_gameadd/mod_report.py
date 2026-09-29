"""
Reports which .dcx override files Mod Engine has actually served.

    python tools/mod_report.py
    python tools/mod_report.py --game-dir "C:\\Games\\Sekiro"

With no arguments it looks for the game in this order:
  1. --game-dir
  2. the SEKIRO_DIR environment variable
  3. the folder this script lives in (if it contains sekiro.exe)
  4. the usual Steam library locations

Exit code is 0 if the hook installed cleanly, 1 otherwise, so this can be used
as a quick health check in a script or CI.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

ARCHIVE_EXT = (".dcx", ".tpf")
LOG_NAME = "modengine_load.log"


def find_game_dir(explicit: str | None) -> Path | None:
    if explicit:
        return Path(explicit)

    env = os.environ.get("SEKIRO_DIR")
    if env:
        return Path(env)

    here = Path(__file__).resolve().parent
    if (here / "sekiro.exe").is_file():
        return here

    candidates = [
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Steam/steamapps/common/Sekiro",
        here.parent,
    ]
    for pf in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
        if pf:
            candidates.append(Path(pf) / "SteamLibrary/steamapps/common/Sekiro")

    for candidate in candidates:
        if (candidate / "sekiro.exe").is_file():
            return candidate.resolve()
    return None


def read_mods_dir(game_dir: Path) -> Path:
    """Honour modOverrideDirectory from modengine.ini, like Mod Engine does."""
    override = "\\mods"
    ini = game_dir / "modengine.ini"
    if ini.is_file():
        try:
            import re
            match = re.search(
                r"^\s*modOverrideDirectory\s*=\s*(.+?)\s*$",
                ini.read_text(encoding="utf-8", errors="replace"),
                re.MULTILINE | re.IGNORECASE,
            )
            if match:
                override = match.group(1).strip().strip('"')
        except OSError:
            pass
    return game_dir / override.lstrip("\\/").replace("\\", os.sep)


def scan_mods(mods: Path) -> set[str]:
    found = set()
    if mods.is_dir():
        for path in mods.rglob("*"):
            if path.is_file() and path.suffix.lower() in ARCHIVE_EXT:
                found.add(path.relative_to(mods).as_posix())
    return found


def read_log(log: Path, mods: Path):
    served: set[str] = set()
    hook_line = ""
    problem = ""
    if not log.is_file():
        return served, hook_line, f"No {LOG_NAME} found in {log.parent}."

    with open(log, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "[OVERRIDE OK ]" in line:
                raw = line.split("[OVERRIDE OK ]", 1)[1].strip()
                p = Path(raw)
                try:
                    served.add(p.relative_to(mods).as_posix())
                except ValueError:
                    served.add(p.as_posix())
            elif "AOB scan hooking archive function" in line and not hook_line:
                hook_line = line.strip()
            elif "FATAL" in line and not problem:
                problem = line.strip()

    if not hook_line and not problem:
        problem = ("Log has no hook line. The DLL loaded but never installed its "
                   "hooks - see docs/TROUBLESHOOTING.md.")
    return served, hook_line, problem


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-dir", help="folder containing sekiro.exe")
    args = parser.parse_args(argv)

    game_dir = find_game_dir(args.game_dir)
    if game_dir is None or not (game_dir / "sekiro.exe").is_file():
        print("Could not find sekiro.exe.")
        print("Pass it explicitly:  python tools/mod_report.py --game-dir \"C:\\Games\\Sekiro\"")
        return 1

    mods = read_mods_dir(game_dir)
    available = scan_mods(mods)
    served, hook_line, problem = read_log(game_dir / LOG_NAME, mods)

    print("=" * 62)
    print(" Mod Engine .dcx override report")
    print("=" * 62)
    print(f"game folder : {game_dir}")
    if hook_line:
        print(f"hook        : {hook_line}")
    if problem:
        print(f"problem     : {problem}")
    print(f"available   : {len(available)} archive file(s) in {mods}")
    print(f"served      : {len(served & available)} distinct file(s) served last session")
    print()

    missing = sorted(available - served)
    if not missing:
        print("All available archives were served.")
    else:
        print(f"Not requested by the game yet ({len(missing)}):")
        for m in missing:
            print("   ", m)
        print()
        print("These load on demand - the game only asks for the files it needs.")
        print("Non-English msg\\<language>\\ folders load only if that language is")
        print("selected in game options. Play further and re-run this to see")
        print("coverage grow.")

    return 0 if (hook_line and not problem) else 1


if __name__ == "__main__":
    sys.exit(main())
