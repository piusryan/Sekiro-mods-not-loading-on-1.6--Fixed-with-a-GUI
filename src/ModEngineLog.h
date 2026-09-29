#pragma once

#include <Windows.h>

// True when modengine.ini sets showDebugLog=1. Controls how much detail is
// written; independent of showConsole, which only controls where it is echoed.
extern bool gDebugLog;

// Mod Engine build: logging is always written to <game dir>\modengine_load.log
// so the .dcx override result can be verified after the game exits.
// A console window is only ever created when [debug] showConsole=1 is set.
void MELogInit();
void MELogClose();
void MELog(const wchar_t* fmt, ...);
void MELogRaw(const wchar_t* line);
