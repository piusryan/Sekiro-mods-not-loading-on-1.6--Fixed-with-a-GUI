# Changes against upstream, and why

Base: [katalash/ModEngine](https://github.com/katalash/ModEngine) at commit
`42d6b9d10903018ff9b15d3d15a911a3e86f6e3d` (2020-03-04).

Apply with:

```bat
patch -p1 < patches/0001-allow-sekiro-1.6.patch
```

The full patched tree is also in `src/`, so you can copy files instead if
`patch` is awkward on Windows.

---

## Summary

| File | Status | What it does |
|---|---|---|
| `dllmain.cpp` | rewritten | Trigger the hook without Steam; drop the version gate; drop the unsafe patches; add logging and a crash filter |
| `Game.cpp` | rewritten | Real game detection instead of a hardcoded Dark Souls II |
| `Game.h` | edited | Added `GAME_UNKNOWN` so "not a game we know" is representable |
| `ModLoader.cpp` | extended | Per-file override logging, inventory scan, hit/miss counters; `wprintf` routed to the logger |
| `stdafx.h` | slimmed | Drop the ImGui/D3D11 includes the fix build does not use |
| `ModEngineLog.h` | **new** | Logging API |
| `ModEngineLog.cpp` | **new** | Thread-safe logger, always writes `modengine_load.log` |
| `LeoSpecial/LeoSpecial.h` | **new** | Minimal stub for a header upstream includes but never vendored |
| `build.cmd` | **new** | Builds with `cl.exe` directly, bypassing the stale v142 project file |

Not modified but no longer compiled: `d3d11hook.cpp`, `Menu.cpp`,
`InputHook.cpp`, `LooseParams.cpp`, `NetworkBlocker.cpp`,
`HideThreadFromDebugger.cpp`, `GameplayPatcher.cpp`, `StackWalker/`, `ImGui/`.

---

## `dllmain.cpp` — rewritten

The old file was 471 lines of Steam-centric setup. The new one is ~150.

### Hook trigger

`InitInstance()` no longer returns after detouring `SteamAPI_Init`. It spawns
`HookWorker`, which retries for up to 15 seconds:

```cpp
for (int attempt = 1; attempt <= 10; attempt++)
{
    MELog(L"[ModEngine] Hook attempt %d/10 ...", attempt);
    ApplyOverrideHooks();
    if (gHooksInstalled.load() || gHookFailed.load())
        break;
    Sleep(1500);
}
```

The retry is not decoration. On a packed or unusually slow start the image can
still be settling on the first attempt, and failing permanently because the
game was 200 ms too slow is a terrible trade. Two `std::atomic<bool>` flags
(`gHooksInstalled`, `gHookFailed`) make it idempotent, so the worker thread and
the Steam detour — both still present — can race without double-hooking.

`MELog()` is called before `GetGameType()` and before any hook, so the log
explains itself even when init fails.

### Removed: the version gate

The whole `CheckSekiroVersion()` / `CheckDkSVersion()` path is gone, along with
the `AllocConsole` + `std::cin.ignore()` blocks that hang the game waiting for
a keypress. The exe size is still logged, so you can see what you are running:

```
[ModEngine] Size: 67799112 bytes
```

### Removed: patches that cannot be safe on 1.6

```cpp
MH_CreateHook((LPVOID)0x14084ea60, &tMSBHitConstructor, ...);   // 1.02/1.03 address
if (MH_EnableHook((LPVOID)0x140e60320) != MH_OK) return false;    // 1.02/1.03 address
if (!ApplyMiscPatches()) throw(0xDEAD0004);
```

These are absolute addresses into the 1.02/1.03 executable. On 1.6 they are
valid-looking, writable addresses in `.text` that contain something else
entirely. A MinHook detour over unrelated code produces a crash inside
whatever the game was doing; a failed `MH_EnableHook` produces `throw`, which
is a crash too. Neither is needed to serve `.dcx` files, so both are removed.

The D3D11/ImGui menu is dropped for the same reason: it is the main thing
requiring `ApplyMiscPatches()`, and it is not needed for mods.

### Kept

`ApplyAllocatorLimitPatchVA()` — the `VirtualAlloc` hook and the AOB-scanned
memory-limit-table patch. Both are AOB-based, both were confirmed working on
1.6, and the second is what makes the AOB scanner's `VirtualProtect` calls
succeed.

### Added: a crash filter

An unhandled-exception filter that writes the code and address to the log and
returns `EXCEPTION_CONTINUE_SEARCH`, so a crash still produces a Windows
dialog *and* a log line naming the fault. The stock DLL's `StackWalker`
crash handler was removed along with the rest of the unused code.

---

## `Game.cpp` — rewritten

Upstream:

```cpp
DSGame GetGameType() { return GAME_DARKSOULS_2_SOTFS; }
```

That is the entire file's logic. The released binary detects the game from the
exe name and prints `Detected game as Sekiro`; that code is not in the repo. So
anyone compiling upstream gets a build that selects the Dark Souls II AOB
signature and hooks a Dark Souls II function.

The new version reads the exe name, maps it to the enum, and logs the result.
It memoises in a `static` so the `wprintf`-heavy `Detect()` does not run on
every call, and it logs the same `EXE name is ...` / `Detected game as ...`
lines the released binary produced, so existing log-scrapers keep working.

Unknown exe names now return `GAME_UNKNOWN` rather than silently pretending to
be Dark Souls II, and `ApplyOverrideHooks()` refuses to hook in that case.

---

## `Game.h` — edited

Added `GAME_UNKNOWN` as the first enumerator so a failed detection is a
distinct, testable state rather than an accident.

---

## `ModLoader.cpp` — extended

The override logic is **unchanged**. This is deliberate: the stock path building
and the `data` / `gamedata` / `game_` prefix handling are correct, and touching
them would risk breaking a working mechanism. Only logging was added.

### Per-file success logging

`CheckFile()` previously printed to the console only when `showDebugLog` was on,
and only via `wprintf`, so everything died with the game. It now records the
result durably:

```
[OVERRIDE OK ] C:\Games\Sekiro\mods\parts\fc_m_0200.partsbnd.dcx
             -> served as "data1:/parts/fc_m_0200.partsbnd.dcx" (156166889 bytes)
[SUMMARY   ] distinct override files served so far: 10
```

The `[repeat request]` tag distinguishes the first serve of a file from the
game re-requesting it. Without it, "served" counts are inflated by however often
the game happens to ask for a popular archive.

### Startup inventory

`LogOverrideInventory()` walks the override directory before the game has
loaded anything and logs every file with its size, marking archives distinctly
from loose files:

```
[INVENTORY] Scanning override directory: C:\Games\Sekiro\mods
[INVENTORY]   ARCH ...\mods\parts\fc_m_0200.partsbnd.dcx (156166889 bytes)
[INVENTORY]   file ...\mods\instructions.txt (1024 bytes)   (not an archive)
[INVENTORY] 39 file(s) available for override.
```

This is what makes "served 10 of 37" interpretable. Without the denominator,
"10" means nothing — it could be 10 of 10 the game wanted, or 10 of 37 with
your actual mod broken. With it, you can tell a working setup from a broken
one immediately.

### `wprintf` → `MELog`

All 19 `wprintf` calls in this file now go through the logger, so everything
lands in the log file, not just in a console that closes with the game.

---

## `stdafx.h` — slimmed

Dropped the ImGui and D3D11 includes. The fix build does not compile the menu,
so it does not need them. Upstream had also committed this file **as UTF-16
LE**, which is why `git` reports it as binary and `patch` will not apply a text
diff to it — copy the replacement from `src/` instead. That is noted in
`.gitattributes` too.

---

## `ModEngineLog.h` / `.cpp` — new

A small logger. The design constraint is that **it must work whether or not a
console exists**, because the whole point is to have evidence after the game
exits, and `AllocConsole()` is unreliable from a worker thread.

- One `HANDLE` to `<game dir>\modengine_load.log`, `CREATE_ALWAYS`, so each run
  is a fresh report.
- `std::mutex` — `ReplaceFileLoadPath` runs on the game's loader threads, which
  are concurrent. Unsynchronised `WriteFile` would interleave and shred lines.
- Wide strings converted to UTF-8 on write, so a non-ASCII mod path does not
  produce mojibake.
- One shared `gDebugLog` flag, set once in `MELogInit()`. Upstream declared
  `gDebugLog` in `dllmain.cpp` and `extern`'d it in two other files; it now
  lives in the logger next to the code that uses it.

---

## `LeoSpecial/LeoSpecial.h` — new stub

`ModLoader.cpp` line 4 is `#include "LeoSpecial/LeoSpecial.h"`, and upstream
ships no `LeoSpecial` directory. This is a straight build break.

The only reference is a dead declaration:

```cpp
uintptr_t ArchiveVEHHookAddress = NULL;
LeoHook ArchiveVEHHook;        // never called; all uses are commented out
```

So the stub only needs the type to exist with the right name. Documented as a
stub in `NOTICE` so nobody mistakes it for the real LeoHook library.

---

## `build.cmd` — new

Invokes `vcvars64.bat` then `cl.exe` directly.

Why not the shipped `.sln`: it pins `PlatformToolset` to `v142` and
`WindowsTargetPlatformVersion` to `10.0.17763.0`. A current VS 2022 install
has `v143` and SDK `10.0.26100`, so the solution needs both overridden. Calling
the compiler directly avoids that, and lets us build only the 11 files the fix
needs instead of all of ImGui — which is most of why the output is 212 KB
rather than several megabytes.

```bat
cl /nologo /LD /O2 /MT /EHsc /std:c++17 /DUNICODE /D_UNICODE ^
   /I"." /I"MinHook\include" /I"dinput8" ^
   dllmain.cpp ModLoader.cpp AOBScanner.cpp Game.cpp ModEngineLog.cpp ^
   "dinput8\dinputWrapper.cpp" ^
   "MinHook\src\buffer.c" "MinHook\src\hook.c" "MinHook\src\trampoline.c" ^
   "MinHook\src\HDE\hde64.c" ^
   /link /DEF:"dinput8\dinput8.def" /MACHINE:X64 psapi.lib shlwapi.lib advapi32.lib
```

`/MACHINE:X64` and the `dinput8.def` are load-bearing: the DLL has to be x64 to
match a 64-bit `sekiro.exe`, and `dinput8.def` is what puts
`DirectInput8Create @1` on the export table so the game's import resolves to us
and we can chain to the real system DLL.

The `LNK4070` warning about `/OUT:DINPUT8.dll` differing from the output
filename is expected and harmless — the `.def` names the library, the linker
uses `-Fe`.

---

## Deliberately not changed

- **`ReplaceFileLoadPath` / `CheckFile` logic** — works correctly on 1.6; only
  logging was added.
- **The 14-byte archive AOB signature** — verified to still match on 1.6. See
  [ROOT_CAUSE.md](ROOT_CAUSE.md).
- **`ApplyAllocatorLimitPatchVA` / `ApplyDS3SekiroAllocatorLimitPatch`** —
  AOB-based, confirmed working on 1.6.
- **`modengine.ini` keys** — every key the original supported still works. This
  build adds no new ones, so existing configs and existing Nexus mod
  documentation remain valid.
