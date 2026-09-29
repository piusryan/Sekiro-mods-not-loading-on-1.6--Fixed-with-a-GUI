# Mod Engine Fixer — user guide

A complete reference for the **Mod Engine 1.6 Fixer** window: what every
button does, what every line of the status panel means, and how to tell
whether your mods are actually loading.

This is the long version. The README has a shorter walkthrough.

---

## 1. What this tool does

Mod Engine lets loose `.dcx` and `.tpf` files sitting in a `mods\` folder
override the game's own files, which is how skin, sword, text and
loading-screen mods work.

The stock 0.1.16 release **silently does nothing** on Sekiro 1.6 and on
non-Steam copies of the game. No error, no crash — the mods just quietly do
not appear. Two separate bugs cause that, both fixed here:

1. Mod Engine only installs its file hooks as a side effect of the game
   calling `SteamAPI_Init`. On a non-Steam install that DLL decrypts itself
   at load time and overwrites the hook, so it never gets installed.
2. A version check rejects anything that is not Sekiro 1.02/1.03, and your
   1.6 executable fails it.

This tool builds a corrected version of the DLL, installs it safely, and then
shows you hard evidence that your mods loaded.

---

## 2. Starting the app

Pick whichever applies:

| Situation | How to start |
|---|---|
| You have the `.exe` | Double-click **`ModEngineFixer.exe`** |
| You have the repo and VS Build Tools | `src\fixer_gui\build.cmd --publish`, then double-click it |
| You want the command line | See [section 7](#7-the-command-line-version) |

A window titled **Mod Engine 1.6 Fixer** opens, showing four status cards, the
action buttons, and a log panel down the bottom.

> The `.exe` must stay next to the `src\` and `docs\` folders.
> It finds them relative to its own location, so keep the whole folder
> together. Moving only the `.exe` elsewhere breaks it.

The window picks its own size from the monitor it opens on and caps itself to
that monitor's work area, so it never opens half off-screen or tucked under the
taskbar. It is also DPI-aware: at 125%, 150% or 175% display scaling the text and
buttons are drawn at the size Windows reports, not squashed to 1:1. Resize it
freely — the layout reflows, and the minimum size is small enough to fit a
laptop screen.

### Command line switches

| Switch | Effect |
|---|---|
| `--game "<dir>"` | Start pointed at a specific Sekiro folder instead of auto-detecting. |
| `--shot "<file.bmp>"` | Render one frame to a BMP and exit. Used for automated checks. |

### The toolchain line

A line near the top always tells you whether you can build:

```
Toolchain: Ready - MSVC 14.44.35207, SDK 10.0.26100.0
```

| Message | Meaning |
|---|---|
| `Ready - MSVC ..., SDK ...` | Build will work. |
| `Missing: ...` | Build will **not** work. Either install the tools ([section 6](#6-build-tools)) or grab a prebuilt DLL. |

---

## 3. The main panel

Everything lives on one screen. Top to bottom: header, game folder row, four
status cards, detail lines, the action buttons, then the log.

### 3.1 The game folder row

```
GAME FOLDER
C:\Games\Sekiro                                    [Browse] [Refresh]
```

Mod Engine resolves your mods relative to this folder, so it has to be right.
It is the folder containing **`sekiro.exe`**.

On startup the app guesses, checking the usual Steam path, the current working
directory, and its own folder. If your game is somewhere unusual it will guess
wrong, so check it.

| Button | What it does |
|---|---|
| **Browse** | Opens a folder picker. Select the folder containing `sekiro.exe`. Re-runs the status check immediately. |
| **Refresh** | Re-reads the folder and rebuilds everything below. Use after copying mods in or changing things outside the app. |

Long paths are elided in the middle, so the drive and the folder name both stay
visible.

### 3.2 The status cards

Four cards, each with a coloured pip.

| Pip colour | Means |
|---|---|
| **jade** (green) | Good. |
| **gold** | Needs attention, not broken. |
| **rust** (orange) | Broken. |
| **red** | A real error. The detail line below says what. |

| Card | Good value | Meaning |
|---|---|---|
| `GAME BUILD` | a known version | Which `sekiro.exe` you have. `too new for 1.03` is **expected on 1.6** — that is the whole point of this project, not an error. |
| `DINPUT8.DLL` | `patched` | The Mod Engine shim is installed. `stock` or `not installed` means mods will not load. |
| `MOD ARCHIVES` | a count like `0 of 0` | How many mod files are in your `mods\` folder. Stays gold until you add some. |
| `LOADER` | `hooked` | The last session's `modengine_load.log` shows the AOB hook fired. **This is the proof the fix works.** |

The `unknown` in the game version is normal — stock Mod Engine never reads the
1.6 version block. That is the whole point of `PATCHES.md`.

### 3.3 The detail lines

Directly under the cards. They spell out anything wrong, plus what the game
actually loaded last session:

```
Mod Engine hooked. 10 of 37 archive(s) served, 27 not loaded
```

`27 not loaded` is **normal and not a problem.** The game only requests the
archives it needs:

- `menu\<lang>\menu_load_*.tpf.dcx` load as you open those specific screens
- everything under `msg\<language>\` loads **only** if you select that language
  in the game's own options — the other 12 language folders stay untouched
  forever, and that is correct
- character model archives load the first time you meet that character

So play further and press **Refresh**. The served count should grow. Expect
roughly 10 files on a normal short session out of the 37 present.

If the hook did not install, the line turns rust and quotes the problem from
`modengine_load.log` instead.

### 3.4 The action buttons

| Button | Does |
|---|---|
| **Build DLL** | Compiles `src\build.cmd`, producing `src\build\dinput8_patched.dll`. Compiler output streams into the log panel. |
| **Install / Repair** | Backs up any existing `dinput8.dll`, copies the patched one in, and creates `modengine.ini` and `mods\` if missing. |
| **Play Sekiro** | Starts `sekiro.exe` with the correct working directory. |
| **Clear** | Empties the log panel. |

They grey out when they cannot run: **Build DLL** until the toolchain is ready,
**Install / Repair** and **Play Sekiro** until a game folder is found. While a
build runs, `game is running` style notes appear next to the action bar.

#### Build DLL

- **Needs:** Visual Studio 2022 Build Tools. Check the toolchain line first.
- **How long:** 10 to 60 seconds.
- **When:** once. Only if you changed the source, or if there is no prebuilt
  binary for you.
- **After:** press **Install / Repair**.

If it fails you get a `Build failed` dialog and the compiler output stays in the
log panel — read the last lines, it usually names the missing component.

#### Install / Repair

What it actually does:

1. Checks `sekiro.exe` is really there. If not you get a `Wrong folder` dialog.
2. If `dinput8.dll` exists, copies it to
   `dinput8.dll.modengine-0.1.16.orig.bak`. Only creates that backup if one does
   not already exist, so your genuine original is never lost.
3. Copies `dinput8_patched.dll` in as `dinput8.dll`.
4. Creates `modengine.ini` pointing at `mods\` if it is missing.
5. Creates an empty `mods\` folder if it is missing.

- **Needs:** a prior successful build. Without one it says
  `DLL not built yet. Press Build first.`
- **When:** once, then any time you want to repair a bad install.
- **Safe to re-run** — it is idempotent.

#### Play Sekiro

- **Needs:** a valid game folder and an installed DLL.
- **Why a button instead of your normal shortcut:** Mod Engine finds `mods\`
  relative to the current working directory. A desktop shortcut or a non-Steam
  launcher often sets that somewhere else, and the mods silently fail to load.
  This button sets it correctly.
- **No console window appears.** The log is tailed in the app instead. See
  [No console window](#no-console-window).
- **After playing:** close the game, then press **Refresh**.

### 3.5 The log panel

The large panel along the bottom. It holds the app's own messages, the build
output, and install results. It scrolls; use the mouse wheel over it.

Colour tells you what a line is: `>` for your own actions, `#` for headings,
red if the line mentions an error.

---

## 4. Reading the load log

`modengine_load.log` in your game folder is the raw evidence. The `LOADER` card
and the detail line summarise it; the file itself looks like this:

```
[ModEngine] AOB scan hooking archive function at 00000001401C76D0
[INVENTORY] Scanning override directory: C:\Games\Sekiro\mods
[INVENTORY] 39 file(s) available for override.
[OVERRIDE OK ] C:\Games\Sekiro\mods\parts\fc_m_0200.partsbnd.dcx
             -> served as "data1:/parts/fc_m_0200.partsbnd.dcx" (156166889 bytes)
```

If you want the served-vs-available diff as a list rather than a count, use the
helper script:

```bat
python in_gameadd\mod_report.py --game-dir "C:\Games\Sekiro"
```

### No console window

The stock and earlier builds of this fix called `AllocConsole()` at startup
whenever `showDebugLog=1`, which meant a black console window sat in front of
the game for the whole session. It is gone. The log file is written either way.

If you want the old behaviour back for debugging by hand, add this to
`modengine.ini`:

```ini
[debug]
showConsole=1
```

| Key in `[debug]` | Default | Effect |
|---|---|---|
| `showDebugLog` | `1` | How much detail goes in the log file. |
| `showConsole` | `0` | Set to `1` to get a console window. |

### If served stays at 0

Something is wrong. Check in this order:

1. Does the `DINPUT8.DLL` card say `patched`? If it says `stock`, reinstall.
2. Does `LOADER` say `hooked`? If not, the hook never installed — the DLL is
   being clobbered again.
3. Did you launch via **Play Sekiro** rather than a shortcut?
4. Is `mods\` in the folder the game folder row names?

---

## 5. Build tools

Only needed for **Build DLL**. If you got a prebuilt DLL from the Releases page,
you never need this.

Install:

1. Visual Studio 2022, the **Community** edition is fine and free
2. On the installer workload screen pick **Desktop development with C++**
3. In the individual components list, tick:
   - **MSVC v143 x64/x86 build tools**
   - **Windows 11 SDK** (or Windows 10 SDK)
4. Install and restart the Fixer

The toolchain line should then read `Ready - MSVC ..., SDK ...`.

---

## 6. Typical first run

1. **Browse** to the folder containing `sekiro.exe`, press **Refresh**
2. Check the `DINPUT8.DLL` card says `patched`, or `not installed` if you have
   not installed yet
3. **Build DLL** — wait for it to report success
4. **Install / Repair** — press OK
5. **Play Sekiro** — play for a few minutes, then close the game
6. Press **Refresh** — confirm the `LOADER` card is jade and the served count
   is above zero
7. Done. Launch Sekiro normally from Steam from now on

Undo at any time: delete `dinput8.dll` and rename
`dinput8.dll.modengine-0.1.16.orig.bak` back to `dinput8.dll`.

---

## 7. The command line version

Everything the GUI does is available without it. Run from inside `ui\`:

```bat
cd ui
python -m modengine_fixer.cli status  --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli build
python -m modengine_fixer.cli install --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli report  --game-dir "C:\Games\Sekiro"
python -m modengine_fixer.cli play    --game-dir "C:\Games\Sekiro"
```

**The subcommand always comes first, `--game-dir` after it.** The reverse
order is rejected by the argument parser.

---

## 8. The helper scripts

Two folders of standalone scripts, for when you want proof without the GUI.

### `in_gameadd\` — run these from your game folder

| Script | Does |
|---|---|
| `mod_report.py` | The served-vs-available archive diff, from the command line. The same data behind the `LOADER` card. |
| `launch_sekiro.py` | Launches the game with the correct working directory. Same job as **Play Sekiro**. |

```bat
cd "C:\Games\Sekiro"
python mod_report.py
```

Neither is required for mods to load. They are conveniences and verification.
You can delete them at any time without affecting anything.

### `tools\` — diagnostics, run from the repo

| Script | Does |
|---|---|
| `check_mods.py` | Inspects the running game: loaded modules, captured console output, import table. For working out why something is not loading. |
| `probe_steamhook.py` | Determines whether `SteamAPI_Init` is hooked. This is the tool that proved bug 1. Start the game, then run it. |

```bat
python tools\probe_steamhook.py --game-dir "C:\Games\Sekiro"
python tools\check_mods.py     --game-dir "C:\Games\Sekiro"
```

Both support `SEKIRO_DIR` instead of `--game-dir` if you prefer.

---

## 9. If something goes wrong

| Symptom | Cause | Fix |
|---|---|---|
| `Wrong folder` on install | No `sekiro.exe` in the path | **Browse** to the right folder |
| Toolchain line says `Missing` | No MSVC | [Section 5](#5-build-tools), or use a prebuilt DLL |
| `Build failed` | Compiler error | Read the log panel, bottom lines |
| `LOADER` says `not hooked`, 0 served | Stock DLL installed, or hook clobbered | Reinstall, then check `DINPUT8.DLL` says `patched` |
| Mods work in-game but not via shortcut | Wrong working directory | Use **Play Sekiro**, or set the shortcut's "Start in" |
| Nothing happens at all | Windows is blocking the `.exe` | Right-click → Properties → tick **Unblock** → Apply |
| SmartScreen warning | Unsigned binary | *More info* → *Run anyway* |
| `Could not launch` | Game folder invalid, or the game is already running | Re-**Browse** and **Refresh** |

More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md). The underlying cause of the
original bug is documented in [ROOT_CAUSE.md](ROOT_CAUSE.md).
