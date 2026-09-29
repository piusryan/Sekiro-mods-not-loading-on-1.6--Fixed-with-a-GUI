#include "ModEngineLog.h"
#include <stdio.h>
#include <stdarg.h>
#include <mutex>

static std::mutex gLogMutex;
static HANDLE gLogFile = INVALID_HANDLE_VALUE;
static bool gConsole = false;
static bool gEcho = false;

bool gDebugLog = true;

static void ResolveLogPath(wchar_t* out, size_t cap)
{
	wchar_t exe[MAX_PATH] = { 0 };
	DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
	if (n == 0 || n == MAX_PATH)
	{
		wcscpy_s(out, cap, L"modengine_load.log");
		return;
	}
	wchar_t* slash = wcsrchr(exe, L'\\');
	if (slash)
		slash[1] = L'\0';
	else
		exe[0] = L'\0';
	wcscat_s(out, cap, exe);
	wcscat_s(out, cap, L"modengine_load.log");
}

void MELogInit()
{
	if (gLogFile != INVALID_HANDLE_VALUE)
		return;

	wchar_t path[MAX_PATH] = { 0 };
	ResolveLogPath(path, MAX_PATH);

	// always truncate so each run produces a fresh report
	gLogFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	// The log file is unconditional, so everything useful is available without
	// a console ever appearing.
	gEcho = (GetPrivateProfileIntW(L"debug", L"showDebugLog", 0, L".\\modengine.ini") == 1);
	gDebugLog = gEcho;

	// Creating a console is a separate, explicit opt-in. sekiro.exe is a GUI
	// subsystem app, so it starts with no console and calling AllocConsole()
	// pops a stray black window that sits there for the whole session. Nobody
	// asked for that and the log file already has the same output, so it only
	// happens when showConsole=1 is set deliberately.
	const bool wantConsole =
		(GetPrivateProfileIntW(L"debug", L"showConsole", 0, L".\\modengine.ini") == 1);

	if (wantConsole && GetConsoleWindow() == NULL)
		AllocConsole();

	// If a console exists by the time we get here -- either because the player
	// asked for one, or because the game was started from a terminal -- write
	// to it. Otherwise stay silent and let the log file do the work.
	if (GetConsoleWindow() != NULL)
	{
		FILE* dummy = nullptr;
		freopen_s(&dummy, "CONOUT$", "w", stdout);
		freopen_s(&dummy, "CONOUT$", "w", stderr);
		gConsole = true;
	}
}

void MELogRaw(const wchar_t* line)
{
	if (gConsole)
	{
		fwprintf(stdout, L"%s\r\n", line);
		fflush(stdout);
	}

	if (gLogFile == INVALID_HANDLE_VALUE)
		return;

	std::lock_guard<std::mutex> lock(gLogMutex);
	DWORD written = 0;

	// convert to UTF-8 and append CRLF
	char utf8[2048];
	int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
	if (n <= 0)
		return;
	utf8[n - 1] = '\r';
	utf8[n] = '\n';

	SetFilePointer(gLogFile, 0, nullptr, FILE_END);
	WriteFile(gLogFile, utf8, (DWORD)(n + 1), &written, nullptr);
	FlushFileBuffers(gLogFile);
}

void MELog(const wchar_t* fmt, ...)
{
	wchar_t buf[2048];
	va_list args;
	va_start(args, fmt);
	vswprintf_s(buf, _TRUNCATE, fmt, args);
	va_end(args);
	MELogRaw(buf);
}

void MELogClose()
{
	if (gLogFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(gLogFile);
		gLogFile = INVALID_HANDLE_VALUE;
	}
}
