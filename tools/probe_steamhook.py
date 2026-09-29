import argparse
import ctypes
import struct
import subprocess
import sys
import time
from ctypes import wintypes

import os
GAME_DIR = os.environ.get("SEKIRO_DIR") or os.path.dirname(os.path.abspath(__file__))
EXE = GAME_DIR + r"\sekiro.exe"
WATCH = ("DINPUT8.dll", "steam_api64.dll")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
TH32CS_SNAPMODULE = 0x00000008
TH32CS_SNAPMODULE32 = 0x00000010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.ReadProcessMemory.restype = wintypes.BOOL


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


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    szopt = struct.unpack_from("<H", d, pe + 0x14)[0]
    opth = pe + 0x18
    magic = struct.unpack_from("<H", d, opth)[0]
    ddoff = 112 if magic == 0x20B else 96
    secs = []
    for i in range(nsec):
        o = opth + szopt + i * 40
        vsz, va, rsz, ra = struct.unpack_from("<IIII", d, o + 8)
        secs.append((va, vsz, ra, rsz))

    def fo(r):
        for va, vsz, ra, rsz in secs:
            if va <= r < va + max(vsz, rsz):
                return ra + (r - va)
        return None

    exp_rva, _ = struct.unpack_from("<II", d, opth + ddoff)
    imp_rva, _ = struct.unpack_from("<II", d, opth + ddoff + 8)
    return fo, fo(exp_rva), fo(imp_rva), opth, ddoff


def import_order(path):
    d = open(path, "rb").read()
    fo, _exp, imp_off, _opth, _dd = sections(d)
    out = []
    desc = imp_off
    while desc:
        name_rva = struct.unpack_from("<I", d, desc + 12)[0]
        if name_rva == 0:
            break
        off = fo(name_rva)
        z = d.find(b"\0", off)
        out.append(d[off:z].decode(errors="replace"))
        desc += 20
    return out


def modules(pid):
    out = {}
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    e = MODULEENTRY32W()
    e.dwSize = ctypes.sizeof(e)
    if kernel32.Module32FirstW(snap, ctypes.byref(e)):
        while True:
            out[e.szModule] = (ctypes.cast(e.modBaseAddr, ctypes.c_void_p).value, e.modBaseSize, e.szExePath)
            if not kernel32.Module32NextW(snap, ctypes.byref(e)):
                break
    kernel32.CloseHandle(snap)
    return out


def main():
    global GAME_DIR, EXE
    ap = argparse.ArgumentParser(
        description="Determine whether SteamAPI_Init is hooked at runtime.")
    ap.add_argument("--game-dir", default=None,
                    help="folder containing sekiro.exe "
                         "(default: $SEKIRO_DIR, else this script's own folder)")
    args = ap.parse_args()
    if args.game_dir:
        GAME_DIR = args.game_dir
        EXE = os.path.join(GAME_DIR, "sekiro.exe")

    order = import_order(EXE)
    print("sekiro.exe static import order (%d dlls):" % len(order))
    for i, name in enumerate(order):
        mark = ""
        if name.lower() in [w.lower() for w in WATCH]:
            mark = "   <== watched"
        if mark or i < 3:
            print("  %2d. %s%s" % (i, name, mark))
    idx = {}
    for i, name in enumerate(order):
        idx[name.lower()] = i
    print()
    for w in WATCH:
        i = idx.get(w.lower())
        print("  %-16s static import index: %s" % (w, i if i is not None else "NOT IMPORTED"))
    di = idx.get("dinput8.dll")
    si = idx.get("steam_api64.dll")
    if di is not None and si is not None:
        print()
        print("  => dinput8.dll is initialised %s steam_api64.dll" % ("BEFORE" if di < si else "AFTER"))

    print()
    print("launching the game to inspect SteamAPI_Init at runtime...")
    proc = subprocess.Popen([EXE], cwd=GAME_DIR)
    try:
        time.sleep(30)
        mods = modules(proc.pid)
        h = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)

        def read(addr, size):
            buf = ctypes.create_string_buffer(size)
            got = ctypes.c_size_t(0)
            if not kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)):
                return None
            return buf.raw[: got.value]

        target = None
        for name, (base, size, path) in mods.items():
            if name.lower() == "steam_api64.dll":
                target = (name, base, size, path)
        if not target:
            print("  steam_api64.dll not loaded")
            return
        name, base, size, path = target
        print("  %s base=%#x size=%#x" % (name, base, size))
        print("  path: %s" % path)

        d = open(path, "rb").read()
        fo, exp_off, _imp, _opth, _dd = sections(d)
        eo = exp_off
        nbase, nfunc, nnames, afun, anames, aord = struct.unpack_from("<IIIIII", d, eo + 16)
        no = fo(anames)
        ao = fo(afun)
        oo = fo(aord)
        rva = None
        for i in range(nnames):
            nr, = struct.unpack_from("<I", d, no + i * 4)
            off = fo(nr)
            z = d.find(b"\0", off)
            if d[off:z].decode(errors="replace") == "SteamAPI_Init":
                ordi, = struct.unpack_from("<H", d, oo + i * 2)
                rva, = struct.unpack_from("<I", d, ao + ordi * 4)
                print("  export ordinal %d -> RVA %#x" % (ordi, rva))
        if rva is None:
            print("  could not resolve SteamAPI_Init RVA")
            return
        live = base + rva
        disk = d[fo(rva):fo(rva) + 16]
        mem = read(live, 16)
        print("  SteamAPI_Init RVA=%#x  live=%#x" % (rva, live))
        print("  on disk  : %s" % disk.hex(" "))
        print("  in memory: %s" % (mem.hex(" ") if mem else "<unreadable>"))
        if mem and mem != disk:
            print("  >>> PROLOGUE PATCHED: MinHook attached to SteamAPI_Init")
        else:
            print("  >>> PROLOGUE UNCHANGED: MinHook never attached to SteamAPI_Init")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
