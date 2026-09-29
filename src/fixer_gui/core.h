#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace fs = std::filesystem;

namespace core {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr wchar_t kInstalledDll[]  = L"dinput8.dll";
inline constexpr wchar_t kBuiltDll[]      = L"dinput8_patched.dll";
inline constexpr wchar_t kLoadLog[]       = L"modengine_load.log";
inline constexpr wchar_t kBackupSuffix[]  = L".modengine-0.1.16.orig.bak";
inline constexpr wchar_t kIniFile[]       = L"modengine.ini";
inline constexpr wchar_t kExeName[]       = L"sekiro.exe";

// Legacy exe sizes stock Mod Engine 0.1.16 accepts
inline constexpr uint64_t kSize102        = 65'682'008;
inline constexpr uint64_t kSize102u       = 65'682'312;
inline constexpr uint64_t kSize103        = 65'688'152;

// ---------------------------------------------------------------------------
// Result types
// ---------------------------------------------------------------------------
struct Toolchain {
    bool        ok      = false;
    std::wstring detail;            // shown in footer
    std::wstring vcvars;            // full path to vcvars64.bat (empty if not found)
};

struct GameStatus {
    fs::path     gameDir;
    bool         ok           = false;
    std::wstring problem;
    fs::path     exe;
    uint64_t     exeSize      = 0;
    std::wstring version;           // "1.02" / "1.03" / "unknown / 1.06+"
    bool         versionOk    = false;
    fs::path     dll;
    uint64_t     dllSize      = 0;
    bool         dllPatched   = false;  // our build (<400 KB)
    bool         dllStale     = false;  // stock 0.1.16 (>500 KB)
    fs::path     backup;
    bool         iniOk        = false;
    std::vector<std::wstring> notes;
};

struct Report {
    std::wstring              hookLine;
    std::wstring              problem;
    std::vector<std::wstring> available;
    std::vector<std::wstring> served;
    // computed
    std::vector<std::wstring> missing() const;
    bool ok() const { return !hookLine.empty() && problem.empty(); }
};

struct BuildResult {
    bool         ok       = false;
    std::wstring log;
    fs::path     dllPath;
};

struct InstallResult {
    bool         ok = false;
    std::wstring message;
    fs::path     backup;
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Repo root: directory that contains src\ and background.jpg.
// Works whether launched from the fixer_gui\ build dir or the repo root.
fs::path repoRoot();

Toolchain    toolchainStatus();
GameStatus   inspectGame(const fs::path& gameDir);
Report       buildReport(const fs::path& gameDir);
bool         isGameRunning();
fs::path     getModsDir(const fs::path& gameDir);

BuildResult  buildDll(std::function<void(const std::wstring&)> callback = nullptr);
InstallResult installDll(const fs::path& gameDir, const fs::path& dllPath = {});
bool         launchGame(const fs::path& gameDir);   // returns false on error

// Try Steam registry → sekiro.exe path
fs::path     detectGameDir();

} // namespace core
