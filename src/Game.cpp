#include "Game.h"
#include <Windows.h>
#include <stdio.h>
#include <string>
#include "ModEngineLog.h"

static DSGame Detect()
{
	char exePath[MAX_PATH] = { 0 };
	DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);

	// basename
	const char* base = exePath;
	for (const char* p = exePath; *p; p++)
	{
		if (*p == '\\' || *p == '/')
			base = p + 1;
	}

	wchar_t wide[MAX_PATH] = { 0 };
	MultiByteToWideChar(CP_ACP, 0, base, -1, wide, MAX_PATH);

	MELog(L"[ModEngine] EXE name is %s", wide);

	DSGame game = GAME_UNKNOWN;

	if (_wcsicmp(wide, L"sekiro.exe") == 0)
		game = GAME_SEKIRO;
	else if (_wcsicmp(wide, L"DarkSoulsIII.exe") == 0)
		game = GAME_DARKSOULS_3;
	else if (_wcsicmp(wide, L"DarkSoulsRemastered.exe") == 0)
		game = GAME_DARKSOULS_REMASTERED;
	else if (_wcsicmp(wide, L"DarkSoulsII.exe") == 0)
		game = GAME_DARKSOULS_2_SOTFS;

	switch (game)
	{
	case GAME_SEKIRO:
		MELog(L"[ModEngine] Detected game as Sekiro");
		break;
	case GAME_DARKSOULS_3:
		MELog(L"[ModEngine] Detected game as Dark Souls III");
		break;
	case GAME_DARKSOULS_REMASTERED:
		MELog(L"[ModEngine] Detected game as Dark Souls Remastered");
		break;
	case GAME_DARKSOULS_2_SOTFS:
		MELog(L"[ModEngine] Detected game as Dark Souls II");
		break;
	default:
		MELog(L"[ModEngine] Detected game as UNKNOWN - override hooks will NOT be installed");
		break;
	}

	return game;
}

DSGame GetGameType()
{
	static DSGame cached = GAME_UNKNOWN;
	if (cached == GAME_UNKNOWN)
		cached = Detect();
	return cached;
}
