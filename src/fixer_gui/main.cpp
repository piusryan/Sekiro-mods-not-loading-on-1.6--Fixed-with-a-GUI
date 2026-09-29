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
// Minimum window size in DESIGN UNITS (multiplied by the window DPI). Kept just
// above what the fixed part of the layout actually needs, so the window can
// always be shrunk to fit even a small screen at 200% scaling: below this the
// log would have nowhere left to go.
static constexpr int  kMinW        = 720;
static constexpr int  kMinH        = 430;

// Design canvas. Layout is authored in design units, and one design unit is
// exactly one DIP: the Direct2D render target already converts DIPs to pixels
// using the window's DPI, so nothing else is allowed to re-apply that factor.
// Only the user zoom (Ctrl+Scroll) is layered on top of the design units, and
// the content block is centred horizontally in the client area.
static constexpr float kDW         = 1040.f;   // reference / maximum content width
static constexpr float kDH         = 700.f;    // reference content height
static constexpr float kMaxContentW = kDW;     // wider windows get margin, not stretch

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
// ---------------------------------------------------------------------------
// Canvas geometry
//
// The render target's DPI already turns DIPs into pixels, so the widget pass
// only has to apply the user zoom and the centring offset:
//
//     pixelX = (designX * uiScale + x0 * uiScale) * dpi/96
//
// W and H are derived from the real client area, so the layout can never be
// cropped – at any window size, DPI or zoom level. Extra width becomes margin
// (the block stays centred and readable) and extra height goes to the log.
// ---------------------------------------------------------------------------
struct Canvas { float W; float H; float x0; };

static Canvas canvasMetrics() {
    const float S     = std::max(0.1f, g_app->uiScale);
    const float avail = std::max(320.f, clientDipW() / S);   // design units
    Canvas c;
    c.W  = std::min(avail, kMaxContentW);
    c.x0 = (avail - c.W) * 0.5f;                             // centre the block
    c.H  = std::max(300.f, clientDipH() / S);
    return c;
}

// raw pixel → design-space (undoes DPI, zoom and the centring offset)
static void toDesign(float px, float py, float& dx, float& dy) {
    float t = dipFactor() * std::max(0.1f, g_app->uiScale);
    dx = px / t - canvasMetrics().x0;
    dy = py / t;
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
    const float S  = a.uiScale;      // user zoom (only zoom factor we apply)
    const Canvas C = canvasMetrics();
    const float W  = C.W;            // content width, centred in the client
    const float H  = C.H;            // content height (fills the client)

    g.rt->BeginDraw();

    // ── Step 1: backdrop, drawn in DIPs with the identity transform ────
    // The render target's DPI does the DIP→pixel conversion, so this fills the
    // whole client at any scaling factor. The wallpaper is fitted uniformly and
    // centred, and is deliberately not affected by the UI zoom.
    g.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    gfxDrawBackdrop(g, {0,0,cW,cH}, pal::kVeil);

    // Atmospheric glows (crimson from the left, ember gold from lower right)
    fillRadial(g, {0,0,cW,cH}, {0,cH*0.4f}, cW*0.7f, cH,
               {pal::kCrimson.r,pal::kCrimson.g,pal::kCrimson.b,0.18f},{0,0,0,0});
    fillRadial(g, {0,0,cW,cH}, {cW*0.75f,cH*1.05f}, cW*0.55f, cH*0.55f,
               {pal::kGold.r,pal::kGold.g,pal::kGold.b,0.06f},{0,0,0,0});

    // ── Step 2: widget canvas (zoom + horizontal centring only) ────────
    const D2D1::Matrix3x2F base =
        D2D1::Matrix3x2F::Scale(S, S) *
        D2D1::Matrix3x2F::Translation(C.x0 * S, 0.f);
    g.rt->SetTransform(base);

    a.buttons.clear();
    a.cards.clear();

    float y = kPad;

    // ── Header (centred) ───────────────────────────────────────────────
    // A soft warm banner keeps the lettering legible whatever the wallpaper is
    // doing directly behind it, and the zoom readout now lives in the footer,
    // so nothing shares a band with the title any more.
    {
        D2D1_COLOR_F top = pal::kBg, bot = pal::kBg;
        top.a = 0.78f; bot.a = 0.f;
        fillGradientV(g, {0, 0, W, kPad + 52.f}, top, bot);
        fillRadial(g, {0, 0, W, kPad + 56.f}, {W*0.5f, y + 16.f}, W*0.62f, 48.f,
                   {pal::kGold.r,pal::kGold.g,pal::kGold.b,0.10f},{0,0,0,0});
    }
    drawText(g, g.fmt.titleC.Get(), {kPad, y + 1.f, W - kPad, y + 31.f},
             L"MOD ENGINE  1.6  FIXER", pal::kGoldPale);
    drawText(g, g.fmt.subC.Get(), {kPad, y + 32.f, W - kPad, y + 46.f},
             L"Sekiro: Shadows Die Twice  \u2013  the 1.6 / non-Steam fix",
             pal::kMuted);
    y += 52.f;

    // Centred divider: two gold hairlines fading outward from an ember diamond.
    {
        const float cy = y + 1.f;
        const float cx = W * 0.5f;
        D2D1_COLOR_F fade = pal::kGold; fade.a = 0.f;
        fillGradientH(g, {kPad,     cy, cx - 9.f, cy + 1.2f}, fade, pal::kGold);
        fillGradientH(g, {cx + 9.f, cy, W - kPad, cy + 1.2f}, pal::kGold, fade);
        const float d = 2.5f;
        g.rt->SetTransform(
            D2D1::Matrix3x2F::Rotation(45.f, D2D1::Point2F(cx, cy + 0.6f)) * base);
        fillRect(g, {cx - d, cy + 0.6f - d, cx + d, cy + 0.6f + d}, pal::kGold);
        g.rt->SetTransform(base);
    }
    y += 9.f;

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
    // Everything above is a fixed-height stack, so the log absorbs whatever
    // height is left over. The floor keeps the rects valid if the window is
    // squeezed to its minimum size.
    const float footY  = H - 20.f;
    const float logBot = std::max(y + 64.f, footY - kPad);
    a.logRect = {kPad, y, W-kPad, logBot};
    {
        std::lock_guard<std::mutex> lk(a.logMtx);
        drawLogArea(g, a.logRect, a.logLines, a.logScroll, kLogLineH);
    }

    // ── Footer: toolchain left, zoom readout right (never overlaps) ───────
    fillGradientH(g, {kPad, logBot+3.f, W-kPad, logBot+4.2f},
                  pal::kGoldDark, pal::kCrimson);
    const float footTop = logBot + 6.f;
    std::wstring foot = a.toolchain.ok
        ? L"Toolchain:  " + a.toolchain.detail
        : L"\u26A0  "     + a.toolchain.detail;
    drawPathText(g, g.fmt.body.Get(),
                 {kPad, footTop, W-kPad-104.f, footTop+14.f}, foot,
                 a.toolchain.ok ? pal::kMuted : pal::kRust);
    {   wchar_t hint[48];
        swprintf_s(hint, L"Ctrl+Scroll  %d%%", (int)(S*100.f));
        drawText(g, g.fmt.body.Get(),
                 {W-kPad-96.f, footTop, W-kPad, footTop+14.f}, hint, pal::kDim); }

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
            // Cheap insurance: a window can change DPI without a WM_DPICHANGED
            // (e.g. it was created before the monitor was known).
            gfxSetDpi(a.gfx, (float)GetDpiForWindow(hwnd));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED:
        // The render target is what converts DIPs to pixels, so it has to be
        // told about the new monitor scale or the whole UI keeps the old one.
        // The text formats are sized in design units, so they need no rebuild.
        gfxSetDpi(a.gfx, (float)HIWORD(wParam));
        { RECT* r=(RECT*)lParam;
          SetWindowPos(hwnd,nullptr,r->left,r->top,
                       r->right-r->left,r->bottom-r->top,
                       SWP_NOZORDER|SWP_NOACTIVATE); }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_GETMINMAXINFO:
        // Minimum size in device pixels for the DPI in effect, then clamped to
        // the monitor's work area. Without the clamp a minimum that is larger
        // than the screen (easy: 820 x 540 design units is 1230 x 810 px at 150%)
        // makes Windows push the window off-screen, cropping it.
        { auto* mm=(MINMAXINFO*)lParam; float s=dipFactor();
          int mw=(int)(kMinW*s), mh=(int)(kMinH*s);
          MONITORINFO mi{sizeof(mi)};
          if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
              const int aw = mi.rcWork.right - mi.rcWork.left;
              const int ah = mi.rcWork.bottom - mi.rcWork.top;
              if (mw > aw) mw = aw;
              if (mh > ah) mh = ah;
          }
          mm->ptMinTrackSize.x = mw;
          mm->ptMinTrackSize.y = mh; }
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

    // Size the window so the *client* area is kDefaultW x kDefaultH design
    // units. CreateWindowExW takes the outer window size, and the border plus
    // caption used to eat ~16x39 px of the canvas, which cropped the right and
    // bottom edges. Centre it inside the work area (not the raw screen, or the
    // taskbar would cover the footer) and clamp it so it always fits.
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const UINT  dpi = GetDpiForSystem();
    const float s   = (float)dpi / 96.f;
    RECT wr{0, 0, (LONG)(kDefaultW*s), (LONG)(kDefaultH*s)};
    AdjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int wW = (int)std::min<LONG>(wr.right - wr.left, wa.right - wa.left);
    const int wH = (int)std::min<LONG>(wr.bottom - wr.top, wa.bottom - wa.top);
    const int wX = wa.left + (wa.right - wa.left - wW) / 2;
    const int wY = wa.top  + (wa.bottom - wa.top  - wH) / 2;

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
