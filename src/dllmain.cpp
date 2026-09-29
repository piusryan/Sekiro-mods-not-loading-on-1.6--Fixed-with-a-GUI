#include "stdafx.h"
#include "dinput8\dinputWrapper.h"
#include "ModEngineLog.h"
#include "Game.h"
#include "ModLoader.h"
#include "AOBScanner.h"
#include "MinHook\include\MinHook.h"

// Export DINPUT8
tDirectInput8Create oDirectInput8Create = NULL;

static std::atomic<bool> gHooksInstalled(false);
static std::atomic<bool> gHookFailed(false);

// ---------------------------------------------------------------------------
// Hook application. The stock Mod Engine only reaches this code from a
// SteamAPI_Init detour, which never fires under SmartSteamEmu because that DLL
// decrypts itself at runtime and clobbers the early MinHook patch. We drive it
// from a worker thread instead, with a retry so a late-decrypted image can still
// be found.
// ---------------------------------------------------------------------------
static void ApplyOverrideHooks()
{
	if (gHooksInstalled.load() || gHookFailed.load())
		return;

	if (GetGameType() == GAME_UNKNOWN)
	{
		MELog(L"[FATAL] Unknown executable, refusing to hook.");
		gHookFailed.store(true);
		return;
	}

	bool loadUXMFiles = (GetPrivateProfileIntW(L"files", L"loadUXMFiles", 0, L".\\modengine.ini") == 1);
	bool useModOverride = (GetPrivateProfileIntW(L"files", L"useModOverrideDirectory", 1, L".\\modengine.ini") == 1);
	bool cachePaths = (GetPrivateProfileIntW(L"files", L"cacheFilePaths", 0, L".\\modengine.ini") == 1);

	// Must have static storage duration: ModLoader keeps this pointer in gModDir and
	// dereferences it for the lifetime of the process, long after this thread's
	// stack is gone. A stack buffer here silently yields a dangling pointer.
	static wchar_t modDir[512] = { 0 };
	GetPrivateProfileStringW(L"files", L"modOverrideDirectory", L"\\mods", modDir, 500, L".\\modengine.ini");

	wchar_t cwd[MAX_PATH] = { 0 };
	GetCurrentDirectoryW(MAX_PATH, cwd);
	MELog(L"[ModEngine] Override directory setting: %s", modDir);
	MELog(L"[ModEngine] Working directory:        %s", cwd);

	if (!HookModLoader(loadUXMFiles, useModOverride, cachePaths, modDir))
	{
		MELog(L"[FATAL] HookModLoader failed - the archive AOB pattern was not found.");
		gHookFailed.store(true);
		return;
	}

	gHooksInstalled.store(true);
	MELog(L"[ModEngine] Mod override hooks installed successfully.");
}

static DWORD WINAPI HookWorker(LPVOID)
{
	// The archive signature lives in the exe's .text, which the loader has already
	// mapped by the time our static import is resolved. Give the game a moment to
	// settle anyway, then retry so a slow-starting image still gets hooked.
	for (int attempt = 1; attempt <= 10; attempt++)
	{
		MELog(L"[ModEngine] Hook attempt %d/10 ...", attempt);
		ApplyOverrideHooks();
		if (gHooksInstalled.load() || gHookFailed.load())
			break;
		Sleep(1500);
	}

	if (!gHooksInstalled.load() && !gHookFailed.load())
		MELog(L"[FATAL] Gave up after 10 attempts.");
	return 0;
}

// ---------------------------------------------------------------------------
// D3D11/ImGui menu, save-file relocation, loose params, network blocking and the
// gameplay patches all use hardcoded 1.02/1.03 addresses and throw on failure.
// None of them are needed to serve .dcx overrides, and on 1.6 they would land on
// unrelated code, so they are intentionally not built into this DLL.
// ---------------------------------------------------------------------------
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
	wchar_t buf[512];
	swprintf_s(buf, L"[CRASH] exception code 0x%08X at %p\n",
		ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
	MELogRaw(buf);
	MELog(L"[CRASH] The game may crash if a mod file is malformed.");
	return EXCEPTION_CONTINUE_SEARCH;
}

static void InitCrashFilter()
{
	SetUnhandledExceptionFilter(CrashFilter);
}

static BOOL InitInstance(HMODULE)
{
	// Load the real dinput8.dll
	wchar_t dllPath[MAX_PATH] = { 0 };
	GetSystemDirectoryW(dllPath, MAX_PATH);
	lstrcatW(dllPath, L"\\dinput8.dll");
	HMODULE hMod = LoadLibraryW(dllPath);
	if (hMod == NULL)
	{
		MELog(L"[ModEngine] FATAL: could not load system dinput8.dll");
		return false;
	}
	oDirectInput8Create = (tDirectInput8Create)GetProcAddress(hMod, "DirectInput8Create");
	if (oDirectInput8Create == NULL)
	{
		MELog(L"[ModEngine] FATAL: system dinput8.dll has no DirectInput8Create");
		return false;
	}
	MELog(L"[ModEngine] Proxied system dinput8.dll OK");

	if (MH_Initialize() != MH_OK)
	{
		MELog(L"[ModEngine] FATAL: MinHook failed to initialize");
		return false;
	}
	MELog(L"[ModEngine] MinHook initialized");

	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	// Logging needs no dependencies, so set it up before anything can fail.
	MELogInit();
	InitCrashFilter();

	if (reason == DLL_PROCESS_ATTACH)
	{
		MELog(L"");
		MELog(L"================ Mod Engine (patched) ================");

		wchar_t exePathW[MAX_PATH] = { 0 };
		DWORD n = GetModuleFileNameW(NULL, exePathW, MAX_PATH);
		if (n)
		{
			FILE* f = nullptr;
			if (n < MAX_PATH && _wfopen_s(&f, exePathW, L"rb") == 0 && f)
			{
				fseek(f, 0, SEEK_END);
				long sz = ftell(f);
				fclose(f);
				MELog(L"[ModEngine] Executable: %s", exePathW);
				MELog(L"[ModEngine] Size: %ld bytes", sz);
			}
			else
			{
				MELog(L"[ModEngine] Executable: %s", exePathW);
			}
		}

		MELog(L"[ModEngine] DLL: %s", L"dinput8.dll (patched build)");

		if (InitInstance(hModule))
			CreateThread(NULL, 0, HookWorker, NULL, 0, NULL);
		else
			MELog(L"[ModEngine] FATAL: init failed, override hooks not installed.");
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		MELog(L"[ModEngine] Shutting down.");
		MELog(L"====================================================");
		MELogClose();
	}

	return TRUE;
}
