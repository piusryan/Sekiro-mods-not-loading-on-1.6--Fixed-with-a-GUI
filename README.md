# Mod Engine for Sekiro 1.6 / non-Steam DRM

Mod Engine (by [katalash](https://github.com/katalash/ModEngine)) lets you load
loose `.dcx` and `.tpf` files from a `mods\` folder instead of the ones baked
into the game's archives. That is how skin, sword, loading-screen and text mods
are installed.

**The stock 0.1.16 release silently does nothing on Sekiro 1.6, or on any
non-Steam copy of the game.** It starts, the game runs, no errors appear — and
your mods are never loaded.

This repository is a patched build of Mod Engine that fixes that, plus a small
GUI and a verification tool so you can prove the mods actually loaded.

---

## The symptom

You install mods the normal way:

```
Sekiro\
├── dinput8.dll          <- Mod Engine
├── modengine.ini
└── mods\
    ├── menu\hi\menu_load_00006.tpf.dcx
    ├── msg\engus\menu.msgbnd.dcx
    └── parts\fc_m_0200.partsbnd.dcx
```

The game launches. You go to load a save. **The modded skin is not there.**
Turn on `showDebugLog=1` and the console shows:

```
[ModEngine] Hooking VirtualAlloc
[ModEngine] Detected base module offset at 0x0000000140000000
[ModEngine] AOB Scanner Initialized
[ModEngine] Patching memory limit table at 0000000143B1BB90
```

...and then **nothing**. No archive hook. No file hook. No mods. It stops there
every single time.

---

## Why it happens

Two separate bugs. You have to fix both.

### Bug 1 — the hook is never installed

Mod Engine only sets up its file hooks as a side effect of the game calling
`SteamAPI_Init`. In `dllmain.cpp` it detours `steam_api64.dll!SteamAPI_Init`
and runs the real setup inside that detour.

On a non-Steam copy, `steam_api64.dll` comes from a DRM emulator
(SmartSteamEmu, CreamAPI, etc.). Those DLLs ship **encrypted on disk and
self-decrypt in memory at load time.** The decryption writes over the whole
image, which destroys the MinHook trampoline patch Mod Engine installed moments
earlier. By the time the game calls `SteamAPI_Init`, the detour is gone and
`HookModLoader()` never runs.

You can confirm this yourself — dump `SteamAPI_Init` while the game is running.
It is a normal, unhooked function prologue, not a jump.

### Bug 2 — the version check rejects your exe

`CheckSekiroVersion()` in `dllmain.cpp` only accepts three exact file sizes:

```cpp
// 1.02      = 65682008
// 1.02 unpacked = 65682312
// 1.03      = 65688152
```

Sekiro 1.6's `sekiro.exe` is **67,799,112 bytes**. So the check fails, Mod
Engine prints "Unsupported version of Sekiro detected", and then blocks
forever on `std::cin.ignore()` waiting for a keypress nobody will ever press.

Full technical analysis with the disassembly evidence: **[docs/ROOT_CAUSE.md](docs/ROOT_CAUSE.md)**

---

## What this fixes

| | Stock 0.1.16 | This build |
|---|---|---|
| Hook trigger | `SteamAPI_Init` detour only | Detour **plus** a worker thread with retry |
| Survives an emulator's self-decryption | No | Yes — the hook is installed after decryption |
| Version gate | 1.02/1.03 file sizes only, then blocks | Removed, with the reason logged |
| Game detection | Stub hardcoded to Dark Souls II | Real detection from the exe name |
| Hardcoded 1.02/1.03 patch addresses | Present, land on random code on 1.6 | Removed |
| Logging | Console only, gone when the game exits | Always writes `modengine_load.log`; no stray console window |
| Console window | Pops up at every launch on 1.6 | Opt-in only, via `showConsole=1` |
| Verifying mods loaded | Impossible by hand | `mod_report.py`, or the GUI's live log pane |

**The AOB scan itself does not need changing.** The 14-byte archive signature
`40 55 56 41 54 41 55 48 83 EC 28 4D 8B E0` still matches on 1.6. See
[docs/ROOT_CAUSE.md](docs/ROOT_CAUSE.md) for the verification.

---

## Requirements

Pick whichever row matches how you got this.

| If you... | You need |
|---|---|
| Downloaded a **Release ZIP** | **Nothing.** Double-click `ModEngineFixer.exe`. Python, Visual Studio and git are all optional. |
| Downloaded a Release ZIP **and** will press *Build DLL* | Visual Studio 2022 (any edition) with the C++ workload. See [Requirements](#requirements). |
| Cloned with `git` | **Visual Studio 2022** with the C++ workload to build the GUI, and/or **Python 3.9+** for the command line. The `.exe` and the prebuilt DLL are not in the repo. |

There are **no pip packages.** Every script here uses only the Python standard
library, so `pip install -r requirements.txt` is a deliberate no-op. The GUI
itself is native C++ with no third-party library at all. Nothing registers a
service, a driver, or a shell extension.

### A note on Python

The GUI does not use Python at all. You only need Python for the command line
interface and the helper scripts, and those need nothing beyond the standard
library.

```bat
cd ui
python -m modengine_fixer.cli status --game-dir "C:\Games\Sekiro"
```

> The `.exe` is not code-signed, so the first launch may show a SmartScreen
> "Windows protected your PC" prompt. Choose *More info* → *Run anyway*.

---

## Quick start

**Easiest:** download a **Release ZIP**, unzip it anywhere, and double-click
`ModEngineFixer.exe`. No Python, no setup, no Visual Studio.

> The `.exe` and the prebuilt `dinput8_patched.dll` are build products and are
> not stored in git, so a `git clone` will not contain them. Use a Release ZIP
> if you want the no-setup path.

**Otherwise** clone the repo and build the app yourself (see
[Building the .exe yourself](#building-the-exe-yourself)):

```bat
git clone https://github.com/YOUR-USERNAME/Sekiro-ModEngine-1.6-Fix
cd Sekiro-ModEngine-1.6-Fix
src\fixer_gui\build.cmd --publish
```

That needs Visual Studio 2022 Build Tools and a Windows SDK, but no Python. A
plain `git clone` also gives you the command line interface, which only needs
Python 3.9+ (see [Requirements](#requirements)).

The GUI does the rest: it finds your game folder, builds the DLL, backs up the
old one, installs it, launches the game, and shows you which mods loaded.

Full button-by-button walkthrough: **[docs/UI_GUIDE.md](docs/UI_GUIDE.md)**.

Prefer a command line? The subcommand always comes **first**, and `--game-dir`
after it:

```bat
cd ui
python -m modengine_fixer.cli status  --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli build
python -m modengine_fixer.cli install --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli report  --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli play    --game-dir "C:\Games\Sekiro"
```

<details>
<summary>No GUI, no Python — build it by hand</summary>

See [Building from source](#building-from-source) below. The only hard
requirement is Visual Studio 2022 Build Tools plus a Windows SDK.
</details>

---

## Using the GUI

`ModEngineFixer.exe` in the repository root is the app. It is a single
self-contained native binary: no Python, no runtime to install, and the icon
and wallpaper are embedded inside it. Just double-click it.

It does the fiddly parts for you: finding the game folder, building the DLL,
backing up whatever was there, and telling you whether your mods actually
loaded.

### Before you start

The `.exe` needs nothing installed.

You do **not** need Visual Studio unless you press *Build*. See
[Requirements](#requirements) below.

### Command line switches

Both are optional:

| Switch | Effect |
| --- | --- |
| `--game "<dir>"` | Start pointed at a specific Sekiro folder instead of auto-detecting. |
| `--shot "<file.bmp>"` | Render one frame to a BMP and exit. Used for automated checks. |

### Starting it

Double-click `ModEngineFixer.exe` in the repository root. Keep it there — the
app locates `src\` and `mods\` relative to its own folder.

### The status cards

Four cards across the top of the panel summarise the current state. Each has a
coloured pip: **jade** means good, **gold** means needs attention, **rust** means
broken, **red** means a real error.

| Card | Good | What it means |
|---|---|---|
| `GAME BUILD` | jade — a known version | Which `sekiro.exe` you have. `too new for 1.03` is expected on 1.6; that is the whole point of this project. |
| `DINPUT8.DLL` | jade — `patched` | The Mod Engine shim is installed. `stock` or `not installed` means mods will not load. |
| `MOD ARCHIVES` | gold — `0 of 0` | How many mod files are in your `mods\` folder. |
| `LOADER` | jade — `hooked` | The last session's `modengine_load.log` shows the AOB hook fired. This is the line that proves it works. |

Under the cards, detail lines spell out anything wrong, plus the mod-archive
summary from the last session:

```
Mod Engine hooked. 10 of 37 archive(s) served, 27 not loaded
```

`27 not loaded` is **normal and not a problem.** The game only loads archives as
it needs them:

- `menu\hi\menu_load_*.tpf.dcx` load as you open those specific screens
- files under `msg\<language>\` load only if you select that language in game
  options — the other 12 folders stay untouched otherwise
- character model archives load the first time you meet that character

So play further and press **Refresh**. The number should grow.

### The game folder

Mod Engine reads your mods relative to this folder, so it matters. The app
guesses it on startup. If the `GAME BUILD` card says the folder is wrong, press
**Browse** and pick the one containing `sekiro.exe` — usually
`C:\Program Files (x86)\Steam\steamapps\common\Sekiro`. Press **Refresh**
afterwards.

### The buttons

| Button | What it does | Press it |
|---|---|---|
| **Build DLL** | Compiles the source. Needs Visual Studio. Output streams into the log panel. | Once, the first time |
| **Install / Repair** | Backs up any existing `dinput8.dll` to `dinput8.dll.modengine-0.1.16.orig.bak`, copies the patched one in, and creates `modengine.ini` and `mods\` if missing. | Once, then again any time you want to repair |
| **Play Sekiro** | Starts `sekiro.exe` with the right working directory. | Every time you want to play |

Buttons grey out when they cannot run — *Build DLL* until the toolchain is
ready, *Install / Repair* and *Play Sekiro* until a game folder is found.
**Clear** empties the log panel.

**Build tools.** A line near the top always tells you whether you can build:

```
Toolchain: Ready - MSVC 14.44.35207, SDK 10.0.26100.0
```

If it says the toolchain is missing, *Build DLL* will not work. Either follow the
[requirements](#requirements) section to install Visual Studio, or download a
prebuilt `dinput8.dll` from the Releases page and use **Install / Repair** after
dropping it in.

Building takes 10 to 60 seconds. The buttons grey out while it runs, and the
compiler output streams into the panel at the bottom, so you can see if anything
goes wrong.

### Typical first run, start to finish

1. **Browse** to your Sekiro folder, press **Refresh**
2. **Build DLL** — wait for the log to say it succeeded
3. **Install / Repair** — press **Play Sekiro** when it confirms
4. Play for a few minutes, close the game
5. Press **Refresh** — confirm the `LOADER` card turns jade and the
   `served` count starts climbing
6. Done. The game folder now has everything; you can launch Sekiro normally
   from Steam or a desktop shortcut from now on

> **One caveat:** if you launch the game from a desktop shortcut whose "Start in"
> folder is not the Sekiro directory, your mods will not load. The *Play Sekiro*
> button exists precisely because it sets that for you. If you prefer a
> shortcut, set its "Start in" to the game folder.

---

## Verifying it worked

The GUI's report tab is the easiest way. From a command line, the same answer:

After playing for a few minutes, run:

```bat
python in_gameadd\mod_report.py
```

```
==============================================================
 Mod Engine .dcx override report
==============================================================
game folder : C:\Games\Sekiro
hook        : [ModEngine] AOB scan hooking archive function at 00000001401C76D0
available   : 37 archive file(s) in C:\Games\Sekiro\mods
served      : 10 distinct file(s) served last session

Not requested by the game yet (27):
    menu\hi\menu_load_00000.tpf.dcx
    ...
```

`available` vs `served` is the whole point. If `served` is 0, the hook did not
install. If it is climbing as you play, your mods are loading.

> **Not every archive will ever show as served, and that is correct.**
> The game only requests the files it actually needs. Files under
> `msg\<language>\` load only if you select that language in game options —
> the other 11 languages are dead weight unless you switch. The remaining
> `menu\hi\menu_load_*.tpf.dcx` files load as you open those screens.

---

## The log

Every run writes **`modengine_load.log`** to the game folder, always, whether or
not `showDebugLog` is on. It is truncated at startup, so it always describes
the most recent session.

```ini
[ModEngine] AOB scan hooking archive function at 00000001401C76D0
[ModEngine] Mod override hooks installed successfully.
[INVENTORY] Scanning override directory: C:\Games\Sekiro\mods
[INVENTORY]   ARCH C:\Games\Sekiro\mods\parts\fc_m_0200.partsbnd.dcx (156166889 bytes)
[INVENTORY] 39 file(s) available for override.
[OVERRIDE OK ] C:\Games\Sekiro\mods\parts\fc_m_0200.partsbnd.dcx
             -> served as "data1:/parts/fc_m_0200.partsbnd.dcx" (156166889 bytes)
[SUMMARY   ] distinct override files served so far: 10
```

The four things worth grepping for:

| Line | Means |
|---|---|
| `AOB scan hooking archive function at ...` | The hook installed. Without this, nothing works. |
| `FATAL` | Something went wrong; the message says what. |
| `[OVERRIDE OK ]` | A mod file was served to the game. |
| `[CRASH]` | An unhandled exception was caught. Please report it. |

### No console window

This build does **not** open a black console window when the game starts.
Sekiro is a GUI application, so the stock behaviour of calling `AllocConsole()`
just puts a stray window in front of the game and leaves it there for the whole
session. Here the log file is written unconditionally and nothing is echoed
unless you ask for it:

| Key in `[debug]` | Default | Effect |
|---|---|---|
| `showDebugLog` | `1` | How much detail goes in the log file. |
| `showConsole` | `0` | Set to `1` to get the old console window back. |

The GUI summarises this file after each run. To watch the console's contents
while the game is running, set `showConsole=1` and leave it on.

---

## Building from source

You only need to build this if you want to change the patch, if you are
contributing, or if there is no prebuilt binary for your situation.

### Prebuilt binary

If the maintainers have attached a `dinput8.dll` to the
[Releases page](../../releases), you do not need to build anything:

1. Download it
2. Copy it into your Sekiro folder, overwriting `dinput8.dll`
3. Back up the original first — rename it to
   `dinput8.dll.modengine-0.1.16.orig.bak`
4. Play. Press **Refresh** in the GUI to confirm

The GUI's **Install / Repair** button does steps 2 and 3 for you if you place
the downloaded file at `src\build\dinput8_patched.dll`.

### Requirements

- Visual Studio 2022 with the **Desktop development with C++** workload and a
  Windows 10/11 SDK. **Any edition works** — Community, Build Tools, Enterprise
  or Professional, in any install location. `build.cmd` locates the toolchain
  with `vswhere.exe` and falls back to the usual 2022 paths, and it reuses the
  environment the GUI already set up when one is present.
  - Fastest way to install just the free Build Tools:

    ```bat
    winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    ```

- `git`

### Build

```bat
git clone --recursive https://github.com/katalash/ModEngine.git
cd ModEngine

REM apply this repo's changes -- see docs/PATCHES.md
patch -p1 < ..\Sekiro-ModEngine-1.6-Fix\patches\0001-allow-sekiro-1.6.patch
copy ..\Sekiro-ModEngine-1.6-Fix\src\ModEngineLog.cpp DS3ModEngine\
copy ..\Sekiro-ModEngine-1.6-Fix\src\ModEngineLog.h  DS3ModEngine\
copy ..\Sekiro-ModEngine-1.6-Fix\src\stdafx.h          DS3ModEngine\
copy ..\Sekiro-ModEngine-1.6-Fix\src\LeoSpecial        DS3ModEngine\ /S
copy ..\Sekiro-ModEngine-1.6-Fix\src\build.cmd         DS3ModEngine\

cd DS3ModEngine
build.cmd
```

Output: `build\dinput8_patched.dll` (~212 KB, x64, exports `DirectInput8Create`).

Then rename it to `dinput8.dll` in your game folder. **Back up the old one
first** — the GUI does this for you.

<details>
<summary>Why not build the .sln?</summary>

It works, but the shipped project file targets `v142` and SDK `10.0.17763.0`.
On a current VS install you have to override both. `build.cmd` calls `cl.exe`
directly with only the files the fix actually needs, which sidesteps the
toolset mismatch, skips the ImGui/D3D11 menu we do not use, and produces a much
smaller binary.

To use the solution anyway, build with:

```bat
msbuild DS3ModEngine.sln /p:Configuration=Release /p:Platform=x64 ^
  /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0
```
</details>

---

## Repository layout

```
.
├── README.md                  <- you are here
├── NOTICE                     <- upstream authorship and licensing
├── LICENSE                    <- MIT, for this repo's original work only
├── ModEngineFixer.exe         <- double-click this (no Python needed)
├── background.jpg             <- wallpaper, embedded into the .exe
├── sekrio_ico.ico             <- app icon, embedded into the .exe
├── docs/
│   ├── UI_GUIDE.md            <- full button-by-button guide
│   ├── ROOT_CAUSE.md          <- why the stock DLL does nothing, with evidence
│   ├── PATCHES.md             <- every change vs upstream, and why
│   └── TROUBLESHOOTING.md     <- symptom -> cause -> fix
├── patches/
│   └── 0001-allow-sekiro-1.6.patch
├── src/                       <- all C++ lives here
│   ├── build.cmd              <- rebuilds dinput8_patched.dll (the hook)
│   └── fixer_gui/             <- the native Fixer GUI
│       ├── build.cmd          <- rebuilds ModEngineFixer.exe
│       ├── main.cpp           <- window, layout, input, threading
│       ├── core.h / core.cpp  <- the logic, ported from core.py
│       ├── d2d.h  / d2d.cpp   <- Direct2D / DirectWrite / WIC rendering
│       ├── theme.h            <- the Sekiro palette
│       ├── fixer.rc           <- embeds the icon, wallpaper and manifest
│       ├── resource.h         <- resource IDs (IDI_APP_ICON, IDB_BACKGROUND)
│       ├── make_icon.py       <- rebuilds sekrio_ico.ico from sekiro.jpg
│       └── app.manifest       <- declares PerMonitorV2 DPI awareness
├── in_gameadd/                <- helpers you run from the game folder
│   ├── mod_report.py          <- served vs available .dcx diff
│   ├── launch_sekiro.py       <- launches the game with the right CWD
│   └── README.md              <- what each one is for
├── tools/                     <- diagnostics, run from this folder
│   ├── check_mods.py          <- runtime diagnostic (modules, console capture)
│   └── probe_steamhook.py     <- proves whether SteamAPI_Init is hooked
└── ui/
    └── modengine_fixer/       <- the same logic in Python, for the CLI
        ├── core.py            <- all the logic, no GUI
        ├── cli.py             <- scriptable interface to it
        └── __init__.py
```

### About the helper scripts

None of them are required for mods to load. They are conveniences and
verification, and you can delete any of them.

- **`in_gameadd\`** — the two you might actually use day to day. They find the
  game folder automatically when run from inside it. Copy them into your
  Sekiro folder if you like.
- **`tools\`** — deeper diagnostics, for when something is not working and you
  want evidence. Run from the repo.

All four accept `--game-dir` if you keep them outside the game folder:

```bat
python in_gameadd\mod_report.py --game-dir "C:\Games\Sekiro"
python in_gameadd\launch_sekiro.py --check --game-dir "C:\Games\Sekiro"
python tools\check_mods.py       --game-dir "C:\Games\Sekiro"
python tools\probe_steamhook.py  --game-dir "C:\Games\Sekiro"
```

`--game-dir` and the `SEKIRO_DIR` environment variable both work, and
`SEKIRO_DIR` takes priority if you set both.

### Building the .exe yourself

`ModEngineFixer.exe` is a native C++ binary built with Visual Studio 2022 Build
Tools (C++ workload) and a Windows 10/11 SDK. There is no third-party library
and no Python involved.

```bat
cd src\fixer_gui
build.cmd
```

That leaves the binary at `src\fixer_gui\ModEngineFixer.exe`. It works from
there, but the usual place to keep it is the repo root, where it finds `src\`
and `docs\` relative to itself. To build and put it there in one step:

```bat
cd src\fixer_gui
build.cmd --publish
```

`--publish` is deliberately opt-in. A plain `build.cmd` never touches the
`ModEngineFixer.exe` already sitting in the repo root, so you cannot clobber a
working copy with an unverified build. Close any running copy first — Windows
locks the file while it is open and the publish step will fail loudly.

The window sizes itself from the monitor it opens on, in DIPs, and is capped to
the work area, so it never opens partially off-screen or under the taskbar. The
exe's manifest declares PerMonitorV2 DPI awareness, so the UI is correct at
125%, 150% and 175% scaling rather than being drawn at 1:1.

---

## Troubleshooting

Quick table, longer version in [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

| Symptom | Cause | Fix |
|---|---|---|
| No `AOB scan hooking` line in the log | Hook did not install | See docs; usually a stale `dinput8.dll` |
| Hook installs, `served` stays 0 | Wrong working directory | Launch the game from the folder containing `sekiro.exe` |
| `served` is 0 but mods look wrong | Mods in the wrong place | Must mirror the archive path under `mods\` |
| Other-language `msg\` files never load | The game never requests them | Expected. Switch language in game options to test. |
| `modengine_load.log` is missing | Wrong folder, or DLL not loaded | Use the GUI's status panel |
| Game crashes on start | A patched 1.02 address is active | You are running the stock DLL, not this build |

---

## Credits

- **katalash** — [Mod Engine](https://github.com/katalash/ModEngine), the entire
  `src/` tree. Please see [NOTICE](NOTICE) before redistributing.
- **djohls** — StackWalker
- **Tsuda Kageyu** — MinHook
- **Omar Cornut** — Dear ImGui
- **FromSoftware** — Sekiro: Shadows Die Twice

Sekiro is a trademark of FromSoftware. This project is not affiliated with or
endorsed by them, or by katalash. It only touches the game's file-loading path;
it contains no game code or assets.
