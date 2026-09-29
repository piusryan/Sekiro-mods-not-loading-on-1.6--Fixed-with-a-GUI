import argparse
import os
import re
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

GAME_DIR = os.environ.get("SEKIRO_DIR") or os.path.dirname(os.path.abspath(__file__))
EXE = "sekiro.exe"
INI = "modengine.ini"
DLL = "dinput8.dll"

WANTED = [
    ("misc", "skipLogos", "1"),
    ("files", "loadUXMFiles", "0"),
    ("files", "useModOverrideDirectory", "1"),
    ("files", "modOverrideDirectory", "\\mods"),
    ("files", "cacheFilePaths", "0"),
    ("debug", "showDebugLog", "1"),
]

KNOWN_ARCHIVE_DIRS = {
    "menu", "msg", "parts", "sfx", "event", "map", "param", "fxr",
    "sound", "movie", "chr", "model", "text", "script", "shader", "data",
}


def parse_ini(text):
    cfg = {}
    section = None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            continue
        if "=" in line and section:
            key, value = line.split("=", 1)
            cfg[(section, key.strip().lower())] = value.strip().strip('"')
    return cfg


def set_key(text, section, key, value):
    pattern = re.compile(r"^\s*%s\s*=" % re.escape(key), re.IGNORECASE)
    lines = text.splitlines()
    cur = None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            cur = stripped[1:-1].strip().lower()
        elif pattern.match(line) and cur == section:
            lines[i] = "%s=%s" % (key, value)
            return "\r\n".join(lines) + "\r\n", True
    return text, False


def append_section(text, section, pairs):
    block = ["", "[%s]" % section]
    block += ["%s=%s" % (k, v) for k, v in pairs]
    body = text.rstrip("\r\n")
    return body + "\r\n" + "\r\n".join(block) + "\r\n"


def audit_mods(mod_dir):
    files = []
    for root, _dirs, names in os.walk(mod_dir):
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, mod_dir).replace("\\", "/")
            files.append((rel, os.path.getsize(full)))
    return files


def main():
    ap = argparse.ArgumentParser(description="Launch Sekiro with Mod Engine's ini guaranteed to be found.")
    ap.add_argument("--check", action="store_true", help="run diagnostics only, do not launch")
    ap.add_argument("--no-fix", action="store_true", help="do not rewrite modengine.ini")
    ap.add_argument("--game-dir", default=None,
                    help="folder containing sekiro.exe "
                         "(default: $SEKIRO_DIR, else this script's own folder)")
    args = ap.parse_args()

    target = args.game_dir or GAME_DIR
    os.chdir(target)
    print("game dir : %s" % target)
    print("cwd      : %s" % os.getcwd())

    ok = True
    for name in (EXE, INI, DLL):
        if os.path.isfile(name):
            print("ok       : %s (%d bytes)" % (name, os.path.getsize(name)))
        else:
            print("MISSING  : %s" % name)
            ok = False

    with open(DLL, "rb") as fh:
        blob = fh.read()
    rel_ini = b"".join(bytes([c, 0]) for c in b".\\modengine.ini")
    if rel_ini in blob:
        print("ok       : dinput8.dll is Mod Engine; it opens the ini as the RELATIVE path '.\\modengine.ini'")
        print("           -> the working directory below is the only thing that decides where it looks")
    else:
        print("WARN     : dinput8.dll does not contain the expected ini path string")

    with open(INI, "r", encoding="utf-8", errors="replace", newline="") as fh:
        raw = fh.read()
    if raw and not raw[0].isascii() or raw.startswith("\ufeff"):
        print("WARN     : modengine.ini has a BOM; Mod Engine reads it with GetPrivateProfile* and may fail")
    crlf = raw.count("\r\n")
    bare_lf = raw.count("\n") - crlf
    if bare_lf:
        print("WARN     : %d line(s) use bare LF; rewrite the file with CRLF" % bare_lf)

    fixed = raw
    for section in ("misc", "files", "debug"):
        pairs = [(k, v) for s, k, v in WANTED if s == section]
        missing = []
        for key, value in pairs:
            fixed, placed = set_key(fixed, section, key, value)
            if not placed:
                missing.append((key, value))
        if missing:
            fixed = append_section(fixed, section, missing)
    cfg = parse_ini(fixed)

    print("-" * 62)
    print("modengine.ini")
    for section, key, value in WANTED:
        got = cfg.get((section, key.lower()), "<missing>")
        mark = "ok  " if got == value else "FIX "
        print("  %s [%-5s] %-24s = %s" % (mark, section, key, got))
        if got != value:
            ok = False

    if fixed != raw and not args.no_fix:
        with open(INI, "w", encoding="utf-8", newline="") as fh:
            fh.write(fixed)
        print("-" * 62)
        print("modengine.ini rewritten")

    mod_dir = os.path.join(GAME_DIR, cfg.get(("files", "modoverridedirectory"), "\\mods").lstrip("\\/"))
    if os.path.isdir(mod_dir):
        files = audit_mods(mod_dir)
        print("-" * 62)
        print("mods     : %s (%d override files)" % (mod_dir, len(files)))
        by_dir = {}
        for rel, size in files:
            top = rel.split("/")[0]
            by_dir.setdefault(top, []).append((rel, size))
        for top in sorted(by_dir):
            entry = by_dir[top]
            flag = "" if top.lower() in KNOWN_ARCHIVE_DIRS else "   <- not a normal archive dir, check it"
            print("  %-10s %3d file(s)%s" % (top + "/", len(entry), flag))
        print("-" * 62)
        print("example override paths the engine will look for:")
        for rel, _size in files[:6]:
            print("  %s" % os.path.join(mod_dir, rel.replace("/", "\\")))
    else:
        print("-" * 62)
        print("MISSING  : mods directory %s" % mod_dir)
        ok = False

    print("-" * 62)
    print("skipLogos is 1, so the logos must NOT appear.")
    print("If they still appear, modengine.ini was not read and the working directory is wrong.")
    print("-" * 62)

    if args.check:
        return 0 if ok else 1
    if not ok:
        print("refusing to launch, fix the problems above first")
        return 1

    print("launching %s with cwd pinned to the game directory" % EXE)
    proc = subprocess.Popen([os.path.join(GAME_DIR, EXE)], cwd=GAME_DIR)
    try:
        return proc.wait()
    except KeyboardInterrupt:
        proc.terminate()
        return 0


if __name__ == "__main__":
    sys.exit(main())
