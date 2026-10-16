#include "render.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dcomp.lib")

static D2D1_RECT_F R(const RectF &r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
static D2D1_COLOR_F C(const Color &c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

bool Renderer::Init(HWND h) {
    hwnd = h;
    RECT rc; GetClientRect(hwnd, &rc);
    width = std::max(1L, rc.right - rc.left);
    height = std::max(1L, rc.bottom - rc.top);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, &d3d, nullptr, &d3dCtx);
    if (FAILED(hr)) {
        Log(L"Hardware D3D11 device failed (0x%08x), falling back to WARP", hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, _countof(levels),
                               D3D11_SDK_VERSION, &d3d, nullptr, &d3dCtx);
        if (FAILED(hr)) return false;
    }

    IDXGIDevice *dxgiDevice = nullptr;
    d3d->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice);
    IDXGIAdapter *adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2 *factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // DirectComposition swap chain with premultiplied alpha: lets the window background be translucent
    // (Mica / Acrylic / plain transparency). The window is created with WS_EX_NOREDIRECTIONBITMAP.
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = factory->CreateSwapChainForComposition(d3d, &sd, nullptr, &swapChain);
    if (SUCCEEDED(hr)) hr = DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void **)&dcomp);
    if (SUCCEEDED(hr)) hr = dcomp->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget);
    if (SUCCEEDED(hr)) hr = dcomp->CreateVisual(&dcompVisual);
    if (SUCCEEDED(hr)) hr = dcompVisual->SetContent(swapChain);
    if (SUCCEEDED(hr)) hr = dcompTarget->SetRoot(dcompVisual);
    if (SUCCEEDED(hr)) hr = dcomp->Commit();
    if (FAILED(hr)) {
        Log(L"DirectComposition unavailable (0x%08x), using an opaque HWND swap chain", hr);
        SafeRelease(dcompVisual);
        SafeRelease(dcompTarget);
        SafeRelease(dcomp);
        SafeRelease(swapChain);
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory->CreateSwapChainForHwnd(d3d, hwnd, &sd, nullptr, nullptr, &swapChain);
        if (FAILED(hr)) {
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.BufferCount = 1;
            hr = factory->CreateSwapChainForHwnd(d3d, hwnd, &sd, nullptr, nullptr, &swapChain);
        }
    }
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    SafeRelease(factory);
    SafeRelease(adapter);
    if (FAILED(hr)) { SafeRelease(dxgiDevice); return false; }

    D2D1_FACTORY_OPTIONS fo = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, (void **)&d2dFactory);
    if (FAILED(hr)) return false;
    hr = d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);
    SafeRelease(dxgiDevice);
    if (FAILED(hr)) return false;
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx);
    if (FAILED(hr)) return false;
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    ctx->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dwrite);
    if (FAILED(hr)) return false;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (FAILED(hr)) return false;

    CreateTarget();
    return true;
}

void Renderer::CreateTarget() {
    IDXGISurface *surface = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(IDXGISurface), (void **)&surface))) return;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, dcompVisual ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE), 96, 96);
    ctx->CreateBitmapFromDxgiSurface(surface, &bp, &target);
    SafeRelease(surface);
    ctx->SetTarget(target);
}

void Renderer::ReleaseTarget() {
    if (ctx) ctx->SetTarget(nullptr);
    SafeRelease(target);
}

void Renderer::Resize(int w, int h) {
    if (w <= 0 || h <= 0 || !swapChain) return;
    if (w == width && h == height) return;
    width = w; height = h;
    ReleaseTarget();
    swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    CreateTarget();
}

void Renderer::Shutdown() {
    for (auto &kv : layoutCache) kv.second.layout->Release();
    layoutCache.clear();
    for (int i = 0; i < FONT_COUNT; i++) {
        SafeRelease(formats[i]);
        SafeRelease(formatsNoWrap[i]);
        SafeRelease(ellipsisSign[i]);
    }
    ReleaseTarget();
    SafeRelease(brush);
    SafeRelease(ctx);
    SafeRelease(d2dDevice);
    SafeRelease(d2dFactory);
    SafeRelease(dcompVisual);
    SafeRelease(dcompTarget);
    SafeRelease(dcomp);
    SafeRelease(swapChain);
    SafeRelease(d3dCtx);
    SafeRelease(d3d);
    SafeRelease(dwrite);
    SafeRelease(wic);
}

static bool FontExists(IDWriteFactory *f, const wchar_t *name) {
    IDWriteFontCollection *col = nullptr;
    if (FAILED(f->GetSystemFontCollection(&col))) return false;
    UINT32 idx; BOOL exists = FALSE;
    col->FindFamilyName(name, &idx, &exists);
    col->Release();
    return exists != FALSE;
}

void Renderer::SetFonts(const std::wstring &uiFontIn, f32 uiSize, const std::wstring &monoFontIn, f32 monoSize) {
    for (auto &kv : layoutCache) kv.second.layout->Release();
    layoutCache.clear();
    for (int i = 0; i < FONT_COUNT; i++) {
        SafeRelease(formats[i]);
        SafeRelease(formatsNoWrap[i]);
        SafeRelease(ellipsisSign[i]);
    }
    std::wstring uiFont = FontExists(dwrite, uiFontIn.c_str()) ? uiFontIn : L"Segoe UI";
    std::wstring monoFont = FontExists(dwrite, monoFontIn.c_str()) ? monoFontIn : L"Consolas";
    const wchar_t *iconFont = FontExists(dwrite, L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";

    struct Def { const wchar_t *family; f32 size; DWRITE_FONT_WEIGHT weight; };
    f32 px = uiSize * scale * (96.f / 72.f);
    Def defs[FONT_COUNT] = {
        {uiFont.c_str(), px, DWRITE_FONT_WEIGHT_NORMAL},
        {uiFont.c_str(), px, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {uiFont.c_str(), px * 0.86f, DWRITE_FONT_WEIGHT_NORMAL},
        {uiFont.c_str(), px * 1.25f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {monoFont.c_str(), monoSize * scale * (96.f / 72.f), DWRITE_FONT_WEIGHT_NORMAL},
        {iconFont, px * 1.05f, DWRITE_FONT_WEIGHT_NORMAL},
        {iconFont, px * 0.8f, DWRITE_FONT_WEIGHT_NORMAL},
    };
    for (int i = 0; i < FONT_COUNT; i++) {
        // locale ko-kr makes DirectWrite prefer Korean glyph variants for CJK unified ideographs
        dwrite->CreateTextFormat(defs[i].family, nullptr, defs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL, defs[i].size, L"ko-kr", &formats[i]);
        formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        dwrite->CreateTextFormat(defs[i].family, nullptr, defs[i].weight, DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL, defs[i].size, L"ko-kr", &formatsNoWrap[i]);
        formatsNoWrap[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        dwrite->CreateEllipsisTrimmingSign(formatsNoWrap[i], &ellipsisSign[i]);

        IDWriteTextLayout *l = nullptr;
        dwrite->CreateTextLayout(L"Ag가", 3, formatsNoWrap[i], 1000, 1000, &l);
        DWRITE_TEXT_METRICS tm;
        l->GetMetrics(&tm);
        DWRITE_LINE_METRICS lm; UINT32 cnt = 0;
        l->GetLineMetrics(&lm, 1, &cnt);
        l->Release();
        metrics[i].size = defs[i].size;
        metrics[i].lineH = tm.height;
        metrics[i].ascent = lm.baseline;
    }
}

bool Renderer::BeginFrame(Color clear) {
    if (!target) return false;
    frameIndex++;
    ctx->BeginDraw();
    ctx->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx->Clear(C(clear));
    return true;
}

void Renderer::EndFrame(bool vsync) {
    while (clipDepth > 0) PopClip();
    HRESULT hr = ctx->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseTarget();
        CreateTarget();
    }
    DXGI_PRESENT_PARAMETERS pp = {};
    hr = swapChain->Present1(vsync ? 1 : 0, 0, &pp);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Log(L"Device lost (0x%08x)", hr);
    }
    EndFrameHousekeeping();
}

void Renderer::EndFrameHousekeeping() {
    if (layoutCache.size() < 4000 && (frameIndex % 60) != 0) return;
    for (auto it = layoutCache.begin(); it != layoutCache.end();) {
        if (frameIndex - it->second.frame > 90) {
            it->second.layout->Release();
            it = layoutCache.erase(it);
        } else ++it;
    }
}

ID2D1SolidColorBrush *Renderer::Brush(const Color &c) {
    brush->SetColor(C(c));
    brush->SetOpacity(1);
    return brush;
}

void Renderer::FillRect(const RectF &r, const Color &c, f32 radius) {
    if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
    if (radius > 0) ctx->FillRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), Brush(c));
    else ctx->FillRectangle(R(r), Brush(c));
}

void Renderer::FillSurface(const RectF &r, const Color &c, f32 radius) {
    if (!translucent) { FillRect(r, c, radius); return; }
    if (r.w <= 0 || r.h <= 0) return;
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    if (radius > 0) ctx->FillRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), Brush(c));
    else ctx->FillRectangle(R(r), Brush(c));
    ctx->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

void Renderer::SetTranslucent(bool on) {
    translucent = on && SupportsTransparency();
    // ClearType needs an opaque destination; grayscale antialiasing stays clean over a see-through background
    if (ctx) ctx->SetTextAntialiasMode(translucent ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void Renderer::StrokeRect(const RectF &r, const Color &c, f32 w, f32 radius) {
    RectF rr(r.x + w * 0.5f, r.y + w * 0.5f, r.w - w, r.h - w);
    if (radius > 0) ctx->DrawRoundedRectangle(D2D1::RoundedRect(R(rr), radius, radius), Brush(c), w);
    else ctx->DrawRectangle(R(rr), Brush(c), w);
}

void Renderer::Line(f32 x1, f32 y1, f32 x2, f32 y2, const Color &c, f32 w) {
    ctx->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), Brush(c), w);
}

void Renderer::FillGradientV(const RectF &r, const Color &top, const Color &bottom) {
    D2D1_GRADIENT_STOP stops[2] = {{0, C(top)}, {1, C(bottom)}};
    ID2D1GradientStopCollection *col = nullptr;
    ctx->CreateGradientStopCollection(stops, 2, &col);
    ID2D1LinearGradientBrush *lg = nullptr;
    ctx->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.Bottom())), col, &lg);
    ctx->FillRectangle(R(r), lg);
    SafeRelease(lg);
    SafeRelease(col);
}

void Renderer::Shadow(const RectF &r, f32 radius, f32 spread, f32 alpha) {
    int steps = 6;
    for (int i = steps; i >= 1; i--) {
        f32 d = spread * i / steps;
        RectF s(r.x - d, r.y - d + spread * 0.3f, r.w + 2 * d, r.h + 2 * d);
        FillRect(s, Color(0, 0, 0, alpha / steps), radius + d);
    }
}

void Renderer::PushClip(const RectF &r) {
    ctx->PushAxisAlignedClip(R(r), D2D1_ANTIALIAS_MODE_ALIASED);
    clipDepth++;
}

void Renderer::PopClip() {
    if (clipDepth <= 0) return;
    ctx->PopAxisAlignedClip();
    clipDepth--;
}

IDWriteTextLayout *Renderer::Layout(FontId f, const std::wstring &s, f32 maxW, bool wrap, bool ellipsis, TextAlign align) {
    u32 flags = (wrap ? 1 : 0) | (ellipsis ? 2 : 0) | ((u32)align << 2);
    u32 w = maxW >= 1e6f ? 0xffffffffu : (u32)std::max(0.f, maxW);
    LayoutKey key{s, (u32)f, w, flags};
    auto it = layoutCache.find(key);
    if (it != layoutCache.end()) {
        it->second.frame = frameIndex;
        return it->second.layout;
    }
    IDWriteTextLayout *l = nullptr;
    f32 lw = maxW >= 1e6f ? 100000.f : std::max(1.f, maxW);
    if (FAILED(dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), wrap ? formats[f] : formatsNoWrap[f], lw, 100000.f, &l)))
        return nullptr;
    if (ellipsis && !wrap) {
        DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        l->SetTrimming(&tr, ellipsisSign[f]);
    }
    DWRITE_TEXT_ALIGNMENT ta = align == ALIGN_CENTER ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : align == ALIGN_RIGHT  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                     : DWRITE_TEXT_ALIGNMENT_LEADING;
    l->SetTextAlignment(ta);
    layoutCache.emplace(key, LayoutEntry{l, frameIndex});
    return l;
}

f32 Renderer::Text(FontId f, const std::wstring &s, const RectF &r, const Color &c, const TextOpts &o) {
    if (s.empty() || r.w <= 0) return 0;
    IDWriteTextLayout *l = Layout(f, s, r.w, o.wrap, o.ellipsis, o.align);
    if (!l) return 0;
    DWRITE_TEXT_METRICS tm;
    l->GetMetrics(&tm);
    f32 y = r.y;
    if (o.vcenter && !o.wrap) y = r.y + (r.h - metrics[f].lineH) * 0.5f;
    Color cc = c;
    cc.a *= o.opacity;
    ctx->DrawTextLayout(D2D1::Point2F(std::floor(r.x), std::floor(y)), l, Brush(cc), D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    return tm.widthIncludingTrailingWhitespace;
}

void Renderer::TextUnderline(FontId f, const std::wstring &s, const RectF &r, const Color &c, u32 from, u32 len) {
    if (s.empty()) return;
    // underline is drawn manually so cached layouts stay immutable
    f32 x0 = r.x + CaretX(f, s, from);
    f32 x1 = r.x + CaretX(f, s, std::min<u32>((u32)s.size(), from + len));
    f32 y = r.y + (r.h - metrics[f].lineH) * 0.5f + metrics[f].ascent + 2 * scale;
    Text(f, s, r, c);
    Line(x0, y, x1, y, c, std::max(1.f, scale));
}

f32 Renderer::Measure(FontId f, const std::wstring &s) {
    if (s.empty()) return 0;
    IDWriteTextLayout *l = Layout(f, s, 1e7f, false, false, ALIGN_LEFT);
    if (!l) return 0;
    DWRITE_TEXT_METRICS tm;
    l->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

f32 Renderer::MeasureWrapped(FontId f, const std::wstring &s, f32 width) {
    if (s.empty()) return metrics[f].lineH;
    IDWriteTextLayout *l = Layout(f, s, width, true, false, ALIGN_LEFT);
    if (!l) return 0;
    DWRITE_TEXT_METRICS tm;
    l->GetMetrics(&tm);
    return tm.height;
}

f32 Renderer::CaretX(FontId f, const std::wstring &s, u32 index) {
    if (s.empty() || index == 0) return 0;
    IDWriteTextLayout *l = Layout(f, s, 1e7f, false, false, ALIGN_LEFT);
    if (!l) return 0;
    FLOAT x, y;
    DWRITE_HIT_TEST_METRICS hm;
    if (index >= s.size()) {
        l->HitTestTextPosition((UINT32)s.size() - 1, TRUE, &x, &y, &hm);
    } else {
        l->HitTestTextPosition(index, FALSE, &x, &y, &hm);
    }
    return x;
}

u32 Renderer::HitTest(FontId f, const std::wstring &s, f32 x) {
    if (s.empty()) return 0;
    IDWriteTextLayout *l = Layout(f, s, 1e7f, false, false, ALIGN_LEFT);
    if (!l) return 0;
    BOOL trailing, inside;
    DWRITE_HIT_TEST_METRICS hm;
    l->HitTestPoint(x, metrics[f].lineH * 0.5f, &trailing, &inside, &hm);
    u32 idx = hm.textPosition + (trailing ? hm.length : 0);
    return std::min<u32>(idx, (u32)s.size());
}

void Renderer::TextHighlightRanges(FontId f, const std::wstring &s, const RectF &r, const std::vector<std::pair<u32, u32>> &ranges, const Color &c) {
    f32 y = r.y + (r.h - metrics[f].lineH) * 0.5f;
    for (auto &rg : ranges) {
        f32 x0 = CaretX(f, s, rg.first);
        f32 x1 = CaretX(f, s, rg.first + rg.second);
        if (x0 >= r.w) continue;
        x1 = std::min(x1, r.w);
        FillRect(RectF(r.x + x0, y, x1 - x0, metrics[f].lineH), c, 2 * scale);
    }
}

ID2D1Bitmap *Renderer::CreateBitmapFromWIC(IWICBitmapSource *src) {
    IWICFormatConverter *conv = nullptr;
    wic->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut))) {
        conv->Release();
        return nullptr;
    }
    ID2D1Bitmap *bmp = nullptr;
    ctx->CreateBitmapFromWicBitmap(conv, nullptr, &bmp);
    conv->Release();
    return bmp;
}

ID2D1Bitmap *Renderer::CreateBitmapFromHIcon(HICON icon) {
    if (!icon) return nullptr;
    IWICBitmap *wb = nullptr;
    if (FAILED(wic->CreateBitmapFromHICON(icon, &wb))) return nullptr;
    ID2D1Bitmap *bmp = CreateBitmapFromWIC(wb);
    wb->Release();
    return bmp;
}

ID2D1Bitmap *Renderer::CreateBitmapBGRA(const void *pixels, u32 w, u32 h, u32 stride) {
    ID2D1Bitmap *bmp = nullptr;
    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ctx->CreateBitmap(D2D1::SizeU(w, h), pixels, stride, &bp, &bmp);
    return bmp;
}

void Renderer::DrawBitmap(ID2D1Bitmap *bmp, const RectF &r, f32 opacity, bool hq) {
    if (!bmp) return;
    ctx->DrawBitmap(bmp, R(r), opacity, hq ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

const std::vector<std::wstring> &Renderer::SystemFontFamilies() {
    if (!fontFamilies.empty() || !dwrite) return fontFamilies;
    IDWriteFontCollection *col = nullptr;
    if (FAILED(dwrite->GetSystemFontCollection(&col, TRUE))) return fontFamilies;
    UINT32 n = col->GetFontFamilyCount();
    for (UINT32 i = 0; i < n; i++) {
        IDWriteFontFamily *fam = nullptr;
        if (FAILED(col->GetFontFamily(i, &fam))) continue;
        IDWriteLocalizedStrings *names = nullptr;
        if (SUCCEEDED(fam->GetFamilyNames(&names))) {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            names->FindLocaleName(L"ko-kr", &idx, &exists); // "맑은 고딕" rather than "Malgun Gothic"
            if (!exists) names->FindLocaleName(L"en-us", &idx, &exists);
            if (!exists) idx = 0;
            UINT32 len = 0;
            names->GetStringLength(idx, &len);
            std::wstring name(len + 1, L'\0');
            names->GetString(idx, &name[0], len + 1);
            name.resize(len);
            if (!name.empty() && name[0] != L'@') fontFamilies.push_back(name); // skip vertical-writing variants
            names->Release();
        }
        fam->Release();
    }
    col->Release();
    std::sort(fontFamilies.begin(), fontFamilies.end(), [](const std::wstring &a, const std::wstring &b) { return NaturalCompare(a, b) < 0; });
    fontFamilies.erase(std::unique(fontFamilies.begin(), fontFamilies.end()), fontFamilies.end());
    return fontFamilies;
}
