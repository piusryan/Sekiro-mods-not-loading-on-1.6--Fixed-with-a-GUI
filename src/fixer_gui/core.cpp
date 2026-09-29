#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>

#include "core.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using namespace core;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static std::wstring toLower(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

static bool fileExists(const fs::path& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static uint64_t fileSize(const fs::path& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return (uint64_t)d.nFileSizeHigh << 32 | d.nFileSizeLow;
}

// Read a single key from a simple INI file (no configparser needed)
static std::wstring iniReadString(const fs::path& ini,
                                  const std::wstring& section,
                                  const std::wstring& key,
                                  const std::wstring& def = L"") {
    wchar_t buf[1024]{};
    GetPrivateProfileStringW(section.c_str(), key.c_str(),
                              def.c_str(), buf, 1024, ini.c_str());
    return buf;
}

static bool iniReadBool(const fs::path& ini,
                        const std::wstring& section,
                        const std::wstring& key,
                        bool def) {
    std::wstring v = iniReadString(ini, section, key, def ? L"1" : L"0");
    return v == L"1" || toLower(v) == L"true" || toLower(v) == L"yes";
}

// ---------------------------------------------------------------------------
// repoRoot
// ---------------------------------------------------------------------------
fs::path core::repoRoot() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path exe = fs::path(buf).parent_path();
    // When run from src\fixer_gui\ go up two levels to the repo root
    if (fs::exists(exe / "core.cpp") || fs::exists(exe / "build.cmd")) {
        exe = exe.parent_path().parent_path();
    } else if (fs::exists(exe.parent_path() / "background.jpg")) {
        exe = exe.parent_path();
    }
    // Verify by checking for background.jpg
    if (fs::exists(exe / "background.jpg")) return exe;
    // Fallback: two levels up from exe
    return fs::path(buf).parent_path().parent_path().parent_path();
}

// ---------------------------------------------------------------------------
// toolchainStatus
// ---------------------------------------------------------------------------
static fs::path findVswhere() {
    for (auto env : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        wchar_t base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(env, base, MAX_PATH)) continue;
        fs::path p = fs::path(base) / "Microsoft Visual Studio" /
                     "Installer" / "vswhere.exe";
        if (fileExists(p)) return p;
    }
    return {};
}

static std::vector<fs::path> vsRoots() {
    std::vector<fs::path> roots;
    auto vswhere = findVswhere();
    if (!vswhere.empty()) {
        SECURITY_ATTRIBUTES sa{sizeof(sa)};
        HANDLE hR, hW;
        if (CreatePipe(&hR, &hW, &sa, 0)) {
            STARTUPINFOW si{sizeof(si)};
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = hW;
            si.hStdError  = hW;
            SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);
            std::wstring cmd = L"\"" + vswhere.wstring() +
                               L"\" -products * -latest -format value -property installationPath";
            wchar_t cmdBuf[2048];
            wcscpy_s(cmdBuf, cmd.c_str());
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(hW); hW = nullptr;
                char buf[4096]{};
                DWORD read = 0;
                std::string out;
                while (ReadFile(hR, buf, sizeof(buf)-1, &read, nullptr) && read)
                    out.append(buf, read);
                WaitForSingleObject(pi.hProcess, 10000);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                std::istringstream ss(out);
                std::string line;
                while (std::getline(ss, line)) {
                    while (!line.empty() && (line.back()=='\r'||line.back()=='\n'||line.back()==' '))
                        line.pop_back();
                    if (!line.empty()) roots.push_back(widen(line));
                }
            }
            if (hW) CloseHandle(hW);
            CloseHandle(hR);
        }
    }
    for (auto env : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        wchar_t base[MAX_PATH]{};
        if (!GetEnvironmentVariableW(env, base, MAX_PATH)) continue;
        for (auto ed : {L"BuildTools", L"Community", L"Professional", L"Enterprise"})
            roots.push_back(fs::path(base) / "Microsoft Visual Studio" / "2022" / ed);
    }
    return roots;
}

Toolchain core::toolchainStatus() {
    Toolchain tc;
    for (auto& root : vsRoots()) {
        fs::path vc = root / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        if (fileExists(vc)) {
            tc.ok     = true;
            tc.vcvars = vc.wstring();
            // Get MSVC toolset version label
            fs::path msvcDir = root / "VC" / "Tools" / "MSVC";
            if (fs::exists(msvcDir)) {
                std::wstring best;
                for (auto& e : fs::directory_iterator(msvcDir))
                    if (e.is_directory() && e.path().filename().wstring() > best)
                        best = e.path().filename().wstring();
                if (!best.empty())
                    tc.detail = L"Ready  \u2013  MSVC " + best;
                else
                    tc.detail = L"Ready  \u2013  MSVC found";
            } else {
                tc.detail = L"Ready";
            }
            return tc;
        }
    }
    tc.ok     = false;
    tc.detail = L"Visual Studio 2022 Build Tools not found";
    return tc;
}

// ---------------------------------------------------------------------------
// getModsDir
// ---------------------------------------------------------------------------
fs::path core::getModsDir(const fs::path& gameDir) {
    fs::path ini = gameDir / kIniFile;
    std::wstring override = L"mods";
    if (fileExists(ini)) {
        std::wstring v = iniReadString(ini, L"files", L"modOverrideDirectory", L"\\mods");
        // strip leading slashes/backslashes
        size_t s = v.find_first_not_of(L"/\\");
        if (s != std::wstring::npos) override = v.substr(s);
        else override = L"mods";
        std::replace(override.begin(), override.end(), L'/', L'\\');
    }
    return gameDir / override;
}

// ---------------------------------------------------------------------------
// inspectGame
// ---------------------------------------------------------------------------
GameStatus core::inspectGame(const fs::path& gameDir) {
    GameStatus st;
    st.gameDir = gameDir;

    if (gameDir.empty() || !fs::exists(gameDir)) {
        st.problem = L"Folder does not exist.";
        return st;
    }
    fs::path exe = gameDir / kExeName;
    if (!fileExists(exe)) {
        st.problem = L"No sekiro.exe found here.  Select the folder that contains it.";
        return st;
    }
    st.exe     = exe;
    st.exeSize = fileSize(exe);
    if (st.exeSize == kSize102)  { st.version = L"1.02";  st.versionOk = true; }
    else if (st.exeSize == kSize102u){ st.version = L"1.02 (unpacked)"; st.versionOk = true; }
    else if (st.exeSize == kSize103) { st.version = L"1.03";  st.versionOk = true; }
    else { st.version = L"1.06+"; st.versionOk = false; }

    fs::path dll = gameDir / kInstalledDll;
    if (fileExists(dll)) {
        st.dll       = dll;
        st.dllSize   = fileSize(dll);
        st.dllPatched = st.dllSize < 400'000;
        st.dllStale   = st.dllSize > 500'000;
        if (st.dllStale)
            st.notes.push_back(std::wstring(kInstalledDll) +
                L" looks like unmodified 0.1.16 \u2013 it loads nothing on 1.06+.");
    } else {
        st.problem = std::wstring(kInstalledDll) + L" not installed yet.";
    }

    fs::path bak = gameDir / (std::wstring(kInstalledDll) + kBackupSuffix);
    if (fileExists(bak)) st.backup = bak;

    fs::path ini = gameDir / kIniFile;
    st.iniOk = fileExists(ini);
    if (!st.iniOk) {
        st.notes.push_back(L"No modengine.ini \u2013 one will be created on Install.");
    } else {
        if (!iniReadBool(ini, L"files", L"useModOverrideDirectory", true))
            st.notes.push_back(L"modengine.ini: useModOverrideDirectory=0 \u2013 mods won\u2019t load.");
    }

    fs::path mods = getModsDir(gameDir);
    if (fs::exists(mods)) {
        int count = 0;
        for (auto& e : fs::recursive_directory_iterator(mods, fs::directory_options::skip_permission_denied)) {
            if (!e.is_regular_file()) continue;
            auto ext = toLower(e.path().extension().wstring());
            if (ext == L".dcx" || ext == L".tpf") count++;
        }
        st.notes.push_back(std::to_wstring(count) + L" archive file(s) in " +
                           mods.filename().wstring() + L"\\");
    } else {
        st.notes.push_back(L"Mods folder missing: " + mods.wstring());
    }

    st.ok = fileExists(dll);
    return st;
}

// ---------------------------------------------------------------------------
// buildReport
// ---------------------------------------------------------------------------
static std::vector<std::wstring> scanAvailable(const fs::path& gameDir) {
    std::vector<std::wstring> out;
    fs::path mods = core::getModsDir(gameDir);
    if (!fs::exists(mods)) return out;
    for (auto& e : fs::recursive_directory_iterator(mods, fs::directory_options::skip_permission_denied)) {
        if (!e.is_regular_file()) continue;
        std::wstring ext = toLower(e.path().extension().wstring());
        if (ext == L".dcx" || ext == L".tpf") {
            out.push_back(fs::relative(e.path(), mods).wstring());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::wstring> Report::missing() const {
    std::vector<std::wstring> m;
    for (auto& a : available) {
        bool found = false;
        for (auto& s : served) if (s == a) { found = true; break; }
        if (!found) m.push_back(a);
    }
    return m;
}

Report core::buildReport(const fs::path& gameDir) {
    Report r;
    r.available = scanAvailable(gameDir);

    fs::path logPath = gameDir / kLoadLog;
    if (!fileExists(logPath)) {
        r.problem = L"No modengine_load.log yet.  Play the game once after installing.";
        return r;
    }

    fs::path mods = getModsDir(gameDir);
    // Read log
    std::wifstream f(logPath);
    f.imbue(std::locale(""));
    std::wstring line;
    while (std::getline(f, line)) {
        if (line.find(L"[OVERRIDE OK ]") != std::wstring::npos) {
            auto pos = line.find(L"[OVERRIDE OK ]");
            std::wstring path = line.substr(pos + 14);
            while (!path.empty() && (path.front()==L' '||path.front()==L'\t')) path.erase(0,1);
            fs::path p(path);
            std::wstring rel;
            try { rel = fs::relative(p, mods).wstring(); }
            catch (...) { rel = p.filename().wstring(); }
            r.served.push_back(rel);
        } else if (line.find(L"AOB scan hooking archive function") != std::wstring::npos
                   && r.hookLine.empty()) {
            r.hookLine = line;
        } else if (line.find(L"FATAL") != std::wstring::npos && r.problem.empty()) {
            r.problem = line;
        }
    }
    std::sort(r.served.begin(), r.served.end());

    if (r.hookLine.empty() && r.problem.empty())
        r.problem = L"Log has no hook line.  DLL loaded but hooks not installed.  See TROUBLESHOOTING.md.";
    return r;
}

// ---------------------------------------------------------------------------
// isGameRunning
// ---------------------------------------------------------------------------
bool core::isGameRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"sekiro.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// ---------------------------------------------------------------------------
// buildDll
// ---------------------------------------------------------------------------
static const wchar_t kDefaultIni[] =
    L"; modengine.ini  \u2013  generated by Mod Engine 1.6 Fixer\r\n"
    L"; See docs/ for a full explanation of every key.\r\n\r\n"
    L"[misc]\r\nskipLogos=1\r\nchainDInput8DLLPath=\"\"\r\n\r\n"
    L"[files]\r\nloadUXMFiles=0\r\nuseModOverrideDirectory=1\r\n"
    L"modOverrideDirectory=\\mods\r\ncacheFilePaths=0\r\n\r\n"
    L"[debug]\r\nshowDebugLog=1\r\nshowConsole=0\r\n";

BuildResult core::buildDll(std::function<void(const std::wstring&)> cb) {
    Toolchain tc = toolchainStatus();
    if (!tc.ok) {
        return {false,
            L"Visual Studio 2022 Build Tools not found.\r\n\r\n"
            L"Install with:\r\n"
            L"  winget install Microsoft.VisualStudio.2022.BuildTools "
            L"--override \"--quiet --wait --norestart "
            L"--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended\""};
    }

    fs::path srcDir  = repoRoot() / L"src";
    fs::path buildCmd = srcDir / L"build.cmd";
    if (!fileExists(buildCmd))
        return {false, L"src\\build.cmd not found at " + buildCmd.wstring()};

    // Build command: vcvars64 then build.cmd, all output captured
    std::wstring cmd = L"cmd.exe /C \"call \"" + tc.vcvars + L"\" >nul 2>&1 && call build.cmd\"";

    SECURITY_ATTRIBUTES sa{sizeof(sa)};
    sa.bInheritHandle = TRUE;
    HANDLE hR, hW;
    if (!CreatePipe(&hR, &hW, &sa, 0))
        return {false, L"Could not create output pipe."};
    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hW;
    si.hStdError  = hW;

    wchar_t cmdBuf[4096];
    wcscpy_s(cmdBuf, cmd.c_str());

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, srcDir.c_str(), &si, &pi)) {
        CloseHandle(hW); CloseHandle(hR);
        return {false, L"CreateProcess failed."};
    }
    CloseHandle(hW);

    std::wstring log;
    char buf[2048]{};
    DWORD read = 0;
    std::string accum;
    while (ReadFile(hR, buf, sizeof(buf)-1, &read, nullptr) && read) {
        buf[read] = '\0';
        accum += buf;
        // Extract lines and stream to callback
        size_t pos;
        while ((pos = accum.find('\n')) != std::string::npos) {
            std::string rawLine = accum.substr(0, pos);
            if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
            accum = accum.substr(pos + 1);
            std::wstring wline = widen(rawLine);
            log += wline + L"\r\n";
            if (cb) cb(wline);
        }
    }
    CloseHandle(hR);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    fs::path built = srcDir / L"build" / kBuiltDll;
    bool ok = (exitCode == 0) && fileExists(built);
    return {ok, log, ok ? built : fs::path{}};
}

// ---------------------------------------------------------------------------
// installDll
// ---------------------------------------------------------------------------
InstallResult core::installDll(const fs::path& gameDir, const fs::path& dllPathIn) {
    fs::path dllPath = dllPathIn.empty()
        ? (repoRoot() / L"src" / L"build" / kBuiltDll)
        : dllPathIn;

    if (!fileExists(dllPath))
        return {false, L"DLL not built yet.  Press Build DLL first."};

    if (!fs::exists(gameDir)) {
        std::error_code ec;
        fs::create_directories(gameDir, ec);
    }

    fs::path target = gameDir / kInstalledDll;
    fs::path backup = gameDir / (std::wstring(kInstalledDll) + kBackupSuffix);
    std::vector<std::wstring> msgs;

    if (fileExists(target) && !fileExists(backup)) {
        if (!CopyFileW(target.c_str(), backup.c_str(), FALSE))
            return {false, L"Could not back up existing DLL."};
        msgs.push_back(L"Backed up original  \u2192  " + backup.filename().wstring());
    } else if (fileExists(target)) {
        msgs.push_back(L"Backup already exists, left untouched.");
    }

    if (!CopyFileW(dllPath.c_str(), target.c_str(), FALSE))
        return {false, L"Could not copy DLL to game folder.  Try Run as Administrator."};

    msgs.push_back(L"Installed " + std::wstring(kInstalledDll) +
                   L"  (" + std::to_wstring(fileSize(target)) + L" bytes)");

    fs::path ini = gameDir / kIniFile;
    if (!fileExists(ini)) {
        HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            // Write UTF-8 without BOM
            std::string utf8;
            int n = WideCharToMultiByte(CP_UTF8, 0, kDefaultIni, -1,
                                        nullptr, 0, nullptr, nullptr);
            utf8.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, kDefaultIni, -1,
                                utf8.data(), n, nullptr, nullptr);
            DWORD written;
            WriteFile(h, utf8.c_str(), (DWORD)utf8.size()-1, &written, nullptr);
            CloseHandle(h);
            msgs.push_back(L"Created modengine.ini");
        }
    }

    fs::path mods = getModsDir(gameDir);
    if (!fs::exists(mods)) {
        std::error_code ec;
        fs::create_directories(mods, ec);
        if (!ec) msgs.push_back(L"Created " + mods.filename().wstring() + L"\\ folder");
    }

    std::wstring msg;
    for (auto& m : msgs) msg += m + L"\r\n";
    return {true, msg, fileExists(backup) ? backup : fs::path{}};
}

// ---------------------------------------------------------------------------
// launchGame
// ---------------------------------------------------------------------------
bool core::launchGame(const fs::path& gameDir) {
    fs::path exe = gameDir / kExeName;
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask       = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb      = L"open";
    sei.lpFile      = exe.c_str();
    sei.lpDirectory = gameDir.c_str();
    sei.nShow       = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

// ---------------------------------------------------------------------------
// detectGameDir  (Steam registry)
// ---------------------------------------------------------------------------
fs::path core::detectGameDir() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion"
        L"\\Uninstall\\Steam App 814380",
        0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH]{};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hk, L"InstallLocation", nullptr, nullptr,
                             (BYTE*)buf, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hk);
            fs::path p(buf);
            if (fs::exists(p / L"sekiro.exe")) return p;
        }
        RegCloseKey(hk);
    }
    return {};
}
