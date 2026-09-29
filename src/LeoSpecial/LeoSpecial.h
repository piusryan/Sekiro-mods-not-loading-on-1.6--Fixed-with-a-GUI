#pragma once

// Minimal stub. The upstream repo references LeoHook from LeoSpecial but does not
// vendor the library; every use in this project is inside commented-out code or a
// dead declaration in ModLoader.cpp. Declaring the type is enough to compile.

class LeoHook
{
public:
	LeoHook() {}
	~LeoHook() {}

	void LeoHookFunction(LPVOID /*target*/, LPVOID /*detour*/) {}
	void LeoHookPrepare(LPVOID /*target*/) {}
	void LeoHookAttach() {}
	void LeoHookDetach() {}
};
