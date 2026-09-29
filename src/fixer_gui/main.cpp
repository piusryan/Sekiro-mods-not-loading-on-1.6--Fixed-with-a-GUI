// ModEngineFixer – main.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <sstream>
#include <filesystem>
#include "core.h"
#include "gfx.h"
#include "resource.h"

#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"comdlg32.lib")

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr wchar_t kClass[]  = L"SekiroModEngineFixer_v2";
static constexpr wchar_t kTitle[]  = L"Mod Engine 1.6 Fixer";
static constexpr int  kDefaultW    = 1040;
static constexpr int  kDefaultH    = 700;
static constexpr int  kMinW        = 820;
static constexpr int  kMinH        = 540;

// Design canvas – layout is authored at these dimensions.
// A single uniform scale (dpi/96 × uiScale) maps them to pixels.
static constexpr float kDW         = 1040.f;   // design width
static constexpr float kDH         = 700.f;    // design height

// Layout metrics (design units)
static constexpr float kPad        = 16.f;
static constexpr float kGap        = 8.f;
static constexpr float kBtnH       = 30.f;   // action buttons (smaller)
static constexpr float kCardH      = 68.f;
static constexpr float kRowH       = 28.f;   // folder row height (smaller)
static constexpr float kLogLineH   = 15.f;
static constexpr size_t kMaxLog    = 2000;

// Folder row: path well takes a fixed fraction, buttons use fixed widths
static constexpr float kBrowseW    = 72.f;   // smaller browse button
static constexpr float kRefreshW   = 64.f;   // smaller refresh button
static constexpr float kBtnGap     = 6.f;
// Path well right edge: leave room for browse + refresh + gaps
static constexpr float kWellRight  = kDW - kPad - kBrowseW - kBtnGap - kRefreshW - kBtnGap;

// ---------------------------------------------------------------------------
// Button IDs
// ---------------------------------------------------------------------------
enum BtnId { kNone=0, kBrowse, kRefresh, kBuild, kInstall, kPlay, kClear };

// ---------------------------------------------------------------------------
// Log colour
// ---------------------------------------------------------------------------
static D2D1_COLOR_F logColour(const std::wstring& s) {
    if (!s.empty() && (s[0]==L'>'||s[0]==L'#')) return pal::kGold;
    std::wstring lo = s;
    for (auto& c : lo) c = towlower(c);
    if (lo.find(L"error")  !=std::wstring::npos ||
        lo.find(L"failed") !=std::wstring::npos ||
        lo.find(L"fatal")  !=std::wstring::npos) return pal::kRust;
    if (lo.find(L" ok")    !=std::wstring::npos ||
        lo.find(L"success")!=std::wstring::npos ||
        lo.find(L"install")!=std::wstring::npos) return pal::kJade;
    if (lo.find(L"warn")   !=std::wstring::npos) return {0.9f,0.7f,0.2f,1.f};
    return pal::kParchment;
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct App {
    HWND  hwnd = nullptr;
    Gfx   gfx;

    fs::path         gameDir;
    core::GameStatus status;
    core::Report     report;
    core::Toolchain  toolchain;

    std::vector<Btn>     buttons;
    std::vector<Card>    cards;
    D2D1_RECT_F          logRect{};
    D2D1_RECT_F          pathWell{};
    int                  logScroll = 0;
    int                  hotId     = kNone;
    int                  downId    = kNone;
    bool                 busy      = false;
    bool                 haveGame  = false;

    std::vector<LogLine> logLines;
    std::mutex           logMtx;

    std::wstring pathText;
    bool         pathFocus = false;
    int          caretPos  = 0;
    bool         caretVis  = true;

    // Ctrl+Scroll zoom (0.5 – 1.5). Whole canvas scaled uniformly.
    float uiScale = 1.0f;
};

static App* g_app = nullptr;

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------
static float dipFactor() {
    return (float)GetDpiForWindow(g_app->hwnd) / 96.f;
}
static float clientDipW() {
    RECT r{}; GetClientRect(g_app->hwnd, &r);
    return (r.right - r.left) / dipFactor();
}
static float clientDipH() {
    RECT r{}; GetClientRect(g_app->hwnd, &r);
    return (r.bottom - r.top) / dipFactor();
}
// raw pixel → design-space (accounts for DPI + zoom)
static void toDesign(float px, float py, float& dx, float& dy) {
    float t = dipFactor() * g_app->uiScale;
    dx = px / t;  dy = py / t;
}

// ---------------------------------------------------------------------------
// Log helpers
// ---------------------------------------------------------------------------
static void logPush(App& a, const std::wstring& text) {
    std::lock_guard<std::mutex> lk(a.logMtx);
    a.logLines.push_back({text, logColour(text)});
    if (a.logLines.size() > kMaxLog)
        a.logLines.erase(a.logLines.begin(),
                         a.logLines.begin() + (a.logLines.size() - kMaxLog));
    a.logScroll = 0;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static void doRefresh() {
    App& a = *g_app;
    a.status    = core::inspectGame(a.gameDir);
    a.report    = core::buildReport(a.gameDir);
    a.toolchain = core::toolchainStatus();
    a.haveGame  = !a.gameDir.empty() && a.status.exeSize > 0;
    InvalidateRect(a.hwnd, nullptr, FALSE);
}

static void doBrowse() {
    App& a = *g_app;
    BROWSEINFOW bi{};
    bi.hwndOwner = a.hwnd;
    bi.lpszTitle = L"Select the Sekiro folder (containing sekiro.exe)";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(pidl, path)) {
        a.gameDir  = path;
        a.pathText = path;
        a.caretPos = (int)a.pathText.size();
        logPush(a, L">> Folder: " + a.gameDir.wstring());
    }
    CoTaskMemFree(pidl);
    doRefresh();
}

static void doBuild() {
    App& a = *g_app;
    if (a.busy) return;
    a.busy = true;
    InvalidateRect(a.hwnd, nullptr, FALSE);
    logPush(a, L">> Building dinput8_patched.dll \u2026");
    std::thread([&a]() {
        auto r = core::buildDll([&a](const std::wstring& line){ logPush(a, line); });
        logPush(a, r.ok ? L">> Build OK  \u2192  " + r.dllPath.wstring()
                        : L">> Build FAILED");
        a.busy = false;
        PostMessageW(a.hwnd, WM_USER+1, 0, 0);
    }).detach();
}

static void doInstall() {
    App& a = *g_app;
    if (a.busy || a.gameDir.empty()) return;
    a.busy = true;
    InvalidateRect(a.hwnd, nullptr, FALSE);
    logPush(a, L">> Installing \u2026");
    auto r = core::installDll(a.gameDir);
    {
        std::wistringstream ss(r.message);
        std::wstring tok;
        while (std::getline(ss, tok)) {
            while (!tok.empty() && (tok.back()==L'\r'||tok.back()==L'\n')) tok.pop_back();
            if (!tok.empty()) logPush(a, L"  " + tok);
        }
    }
    if (r.ok)
        MessageBoxW(a.hwnd, (L"Install complete.\r\n\r\n" + r.message).c_str(),
                    kTitle, MB_OK|MB_ICONINFORMATION);
    else
        MessageBoxW(a.hwnd, r.message.c_str(), kTitle, MB_OK|MB_ICONERROR);
    a.busy = false;
    doRefresh();
}

static void doPlay() {
    App& a = *g_app;
    if (a.gameDir.empty()) return;
    if (!core::launchGame(a.gameDir))
        MessageBoxW(a.hwnd,
            L"Could not launch sekiro.exe.\nTry Run as Administrator.",
            kTitle, MB_OK|MB_ICONERROR);
    else
        logPush(a, L">> Launched sekiro.exe");
    doRefresh();
}

static void activate(int id) {
    switch (id) {
        case kBrowse:  doBrowse(); break;
        case kRefresh: doRefresh(); logPush(*g_app, L">> Refreshed"); break;
        case kBuild:   doBuild();   break;
        case kInstall: doInstall(); break;
        case kPlay:    doPlay();    break;
        case kClear: {
            std::lock_guard<std::mutex> lk(g_app->logMtx);
            g_app->logLines.clear(); g_app->logScroll = 0;
            InvalidateRect(g_app->hwnd, nullptr, FALSE);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Btn factory (non-aggregate because of std::wstring member)
// ---------------------------------------------------------------------------
static Btn makeBtn(int id, D2D1_RECT_F r, const wchar_t* lbl,
                   BtnStyle sty, bool en, bool hot, bool dn) {
    Btn b; b.id=id; b.rect=r; b.label=lbl;
    b.style=sty; b.enabled=en; b.hot=hot; b.down=dn;
    return b;
}

// ---------------------------------------------------------------------------
// Hit testing (raw pixel coords in → design coords used internally)
// ---------------------------------------------------------------------------
static int hitBtn(const App& a, float px, float py) {
    float dx, dy; toDesign(px, py, dx, dy);
    for (auto& b : a.buttons)
        if (b.enabled && dx>=b.rect.left && dx<=b.rect.right &&
                         dy>=b.rect.top  && dy<=b.rect.bottom)
            return b.id;
    return kNone;
}
static bool inWell(const App& a, float px, float py) {
    float dx, dy; toDesign(px, py, dx, dy);
    return dx>=a.pathWell.left && dx<=a.pathWell.right &&
           dy>=a.pathWell.top  && dy<=a.pathWell.bottom;
}
static bool inLog(const App& a, float px, float py) {
    float dx, dy; toDesign(px, py, dx, dy);
    return dx>=a.logRect.left && dx<=a.logRect.right &&
           dy>=a.logRect.top  && dy<=a.logRect.bottom;
}

// ---------------------------------------------------------------------------
// Render  (called every WM_PAINT)
// ---------------------------------------------------------------------------
static void render(App& a) {
    Gfx& g = a.gfx;
    if (!g.rt) return;

    const float cW = clientDipW();   // real client width in DIPs
    const float cH = clientDipH();   // real client height in DIPs
    const float k  = dipFactor();    // DPI scale
    const float S  = a.uiScale;      // user zoom
    // Combined transform: design units → pixels
    const float tot = k * S;

    g.rt->BeginDraw();

    // ── Step 1: draw background at full client size (DPI scale only) ──
    g.rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k));
    if (g.bgBitmap) {
        g.rt->DrawBitmap(g.bgBitmap.Get(), D2D1_RECT_F{0,0,cW,cH}, 1.f,
                         D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        fillRect(g, {0,0,cW,cH}, pal::kVeil);
    } else {
        fillRadial(g, {0,0,cW,cH}, {cW*0.2f,cH*0.05f}, cW*1.1f, cH*1.2f,
                   pal::kBackdropTop, pal::kBackdropBot);
    }
    // Atmospheric glows
    fillRadial(g, {0,0,cW,cH}, {0,cH*0.4f}, cW*0.7f, cH,
               {pal::kCrimson.r,pal::kCrimson.g,pal::kCrimson.b,0.18f},{0,0,0,0});
    fillRadial(g, {0,0,cW,cH}, {cW*0.75f,cH*1.05f}, cW*0.55f, cH*0.55f,
               {pal::kGold.r,pal::kGold.g,pal::kGold.b,0.06f},{0,0,0,0});

    // ── Step 2: draw all UI widgets at design scale (DPI × zoom) ──────
    g.rt->SetTransform(D2D1::Matrix3x2F::Scale(tot, tot));

    a.buttons.clear();
    a.cards.clear();

    // Design canvas dimensions
    const float W = kDW;
    const float H = kDH;
    float y = kPad;

    // ── Header ──────────────────────────────────────────────────────────
    drawText(g, g.fmt.title.Get(),
             {kPad, y, W-kPad, y+26.f}, L"MOD ENGINE 1.6 FIXER", pal::kParchment);
    drawText(g, g.fmt.subtitle.Get(),
             {kPad+2, y+27.f, W-kPad, y+40.f},
             L"Resurrecting Fromashura for Sekiro: Shadows Die Twice 1.06",
             pal::kMuted);
    // Zoom % hint top-right
    {   wchar_t hint[32];
        swprintf_s(hint, L"Ctrl+Scroll  %d%%", (int)(S*100.f));
        drawText(g, g.fmt.body.Get(),
                 {W-110.f, y+2.f, W-kPad, y+14.f}, hint, pal::kDim); }
    y += 46.f;

    // Divider
    fillGradientH(g, {kPad, y, W-kPad, y+1.5f}, pal::kGold, pal::kCrimson);
    y += 7.f;

    // ── Game folder row ─────────────────────────────────────────────────
    drawText(g, g.fmt.label.Get(),
             {kPad, y, W-kPad, y+11.f}, L"GAME FOLDER", pal::kDim);
    y += 12.f;

    // Browse / Refresh buttons – fixed width, right-aligned
    const float rowR    = W - kPad;
    const float browseX = rowR - kBrowseW;
    const float refX    = browseX - kBtnGap - kRefreshW;
    const float wellL   = kPad;
    const float wellR   = refX - kBtnGap;   // path well ends here (shortened)

    a.pathWell = {wellL, y, wellR, y+kRowH};
    fillRounded(g, a.pathWell, pal::kBgWell, 4.f);
    strokeRounded(g, a.pathWell,
                  a.pathFocus ? pal::kGold : pal::kBorder, 4.f, 1.f);

    // Path text with mid-ellipsis
    const std::wstring dispPath =
        a.pathText.empty() ? L"(no folder \u2013 press Browse)" : a.pathText;
    drawPathText(g, g.fmt.mono.Get(),
                 {wellL+8.f, y+2.f, wellR-6.f, y+kRowH-2.f},
                 dispPath, a.pathText.empty() ? pal::kDim : pal::kParchment);

    // Caret
    if (a.pathFocus && a.caretVis && !a.pathText.empty()) {
        std::wstring before = a.pathText.substr(0,
            std::min((size_t)a.caretPos, a.pathText.size()));
        float cx = wellL + 8.f + measureW(g, g.fmt.mono.Get(), before);
        cx = std::min(cx, wellR - 6.f);
        fillRect(g, {cx, y+4.f, cx+1.5f, y+kRowH-4.f}, pal::kParchment);
    }

    a.buttons.push_back(makeBtn(kBrowse,
        {browseX, y, browseX+kBrowseW, y+kRowH},
        L"Browse", BtnStyle::Primary, true,
        a.hotId==kBrowse, a.downId==kBrowse));
    a.buttons.push_back(makeBtn(kRefresh,
        {refX, y, refX+kRefreshW, y+kRowH},
        L"Refresh", BtnStyle::Primary, true,
        a.hotId==kRefresh, a.downId==kRefresh));
    for (auto& b : a.buttons) drawBtn(g, b);  // draw now, keep in list for hit-test
    y += kRowH + kGap;

    // ── Status cards ────────────────────────────────────────────────────
    core::GameStatus& st = a.status;
    core::Report&     rp = a.report;

    auto szStr = [](uint64_t v) {
        std::wstring s = std::to_wstring(v);
        for (int i=(int)s.size()-3; i>0; i-=3) s.insert((size_t)i, L",");
        return s;
    };

    std::wstring bldV = a.gameDir.empty() ? L"(select folder)"
                      : st.versionOk      ? st.version
                      : st.version.empty()? L"not found" : st.version;
    CardColour   bldC = a.gameDir.empty()  ? CardColour::Dim
                      : st.versionOk       ? CardColour::Green
                      : st.version.empty() ? CardColour::Dim : CardColour::Red;

    std::wstring dllV; CardColour dllC;
    if (st.dll.empty())     {dllV=L"not installed";                       dllC=CardColour::Red;}
    else if(st.dllStale)    {dllV=L"stale 0.1.16";                       dllC=CardColour::Gold;}
    else if(st.dllPatched)  {dllV=L"patched  "+szStr(st.dllSize)+L" B";  dllC=CardColour::Green;}
    else                    {dllV=L"present  "+szStr(st.dllSize)+L" B";   dllC=CardColour::Gold;}

    size_t nA = rp.available.size();
    std::wstring modV = nA ? std::to_wstring(nA)+L" archive"+(nA==1?L"":L"s") : L"none found";
    CardColour   modC = nA ? CardColour::Gold : CardColour::Dim;
    std::wstring ldV  = rp.ok() ? L"hooked" : L"not hooked";
    CardColour   ldC  = rp.ok() ? CardColour::Green : CardColour::Red;

    struct CS { const wchar_t* t; std::wstring v; CardColour c; };
    CS specs[4] = {
        {L"GAME BUILD",   bldV, bldC},
        {L"DINPUT8.DLL",  dllV, dllC},
        {L"MOD ARCHIVES", modV, modC},
        {L"LOADER",       ldV,  ldC},
    };
    const float cW2 = (W - kPad*2.f - kGap*3.f) / 4.f;
    for (int i=0;i<4;i++) {
        float cx = kPad + i*(cW2+kGap);
        Card card;
        card.rect={cx,y,cx+cW2,y+kCardH};
        card.title=specs[i].t; card.value=specs[i].v; card.colour=specs[i].c;
        a.cards.push_back(card);
        drawCard(g, card);
    }
    y += kCardH + kGap;

    // ── Status line ──────────────────────────────────────────────────────
    std::wstring statusTxt;
    if (!st.problem.empty() && st.problem.find(core::kInstalledDll)==std::wstring::npos)
        statusTxt += st.problem;
    if (rp.ok() && nA) {
        if (!statusTxt.empty()) statusTxt += L"  \u2022  ";
        statusTxt += L"Mod Engine hooked \u2013 "+std::to_wstring(rp.served.size())+L" of "+std::to_wstring(nA)+L" served";
        auto mis=rp.missing(); if(!mis.empty()) statusTxt+=L", "+std::to_wstring(mis.size())+L" not loaded";
    } else if (!rp.ok() && !rp.problem.empty()) {
        if (!statusTxt.empty()) statusTxt += L"  \u2022  ";
        statusTxt += rp.problem;
    }
    for (auto& n:st.notes){if(!statusTxt.empty())statusTxt+=L"  \u2022  ";statusTxt+=n;}
    if (!statusTxt.empty()) {
        D2D1_COLOR_F sc=(st.problem.empty()&&rp.ok())?pal::kJade:pal::kRust;
        drawText(g, g.fmt.body.Get(), {kPad,y,W-kPad,y+13.f}, statusTxt, sc);
    }
    y += 15.f;

    // ── Action buttons (3 equal, smaller) ───────────────────────────────
    const float actW = (W - kPad*2.f - kGap*2.f) / 3.f;
    struct AS { int id; const wchar_t* lbl; BtnStyle sty; bool en; };
    AS acts[3] = {
        {kBuild,   L"Build DLL",          BtnStyle::Primary, a.toolchain.ok&&!a.busy},
        {kInstall, L"Install / Repair",   BtnStyle::Primary, a.haveGame&&!a.busy},
        {kPlay,    L"\u25B6  Play Sekiro", BtnStyle::Gold,   a.haveGame},
    };
    for (int i=0;i<3;i++) {
        float ax = kPad + i*(actW+kGap);
        Btn b; b.id=acts[i].id;
        b.rect={ax,y,ax+actW,y+kBtnH};
        b.label=acts[i].lbl; b.style=acts[i].sty; b.enabled=acts[i].en;
        b.hot=(a.hotId==acts[i].id); b.down=(a.downId==acts[i].id);
        a.buttons.push_back(b);
        drawBtn(g, b);
    }
    y += kBtnH + kGap;

    // ── Log label + Clear button ─────────────────────────────────────────
    drawText(g, g.fmt.label.Get(), {kPad,y,kPad+30.f,y+11.f}, L"LOG", pal::kDim);
    {
        Btn clr; clr.id=kClear;
        clr.rect={W-kPad-60.f, y, W-kPad, y+18.f};
        clr.label=L"Clear"; clr.style=BtnStyle::Ghost;
        clr.enabled=true; clr.hot=(a.hotId==kClear); clr.down=(a.downId==kClear);
        a.buttons.push_back(clr);
        drawBtn(g, clr);
    }
    y += 22.f;

    // ── Log area ─────────────────────────────────────────────────────────
    const float logBot = H - kPad - 16.f;
    a.logRect = {kPad, y, W-kPad, logBot};
    {
        std::lock_guard<std::mutex> lk(a.logMtx);
        drawLogArea(g, a.logRect, a.logLines, a.logScroll, kLogLineH);
    }

    // ── Footer ────────────────────────────────────────────────────────────
    fillGradientH(g, {kPad, logBot+2.f, W-kPad, logBot+3.f},
                  pal::kGoldDark, pal::kCrimson);
    std::wstring foot = a.toolchain.ok
        ? L"Toolchain:  " + a.toolchain.detail
        : L"\u26A0  "     + a.toolchain.detail;
    drawText(g, g.fmt.body.Get(), {kPad, logBot+5.f, W-kPad, H}, foot,
             a.toolchain.ok ? pal::kMuted : pal::kRust);

    HRESULT hr = g.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        gfxDestroy(g); gfxCreate(a.hwnd, g);
        gfxLoadBackground(g, GetModuleHandleW(nullptr), IDB_BACKGROUND);
        fmtCreate(g, g.fmt);
    }
}

// ---------------------------------------------------------------------------
// Caret blink timer
// ---------------------------------------------------------------------------
static void CALLBACK caretBlink(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_app) return;
    g_app->caretVis = !g_app->caretVis;
    InvalidateRect(g_app->hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App& a = *g_app;
    switch (msg) {

    case WM_CREATE:
        { BOOL dark=TRUE; DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark)); }
        SetTimer(hwnd, 1, 530, caretBlink);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        gfxDestroy(a.gfx);
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        if (a.gfx.rt) {
            UINT w=LOWORD(lParam), h=HIWORD(lParam);
            gfxResize(a.gfx, w, h);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED:
        a.gfx.dpi   = HIWORD(wParam);
        a.gfx.scale = a.gfx.dpi / 96.f;
        { RECT* r=(RECT*)lParam;
          SetWindowPos(hwnd,nullptr,r->left,r->top,
                       r->right-r->left,r->bottom-r->top,
                       SWP_NOZORDER|SWP_NOACTIVATE); }
        fmtCreate(a.gfx, a.gfx.fmt);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_GETMINMAXINFO:
        { auto* mm=(MINMAXINFO*)lParam; float s=dipFactor();
          mm->ptMinTrackSize.x=(LONG)(kMinW*s);
          mm->ptMinTrackSize.y=(LONG)(kMinH*s); }
        return 0;

    case WM_PAINT:
        { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); render(a); EndPaint(hwnd,&ps); }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_USER+1:
        doRefresh();
        return 0;

    // ── Mouse ────────────────────────────────────────────────────────────
    case WM_MOUSEMOVE: {
        float px=(float)GET_X_LPARAM(lParam), py=(float)GET_Y_LPARAM(lParam);
        int nh = hitBtn(a, px, py);
        if (nh != a.hotId) { a.hotId=nh; InvalidateRect(hwnd,nullptr,FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        a.hotId=a.downId=kNone;
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        float px=(float)GET_X_LPARAM(lParam), py=(float)GET_Y_LPARAM(lParam);
        a.downId = hitBtn(a, px, py);
        bool wc = inWell(a, px, py);
        if (wc != a.pathFocus) {
            a.pathFocus=wc; a.caretVis=true;
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        float px=(float)GET_X_LPARAM(lParam), py=(float)GET_Y_LPARAM(lParam);
        int up = hitBtn(a, px, py);
        if (up!=kNone && up==a.downId) activate(up);
        a.downId=kNone;
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }

    // Ctrl+Scroll = zoom layout; plain scroll over log = scroll log
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            a.uiScale = std::clamp(a.uiScale + delta*0.05f, 0.5f, 1.5f);
            InvalidateRect(hwnd,nullptr,FALSE);
        } else {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd,&pt);
            if (inLog(a,(float)pt.x,(float)pt.y)) {
                a.logScroll = std::max(0, a.logScroll - delta*3);
                InvalidateRect(hwnd,nullptr,FALSE);
            }
        }
        return 0;
    }

    // ── Keyboard (path well) ─────────────────────────────────────────────
    case WM_CHAR: {
        if (!a.pathFocus) break;
        wchar_t ch=(wchar_t)wParam;
        if (ch==L'\r'||ch==L'\n') {
            a.gameDir=a.pathText; a.pathFocus=false; doRefresh();
        } else if (ch==L'\b') {
            if (a.caretPos>0){a.pathText.erase((size_t)a.caretPos-1,1);a.caretPos--;}
        } else if (ch>=32) {
            a.pathText.insert((size_t)a.caretPos,1,ch); a.caretPos++;
        }
        a.caretVis=true; InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        if (!a.pathFocus) break;
        switch(wParam) {
        case VK_DELETE:
            if(a.caretPos<(int)a.pathText.size())a.pathText.erase((size_t)a.caretPos,1); break;
        case VK_LEFT:  if(a.caretPos>0)a.caretPos--; break;
        case VK_RIGHT: if(a.caretPos<(int)a.pathText.size())a.caretPos++; break;
        case VK_HOME:  a.caretPos=0; break;
        case VK_END:   a.caretPos=(int)a.pathText.size(); break;
        case VK_ESCAPE: a.pathFocus=false; break;
        case 'V':
            if (GetKeyState(VK_CONTROL)&0x8000) {
                if (OpenClipboard(hwnd)) {
                    HANDLE hd=GetClipboardData(CF_UNICODETEXT);
                    if(hd){wchar_t*t=(wchar_t*)GlobalLock(hd);
                        if(t){std::wstring p(t);a.pathText.insert((size_t)a.caretPos,p);
                              a.caretPos+=(int)p.size();GlobalUnlock(hd);}}
                    CloseClipboard();
                }
            }
            break;
        case 'A':
            if(GetKeyState(VK_CONTROL)&0x8000)a.caretPos=(int)a.pathText.size(); break;
        }
        a.caretVis=true; InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }

    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd,&pt);
        float px=(float)pt.x, py=(float)pt.y;
        if (inWell(a,px,py)){SetCursor(LoadCursorW(nullptr,IDC_IBEAM));return TRUE;}
        if (hitBtn(a,px,py)!=kNone){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&ic);

    App app; g_app = &app;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style         = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm       = wc.hIcon;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr,
            L"Failed to register window class.\nThe application cannot start.",
            kTitle, MB_OK|MB_ICONERROR);
        return 1;
    }

    float s  = (float)GetDpiForSystem()/96.f;
    int wW=(int)(kDefaultW*s), wH=(int)(kDefaultH*s);
    int wX=(GetSystemMetrics(SM_CXSCREEN)-wW)/2;
    int wY=(GetSystemMetrics(SM_CYSCREEN)-wH)/2;

    HWND hwnd = CreateWindowExW(0, kClass, kTitle, WS_OVERLAPPEDWINDOW,
                                wX, wY, wW, wH,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr,
            L"Failed to create window.\nThe application cannot start.",
            kTitle, MB_OK|MB_ICONERROR);
        return 1;
    }
    app.hwnd = hwnd;

    if (!gfxCreate(hwnd, app.gfx)) {
        MessageBoxW(hwnd,
            L"Failed to initialise Direct2D.\n"
            L"Update your graphics drivers and try again.",
            kTitle, MB_OK|MB_ICONERROR);
        return 1;
    }
    gfxLoadBackground(app.gfx, hInst, IDB_BACKGROUND);

    app.gameDir  = core::detectGameDir();
    app.pathText = app.gameDir.empty() ? L"" : app.gameDir.wstring();
    app.caretPos = (int)app.pathText.size();

    app.toolchain = core::toolchainStatus();
    doRefresh();

    logPush(app, L"# Mod Engine 1.6 Fixer  \u2013  ready");
    if (!app.gameDir.empty())
        logPush(app, L"# Auto-detected:  " + app.gameDir.wstring());
    else
        logPush(app, L"# Select the folder containing sekiro.exe then press Install.");

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
