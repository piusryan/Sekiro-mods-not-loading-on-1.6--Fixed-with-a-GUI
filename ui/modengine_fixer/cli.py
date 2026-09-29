"""Command line front end — same operations as the GUI, scriptable.

    python -m modengine_fixer.cli status --game-dir "C:\\Games\\Sekiro"
    python -m modengine_fixer.cli build
    python -m modengine_fixer.cli install --game-dir "C:\\Games\\Sekiro"
    python -m modengine_fixer.cli report --game-dir "C:\\Games\\Sekiro"
    python -m modengine_fixer.cli play   --game-dir "C:\\Games\\Sekiro"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from modengine_fixer import core
else:
    from . import core

GAME_DIR_HELP = "folder containing sekiro.exe"


def cmd_status(args) -> int:
    st = core.inspect_game(Path(args.game_dir))
    print(f"game folder : {st.game_dir}")
    if st.exe:
        print(f"sekiro.exe  : {st.exe_size:,} bytes  ({st.version})")
        verdict = "stock Mod Engine accepts this" if st.version_supported \
            else "stock Mod Engine REJECTS this (1.6+) - the thing we fix"
        print(f"             {verdict}")
    if st.dll:
        tag = "patched build" if st.dll_is_patched else "stock 0.1.16?"
        print(f"dinput8.dll : {st.dll_size:,} bytes  ({tag})")
    else:
        print("dinput8.dll : not installed")
    if st.backup:
        print(f"backup      : {st.backup.name}")
    print(f"mods folder : {core.get_mods_dir(Path(args.game_dir))}")

    ok, detail = core.toolchain_status()
    print(f"build tools : {detail}")
    if st.notes:
        print()
        for note in st.notes:
            print(f"  - {note}")
    if st.problem:
        print(f"\n[!] {st.problem}")
        return 1
    return 0


def cmd_build(args) -> int:
    def echo(line: str) -> None:
        print(line, flush=True)

    print("building... (first run takes 10-60 seconds)")
    result = core.build_dll(callback=echo)
    if result.ok and result.dll_path:
        print(f"\nOK -> {result.dll_path}")
        return 0
    print("\nBUILD FAILED")
    return 1


def cmd_install(args) -> int:
    game_dir = Path(args.game_dir)
    if not (game_dir / "sekiro.exe").is_file():
        print(f"error: no sekiro.exe in {game_dir}", file=sys.stderr)
        return 1
    result = core.install_dll(game_dir)
    print(result.message)
    return 0 if result.ok else 1


def cmd_report(args) -> int:
    game_dir = Path(args.game_dir)
    rep = core.build_report(game_dir)
    if rep.hook_line:
        print(rep.hook_line)
    if rep.problem:
        print(f"[!] {rep.problem}")
    print(f"available : {len(rep.available)}")
    print(f"served    : {len(rep.served)}")
    if rep.served:
        print("\nserved:")
        for s in rep.served:
            print("  +", s)
    if rep.missing:
        print("\nnot requested yet (expected - the game only loads what it needs):")
        for s in rep.missing:
            print("  .", s)
    return 0 if rep.ok else 1


def cmd_play(args) -> int:
    game_dir = Path(args.game_dir)
    if not (game_dir / "sekiro.exe").is_file():
        print(f"error: no sekiro.exe in {game_dir}", file=sys.stderr)
        return 1
    core.launch_game(game_dir)
    print("launched sekiro.exe")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="modengine_fixer.cli",
        description="Mod Engine 1.6 Fixer - command line interface",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name, fn, help_text, needs_game=True):
        p = sub.add_parser(name, help=help_text)
        if needs_game:
            p.add_argument("--game-dir", required=True, help=GAME_DIR_HELP)
        p.set_defaults(func=fn)
        return p

    add("status", cmd_status, "show what is installed and whether it looks right")
    add("build", cmd_build, "compile the patched DLL", needs_game=False)
    add("install", cmd_install, "back up any old DLL and install the patched one")
    add("report", cmd_report, "which mods were served on the last run")
    add("play", cmd_play, "launch the game with the correct working directory")
    return parser


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
