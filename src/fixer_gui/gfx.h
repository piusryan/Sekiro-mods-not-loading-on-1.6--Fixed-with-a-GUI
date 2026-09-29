#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Sekiro colour palette
// ---------------------------------------------------------------------------
namespace pal {
    // Backgrounds
    inline constexpr D2D1_COLOR_F kBg         = {0.050f, 0.043f, 0.035f, 1.f}; // near-black warm
    inline constexpr D2D1_COLOR_F kBgPanel    = {0.100f, 0.085f, 0.065f, 1.f};
    inline constexpr D2D1_COLOR_F kBgCard     = {0.115f, 0.098f, 0.075f, 0.88f};
    inline constexpr D2D1_COLOR_F kBgWell     = {0.060f, 0.050f, 0.038f, 0.92f};
    inline constexpr D2D1_COLOR_F kBgLog      = {0.030f, 0.025f, 0.018f, 0.93f};
    inline constexpr D2D1_COLOR_F kVeil       = {0.020f, 0.016f, 0.010f, 0.62f};

    // Borders
    inline constexpr D2D1_COLOR_F kBorder     = {0.200f, 0.165f, 0.110f, 1.f};
    inline constexpr D2D1_COLOR_F kBorderHi   = {0.340f, 0.280f, 0.170f, 1.f};

    // Text
    inline constexpr D2D1_COLOR_F kParchment  = {0.870f, 0.820f, 0.720f, 1.f};
    inline constexpr D2D1_COLOR_F kMuted      = {0.500f, 0.460f, 0.390f, 1.f};
    inline constexpr D2D1_COLOR_F kDim        = {0.280f, 0.250f, 0.200f, 1.f};

    // Accent
    inline constexpr D2D1_COLOR_F kGold       = {0.800f, 0.630f, 0.140f, 1.f};
    inline constexpr D2D1_COLOR_F kGoldPale   = {0.930f, 0.790f, 0.400f, 1.f};
    inline constexpr D2D1_COLOR_F kGoldDark   = {0.420f, 0.310f, 0.055f, 1.f};
    inline constexpr D2D1_COLOR_F kJade       = {0.230f, 0.680f, 0.380f, 1.f};
    inline constexpr D2D1_COLOR_F kRust       = {0.760f, 0.220f, 0.220f, 1.f};
    inline constexpr D2D1_COLOR_F kBlood      = {0.480f, 0.060f, 0.060f, 1.f};
    inline constexpr D2D1_COLOR_F kCrimson    = {0.430f, 0.055f, 0.055f, 1.f};
    inline constexpr D2D1_COLOR_F kEmber      = {0.780f, 0.250f, 0.060f, 1.f};

    // Backdrop radials
    inline constexpr D2D1_COLOR_F kBackdropTop = {0.080f, 0.060f, 0.040f, 1.f};
    inline constexpr D2D1_COLOR_F kBackdropBot = {0.025f, 0.018f, 0.010f, 1.f};
}

// ---------------------------------------------------------------------------
// Text formats
// ---------------------------------------------------------------------------
struct Fmt {
    ComPtr<IDWriteTextFormat> title;     // 20pt SemiBold
    ComPtr<IDWriteTextFormat> subtitle;  // 10pt Regular
    ComPtr<IDWriteTextFormat> label;     // 8pt SemiBold  (card heading)
    ComPtr<IDWriteTextFormat> value;     // 13pt SemiBold (card value)
    ComPtr<IDWriteTextFormat> btn;       // 11pt SemiBold (button label)
    ComPtr<IDWriteTextFormat> body;      // 10pt Regular  (status / footer)
    ComPtr<IDWriteTextFormat> mono;      // 10pt Consolas (path / log)
    ComPtr<IDWriteTextFormat> logLine;   // 10pt Consolas (log area)
};

// ---------------------------------------------------------------------------
// Gfx context  – one per window
// ---------------------------------------------------------------------------
struct Gfx {
    ComPtr<ID2D1Factory>           factory;
    ComPtr<IDWriteFactory>         dw;
    ComPtr<IWICImagingFactory>     wic;
    ComPtr<ID2D1HwndRenderTarget>  rt;
    ComPtr<ID2D1SolidColorBrush>   brush;   // reused, colour set each draw call
    ComPtr<ID2D1Bitmap>            bgBitmap; // decoded background.jpg
    Fmt                            fmt;
    float                          dpi    = 96.f;
    float                          scale  = 1.f;   // dpi/96
    UINT                           pxW    = 1;
    UINT                           pxH    = 1;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool gfxCreate(HWND hwnd, Gfx& g);
void gfxDestroy(Gfx& g);
void gfxResize(Gfx& g, UINT w, UINT h);
bool gfxLoadBackground(Gfx& g, HMODULE hmod, int resId); // decode RCDATA JPEG
bool fmtCreate(Gfx& g, Fmt& f);

// ---------------------------------------------------------------------------
// Drawing primitives  (all coords in DIPs)
// ---------------------------------------------------------------------------
void fillRect      (Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c);
void fillRounded   (Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c, float radius = 4.f);
void strokeRounded (Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F c, float radius = 4.f, float w = 1.f);
void fillGradientV (Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F top, D2D1_COLOR_F bot);
void fillGradientH (Gfx& g, D2D1_RECT_F r, D2D1_COLOR_F l,   D2D1_COLOR_F r2);
void fillRadial    (Gfx& g, D2D1_RECT_F r, D2D1_POINT_2F c,
                   float rx, float ry, D2D1_COLOR_F inner, D2D1_COLOR_F outer);

void drawText      (Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
                   const std::wstring& text, D2D1_COLOR_F c);
void drawTextClip  (Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
                   const std::wstring& text, D2D1_COLOR_F c);

// Measure text width in DIPs (used for path ellipsis)
float measureW     (Gfx& g, IDWriteTextFormat* fmt, const std::wstring& text);

// Draw text with mid-path ellipsis (…) fitting inside rect width
void drawPathText  (Gfx& g, IDWriteTextFormat* fmt, D2D1_RECT_F r,
                   const std::wstring& text, D2D1_COLOR_F c);

// ---------------------------------------------------------------------------
// Sekiro-themed button
// ---------------------------------------------------------------------------
enum class BtnStyle { Primary, Gold, Ghost };

struct Btn {
    int          id      = 0;
    D2D1_RECT_F  rect{};
    std::wstring label;
    BtnStyle     style   = BtnStyle::Primary;
    bool         enabled = true;
    bool         hot     = false;   // mouse hover
    bool         down    = false;   // mouse pressed
};

void drawBtn(Gfx& g, const Btn& b);

// ---------------------------------------------------------------------------
// Status card
// ---------------------------------------------------------------------------
enum class CardColour { Gold, Green, Red, Dim };

struct Card {
    D2D1_RECT_F  rect{};
    std::wstring title;
    std::wstring value;
    CardColour   colour = CardColour::Dim;
};

void drawCard(Gfx& g, const Card& c);

// ---------------------------------------------------------------------------
// Log area  (drawn directly into the render target)
// ---------------------------------------------------------------------------
struct LogLine {
    std::wstring text;
    D2D1_COLOR_F colour;
};

void drawLogArea(Gfx& g, D2D1_RECT_F rect,
                 const std::vector<LogLine>& lines,
                 int scrollOffset,       // lines scrolled from bottom (0 = tail)
                 float lineH);
