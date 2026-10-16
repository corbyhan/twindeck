// D3D11 swap chain + Direct2D/DirectWrite drawing.
// DirectWrite provides automatic font fallback (한글: Malgun Gothic 등) unlike a fixed glyph atlas.
#pragma once
#include "common.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <dcomp.h>

enum FontId {
    FONT_UI,       // main UI text
    FONT_UI_BOLD,
    FONT_SMALL,
    FONT_TITLE,
    FONT_MONO,     // inspector text preview
    FONT_ICON,     // Segoe Fluent Icons / MDL2 Assets
    FONT_ICON_SMALL,
    FONT_COUNT
};

enum TextAlign { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

struct TextOpts {
    TextAlign align = ALIGN_LEFT;
    bool ellipsis = true;
    bool wrap = false;
    bool vcenter = true;
    f32 opacity = 1.f;
};

struct FontMetrics {
    f32 size = 0;     // DIP
    f32 lineH = 0;    // line height in px
    f32 ascent = 0;
};

class Renderer {
public:
    bool Init(HWND hwnd);
    void Shutdown();
    void Resize(int w, int h);
    bool BeginFrame(Color clear);
    void EndFrame(bool vsync);

    void SetScale(f32 s) { scale = s; }
    f32  Scale() const { return scale; }
    void SetFonts(const std::wstring &uiFont, f32 uiSize, const std::wstring &monoFont, f32 monoSize);
    const FontMetrics &Metrics(FontId f) const { return metrics[f]; }

    // primitives (pixel coordinates)
    void FillRect(const RectF &r, const Color &c, f32 radius = 0);
    // background surfaces: in translucent mode they replace the pixels below (incl. alpha) instead of
    // blending, so nested surfaces keep the chosen opacity rather than stacking up to opaque
    void FillSurface(const RectF &r, const Color &c, f32 radius = 0);
    // translucent window background (needs the DirectComposition swap chain)
    bool SupportsTransparency() const { return dcompVisual != nullptr; }
    void SetTranslucent(bool on);
    bool Translucent() const { return translucent; }
    void StrokeRect(const RectF &r, const Color &c, f32 width = 1, f32 radius = 0);
    void Line(f32 x1, f32 y1, f32 x2, f32 y2, const Color &c, f32 width = 1);
    void FillGradientV(const RectF &r, const Color &top, const Color &bottom);
    void Shadow(const RectF &r, f32 radius, f32 spread, f32 alpha);
    void PushClip(const RectF &r);
    void PopClip();

    // text
    f32  Text(FontId f, const std::wstring &s, const RectF &r, const Color &c, const TextOpts &o = TextOpts());
    void TextUnderline(FontId f, const std::wstring &s, const RectF &r, const Color &c, u32 underlineFrom, u32 underlineLen);
    f32  Measure(FontId f, const std::wstring &s);
    f32  MeasureWrapped(FontId f, const std::wstring &s, f32 width); // returns height
    // caret / hit testing for single-line text
    f32  CaretX(FontId f, const std::wstring &s, u32 index);
    u32  HitTest(FontId f, const std::wstring &s, f32 x);
    // highlight match ranges (filter) – draws background behind character ranges
    void TextHighlightRanges(FontId f, const std::wstring &s, const RectF &r, const std::vector<std::pair<u32, u32>> &ranges, const Color &c);

    // bitmaps
    ID2D1Bitmap *CreateBitmapFromHIcon(HICON icon);
    ID2D1Bitmap *CreateBitmapBGRA(const void *pixels, u32 w, u32 h, u32 stride);
    ID2D1Bitmap *CreateBitmapFromWIC(IWICBitmapSource *src);
    void DrawBitmap(ID2D1Bitmap *bmp, const RectF &r, f32 opacity = 1, bool highQuality = true);

    // installed font families (Korean name when available), sorted; cached
    const std::vector<std::wstring> &SystemFontFamilies();
    IWICImagingFactory *Wic() { return wic; }
    IDWriteFactory *DWrite() { return dwrite; }
    HWND Hwnd() const { return hwnd; }
    int Width() const { return width; }
    int Height() const { return height; }
    void EndFrameHousekeeping();

private:
    IDWriteTextLayout *Layout(FontId f, const std::wstring &s, f32 maxW, bool wrap, bool ellipsis, TextAlign align);
    ID2D1SolidColorBrush *Brush(const Color &c);
    void CreateTarget();
    void ReleaseTarget();

    HWND hwnd = nullptr;
    int width = 0, height = 0;
    f32 scale = 1;

    ID3D11Device *d3d = nullptr;
    ID3D11DeviceContext *d3dCtx = nullptr;
    IDXGISwapChain1 *swapChain = nullptr;
    IDCompositionDevice *dcomp = nullptr;
    IDCompositionTarget *dcompTarget = nullptr;
    IDCompositionVisual *dcompVisual = nullptr;
    bool translucent = false;
    ID2D1Factory1 *d2dFactory = nullptr;
    ID2D1Device *d2dDevice = nullptr;
    ID2D1DeviceContext *ctx = nullptr;
    ID2D1Bitmap1 *target = nullptr;
    ID2D1SolidColorBrush *brush = nullptr;
    IDWriteFactory *dwrite = nullptr;
    IWICImagingFactory *wic = nullptr;
    IDWriteTextFormat *formats[FONT_COUNT] = {};
    IDWriteTextFormat *formatsNoWrap[FONT_COUNT] = {};
    IDWriteInlineObject *ellipsisSign[FONT_COUNT] = {};
    FontMetrics metrics[FONT_COUNT];
    int clipDepth = 0;

    struct LayoutKey {
        std::wstring s;
        u32 font;
        u32 w;
        u32 flags;
        bool operator==(const LayoutKey &o) const { return font == o.font && w == o.w && flags == o.flags && s == o.s; }
    };
    struct LayoutKeyHash {
        size_t operator()(const LayoutKey &k) const {
            return std::hash<std::wstring>()(k.s) ^ (k.font * 0x9E3779B1u) ^ ((size_t)k.w << 8) ^ ((size_t)k.flags << 20);
        }
    };
    struct LayoutEntry { IDWriteTextLayout *layout; u64 frame; };
    std::unordered_map<LayoutKey, LayoutEntry, LayoutKeyHash> layoutCache;
    u64 frameIndex = 0;
    std::vector<std::wstring> fontFamilies;
};
