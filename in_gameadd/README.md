# `in_gameadd\` — helpers that belong next to your game

Two standalone scripts, used by the [Mod Engine Fixer](../docs/UI_GUIDE.md) and
useful on their own.

**Neither is required for mods to load.** They are conveniences and
verification. Delete them any time and nothing breaks.

## How to use them

They are designed to live in your Sekiro folder and work from there with no
arguments, because they look for `sekiro.exe` next to themselves:

```bat
cd "C:\Games\Sekiro"
python mod_report.py
```

If you would rather keep them here, point them at the game:

```bat
python in_gameadd\mod_report.py --game-dir "C:\Games\Sekiro"
```

`--game-dir`, or the `SEKIRO_DIR` environment variable, both work.
`SEKIRO_DIR` wins if you set both.

## `mod_report.py`

Shows which of your mod files the game actually loaded.

It reads `modengine_load.log` from the game folder and compares the files
Mod Engine served against the `.dcx` / `.tpf` files sitting in `mods\`:

```
available : 37
served    : 10
```

Anything under **SERVED** is a mod that is working. Anything under
**NOT REQUESTED YET** has not been asked for by the game — which is normal, not
a fault. Mod Engine only loads what the game needs:

- `menu\<lang>\menu_load_*.tpf.dcx` load as you open those screens
- everything under `msg\<language>\` loads only if you select that language in
  the game's own options
- character model archives load the first time you meet that character

Exit code is `0` when at least one archive was served, `1` when none were.
That makes it usable in scripts.

## `launch_sekiro.py`

Starts the game with the correct working directory.

This matters because Mod Engine opens `modengine.ini` as the **relative** path
`.\modengine.ini`. The only thing that decides where it looks is the working
directory. A desktop shortcut, a launcher, or starting the game from somewhere
else usually points that somewhere wrong, and your mods silently fail to load.

This script is the same as the **Play game** button in the GUI, for when you
would rather use a shortcut or a batch file.

```bat
python launch_sekiro.py                 :: fix the ini, then launch
python launch_sekiro.py --check         :: report what it finds, do not launch
python launch_sekiro.py --no-fix        :: launch, leave modengine.ini alone
```

`--check` is the useful one for troubleshooting. It prints the working
directory, the sizes of `sekiro.exe` / `modengine.ini` / `dinput8.dll`, whether
the DLL really is Mod Engine, and any problems it finds in the ini — without
starting the game.

## Deeper diagnostics

For when you want evidence about *why* something is not loading, see
[`..\tools\`](../README.md) — `check_mods.py` inspects the running game, and
`probe_steamhook.py` determines whether `SteamAPI_Init` is hooked.
