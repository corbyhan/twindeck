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

// ------------------------------------------------------------------ status bar slider popup
// Track from top (XL icons) to bottom (details). The icon part is continuous (log scale), the rest snaps.
static const f32 kUM = 0.6f, kUList = 0.73f, kUTree = 0.865f, kUDetails = 1.f;

static f32 IconU(f32 px) { return kUM * logf(kIconMax / Clamp(px, kIconMin, kIconMax)) / logf(kIconMax / kIconMin); }

static f32 TabU(const Tab *t) {
    switch (t->viewMode) {
    case LV_ICONS: return IconU(t->iconSize);
    case LV_LIST: return kUList;
    case LV_TREE: return kUTree;
    default: return kUDetails;
    }
}

static void ApplyU(App *a, Tab *t, f32 u) {
    u = Clamp(u, 0.f, 1.f);
    if (u <= kUM + 0.04f) {
        f32 k = Clamp(u, 0.f, kUM) / kUM;
        f32 px = kIconMax * powf(kIconMin / kIconMax, k);
        a->SetViewMode(t, LV_ICONS, std::floor(px + 0.5f));
        a->sessionDirty = true;
    } else if (u < (kUList + kUTree) / 2) a->SetViewMode(t, LV_LIST);
    else if (u < (kUTree + kUDetails) / 2) a->SetViewMode(t, LV_TREE);
    else a->SetViewMode(t, LV_DETAILS);
}

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
    f32 w = std::floor(std::max(190 * S, lh * 10)), h = std::floor(std::max(300 * S, lh * 15));
    RectF pr(std::max(4 * S, popup.x - w), std::max(4 * S, popup.y - h - 6 * S), w, h);
    popup.rect = pr;
    r.Shadow(pr, 8 * S, 12 * S, 0.3f);
    r.FillRect(pr, theme.popupBg, 8 * S);
    r.StrokeRect(pr, theme.popupBorder, 1 * S, 8 * S);

    f32 ty0 = pr.y + 22 * S, ty1 = pr.Bottom() - 22 * S;
    f32 tx = pr.Right() - 24 * S;
    auto yOf = [&](f32 u) { return ty0 + u * (ty1 - ty0); };
    // track
    RectF track(tx - 2 * S, ty0, 4 * S, ty1 - ty0);
    r.FillRect(track, theme.border, 2 * S);
    // labels
    struct Stop { f32 u; int mode; f32 px; const wchar_t *en, *ko; };
    Stop stops[] = {
        {0, LV_ICONS, kIconMax, L"XL Icons", L"아주 큰 아이콘"},
        {IconU(96), LV_ICONS, 96, L"L Icons", L"큰 아이콘"},
        {kUM, LV_ICONS, kIconMin, L"M Icons", L"보통 아이콘"},
        {kUList, LV_LIST, 0, L"List", L"목록"},
        {kUTree, LV_TREE, 0, L"Tree", L"트리"},
        {kUDetails, LV_DETAILS, 0, L"Details", L"자세히"},
    };
    f32 rowH = std::floor(lh + 8 * S);
    TextOpts go; go.align = ALIGN_CENTER; go.ellipsis = false;
    for (auto &st : stops) {
        RectF lr(pr.x + 8 * S, std::floor(yOf(st.u) - rowH / 2), tx - pr.x - 24 * S, rowH);
        bool cur = st.mode == t->viewMode && (st.mode != LV_ICONS || fabsf(t->iconSize - st.px) < 1);
        bool hov = Hover(lr) && !popup.viewDrag;
        if (cur) r.FillRect(lr, theme.selection, 4 * S);
        else if (hov) r.FillRect(lr, theme.hover, 4 * S);
        r.Text(FONT_ICON_SMALL, GlyphFor(st.mode), RectF(lr.x + 4 * S, lr.y, 20 * S, lr.h), theme.textDim, go);
        r.Text(FONT_UI, TL(st.en, st.ko), RectF(lr.x + 30 * S, lr.y, lr.w - 34 * S, lr.h), theme.text);
        r.Line(tx - 6 * S, yOf(st.u), tx + 6 * S, yOf(st.u), theme.textFaint, 1 * S);
        if (hov && in.pressed[0]) {
            SetViewMode(t, st.mode, st.px);
            in.pressed[0] = false;
        }
    }
    // knob
    f32 ky = yOf(TabU(t));
    RectF knob(tx - 8 * S, ky - 8 * S, 16 * S, 16 * S);
    RectF hit(tx - 14 * S, ty0 - 10 * S, 28 * S, ty1 - ty0 + 20 * S);
    if (Hover(hit) && in.pressed[0]) { popup.viewDrag = true; in.pressed[0] = false; }
    if (popup.viewDrag) {
        if (!in.down[0]) popup.viewDrag = false;
        else ApplyU(this, t, (in.my - ty0) / (ty1 - ty0));
        ky = yOf(TabU(t));
        knob = RectF(tx - 8 * S, ky - 8 * S, 16 * S, 16 * S);
    }
    r.FillRect(RectF(tx - 2 * S, ky, 4 * S, ty1 - ky), theme.accent.WithAlpha(0.5f), 2 * S);
    r.FillRect(knob, theme.accent, 8 * S);
    r.StrokeRect(knob, theme.popupBg, 2 * S, 8 * S);
    if (Hover(pr) && in.wheel != 0) { ZoomView(t, in.wheel); in.wheel = 0; }
    bool anyPress = in.pressed[0] || in.pressed[1] || in.pressed[2];
    if (anyPress && !Hover(pr) && !popup.viewDrag) ClosePopup();
}
