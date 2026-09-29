# Troubleshooting

Start here. Almost every report of "Mod Engine does nothing" is one of the
first three.

---

## 0. The one command that answers most questions

```bat
python in_gameadd\mod_report.py
```

If `served` is above 0, the system is working and the problem is your mods, not
Mod Engine. If it is 0, work down this page.

---

## 1. The log stops after six lines and never mentions the hook

```
[ModEngine] Hooking VirtualAlloc
[ModEngine] Detected base module offset at 0x0000000140000000
[ModEngine] AOB Scanner Initialized
[ModEngine] Patching memory limit table at 0000000143B1BB90
```

**Cause.** You are running the stock 0.1.16 DLL. It only installs its file hooks
from a detour on `steam_api64.dll!SteamAPI_Init`, and on a non-Steam copy that
DLL decrypts itself at load time and wipes the patch. The detour never fires.

**Fix.** Use the DLL from this repository. See [ROOT_CAUSE.md](ROOT_CAUSE.md).

**Check.** You should see this line instead, shortly after startup:

```
[ModEngine] AOB scan hooking archive function at 00000001401C76D0
```

---

## 2. Game hangs at startup with a console window

**Cause.** The version gate. `CheckSekiroVersion()` matches only 1.02/1.03 file
sizes; on 1.6 it prints a warning and blocks on `std::cin.ignore()`, waiting
for a keypress that never comes.

**Fix.** This build removes the gate. If you see this, you are running the
stock DLL — same as case 1. Pressing a key will let the stock DLL continue,
but it will not load your mods.

---

## 3. The hook installs, but `served` stays 0

```
[ModEngine] AOB scan hooking archive function at 00000001401C76D0
[ModEngine] Mod override hooks installed successfully.
[INVENTORY] 39 file(s) available for override.
...
served : 0
```

Everything is installed and the mods folder is being seen. The problem is path
resolution.

### 3a. Wrong working directory

Mod Engine resolves `mods\` relative to the process's current directory, not to
`sekiro.exe`. If you launch from a shortcut whose "Start in" is somewhere else,
every lookup misses by a directory.

**Check** the log for the working directory:

```
[ModEngine] Working directory:        C:\Games\Sekiro
```

That must be the folder containing `sekiro.exe`.

**Fix.** Launch from that folder, or use the GUI's **Play** button, or
`python in_gameadd\launch_sekiro.py`.

### 3b. `useModOverrideDirectory` is 0

```ini
[files]
useModOverrideDirectory=1
modOverrideDirectory=\mods
```

### 3c. The override folder name is wrong

The default is `\doa` in some builds. Check the log:

```
[ModEngine] Override directory setting: \mods
[INVENTORY] Scanning override directory: C:\Games\Sekiro\mods
```

The second line must point at a folder that exists.

---

## 4. Hook works, `served` is non-zero, but my mod still does not show

Now it is a mod problem, not an engine problem. The log tells you which archive
was requested and which file answered it:

```
[OVERRIDE OK ] C:\Games\Sekiro\mods\parts\fc_m_0200.partsbnd.dcx
             -> served as "data1:/parts/fc_m_0200.partsbnd.dcx" (156166889 bytes)
```

### 4a. The mod is for a file the game never requests

`partsbnd.dcx` files load when that character model first appears. A mod for
Wolf's model will not load until you meet Wolf. Play further, then re-run
`mod_report.py`.

### 4b. The archive name does not match

The folder layout under `mods\` must mirror the archive path in the log, minus
the `data1:` prefix:

| Game requests | Override must be at |
|---|---|
| `data1:/parts/fc_m_0200.partsbnd.dcx` | `mods\parts\fc_m_0200.partsbnd.dcx` |
| `data1:/msg/engus/menu.msgbnd.dcx` | `mods\msg\engus\menu.msgbnd.dcx` |
| `data1:/menu/hi/menu_load_00006.tpf.dcx` | `mods\menu\hi\menu_load_00006.tpf.dcx` |

The extension must be `.dcx` for archives, `.tpf` for menu files. A file named
`.tpf` inside a folder is treated as a loose file and ignored.

### 4c. The mod itself is broken

A `.dcx` served successfully can still be malformed — wrong game version, wrong
archive index, or a file that is actually an `.uxm` loose file. Sekiro will
either ignore it or fail to load the archive. If the log shows `[OVERRIDE OK ]`
and the game misbehaves, suspect the mod and test with one file at a time.

---

## 5. Other-language `msg\` files never load

**Not a bug.** Sekiro only loads the message archive for the active language.
The other 12 folders under `mods\msg\` are never opened unless you switch
language in game options. To test one, change the language and relaunch, then
check the log for it.

`mod_report.py` lists these under "not requested yet" by design.

---

## 6. `modengine_load.log` does not exist

- Confirm `dinput8.dll` in the game folder is the patched build (~212 KB). The
  stock 0.1.16 is ~615 KB.
- Confirm the log is being written to the folder containing `sekiro.exe` — the
  path is derived from the exe's own location, not the working directory.
- Confirm nothing is blocking the file (antivirus, or a previous run's handle).
  The log is truncated (`CREATE_ALWAYS`) each launch, so a stale handle from a
  still-running game is a real possibility. Close the game first.

---

## 7. Game crashes on startup

**Cause.** Almost always a stock DLL, or a stock DLL chained via
`chainDInput8DLLPath`. The stock build applies hardcoded 1.02/1.03 addresses
(`0x14084ea60`, `0x140e60320`) that are unrelated code on 1.6.

**Check** the log for `[CRASH]` — this build logs the exception code and address:

```
[CRASH] exception code 0xC0000005 at 0000000140XXXXXX
```

Please report these with the log.

**Fix.** Remove `chainDInput8DLLPath` chaining, or make sure the chained DLL is
also 1.6-safe.

---

## 8. Two `dinput8.dll` entries in the module list

```
DINPUT8.dll   C:\Games\Sekiro\DINPUT8.dll      <- us
dinput8.dll   C:\WINDOWS\system32\dinput8.dll <- the real one we chain to
```

**This is correct.** Windows is case-insensitive, so these are two different
files in two different directories, not a duplicate load. The game-folder one is
Mod Engine, which forwards `DirectInput8Create` to the system one.

---

## 9. Nothing happens and there is no log, no console, nothing

Check the DLL is actually being loaded at all:

```bat
python tools\check_mods.py
```

It attaches to the running game and lists loaded modules. If the game-folder
`DINPUT8.dll` is absent from the list, it was never loaded — usually because it
is the wrong architecture (x86 instead of x64), or a second injector already
owns `DirectInput8Create`.

To confirm the export exists:

```bat
dumpbin /exports dinput8.dll
```

You want:

```
1    0 00004EC0 DirectInput8Create
```

---

## 10. Still stuck

Collect these and open an issue:

1. `modengine_load.log` from a full run
2. Output of `python in_gameadd\mod_report.py`
3. Output of `python tools\check_mods.py`
4. `sekiro.exe` file size (`dir sekiro.exe`)
5. Your `modengine.ini`
6. What you expected to happen, and what happened instead
