# Root cause: why stock Mod Engine 0.1.16 does nothing on Sekiro 1.6

Everything below was verified on a real install: Windows 11, `sekiro.exe`
version 1.6.0.0, 67,799,112 bytes, official Steam release.

Short version: **two independent bugs, both must be fixed.** Bug 1 is why the
hook never installs. Bug 2 is what you hit next, once you have fixed Bug 1.

---

## The last thing that happens

Turn on `showDebugLog=1` and this is the complete, unvarying output:

```
[ModEngine] Hooking VirtualAlloc
[ModEngine] Detected base module offset at 0x0000000140000000
[ModEngine] AOB Scanner Initialized
[ModEngine] Patching memory limit table at 0000000143B1BB90
```

Six lines. No archive hook, no file hook, no `mods\` path anywhere. The game
itself runs fine — which is what makes this so confusing.

The missing line is `Hooking archive loader functions`, which would come from
`HookModLoader()`. It is never called.

---

## Bug 1: the hook is installed from `DllMain`, gated on a load-order race

### How the stock DLL works

In `dllmain.cpp`, `DLL_PROCESS_ATTACH` calls `InitInstance()`, which does:

```cpp
auto steamApiHwnd = GetModuleHandleW(L"steam_api64.dll");
auto initAddr     = GetProcAddress(steamApiHwnd, "SteamAPI_Init");
MH_CreateHook(initAddr, &onSteamInit, reinterpret_cast<LPVOID*>(&fpSteamInit));
MH_EnableHook(initAddr);
```

and the detour body is the *only* place the real work happens:

```cpp
DWORD64 __cdecl onSteamInit()
{
    ApplyPostUnpackHooks();   // <- calls HookModLoader()
    return fpSteamInit();
}
```

So the entire mod-loading system is gated on the game calling `SteamAPI_Init`
**and** on that detour still being in place at that moment.

### Why the detour is not reliably there

That design hands two conditions to factors Mod Engine does not control.

**One: is `steam_api64.dll` mapped yet?** `GetModuleHandleW` is called from our
`DllMain`, and DLL initialisation order across independent modules is not
guaranteed. If our import list is resolved before the game's, the handle is
`NULL`, `initAddr` is `NULL`, and `MH_CreateHook` fails inside a `DllMain` where
nobody is checking a return value.

**Two: does the patch survive until the call arrives?** `DllMain` runs while the
Windows loader is still holding the loader lock. Microsoft is explicit that
`DllMain` should not call into other modules, and patching executable code in a
module that has not finished initialising is exactly the case they warn about.
Any of the following can land in that window:

- Steam or an injected overlay DLL finishing its own initialisation and
  rewriting code pages
- antivirus or anti-cheat software scanning and remapping the image
- Steam updating its redistributables mid-launch
- a slow disk, so `sekiro.exe` pages are demand-paged in after the patch is
  written

None of these are exotic, and all of them fail the same way: silently.

### The evidence

`steam_api64.dll` is 1249 exports and `SteamAPI_Init` is genuinely present, at
RVA `0x2ECF8` — so "the export is missing" is *not* the explanation.

On disk, at that RVA:

```
40 55 56 41 54 ...              <- a function prologue, i.e. not packed
```

In memory, 30 seconds into a running game:

```
40 53 48 83 EC 20 33 C9 C6 05 ...   <- ordinary prologue: push rbx;
                                       sub rsp,20; xor ecx,ecx
```

A MinHook detour would look like a register load followed by a jump
(`48 B8 <64-bit address> FF E0`, or `48 8B 05 <rel32> E9`). **There is no jump.
The function is not hooked.** The export is real and the code is intact — the
patch is simply not there, exactly as the load-order model predicts.

> `tools/probe_steamhook.py` reproduces this check. Run it against a live game
> and it prints the prologue bytes so you can see it for yourself.

### The fix

Do not depend on load order at all. Run the setup from a worker thread created
once `DllMain` has returned and module initialisation has settled, with bounded
retry so a slow start still gets hooked. Keep the Steam detour too — it is
harmless and still works when it does fire. Guard both paths so the hook is
only ever installed once.

---

## Bug 2: the version gate rejects 1.6 and then hangs

Fixing Bug 1 gets you one line further, into this:

```cpp
BOOL CheckSekiroVersion()
{
    // 1.02         = 65682008
    // 1.02 unpacked = 65682312
    // 1.03         = 65688152
    if (size == 65682008 || size == 65682312 || size == 65688152)
        return true;
    return false;
}
```

It matches on **exact file size only**. A 1.6 `sekiro.exe` is 67,799,112 bytes,
which is in none of those, so it returns false. Then in `ApplyPostUnpackHooks()`:

```cpp
if ((GetGameType() == GAME_SEKIRO) && !CheckSekiroVersion())
{
    AllocConsole();
    ...
    printf("Unsupported version of Sekiro detected...");
    std::cin.ignore();     // <- blocks forever, no keypress ever arrives
    FreeConsole();
}
```

It opens a console, prints a warning, and waits for a keypress that will never
come. The game appears to hang at startup with a stray console window.

A file-size check is a weak proxy for a version check, and it breaks on every
update, because an update changes the executable. Remove the gate.

---

## Bug 3 (bonus): the upstream source does not even detect Sekiro

If you build from the katalash source and it "works", check what you built.
`Game.cpp` is a stub:

```cpp
DSGame GetGameType()
{
    return GAME_DARKSOULS_2_SOTFS;   // <- always Dark Souls II
}
```

The real 0.1.16 binary detects the game from the exe name and logs
`Detected game as Sekiro`, but **that detection is not in the public repo.**
Compiling upstream as-is gives you a build that thinks it is Dark Souls II,
takes the wrong AOB signature, and hooks the wrong function. Replace `Game.cpp`
with real detection.

---

## What is *not* broken: the AOB signature

The obvious worry is that 1.6 moved everything, so the signature no longer
matches. It does match. You can check this without compiling anything — just
scan `sekiro.exe` for the 14 bytes
`40 55 56 41 54 41 55 48 83 EC 28 4D 8B E0` from
`ModLoader.cpp::GetArchiveFunctionAddress()`:

```
2 matches in .text:
  rva=0x001c76d0
  rva=0x004e8950
```

`AOBScanner::Scan()` returns the **first** match scanning low-to-high, and the
first is `0x001c76d0`. Cross-check that against the address upstream itself
documents as known-good for 1.02:

```cpp
//return (LPVOID)0x1401c5d80;   // <- 1.02
//return (LPVOID)0x1401c5d80;
```

RVA `0x1c5d80` vs. our `0x1c76d0` — the same function, a few KB away because
the game grew. And indeed, at runtime the patched build logs:

```
[ModEngine] AOB scan hooking archive function at 00000001401C76D0
```

Exactly the predicted address. **The signature needs no change.** This is worth
stating explicitly, because it is the thing most people assume is broken and
end up rewriting signatures for no reason.

---

## The whole fix, end to end

| # | Bug | Symptom | Fix |
|---|---|---|---|
| 1 | Setup gated behind a `SteamAPI_Init` detour installed from `DllMain`, lost to a module load-order race | Log stops after 6 lines, no hook | Also drive setup from a worker thread, with bounded retry |
| 2 | `CheckSekiroVersion()` accepts only 1.02/1.03 file sizes, then blocks on `std::cin.ignore()` | Game hangs at startup, stray console | Remove the gate, log the reason instead |
| 3 | `Game.cpp` hardcodes Dark Souls II | Wrong signature, wrong hook | Real detection from the exe name |
| 4 | `LeoSpecial/LeoSpecial.h` is included but never vendored — will not compile | Build error | Minimal stub; every upstream use is dead code |
| 5 | `ApplyMiscPatches()` hardcodes 1.02/1.03 addresses (`0x14084ea60`, `0x140e60320`) | Crashes on 1.6, or `throw(0xDEAD0004)` | Dropped. Not needed to serve `.dcx`. |
| 6 | No persistent log | Cannot tell whether a mod loaded | Always write `modengine_load.log` |
| 7 | **Ours:** `modOverrideDirectory` read into a stack buffer whose pointer is kept for the process lifetime | Hook installs, `mods\` silently missing from every path, 0 files served | Make it `static` |

Bug 7 is worth calling out because it is the kind of thing that costs an
afternoon. `HookModLoader` stores the pointer you hand it in `gModDir` and
dereferences it on *every* archive request for the rest of the process. If that
buffer is a local in a function that returns — as it was in our first attempt —
the pointer dangles, and the thread's stack gets reused by something else. No
crash, no log line, just an empty directory in the middle of every path. The
symptom is a hook that installs cleanly and serves nothing, which looks
identical to "the mods are in the wrong place."

Full file-by-file detail: [PATCHES.md](PATCHES.md).
