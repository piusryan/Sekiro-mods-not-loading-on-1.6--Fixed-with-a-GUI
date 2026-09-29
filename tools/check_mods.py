import argparse
import ctypes
import os
import struct
import subprocess
import sys
import time
from ctypes import wintypes

GAME_DIR = os.environ.get("SEKIRO_DIR") or os.path.dirname(os.path.abspath(__file__))
EXE = "sekiro.exe"
LOG = os.path.join(GAME_DIR, "modengine_console.log")
RUNTIME = 60
POLL = 2.0

STD_OUTPUT_HANDLE = -11
CREATE_NEW_CONSOLE = 0x10

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class COORD(ctypes.Structure):
    _fields_ = [("X", wintypes.SHORT), ("Y", wintypes.SHORT)]


class SMALL_RECT(ctypes.Structure):
    _fields_ = [
        ("Left", wintypes.SHORT),
        ("Top", wintypes.SHORT),
        ("Right", wintypes.SHORT),
        ("Bottom", wintypes.SHORT),
    ]


class CONSOLE_SCREEN_BUFFER_INFO(ctypes.Structure):
    _fields_ = [
        ("dwSize", COORD),
        ("dwCursorPosition", COORD),
        ("wAttributes", wintypes.WORD),
        ("srWindow", SMALL_RECT),
        ("dwMaximumWindowSize", COORD),
    ]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("th32ModuleID", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("GlblcntUsage", wintypes.DWORD),
        ("ProccntUsage", wintypes.DWORD),
        ("modBaseAddr", ctypes.POINTER(ctypes.c_ubyte)),
        ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE),
        ("szModule", wintypes.WCHAR * 256),
        ("szExePath", wintypes.WCHAR * 260),
    ]


kernel32.AttachConsole.argtypes = [wintypes.DWORD]
kernel32.AttachConsole.restype = wintypes.BOOL
kernel32.FreeConsole.restype = wintypes.BOOL
kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
kernel32.GetStdHandle.restype = wintypes.HANDLE
kernel32.GetConsoleScreenBufferInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(CONSOLE_SCREEN_BUFFER_INFO)]
kernel32.GetConsoleScreenBufferInfo.restype = wintypes.BOOL
kernel32.ReadConsoleOutputCharacterW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, COORD, ctypes.POINTER(wintypes.DWORD)]
kernel32.ReadConsoleOutputCharacterW.restype = wintypes.BOOL
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.WaitForSingleObject.restype = wintypes.DWORD
kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.Module32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
kernel32.Module32FirstW.restype = wintypes.BOOL
kernel32.Module32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W)]
kernel32.Module32NextW.restype = wintypes.BOOL
kernel32.WriteConsoleW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
kernel32.WriteConsoleW.restype = wintypes.BOOL
kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
kernel32.CreateFileW.restype = wintypes.HANDLE

user32 = ctypes.WinDLL("user32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def list_windows(pid):
    found = []

    def cb(hwnd, _lparam):
        owner = wintypes.DWORD(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        found.append(
            {
                "hwnd": hwnd,
                "title": buf.value,
                "visible": bool(user32.IsWindowVisible(hwnd)),
                "hung": bool(user32.IsHungAppWindow(hwnd)),
                "size": (rect.right - rect.left, rect.bottom - rect.top),
            }
        )
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x00000001
FILE_SHARE_WRITE = 0x00000002
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value
CON_OUT = None


def open_console_out():
    global CON_OUT
    CON_OUT = kernel32.CreateFileW(
        "CONOUT$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        None,
        OPEN_EXISTING,
        0,
        None,
    )
    return CON_OUT is not None and CON_OUT != INVALID_HANDLE_VALUE


def list_modules(pid):
    TH32CS_SNAPMODULE = 0x00000008
    TH32CS_SNAPMODULE32 = 0x00000010
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if snap == ctypes.c_void_p(-1).value:
        return []
    out = []
    entry = MODULEENTRY32W()
    entry.dwSize = ctypes.sizeof(MODULEENTRY32W)
    ok = kernel32.Module32FirstW(snap, ctypes.byref(entry))
    while ok:
        out.append(
            {
                "name": entry.szModule,
                "base": ctypes.cast(entry.modBaseAddr, ctypes.c_void_p).value,
                "size": entry.modBaseSize,
                "path": entry.szExePath,
            }
        )
        ok = kernel32.Module32NextW(snap, ctypes.byref(entry))
    kernel32.CloseHandle(snap)
    return out


PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL


def remote(proc_handle):
    def read(addr, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t(0)
        if not kernel32.ReadProcessMemory(proc_handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)):
            return None
        return buf.raw[: got.value]

    def cstr(addr, limit=512):
        out = b""
        while len(out) < limit:
            chunk = read(addr + len(out), 2)
            if not chunk or len(chunk) < 2:
                break
            if chunk[:2] == b"\x00\x00":
                break
            out += chunk[:2]
        return out.decode("utf-16-le", errors="replace")

    def imports(base):
        hdr = read(base, 0x400)
        if not hdr or len(hdr) < 0x40:
            return {}
        e_lfanew = struct.unpack_from("<I", hdr, 0x3C)[0]
        pe = read(base + e_lfanew, 0x200)
        if not pe or len(pe) < 0x80:
            return {}
        magic = struct.unpack_from("<H", pe, 0x18)[0]
        dd = 0x18 + (0x70 if magic == 0x20B else 0x60)
        imp_rva = struct.unpack_from("<I", pe, dd + 8)[0]
        if not imp_rva:
            return {}
        result = {}
        desc = base + imp_rva
        for _ in range(256):
            raw = read(desc, 20)
            if not raw or len(raw) < 20:
                break
            oft, _ts, _fc, name_rva, ft = struct.unpack_from("<IIIII", raw, 0)
            if name_rva == 0:
                break
            entries = {}
            lookup = oft or ft
            idx = 0
            while idx < 4096:
                thunk = read(base + lookup + idx * 8, 8)
                if not thunk or len(thunk) < 8:
                    break
                val = struct.unpack_from("<Q", thunk, 0)[0]
                if val == 0:
                    break
                if not (val >> 63):
                    name = cstr(base + (val & 0x7FFFFFFF) + 2)
                    entries[name or "ordinal_%d" % (val & 0xFFFF)] = base + ft + idx * 8
                idx += 1
            result[cstr(base + name_rva)] = entries
            desc += 20
        return result

    return read, imports


def probe_iat(pid, modules):
    h = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        return "OpenProcess failed (winerror=%d)" % ctypes.get_last_error()
    try:
        read, imports = remote(h)
        main = next((m for m in modules if m["name"].lower() == "sekiro.exe"), None)
        if not main:
            return "sekiro.exe is not in the module list"
        imp = imports(main["base"])
        if not imp:
            return "could not parse the import directory (base=%#x)" % main["base"]
        me = next((m for m in modules if m["name"].lower() == "dinput8.dll"), None)
        total = sum(len(v) for v in imp.values())
        head = "parsed %d IAT slots across %d imported dlls" % (total, len(imp))
        if not me:
            return head + "\n  dinput8.dll is not loaded, so no slot can point into it"
        hits = []
        for dll, entries in imp.items():
            for fname, slot in entries.items():
                raw = read(slot, 8)
                if not raw or len(raw) < 8:
                    continue
                val = struct.unpack_from("<Q", raw, 0)[0]
                if me["base"] <= val < me["base"] + me["size"]:
                    hits.append((dll, fname, val))
        if hits:
            body = "\n".join("  HOOKED  %s!%s -> %#x" % (d, f, v) for d, f, v in hits)
            return "%s\n  %d slot(s) redirected into dinput8.dll:\n%s" % (head, len(hits), body)
        return "%s\n  0 slots redirected into dinput8.dll (base=%#x size=%#x)" % (
            head,
            me["base"],
            me["size"],
        )
    finally:
        kernel32.CloseHandle(h)


def read_console():
    handle = CON_OUT
    if not handle:
        return None
    info = CONSOLE_SCREEN_BUFFER_INFO()
    if not kernel32.GetConsoleScreenBufferInfo(handle, ctypes.byref(info)):
        return None
    cols = max(1, info.dwSize.X)
    rows = max(1, info.dwSize.Y)
    lines = []
    total = 0
    for y in range(rows):
        buf = ctypes.create_unicode_buffer(cols + 1)
        read = wintypes.DWORD(0)
        if not kernel32.ReadConsoleOutputCharacterW(handle, buf, cols, COORD(0, y), ctypes.byref(read)):
            continue
        total += read.value
        lines.append(buf.value[: read.value].rstrip())
    while lines and not lines[-1]:
        lines.pop()
    return lines, cols, rows, total


def main():
    global GAME_DIR, LOG
    ap = argparse.ArgumentParser(
        description="Launch Sekiro and report which Mod Engine files were served.")
    ap.add_argument("--game-dir", default=None,
                    help="folder containing sekiro.exe "
                         "(default: $SEKIRO_DIR, else this script's own folder)")
    args = ap.parse_args()
    if args.game_dir:
        GAME_DIR = args.game_dir
        LOG = os.path.join(GAME_DIR, "modengine_console.log")

    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

    os.chdir(GAME_DIR)
    if not os.path.isfile(EXE):
        print("missing %s" % EXE)
        return 1

    print("launching %s with a dedicated console so Mod Engine's output is readable" % EXE)
    proc = subprocess.Popen(
        [os.path.join(GAME_DIR, EXE)],
        cwd=GAME_DIR,
        creationflags=CREATE_NEW_CONSOLE,
    )

    attached = False
    last_error = 0
    kernel32.FreeConsole()
    deadline = time.time() + 20
    while time.time() < deadline:
        if kernel32.AttachConsole(proc.pid):
            attached = True
            break
        last_error = ctypes.get_last_error()
        if proc.poll() is not None:
            break
        time.sleep(0.5)

    if not attached:
        print("could not attach to the game console (pid %d), winerror=%d" % (proc.pid, last_error))
        if proc.poll() is None:
            proc.kill()
        return 2

    print("attached to the game console, capturing for %ds" % RUNTIME)
    if not open_console_out():
        print("could not open CONOUT$ for the attached console")
        proc.kill()
        return 3

    written = wintypes.DWORD(0)
    kernel32.WriteConsoleW(CON_OUT, ">>> opencode capture probe <<<", 30, ctypes.byref(written), None)
    time.sleep(0.5)

    modules = []
    snapshots = []
    win_log = []
    seen_windows = {}
    end = time.time() + RUNTIME
    while time.time() < end:
        if not modules:
            modules = list_modules(proc.pid)
        for win in list_windows(proc.pid):
            state = (win["title"], win["visible"], win["hung"], win["size"])
            key = "%s|vis=%s|hung=%s|%sx%s" % (win["title"] or "<untitled>", win["visible"], win["hung"], win["size"][0], win["size"][1])
            if key not in seen_windows:
                seen_windows[key] = RUNTIME - int(end - time.time())
                win_log.append("t=%3ds  %s" % (seen_windows[key], key))
        snap = read_console()
        if snap and snap[0] and (not snapshots or len(snap[0]) > len(snapshots[-1][0])):
            snapshots.append(snap)
        if proc.poll() is not None:
            break
        time.sleep(POLL)

    if proc.poll() is None:
        iat_report = probe_iat(proc.pid, modules)
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        snap = read_console()
        if snap and snap[0]:
            snapshots.append(snap)
    else:
        iat_report = "game exited before the import table could be probed"

    final = snapshots[-1][0] if snapshots else []
    buffer_info = ""
    if snapshots:
        _lines, cols, rows, total = snapshots[-1]
        buffer_info = " (buffer %dx%d, %d chars read)" % (cols, rows, total)
    with open(LOG, "w", encoding="utf-8", errors="replace") as fh:
        fh.write("\n".join(final))

    print("-" * 62)
    print("captured %d console line(s)%s -> %s" % (len(final), buffer_info, LOG))
    print("-" * 62)
    for line in final:
        print(line)
    print("-" * 62)
    probe_ok = any("opencode capture probe" in line for line in final)
    print("-" * 62)
    print("capture path self-test : %s" % ("OK" if probe_ok else "FAILED"))
    names = [m["name"] for m in modules]
    print("dinput8.dll loaded      : %s" % ("yes" if "dinput8.dll" in [n.lower() for n in names] else "NO"))
    print("modules seen (%d)       : %s" % (len(modules), ", ".join(names[:12])))
    print("-" * 62)
    for m in modules:
        if m["name"].lower() in ("dinput8.dll", "sekiro.exe", "steam_api64.dll"):
            print("  %-16s %#x  %s" % (m["name"], m["base"], m["path"]))
    print("-" * 62)
    print("live import table (game IAT entries Mod Engine would detour):")
    print(iat_report)
    print("-" * 62)
    print("game window timeline:")
    for entry in win_log or ["  (no window was ever created)"]:
        print("  " + entry)
    print("-" * 62)
    archive = [l for l in final if "Archive" in l or "FileHook" in l or "override" in l]
    print("file/archive hook lines : %d" % len(archive))
    for line in archive:
        print("  " + line)
    print("-" * 62)
    if probe_ok and "dinput8.dll" not in [m["name"].lower() for m in modules]:
        print("-> dinput8.dll was never loaded; Mod Engine is not in the process at all.")
    elif not probe_ok:
        print("-> the console reader itself is broken, ignore the result above.")
    elif not win_log:
        print("-> the game never opened a window in %ds, so Mod Engine never got far." % RUNTIME)
    elif not archive:
        print("-> the game opened a window but Mod Engine never installed the")
        print("   file/archive hooks, so it cannot see anything in mods\\.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
