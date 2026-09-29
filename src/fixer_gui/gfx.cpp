#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include "gfx.h"

#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool gfxCreate(HWND hwnd, Gfx& g) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                  g.factory.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(g.dw.GetAddressOf()))))
        return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(g.wic.GetAddressOf()))))
        return false;

    g.dpi   = (float)GetDpiForWindow(hwnd);
    g.scale = g.dpi / 96.f;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    g.pxW = (UINT)std::max(1L, rc.right  - rc.left);
    g.pxH = (UINT)std::max(1L, rc.bottom - rc.top);

    auto rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        g.dpi, g.dpi);
    auto hwProps = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(g.pxW, g.pxH));

    if (FAILED(g.factory->CreateHwndRenderTarget(rtProps, hwProps,
                                                  g.rt.GetAddressOf())))
        return false;

    g.rt->CreateSolidColorBrush(D2D1::ColorF(1,1,1), g.brush.GetAddressOf());
    return fmtCreate(g, g.fmt);
}

void gfxDestroy(Gfx& g) {
    g.fmt    = {};
    g.brush.Reset();
    g.bgBitmap.Reset();
    g.rt.Reset();
    g.wic.Reset();
    g.dw.Reset();
    g.factory.Reset();
}

void gfxResize(Gfx& g, UINT w, UINT h) {
    g.pxW = std::max(1u, w);
    g.pxH = std::max(1u, h);
    if (g.rt) g.rt->Resize(D2D1::SizeU(g.pxW, g.pxH));
}

void gfxSetDpi(Gfx& g, float dpi) {
    // D2D turns DIPs into pixels using the render target's DPI. When the window
    // moves to a monitor with a different scaling factor the target has to be
    // told, or the whole UI keeps the old scale.
    if (dpi <= 0.f) return;
    g.dpi   = dpi;
    g.scale = dpi / 96.f;
    if (g.rt) g.rt->SetDpi(dpi, dpi);
}

// ---------------------------------------------------------------------------
// Text formats
// ---------------------------------------------------------------------------
static IDWriteTextFormat* makeFmt(IDWriteFactory* dw,
                                   const wchar_t* family, float size,
                                   DWRITE_FONT_WEIGHT wt,
                                   DWRITE_TEXT_ALIGNMENT ha,
                                   DWRITE_PARAGRAPH_ALIGNMENT va) {
    IDWriteTextFormat* f = nullptr;
    if (FAILED(dw->CreateTextFormat(family, nullptr, wt,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"en-us", &f))) return nullptr;
    f->SetTextAlignment(ha);
    f->SetParagraphAlignment(va);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return f;
}

bool fmtCreate(Gfx& g, Fmt& f) {
    auto* dw = g.dw.Get();
    bool ok = true;
    auto L = DWRITE_TEXT_ALIGNMENT_LEADING;
    auto C = DWRITE_TEXT_ALIGNMENT_CENTER;
    auto M = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    auto T = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;

    // NOTE: sizes are design units (== DIPs at 100% zoom). They are deliberately
    // NOT multiplied by g.scale: the render target's DPI already converts DIPs to
    // pixels, and the widget pass only applies the user zoom on top. Scaling here
    // as well made every string 1.25x/1.5x too big for its layout rect on a
    // non-100% display, which is what pushed the header over the subtitle.
    auto set = [&](ComPtr<IDWriteTextFormat>& slot,
                   const wchar_t* fam, float sz, DWRITE_FONT_WEIGHT wt,
                   DWRITE_TEXT_ALIGNMENT ha, DWRITE_PARAGRAPH_ALIGNMENT va) {
        slot.Attach(makeFmt(dw, fam, sz, wt, ha, va));
        ok &= (slot != nullptr);
    };

    set(f.title,    L"Segoe UI", 20, DWRITE_FONT_WEIGHT_SEMI_BOLD,  L, T);
    set(f.subtitle, L"Segoe UI", 10, DWRITE_FONT_WEIGHT_NORMAL,     L, M);
    set(f.titleC,   L"Segoe UI", 20, DWRITE_FONT_WEIGHT_SEMI_BOLD,  C, T);
    set(f.subC,     L"Segoe UI", 10, DWRITE_FONT_WEIGHT_NORMAL,     C, M);
    set(f.label,    L"Segoe UI",  8, DWRITE_FONT_WEIGHT_SEMI_BOLD,  L, T);
    set(f.value,    L"Segoe UI", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD,  L, T);
    set(f.btn,      L"Segoe UI", 11, DWRITE_FONT_WEIGHT_SEMI_BOLD,  C, M);
    set(f.body,     L"Segoe UI", 10, DWRITE_FONT_WEIGHT_NORMAL,     L, M);
    set(f.mono,     L"Consolas", 10, DWRITE_FONT_WEIGHT_NORMAL,     L, M);
    set(f.logLine,  L"Consolas", 10, DWRITE_FONT_WEIGHT_NORMAL,     L, T);
    return ok;
}

// ---------------------------------------------------------------------------
// Background image (JPEG embedded as RCDATA)
// ---------------------------------------------------------------------------
bool gfxLoadBackground(Gfx& g, HMODULE hmod, int resId) {
    if (!g.wic || !g.rt) return false;

    HRSRC hr = FindResourceW(hmod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(hmod, hr);
    if (!hg) return false;
    DWORD  sz  = SizeofResource(hmod, hr);
    void*  ptr = LockResource(hg);
    if (!ptr || sz == 0) return false;

    ComPtr<IWICStream> stream;
    if (FAILED(g.wic->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory((BYTE*)ptr, sz))) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(g.wic->CreateDecoderFromStream(stream.Get(), nullptr,
               WICDecodeMetadataCacheOnLoad, &decoder))) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(g.wic->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeCustom))) return false;

    g.bgBitmap.Reset();
    if (FAILED(g.rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr,
               g.bgBitmap.GetAddressOf()))) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Colour brush helper
// ---------------------------------------------------------------------------
static void setBrush(Gfx& g, D2D1_COLOR_F c) {
    g.brush->SetColor(c);
}

// ---------------------------------------------------------------------------
// Fill / stroke primitives
// ---------------------------------------------------------------------------
void fillRect(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    setBrush(g, c);
    g.rt->FillRectangle(r, g.brush.Get());
}

void fillRounded(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c, float radius) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    setBrush(g, c);
    float rr = std::min(radius,
        std::min(r.right - r.left, r.bottom - r.top) * 0.5f);
    g.rt->FillRoundedRectangle(D2D1::RoundedRect(r, rr, rr), g.brush.Get());
}

void strokeRounded(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c, float radius, float w) {
    if (r.right <= r.left + 1 || r.bottom <= r.top + 1) return;
    setBrush(g, c);
    float rr = std::min(radius,
        std::min(r.right - r.left, r.bottom - r.top) * 0.5f);
    g.rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rr, rr), g.brush.Get(), w);
}

static void gradientV(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F top, D2D1_COLOR_F bot) {
    D2D1_GRADIENT_STOP stops[2] = {{0.f,top},{1.f,bot}};
    ComPtr<ID2D1GradientStopCollection> col;
    if (FAILED(g.rt->CreateGradientStopCollection(stops,2,D2D1_GAMMA_2_2,
               D2D1_EXTEND_MODE_CLAMP,col.GetAddressOf()))) return;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES p;
    p.startPoint = {r.left,r.top};
    p.endPoint   = {r.left,r.bottom};
    D2D1_BRUSH_PROPERTIES bp{};
    ComPtr<ID2D1LinearGradientBrush> b;
    g.rt->CreateLinearGradientBrush(&p,&bp,col.Get(),b.GetAddressOf());
    if (b) g.rt->FillRectangle(r,b.Get());
}

static void gradientH(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F l, D2D1_COLOR_F rt2) {
    D2D1_GRADIENT_STOP stops[2] = {{0.f,l},{1.f,rt2}};
    ComPtr<ID2D1GradientStopCollection> col;
    if (FAILED(g.rt->CreateGradientStopCollection(stops,2,D2D1_GAMMA_2_2,
               D2D1_EXTEND_MODE_CLAMP,col.GetAddressOf()))) return;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES p;
    p.startPoint = {r.left,r.top};
    p.endPoint   = {r.right,r.top};
    D2D1_BRUSH_PROPERTIES bp{};
    ComPtr<ID2D1LinearGradientBrush> b;
    g.rt->CreateLinearGradientBrush(&p,&bp,col.Get(),b.GetAddressOf());
    if (b) g.rt->FillRectangle(r,b.Get());
}

void fillGradientV(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F top, D2D1_COLOR_F bot) {
    gradientV(g, r, top, bot);
}
void fillGradientH(Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F l, D2D1_COLOR_F r2) {
    gradientH(g, r, l, r2);
}

void fillRadial(Gfx& g, D2D1_RECT_F r, D2D1_POINT_2F centre,
                float rx, float ry, D2D1_COLOR_F inner, D2D1_COLOR_F outer) {
    D2D1_GRADIENT_STOP stops[2] = {{0.f,inner},{1.f,outer}};
    ComPtr<ID2D1GradientStopCollection> col;
    if (FAILED(g.rt->CreateGradientStopCollection(stops,2,D2D1_GAMMA_2_2,
               D2D1_EXTEND_MODE_CLAMP,col.GetAddressOf()))) return;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp;
    rp.center            = centre;
    rp.gradientOriginOffset = {0,0};
    rp.radiusX           = std::max(1.f,rx);
    rp.radiusY           = std::max(1.f,ry);
    D2D1_BRUSH_PROPERTIES bp{};
    ComPtr<ID2D1RadialGradientBrush> b;
    g.rt->CreateRadialGradientBrush(&rp,&bp,col.Get(),b.GetAddressOf());
    if (b) g.rt->FillRectangle(r,b.Get());
}

// ---------------------------------------------------------------------------
// Wallpaper – uniform "cover" fit, centred, clipped
// ---------------------------------------------------------------------------
void gfxDrawBackdrop(Gfx& g, D2D1_RECT_F target, D2D1_COLOR_F veil) {
    if (target.right <= target.left || target.bottom <= target.top) return;

    if (!g.bgBitmap) {
        // No wallpaper decoded – fall back to the plain warm backdrop.
        fillRadial(g, target, {target.left + 0.20f * (target.right - target.left),
                               target.top  + 0.05f * (target.bottom - target.top)},
                   1.10f * (target.right - target.left),
                   1.20f * (target.bottom - target.top),
                   pal::kBackdropTop, pal::kBackdropBot);
        return;
    }

    const D2D1_SIZE_F img = g.bgBitmap->GetSize();   // source pixels
    if (img.width <= 0.f || img.height <= 0.f) return;

    const float k  = (g.scale > 0.f) ? g.scale : 1.f;   // DIP -> pixel
    const float tw = target.right  - target.left;
    const float th = target.bottom - target.top;

    // "Cover": the smaller of the two ratios that still fills the whole target.
    // Comparing in pixel space keeps the art at its natural crispness.
    const float sc  = std::max((tw * k) / img.width, (th * k) / img.height);
    const float dw  = (img.width  * sc) / k;            // back to DIPs
    const float dh  = (img.height * sc) / k;
    const float dx  = target.left + (tw - dw) * 0.5f;   // centred, never top-left
    const float dy  = target.top  + (th - dh) * 0.5f;

    g.rt->PushAxisAlignedClip(target, D2D1_ANTIALIAS_MODE_ALIASED);
    g.rt->DrawBitmap(g.bgBitmap.Get(),
                     D2D1_RECT_F{dx, dy, dx + dw, dy + dh}, 1.f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    g.rt->PopAxisAlignedClip();

    if (veil.a > 0.f) fillRect(g, target, veil);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
void drawText(Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
              const std::wstring& text, D2D1_COLOR_F c) {
    if (!fmt || text.empty() || r.right <= r.left || r.bottom <= r.top) return;
    setBrush(g, c);
    g.rt->DrawTextW(text.c_str(), (UINT32)text.size(), fmt, r, g.brush.Get(),
                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void drawTextClip(Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
                  const std::wstring& text, D2D1_COLOR_F c) {
    drawText(g, fmt, r, text, c);
}

float measureW(Gfx& g, IDWriteTextFormat* fmt, const std::wstring& text) {
    if (!fmt || text.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g.dw->CreateTextLayout(text.c_str(), (UINT32)text.size(),
               fmt, 1e6f, 1e6f, layout.GetAddressOf()))) return 0.f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.width;
}

void drawPathText(Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
                  const std::wstring& text, D2D1_COLOR_F c) {
    if (!fmt || text.empty() || r.right <= r.left || r.bottom <= r.top) return;
    float avail = r.right - r.left;
    if (measureW(g, fmt, text) <= avail) {
        drawText(g, fmt, r, text, c);
        return;
    }
    // Mid ellipsis: keep head (drive) and tail (folder name)
    const float ellW = measureW(g, fmt, L"\u2026");
    const float budget = std::max(0.f, avail - ellW);
    // Binary search for how much head fits in 55% of budget
    auto fitLen = [&](const std::wstring& s, float maxW) -> size_t {
        size_t lo = 0, hi = s.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo + 1) / 2;
            if (measureW(g, fmt, s.substr(0, mid)) <= maxW) lo = mid;
            else hi = mid - 1;
        }
        return lo;
    };
    size_t head = fitLen(text, budget * 0.55f);
    // Tail: reverse, fit, reverse back
    std::wstring rev(text.rbegin(), text.rend());
    size_t tail = fitLen(rev, budget * 0.45f);
    std::wstring clipped = text.substr(0, head)
                         + L"\u2026"
                         + text.substr(text.size() - tail);
    drawText(g, fmt, r, clipped, c);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
void drawBtn(Gfx& g, const Btn& b) {
    if (b.rect.right <= b.rect.left || b.rect.bottom <= b.rect.top) return;
    D2D1_RECT_F r = b.rect;

    D2D1_COLOR_F bg, border, fg;
    if (!b.enabled) {
        bg     = pal::kBgPanel;
        border = pal::kBorder;
        fg     = pal::kDim;
    } else if (b.style == BtnStyle::Gold) {
        bg     = b.down ? pal::kBlood  : (b.hot ? pal::kEmber   : pal::kCrimson);
        border = b.hot  ? pal::kGoldPale : pal::kGold;
        fg     = b.enabled ? pal::kGoldPale : pal::kDim;
    } else if (b.style == BtnStyle::Ghost) {
        bg     = b.hot ? pal::kBgPanel : D2D1_COLOR_F{0,0,0,0};
        border = b.hot ? pal::kBorder  : D2D1_COLOR_F{0,0,0,0};
        fg     = pal::kMuted;
    } else { // Primary
        bg     = b.down ? pal::kBlood   : (b.hot ? pal::kBgPanel : pal::kBgPanel);
        border = b.hot  ? pal::kGold    : pal::kBorder;
        fg     = b.enabled ? pal::kParchment : pal::kDim;
    }

    fillRounded(g, r, bg, 4.f);
    strokeRounded(g, r, border, 4.f, 1.f);

    // Gold shimmer on hover (top edge highlight)
    if (b.enabled && b.hot && b.style != BtnStyle::Ghost) {
        D2D1_RECT_F topEdge = {r.left+5, r.top+1, r.right-5, r.top+2};
        D2D1_COLOR_F shimmer = pal::kGold;
        shimmer.a = 0.45f;
        fillRect(g, topEdge, shimmer);
    }

    drawText(g, g.fmt.btn.Get(), r, b.label, fg);
}

// ---------------------------------------------------------------------------
// Card
// ---------------------------------------------------------------------------
void drawCard(Gfx& g, const Card& c) {
    D2D1_RECT_F r = c.rect;
    if (r.right <= r.left || r.bottom <= r.top) return;

    // Card background with slight transparency (shows BG through)
    fillRounded(g, r, pal::kBgCard, 5.f);
    strokeRounded(g, r, pal::kBorder, 5.f, 1.f);

    // Top gold accent line
    D2D1_RECT_F topLine = {r.left+4, r.top+1, r.right-4, r.top+2};
    D2D1_COLOR_F lineC = pal::kGoldDark;
    fillRect(g, topLine, lineC);

    // Status pip  (small rounded square, 7x7 design units, coloured)
    D2D1_COLOR_F pipC;
    switch (c.colour) {
        case CardColour::Green: pipC = pal::kJade;  break;
        case CardColour::Gold:  pipC = pal::kGold;  break;
        case CardColour::Red:   pipC = pal::kRust;  break;
        default:                pipC = pal::kDim;   break;
    }
    float pipSize = 7.f;
    D2D1_RECT_F pip = {r.left+10, r.top+9, r.left+10+pipSize, r.top+9+pipSize};
    fillRounded(g, pip, pipC, 2.f);

    // Title label
    float labelX = pip.right + 6;
    D2D1_RECT_F titleR = {labelX, r.top+8, r.right-6, r.top+20};
    drawTextClip(g, g.fmt.label.Get(), titleR, c.title, pal::kMuted);

    // Value
    D2D1_COLOR_F valC;
    switch (c.colour) {
        case CardColour::Green: valC = pal::kJade;     break;
        case CardColour::Gold:  valC = pal::kGold;     break;
        case CardColour::Red:   valC = pal::kRust;     break;
        default:                valC = pal::kDim;      break;
    }
    D2D1_RECT_F valR = {r.left+10, r.top+24, r.right-8, r.bottom-6};
    drawTextClip(g, g.fmt.value.Get(), valR, c.value, valC);
}

// ---------------------------------------------------------------------------
// Log area
// ---------------------------------------------------------------------------
void drawLogArea(Gfx& g, D2D1_RECT_F rect,
                 const std::vector<LogLine>& lines,
                 int scrollOffset, float lineH) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    // Background
    fillRounded(g, rect, pal::kBgLog, 4.f);
    strokeRounded(g, rect, pal::kBorder, 4.f, 1.f);

    // Top highlight
    D2D1_RECT_F topLine = {rect.left+4, rect.top+1, rect.right-4, rect.top+2};
    D2D1_COLOR_F tl = pal::kGoldDark; tl.a=0.5f;
    fillRect(g, topLine, tl);

    const float padX = 10.f, padY = 8.f;
    const float viewH = rect.bottom - rect.top - padY * 2.f;
    const int capacity = std::max(1, (int)(viewH / lineH));
    const int total    = (int)lines.size();
    const int maxScroll = std::max(0, total - capacity);
    int scroll = std::clamp(scrollOffset, 0, maxScroll);
    const int first = total - capacity - scroll;
    const int start = std::max(0, first);
    const int shown = std::min(capacity, total - start);

    // Clip to log area
    D2D1_RECT_F clipR = rect;
    clipR.left   += 1; clipR.right  -= 1;
    clipR.top    += 1; clipR.bottom -= 1;
    g.rt->PushAxisAlignedClip(clipR, D2D1_ANTIALIAS_MODE_ALIASED);

    if (shown == 0) {
        D2D1_RECT_F empty = {rect.left+padX, rect.top+padY,
                             rect.right-padX, rect.top+padY+lineH};
        drawText(g, g.fmt.logLine.Get(), empty, L"(no output yet)", pal::kDim);
    } else {
        float y = rect.top + padY;
        for (int i = 0; i < shown; i++) {
            const LogLine& ll = lines[(size_t)(start + i)];
            D2D1_RECT_F lr = {rect.left+padX, y, rect.right-padX, y+lineH};
            drawTextClip(g, g.fmt.logLine.Get(), lr, ll.text, ll.colour);
            y += lineH;
        }
    }

    g.rt->PopAxisAlignedClip();
}
