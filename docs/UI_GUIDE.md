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
| You have Python | Open a prompt in the repo folder and run `python ui\fixer.py` |
| You want the command line | See [section 9](#9-the-command-line-version) |

A window titled **Mod Engine 1.6 Fixer** opens, with three tabs: `1. Setup`,
`2. Mod report`, `3. Help`.

> The `.exe` must stay next to the `src\`, `docs\` and `in_gameadd\` folders.
> It finds them relative to its own location, so keep the whole folder
> together. Moving only the `.exe` elsewhere breaks it.

### The window footer

The thin strip along the bottom is always visible, on every tab:

```
Build tools: Ready - MSVC 14.44.35207, SDK 10.0.26100.0
```

| Message | Meaning |
|---|---|
| `Ready - MSVC ..., SDK ...` | Build will work. |
| `Missing: ...` | Build will **not** work. Either install the tools ([section 7](#7-build-tools)) or grab a prebuilt DLL. |

---

## 3. Tab 1 — Setup

Where all the actual work happens. Three areas, top to bottom.

### 3.1 The game folder row

```
Game folder: [ C:\Games\Sekiro                          ] [Browse...] [Refresh]
```

Mod Engine resolves your mods relative to this folder, so it has to be right.
It is the folder containing **`sekiro.exe`**.

On startup the app guesses, checking the usual Steam path, the current working
directory, and its own folder. If your game is somewhere unusual it will guess
wrong, so check it.

| Button | What it does |
|---|---|
| **Browse...** | Opens a folder picker. Select the folder containing `sekiro.exe`. Re-runs the status check immediately. |
| **Refresh** | Re-reads the folder and rebuilds the status panel. Use after copying mods in or changing things outside the app. |

You can also just paste a path into the box and press Refresh.

### 3.2 The status panel

The large grey text area. It is the main thing to read.

```
[+] sekiro.exe      C:\Games\Sekiro\sekiro.exe
    size            67,799,112 bytes
    version         unknown / newer than 1.03  ->  too new for stock Mod Engine (this is what we fix)
[+] dinput8.dll     217,600 bytes  (PATCHED BUILD)
[+] backup          dinput8.dll.modengine-0.1.16.orig.bak
[i] mods folder    C:\Games\Sekiro\mods

[+] [ModEngine] AOB scan hooking archive function at 00000001401C76D0
[i] archives       10 of 37 served

Looks good. Play the game and check the Mod report tab.
```

**Line by line:**

| Line | Means |
|---|---|
| `[+] sekiro.exe` | Found the executable. Good. |
| `size 67,799,112` | Byte size. Only useful for spotting a wrong/partial file. |
| `version ... too new for stock Mod Engine` | **Expected on 1.6.** This is the bug being fixed. Not an error. |
| `dinput8.dll (PATCHED BUILD)` | **Correct.** The working version is installed. |
| `dinput8.dll (stock 0.1.16?)` | **Wrong.** The useless version. Rebuild and reinstall. |
| `[-] dinput8.dll not installed` | Normal before your first install. |
| `[+] backup ...orig.bak` | Your original DLL is saved. You can always undo. |
| `[i] mods folder ...` | Where your mods were found. |
| `AOB scan hooking archive function at 0x1401C76D0` | **The hook installed.** This single line is the proof the fix works. |
| `archives 10 of 37 served` | Your last session loaded 10 of the 37 mod files. |
| `[!] ...` | A warning. The text says what to do. |
| `Looks good.` | Everything checks out. Go play. |

The `unknown` in the version line is normal — stock Mod Engine never reads the
1.6 version block. That is the whole point of `PATCHES.md`.

### 3.3 The three action buttons

Hover any of them for a tooltip. They grey out while a build is running.

#### Build patched DLL

Compiles `src\build.cmd` and produces `src\build\dinput8_patched.dll`.

- **Needs:** Visual Studio 2022 Build Tools. Check the footer first.
- **How long:** 10 to 60 seconds. Compiler output streams into the box below.
- **When:** once. Only if you changed the source, or if there is no prebuilt
  binary for you.
- **After:** press **Install / repair**.

If the build fails you get a `Build failed` dialog and the compiler output
stays in the lower box — read the last lines, it usually names the missing
component.

#### Install / repair

Copies the built DLL into your game folder and makes sure the config exists.

What it actually does:

1. Checks `sekiro.exe` is really there. If not you get a `Wrong folder` dialog.
2. If `dinput8.dll` exists, renames it to
   `dinput8.dll.modengine-0.1.16.orig.bak`. Only overwrites an older backup,
   so your genuine original is never lost.
3. Copies `dinput8_patched.dll` in as `dinput8.dll`.
4. Creates `modengine.ini` pointing at `mods\` if it is missing.
5. Creates an empty `mods\` folder if it is missing.

- **Needs:** a prior successful build. If you press it with no build present
  it offers to build first.
- **When:** once, then any time you want to repair a bad install.
- **Safe to re-run** — it is idempotent.

#### Play game

Starts `sekiro.exe` with the correct working directory, then tells you to play
a few minutes and check the report.

- **Needs:** a valid game folder and an installed DLL.
- **Why a button instead of your normal shortcut:** Mod Engine finds `mods\`
  relative to the current working directory. A desktop shortcut or a
  non-Steam launcher often sets that somewhere else, and the mods silently
  fail to load. This button sets it correctly.
- **After playing:** close the game, go to tab 2, press **Refresh report**.

### 3.4 The build output box

The small box under the buttons. Compiler output during a build. Read it when
a build fails — it names the exact error.

---

## 4. Tab 2 — Mod report

The payoff. This is the evidence.

After playing for a few minutes and closing the game, press **Refresh report**.

```
[ModEngine] AOB scan hooking archive function at 00000001401C76D0

Available archives : 37
Served last session : 10

SERVED (your mods are loading)
----------------------------------------------------------
  + parts/fc_m_0200.partsbnd.dcx
  + msg/engus/menu.msgbnd.dcx
  + menu/hi/menu_load_00008.tpf.dcx

NOT REQUESTED YET
----------------------------------------------------------
  . menu/ja/menu_load_00008.tpf.dcx
  . parts/fc_c_0100.partsbnd.dcx
  ...

Not a problem. The game only requests the files it needs.
```

| Control | Does |
|---|---|
| **Refresh report** | Re-reads `modengine_load.log` from the game folder and re-renders this tab. |
| The bold count on the right | `served / available`. Shown live on the tab. |

### SERVED

Files your `mods\` folder actually delivered. **This is the thing you want to
see.** Each line is a mod that is working.

### NOT REQUESTED YET

Files the game has not asked for. **This is normal and not a problem.** The
game only loads what it needs:

- `menu\<lang>\menu_load_*.tpf.dcx` load as you open those specific screens
- everything under `msg\<language>\` loads **only** if you select that language
  in the game's own options — the other 12 language folders stay untouched
  forever, and that is correct
- character model archives load the first time you meet that character

So play further and refresh. The served list should grow. Expect roughly 10
files on a normal short session out of the 37 present.

### If served stays at 0

Something is wrong. Check in this order:

1. `dinput8.dll` says `PATCHED BUILD`? If it says `stock 0.1.16?`, reinstall.
2. Does the `AOB scan hooking...` line appear? If not, the hook never
   installed — the DLL is being clobbered again.
3. Did you launch via **Play game** rather than a shortcut?
4. Is `mods\` in the folder the status panel names?

---

## 5. Tab 3 — Help

A built-in copy of these instructions, so the tool is self-documenting even
if you hand it to someone without the repo. Same content, condensed.

---

## 6. The two footer buttons

| Button | Does |
|---|---|
| **Open game folder** | Opens Explorer at your game directory |
| **Docs** | Opens the `docs\` folder, including this guide |

---

## 7. Build tools

Only needed for **Build patched DLL**. If you got a prebuilt DLL from the
Releases page, you never need this.

Install:

1. Visual Studio 2022, the **Community** edition is fine and free
2. On the installer workload screen pick **Desktop development with C++**
3. In the individual components list, tick:
   - **MSVC v143 x64/x86 build tools**
   - **Windows 11 SDK** (or Windows 10 SDK)
4. Install and restart the Fixer

The footer should then read `Build tools: Ready`.

---

## 8. Typical first run

1. **Browse...** to the folder containing `sekiro.exe`, press **Refresh**
2. Check the status panel shows `PATCHED BUILD`, or `not installed` if you
   have not installed yet
3. **Build patched DLL** — wait for `Build succeeded`
4. **Install / repair** — press OK
5. **Play game** — play for a few minutes, then close
6. Tab **2. Mod report** → **Refresh report** — confirm files under SERVED
7. Done. Launch Sekiro normally from Steam from now on

Undo at any time: delete `dinput8.dll` and rename
`dinput8.dll.modengine-0.1.16.orig.bak` back to `dinput8.dll`.

---

## 9. The command line version

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

## 10. The helper scripts

Two folders of standalone scripts, for when you want proof without the GUI.

### `in_gameadd\` — run these from your game folder

| Script | Does |
|---|---|
| `mod_report.py` | The same report as tab 2, from the command line. Also the source of the tab 2 data. |
| `launch_sekiro.py` | Launches the game with the correct working directory. Same job as **Play game**. |

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

## 11. If something goes wrong

| Symptom | Cause | Fix |
|---|---|---|
| `Wrong folder` on install | No `sekiro.exe` in the path | **Browse...** to the right folder |
| `Build tools: Missing` | No MSVC | [Section 7](#7-build-tools), or use a prebuilt DLL |
| `Build failed` | Compiler error | Read the build output box, bottom lines |
| Report shows 0 served, no hook line | Stock DLL installed, or hook clobbered | Reinstall, then check `dinput8.dll` says `PATCHED BUILD` |
| Mods work in-game but not via shortcut | Wrong working directory | Use **Play game**, or set the shortcut's "Start in" |
| Nothing happens at all | Windows is blocking the `.exe` | Right-click → Properties → tick **Unblock** → Apply |
| App closes instantly | Crash | A `fixer_crash.log` appears next to the `.exe` with the details |
| `Could not launch` | Game folder invalid, or the game is already running | Re-**Browse...** and **Refresh** |

More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md). The underlying cause of the
original bug is documented in [ROOT_CAUSE.md](ROOT_CAUSE.md).
