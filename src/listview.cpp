// File list view modes: details / tree / list / icons (M..XL), their geometry and the status bar size slider.
#include "app.h"
#include <math.h>

static const f32 kIconMin = 48, kIconMax = 256;

// ------------------------------------------------------------------ geometry
void App::UpdateGrid(Tab *t, f32 width) {
    if (!IsGridView(t)) return;
    GridLayout &g = t->grid;
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 pad = 6 * S;
    f32 ip = std::floor(Clamp(t->iconSize, 32.f, kIconMax) * S);
    f32 cellW, cellH;
    if (t->viewMode == LV_LIST) {
        cellW = std::floor(std::max(200 * S, lh * 13));
        cellH = RowHeight();
    } else {
        cellW = std::floor(std::max(ip + 24 * S, lh * 5.5f));
        cellH = std::floor(ip + 20 * S + lh * 2);
    }
    f32 avail = std::max(cellW, width - 2 * pad);
    int cols = std::max(1, (int)(avail / cellW));
    cellW = std::floor(avail / cols); // spread the cells over the full width
    if (g.rowsGen == t->rowsGen && g.mode == t->viewMode && g.width == width && g.cellW == cellW && g.cellH == cellH &&
        g.items.size() == t->rows.size())
        return;
    g.rowsGen = t->rowsGen;
    g.mode = t->viewMode;
    g.width = width;
    g.cellW = cellW;
    g.cellH = cellH;
    g.iconPx = ip;
    g.cols = cols;
    g.items.resize(t->rows.size());
    f32 headerH = RowHeight();
    f32 y = pad;
    int col = 0;
    for (size_t i = 0; i < t->rows.size(); i++) {
        if (t->rows[i].header) {
            if (col) { y += cellH; col = 0; }
            if (i) y += 4 * S;
            g.items[i] = RectF(pad, y, cols * cellW, headerH);
            y += headerH;
            continue;
        }
        g.items[i] = RectF(pad + col * cellW, y, cellW, cellH);
        if (++col == cols) { col = 0; y += cellH; }
    }
    if (col) y += cellH;
    g.contentH = y + pad;
}

RectF App::ItemRect(Tab *t, int row) {
    if (IsGridView(t) && row >= 0 && row < (int)t->grid.items.size() && t->grid.rowsGen == t->rowsGen) return t->grid.items[row];
    f32 rh = RowHeight();
    return RectF(0, row * rh, t->grid.width > 0 ? t->grid.width : 400 * S, rh);
}

int App::ItemAtPoint(Tab *t, f32 x, f32 y) {
    int n = (int)t->rows.size();
    if (!IsGridView(t) || t->grid.rowsGen != t->rowsGen) {
        int row = (int)floorf(y / RowHeight());
        return row >= 0 && row < n ? row : -1;
    }
    const auto &items = t->grid.items;
    // items are ordered by line, so the first candidate is found by binary search on the bottom edge
    size_t i = std::partition_point(items.begin(), items.end(), [&](const RectF &it) { return it.Bottom() <= y; }) - items.begin();
    for (; i < items.size() && items[i].y <= y; i++)
        if (items[i].Contains(x, y)) return (int)i;
    return -1;
}

f32 App::ListContentHeight(Tab *t) {
    if (IsGridView(t) && t->grid.rowsGen == t->rowsGen) return t->grid.contentH;
    return t->rows.size() * RowHeight();
}

int App::PageItems(Tab *t) {
    if (IsGridView(t) && t->grid.cellH > 0) return std::max(1, t->grid.cols * std::max(1, (int)(t->viewH / t->grid.cellH)));
    return std::max(1, (int)(t->viewH / RowHeight()) - 1);
}

int App::GridNeighbor(Tab *t, int row, int dx, int dy) {
    int n = (int)t->rows.size();
    if (n == 0) return 0;
    row = Clamp(row, 0, n - 1);
    if (dx) return Clamp(row + dx, 0, n - 1);
    const auto &items = t->grid.items;
    if ((int)items.size() != n) return Clamp(row + dy, 0, n - 1);
    const RectF &c = items[row];
    f32 cx = c.x + c.w / 2;
    int j = row;
    if (dy > 0) {
        while (j < n && items[j].y <= c.y + 0.5f) j++;
        if (j >= n) return n - 1;
    } else {
        while (j >= 0 && items[j].y >= c.y - 0.5f) j--;
        if (j < 0) return 0;
    }
    // pick the item of that line closest to the current column
    f32 lineY = items[j].y;
    int best = j;
    f32 bestD = 1e30f;
    for (int k = j; k >= 0 && k < n && fabsf(items[k].y - lineY) < 0.5f; k += dy > 0 ? 1 : -1) {
        f32 d = fabsf(items[k].x + items[k].w / 2 - cx);
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

// ------------------------------------------------------------------ modes
void App::SetViewMode(Tab *t, int mode, f32 iconSize) {
    if (!t) return;
    mode = Clamp(mode, 0, (int)LV_COUNT - 1);
    if (iconSize > 0) t->iconSize = Clamp(iconSize, kIconMin, kIconMax);
    if (t->viewMode == mode && iconSize <= 0) return;
    t->viewMode = mode;
    t->ensureCursor = true;
    sessionDirty = true;
    Invalidate();
}

// Ctrl+wheel / zoom commands: details (or tree) <-> list <-> icons 48..256
void App::ZoomView(Tab *t, f32 notches) {
    if (!t || notches == 0) return;
    int steps = (int)fabsf(notches) > 0 ? (int)fabsf(notches) : 1;
    for (int s = 0; s < steps; s++) {
        if (notches > 0) {
            if (t->viewMode == LV_DETAILS || t->viewMode == LV_TREE) SetViewMode(t, LV_LIST);
            else if (t->viewMode == LV_LIST) SetViewMode(t, LV_ICONS, kIconMin);
            else { t->iconSize = std::min(kIconMax, std::floor(t->iconSize * 1.15f + 0.5f)); sessionDirty = true; }
        } else {
            if (t->viewMode == LV_ICONS) {
                if (t->iconSize <= kIconMin + 0.5f) SetViewMode(t, LV_LIST);
                else { t->iconSize = std::max(kIconMin, std::floor(t->iconSize / 1.15f + 0.5f)); sessionDirty = true; }
            } else if (t->viewMode == LV_LIST) SetViewMode(t, LV_DETAILS);
        }
    }
    t->ensureCursor = true;
    Invalidate();
}

std::wstring App::ViewModeName(const Tab *t) const {
    switch (t->viewMode) {
    case LV_TREE: return TL(L"Tree", L"트리");
    case LV_LIST: return TL(L"List", L"목록");
    case LV_ICONS:
        if (t->iconSize >= 200) return TL(L"XL Icons", L"아주 큰 아이콘");
        if (t->iconSize >= 90) return TL(L"L Icons", L"큰 아이콘");
        return TL(L"M Icons", L"보통 아이콘");
    default: return TL(L"Details", L"자세히");
    }
}

static const wchar_t *GlyphFor(int mode) {
    switch (mode) {
    case LV_TREE: return L"\xF168";
    case LV_LIST: return L"\xE179";
    case LV_ICONS: return L"\xE91B";
    default: return L"\xE8FD";
    }
}
const wchar_t *App::ViewModeGlyph(const Tab *t) const { return GlyphFor(t->viewMode); }

// ------------------------------------------------------------------ status bar view popup
// A row of mode buttons; for the icon view a horizontal size slider (log scale 48..256 px) below it.
static f32 IconT(f32 px) { return logf(Clamp(px, kIconMin, kIconMax) / kIconMin) / logf(kIconMax / kIconMin); }
static f32 IconPxAt(f32 t) { return std::floor(kIconMin * powf(kIconMax / kIconMin, Clamp(t, 0.f, 1.f)) + 0.5f); }

void App::OpenViewPopup(Tab *t, RectF anchor) {
    popup = Popup();
    popup.kind = POPUP_VIEW;
    popup.viewTabId = t->id;
    popup.x = anchor.Right();
    popup.y = anchor.y;
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    Invalidate();
}

void App::DrawViewPopup() {
    Tab *t = FindTab(popup.viewTabId);
    if (!t) { ClosePopup(); return; }
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 pad = std::floor(12 * S);
    f32 btnW = std::floor(std::max(76 * S, lh * 4.2f)), btnH = std::floor(lh * 2 + 30 * S);
    f32 gap = std::floor(6 * S);
    bool iconsView = t->viewMode == LV_ICONS;
    f32 sliderH = iconsView ? std::floor(lh * 2 + 26 * S) : 0;
    f32 w = pad * 2 + btnW * 4 + gap * 3;
    f32 h = pad + lh + 10 * S + btnH + sliderH + pad;
    RectF pr(std::max(4 * S, popup.x - w), std::max(4 * S, popup.y - h - 6 * S), w, h);
    popup.rect = pr;
    r.Shadow(pr, 8 * S, 12 * S, 0.3f);
    r.FillRect(pr, theme.popupBg, 8 * S);
    r.StrokeRect(pr, theme.popupBorder, 1 * S, 8 * S);
    f32 y = pr.y + pad;
    r.Text(FONT_UI_BOLD, TL(L"View", L"보기"), RectF(pr.x + pad, y, w - 2 * pad, lh), theme.text);
    y += lh + 10 * S;

    struct Mode { int mode; const wchar_t *en, *ko; };
    const Mode modes[4] = {{LV_DETAILS, L"Details", L"자세히"}, {LV_TREE, L"Tree", L"트리"}, {LV_LIST, L"List", L"목록"},
                           {LV_ICONS, L"Icons", L"아이콘"}};
    TextOpts c; c.align = ALIGN_CENTER; c.ellipsis = false;
    for (int i = 0; i < 4; i++) {
        RectF b(pr.x + pad + i * (btnW + gap), y, btnW, btnH);
        bool cur = t->viewMode == modes[i].mode;
        bool hov = Hover(b) && !popup.viewDrag;
        r.FillRect(b, cur ? theme.selection : (hov ? theme.hover : theme.inputBg), 6 * S);
        r.StrokeRect(b, cur ? theme.accent : theme.border, cur ? std::max(1.5f, 1.5f * S) : std::max(1.f, S), 6 * S);
        r.Text(FONT_ICON, GlyphFor(modes[i].mode), RectF(b.x, b.y + 8 * S, b.w, lh + 8 * S), cur ? theme.accent : theme.textDim, c);
        r.Text(FONT_SMALL, TL(modes[i].en, modes[i].ko), RectF(b.x + 2 * S, b.Bottom() - lh - 8 * S, b.w - 4 * S, lh), cur ? theme.text : theme.textDim, c);
        if (hov && in.pressed[0]) {
            SetViewMode(t, modes[i].mode);
            in.pressed[0] = false;
        }
    }
    y += btnH;

    if (iconsView) {
        y += 12 * S;
        r.Text(FONT_SMALL, TL(L"Icon size", L"아이콘 크기"), RectF(pr.x + pad, y, w / 2, lh), theme.textDim);
        TextOpts ro; ro.align = ALIGN_RIGHT;
        r.Text(FONT_SMALL, Format(L"%.0f px", t->iconSize), RectF(pr.x + w / 2, y, w / 2 - pad, lh), theme.text, ro);
        y += lh + 6 * S;
        f32 smallW = r.Measure(FONT_SMALL, TL(L"Small", L"작게")), bigW = r.Measure(FONT_SMALL, TL(L"Large", L"크게"));
        r.Text(FONT_SMALL, TL(L"Small", L"작게"), RectF(pr.x + pad, y, smallW + 4 * S, lh), theme.textFaint);
        r.Text(FONT_SMALL, TL(L"Large", L"크게"), RectF(pr.Right() - pad - bigW - 2 * S, y, bigW + 4 * S, lh), theme.textFaint);
        f32 tx0 = pr.x + pad + smallW + 12 * S, tx1 = pr.Right() - pad - bigW - 12 * S;
        f32 cy = std::floor(y + lh / 2);
        RectF hit(tx0 - 10 * S, cy - 12 * S, tx1 - tx0 + 20 * S, 24 * S);
        if (Hover(hit) && in.pressed[0]) { popup.viewDrag = true; in.pressed[0] = false; }
        if (popup.viewDrag) {
            if (!in.down[0]) popup.viewDrag = false;
            else {
                f32 px = IconPxAt((in.mx - tx0) / std::max(1.f, tx1 - tx0));
                if (px != t->iconSize) { t->iconSize = px; sessionDirty = true; Invalidate(); }
            }
        }
        f32 kx = tx0 + IconT(t->iconSize) * (tx1 - tx0);
        r.FillRect(RectF(tx0, cy - 2 * S, tx1 - tx0, 4 * S), theme.border, 2 * S);
        r.FillRect(RectF(tx0, cy - 2 * S, kx - tx0, 4 * S), theme.accent, 2 * S);
        RectF knob(kx - 8 * S, cy - 8 * S, 16 * S, 16 * S);
        r.FillRect(knob, theme.accent, 8 * S);
        r.StrokeRect(knob, theme.popupBg, 2 * S, 8 * S);
        if (Hover(hit) || popup.viewDrag) cursorKind = 1;
    }
    if (Hover(pr) && in.wheel != 0) { ZoomView(t, in.wheel); in.wheel = 0; }
    bool anyPress = in.pressed[0] || in.pressed[1] || in.pressed[2];
    if (anyPress && !Hover(pr) && !popup.viewDrag) ClosePopup();
}
