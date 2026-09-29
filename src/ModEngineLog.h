#pragma once

#include <Windows.h>

// True when modengine.ini asks for verbose logging.
extern bool gDebugLog;

// Mod Engine build: logging is always written to <game dir>\modengine_load.log
// so the .dcx override result can be verified after the game exits.
void MELogInit();
void MELogClose();
void MELog(const wchar_t* fmt, ...);
void MELogRaw(const wchar_t* line);
