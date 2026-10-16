// Main window drawing & mouse interaction (immediate mode)
#include "app.h"
#include "jobs.h"
#include <shlobj.h>

static int g_pressRow = -1;
static Tab *g_pressTab = nullptr;
static bool g_pressWasSelected = false;
static f32 g_scrollGrab = 0;

// ================================================================== frame
void App::Frame() {
    if (!ready) return;
    frame++;
    u64 now = GetTickCount64();

    // debounced directory change reloads
    for (auto it = pendingReloads.begin(); it != pendingReloads.end();) {
        if (now < it->second) { ++it; continue; }
        std::wstring dir = it->first;
        it = pendingReloads.erase(it);
        ForEachPanel([&](Panel *p) {
            for (auto &t : p->tabs) {
                if (t->kind != VIEW_FOLDER) continue;
                if (_wcsicmp(t->path.c_str(), dir.c_str()) == 0) Reload(t.get());
                else if (t->children.count(dir)) {
                    DropRows(t.get());
                    std::vector<FileEntry> list;
                    ListDirectory(dir, list);
                    t->children[dir] = std::move(list);
                    t->rowsDirty = true;
                }
            }
        });
        if (!insp.path.empty() && PathParent(insp.path) == dir) { std::wstring keep = insp.path; ClearInspector(); UpdateInspector(); }
    }
    if (sessionDirty && now - lastSessionSave > 5000) SaveSession();

    Draw();
    in.EndFrame();
}

// ================================================================== helpers
void App::SetTooltip(u64 id, const std::wstring &tip) {
    if (!settings.showTooltips || tip.empty()) return;
    if (lastHoverId != id) {
        lastHoverId = id;
        hoverStartTick = GetTickCount64();
    }
    tooltip = tip;
    tooltipX = in.mx;
    tooltipY = in.my;
}

bool App::IconButton(u64 id, RectF rc, const wchar_t *glyph, const std::wstring &tip, bool enabled, bool toggled, FontId font) {
    bool hov = Hover(rc);
    if (toggled) r.FillRect(rc, theme.selection, 4 * S);
    if (hov && enabled) {
        r.FillRect(rc, in.down[0] ? theme.selectionInactive : theme.hover, 4 * S);
        SetTooltip(id, tip);
        cursorKind = 0;
    }
    TextOpts o;
    o.align = ALIGN_CENTER;
    o.ellipsis = false;
    r.Text(font, glyph, rc, enabled ? (toggled ? theme.accent : theme.textDim) : theme.textFaint, o);
    return enabled && hov && in.pressed[0];
}

void App::DrawScrollbar(RectF track, f32 contentH, f32 viewH, f32 &scroll, u64 id) {
    if (contentH <= viewH + 0.5f) return;
    f32 maxScroll = contentH - viewH;
    f32 th = std::max(24 * S, track.h * viewH / contentH);
    f32 ty = track.y + (track.h - th) * (scroll / maxScroll);
    RectF thumb(track.x + track.w - 6 * S, ty, 4 * S, th);
    bool hov = Hover(RectF(track.x, track.y, track.w, track.h));
    if (hov || activeId == id) thumb = RectF(track.x + track.w - 9 * S, ty, 7 * S, th);
    r.FillRect(thumb, activeId == id ? theme.textDim.WithAlpha(0.6f) : theme.scrollbar, 3 * S);
    if (in.pressed[0] && hov) {
        if (in.my >= ty && in.my <= ty + th) {
            activeId = id;
            g_scrollGrab = in.my - ty;
        } else {
            scroll = Clamp(scroll + (in.my < ty ? -viewH : viewH), 0.f, maxScroll);
        }
        in.pressed[0] = false; // consume
    }
    if (activeId == id) {
        if (!in.down[0]) activeId = 0;
        else {
            f32 t = (in.my - g_scrollGrab - track.y) / std::max(1.f, track.h - th);
            scroll = Clamp(t * maxScroll, 0.f, maxScroll);
            Invalidate();
        }
    }
}

void App::DrawTextEdit(TextEdit &e, RectF rc, FontId f, const Color &c, const std::wstring &placeholder, bool focused) {
    r.PushClip(rc);
    std::wstring shown = e.text;
    u32 caret = std::min<u32>(e.caret, (u32)shown.size());
    u32 compLen = 0;
    if (focused && ime.composing && !ime.comp.empty()) {
        // show the in-progress Hangul syllable inline (조합 중인 글자 표시)
        std::wstring base = shown;
        if (e.HasSel()) {
            base.erase(e.SelMin(), e.SelMax() - e.SelMin());
            caret = e.SelMin();
        }
        shown = base.substr(0, caret) + ime.comp + base.substr(caret);
        compLen = (u32)ime.comp.size();
    }
    f32 caretX = r.CaretX(f, shown, caret + (compLen ? (u32)ime.cursor : 0));
    // keep caret visible
    if (caretX - e.scrollX > rc.w - 4 * S) e.scrollX = caretX - rc.w + 4 * S;
    if (caretX - e.scrollX < 0) e.scrollX = caretX;
    if (e.scrollX < 0) e.scrollX = 0;
    RectF tr(rc.x - e.scrollX, rc.y, std::max(rc.w + e.scrollX, r.Measure(f, shown) + 8 * S), rc.h);

    if (shown.empty() && !placeholder.empty()) {
        TextOpts o;
        r.Text(f, placeholder, rc, theme.textFaint, o);
    }
    if (focused && e.HasSel() && !compLen) {
        f32 x0 = r.CaretX(f, shown, e.SelMin()), x1 = r.CaretX(f, shown, e.SelMax());
        f32 lh = r.Metrics(f).lineH;
        r.FillRect(RectF(tr.x + x0, rc.y + (rc.h - lh) / 2, x1 - x0, lh), theme.selection);
    }
    TextOpts o;
    o.ellipsis = false;
    if (compLen) r.TextUnderline(f, shown, tr, c, caret, compLen);
    else r.Text(f, shown, tr, c, o);

    if (focused) {
        f32 lh = r.Metrics(f).lineH;
        f32 cx = tr.x + caretX;
        u64 el = GetTickCount64() - e.blinkStart;
        if (compLen || (el / 530) % 2 == 0) r.FillRect(RectF(cx, rc.y + (rc.h - lh) / 2, std::max(1.f, S), lh), compLen ? theme.accent : c);
        UpdateImePosition(cx, rc.y + (rc.h - lh) / 2, lh);
    }
    r.PopClip();

    // mouse selection
    if (Hover(rc)) cursorKind = 3;
    if (in.pressed[0] && Hover(rc)) {
        u32 pos = r.HitTest(f, e.text, in.mx - rc.x + e.scrollX);
        if (in.clickCount >= 2) { e.anchor = 0; e.caret = (u32)e.text.size(); }
        else {
            e.caret = pos;
            if (!in.shift) e.anchor = pos;
            mouseEdit = &e;
        }
        e.blinkStart = GetTickCount64();
    }
    if (mouseEdit == &e) {
        if (!in.down[0]) mouseEdit = nullptr;
        else e.caret = r.HitTest(f, e.text, in.mx - rc.x + e.scrollX);
    }
}

// ================================================================== top level
void App::Draw() {
    cursorKind = 0;
    std::wstring prevTip = tooltip;
    tooltip.clear();
    if (!r.BeginFrame(theme.bg)) return;
    RectF client(0, 0, (f32)r.Width(), (f32)r.Height());
    mouseBlocked = popup.kind != POPUP_NONE;

    RectF rest = client;
    sidebarW = Clamp(sidebarW, 120 * S, client.w * 0.5f);
    titleH = std::floor(r.Metrics(FONT_UI).lineH + 16 * S);
    DrawTitleBar(rest.CutTop(titleH));
    if (setup.active) {
        DrawSetup(rest);
        DrawToasts();
        mouseBlocked = false;
        DrawPopup();
        DrawTooltip();
        if (tooltip.empty()) lastHoverId = 0;
        r.EndFrame(settings.vsync);
        return;
    }
    // sidebar
    if (sidebarOpen) {
        RectF sb = rest.CutLeft(sidebarW);
        DrawSidebar(sb);
        RectF split(sb.Right() - 3 * S, sb.y, 6 * S, sb.h);
        if (Hover(split) || dragSplitter == 1) cursorKind = 1;
        if (Hover(split) && in.pressed[0]) { dragSplitter = 1; in.pressed[0] = false; }
        if (dragSplitter == 1) {
            if (!in.down[0]) { dragSplitter = 0; sessionDirty = true; }
            else { sidebarW = Clamp(in.mx, 120 * S, client.w * 0.5f); Invalidate(); }
        }
        r.Line(sb.Right() - 0.5f, sb.y, sb.Right() - 0.5f, sb.Bottom(), theme.border);
    } else {
        // collapsed sidebar: a slim icon rail so the sidebar can always be reopened
        f32 bh = std::floor(r.Metrics(FONT_UI).lineH + 10 * S);
        RectF rail = rest.CutLeft(bh + 12 * S);
        r.FillSurface(rail, theme.sidebarBg);
        r.Line(rail.Right() - 0.5f, rail.y, rail.Right() - 0.5f, rail.Bottom(), theme.border);
        f32 y = rail.y + 6 * S;
        f32 iconSz = std::floor(r.Metrics(FONT_UI).lineH * 1.05f);
        auto railButton = [&](u64 id, const wchar_t *glyph, const std::wstring &tip, ID2D1Bitmap *bmp = nullptr) {
            RectF b(rail.x + 6 * S, y, bh, bh);
            y += bh + 4 * S;
            if (!bmp) return IconButton(id, b, glyph, tip);
            bool hov = Hover(b);
            if (hov) { r.FillRect(b, in.down[0] ? theme.selectionInactive : theme.hover, 4 * S); SetTooltip(id, tip); }
            r.DrawBitmap(bmp, RectF(b.x + (b.w - iconSz) / 2, b.y + (b.h - iconSz) / 2, iconSz, iconSz));
            return hov && in.pressed[0];
        };
        Tab *cur = CurTab();
        if (railButton(HashId(this, 111), GLYPH_HOME, T(S_Home), HomeIcon()) && cur) Navigate(cur, HomePath());
        if (railButton(HashId(this, 112), GLYPH_PC, T(S_ThisPC), icons.GetSpecial(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}")) && cur) Navigate(cur, PATH_THIS_PC);
        if (railButton(HashId(this, 113), GLYPH_DELETE, T(S_RecycleBin), icons.GetSpecial(L"::{645FF040-5081-101B-9F08-00AA002F954E}")) && cur) Navigate(cur, PATH_RECYCLE);
        if (railButton(HashId(this, 114), GLYPH_STAR, T(S_Bookmarks)) && cur) {
            std::vector<MenuItem> m;
            for (auto &b : user.bookmarks) {
                MenuItem it;
                it.label = PathFileName(b).empty() ? b : PathFileName(b);
                std::wstring p = b;
                it.action = [this, p] { if (Tab *t = CurTab()) Navigate(t, p); };
                m.push_back(it);
            }
            if (m.empty()) { MenuItem it; it.label = TL(L"No bookmarks", L"즐겨찾기 없음"); it.enabled = false; m.push_back(it); }
            OpenMenu(m, rail.Right(), y - bh - 4 * S);
        }
        RectF opt(rail.x + 6 * S, rail.Bottom() - bh - 6 * S, bh, bh);
        if (IconButton(HashId(this, 115), opt, GLYPH_SETTINGS, TL(L"Options", L"옵션") + std::wstring(L"  ") + HotkeyText(L"OptionsOpen")))
            OpenOptions();
    }
    // inspector
    if (inspectorOpen) {
        bool bottom = settings.inspectorLayout == INSPECTOR_BOTTOM;
        inspectorSize = Clamp(inspectorSize, 160 * S, (bottom ? rest.h : rest.w) * 0.8f);
        RectF ir = bottom ? rest.CutBottom(inspectorSize) : rest.CutRight(inspectorSize);
        DrawInspector(ir);
        RectF split = bottom ? RectF(ir.x, ir.y - 3 * S, ir.w, 6 * S) : RectF(ir.x - 3 * S, ir.y, 6 * S, ir.h);
        if (Hover(split) || dragSplitter == 2) cursorKind = bottom ? 2 : 1;
        if (Hover(split) && in.pressed[0]) { dragSplitter = 2; in.pressed[0] = false; }
        if (dragSplitter == 2) {
            if (!in.down[0]) { dragSplitter = 0; sessionDirty = true; }
            else {
                inspectorSize = bottom ? (client.h - in.my) : (client.w - in.mx);
                Invalidate();
            }
        }
        if (bottom) r.Line(ir.x, ir.y + 0.5f, ir.Right(), ir.y + 0.5f, theme.border);
        else r.Line(ir.x + 0.5f, ir.y, ir.x + 0.5f, ir.Bottom(), theme.border);
    }
    if (root) DrawLayout(root.get(), rest);

    DrawToasts();
    cloud.Flush();
    thumbs.Flush();
    mouseBlocked = false;
    DrawPopup();
    DrawTooltip();
    if (tooltip.empty()) lastHoverId = 0;
    r.EndFrame(settings.vsync);
}

void App::DrawLayout(LayoutNode *n, RectF rc) {
    n->rect = rc;
    if (n->leaf) {
        DrawPanel(n->panel.get(), rc);
        return;
    }
    f32 gap = 4 * S;
    RectF a = rc, b = rc, split;
    if (n->vertical) {
        f32 h = std::floor((rc.h - gap) * n->ratio);
        a.h = h;
        b.y = rc.y + h + gap;
        b.h = rc.h - h - gap;
        split = RectF(rc.x, rc.y + h - 1 * S, rc.w, gap + 2 * S);
    } else {
        f32 w = std::floor((rc.w - gap) * n->ratio);
        a.w = w;
        b.x = rc.x + w + gap;
        b.w = rc.w - w - gap;
        split = RectF(rc.x + w - 1 * S, rc.y, gap + 2 * S, rc.h);
    }
    DrawLayout(n->a.get(), a);
    DrawLayout(n->b.get(), b);
    r.FillSurface(n->vertical ? RectF(rc.x, rc.y + a.h, rc.w, gap) : RectF(rc.x + a.w, rc.y, gap, rc.h), theme.bg);
    bool hov = Hover(split);
    if (hov || (dragSplitter == 3 && dragNode == n)) cursorKind = n->vertical ? 2 : 1;
    if (hov && in.pressed[0]) { dragSplitter = 3; dragNode = n; in.pressed[0] = false; }
    if (dragSplitter == 3 && dragNode == n) {
        if (!in.down[0]) { dragSplitter = 0; dragNode = nullptr; sessionDirty = true; }
        else {
            n->ratio = n->vertical ? (in.my - rc.y) / rc.h : (in.mx - rc.x) / rc.w;
            n->ratio = Clamp(n->ratio, 0.1f, 0.9f);
            Invalidate();
        }
    }
}

// ================================================================== title bar
void App::ShowAppMenu(f32 x, f32 y) {
    std::vector<MenuItem> m;
    auto add = [&](const wchar_t *en, const wchar_t *ko, const wchar_t *cmd) {
        MenuItem it;
        it.label = TL(en, ko);
        it.hotkey = HotkeyText(cmd);
        std::wstring c = cmd;
        it.action = [this, c] { RunCommand(c); };
        if (const Command *cc = FindCommand(cmd)) if (cc->checked) it.checked = cc->checked();
        m.push_back(it);
    };
    auto sep = [&] { MenuItem it; it.separator = true; m.push_back(it); };
    add(L"New Window", L"새 창", L"NewWindow");
    add(L"New Tab", L"새 탭", L"NewTab");
    add(L"Reopen Closed Tab", L"닫은 탭 다시 열기", L"ReopenClosedTab");
    sep();
    add(L"Command Palette", L"명령 팔레트", L"CommandsOpen");
    add(L"Go To…", L"이동…", L"GoToOpen");
    sep();
    add(L"Quick Access", L"빠른 실행 사이드바", L"ToggleSidebar");
    add(L"Inspector", L"미리 보기 창", L"InspectorToggle");
    add(L"Split Right", L"오른쪽으로 분할", L"PanelSplitRight");
    add(L"Split Bottom", L"아래로 분할", L"PanelSplitBottom");
    add(L"Show Hidden Files", L"숨김 파일 표시", L"ToggleHiddenFiles");
    sep();
    add(L"Toggle Dark/Light Theme", L"다크/라이트 테마 전환", L"ToggleTheme");
    add(L"Toggle Language (한국어/English)", L"언어 전환 (한국어/English)", L"ToggleLanguage");
    add(L"Options", L"옵션", L"OptionsOpen");
    sep();
    add(L"Exit", L"종료", L"CloseWindow");
    OpenMenu(std::move(m), x, y);
}

int App::NonClientHitTest(int x, int y) {
    f32 fx = (f32)x, fy = (f32)y;
    if (fy >= titleH) return HTCLIENT;
    if (maxButtonRect.Contains(fx, fy)) return HTMAXBUTTON;
    for (auto &rc : captionHits) if (rc.Contains(fx, fy)) return HTCLIENT;
    return HTCAPTION;
}

void App::DrawTitleBar(RectF rc) {
    captionHits.clear();
    maxButtonRect = RectF();
    bool focusedWin = GetForegroundWindow() == hwnd;
    r.FillSurface(rc, theme.headerBg);
    f32 bw = std::floor(rc.h - 8 * S);
    f32 railW = std::floor(r.Metrics(FONT_UI).lineH + 10 * S) + 12 * S;
    // the left zone lines up with the sidebar (or the collapsed rail) below it
    f32 leftW = sidebarOpen ? std::max(sidebarW, 2 * bw + 20 * S) : std::max(railW, 2 * bw + 20 * S);
    RectF left = rc;
    left = left.CutLeft(leftW);
    if (setup.active) {
        r.FillSurface(left, theme.headerBg);
        f32 isz = std::floor(r.Metrics(FONT_UI).lineH);
        if (!setup.logo) {
            HICON ic = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
            setup.logo = r.CreateBitmapFromHIcon(ic);
            if (ic) DestroyIcon(ic);
        }
        if (setup.logo) r.DrawBitmap(setup.logo, RectF(rc.x + 12 * S, rc.y + (rc.h - isz) / 2, isz, isz));
        r.Text(FONT_UI, TL(L"TwinDeck Setup", L"TwinDeck 설치"), RectF(rc.x + 20 * S + isz, rc.y, 300 * S, rc.h), focusedWin ? theme.textDim : theme.textFaint);
    } else {
    r.FillSurface(left, theme.sidebarBg);
    RectF menuB(left.x + 6 * S, rc.y + 4 * S, bw, bw);
    RectF sideB(sidebarOpen ? left.Right() - bw - 6 * S : menuB.Right() + 4 * S, rc.y + 4 * S, bw, bw);
    captionHits.push_back(menuB);
    captionHits.push_back(sideB);
    if (IconButton(HashId(this, 120), menuB, GLYPH_SIDEBAR, TL(L"Menu", L"메뉴"), true, false, FONT_ICON)) {
        in.pressed[0] = false;
        ShowAppMenu(menuB.x, menuB.Bottom() + 2 * S);
    }
    std::wstring sideTip = sidebarOpen ? TL(L"Hide quick access", L"빠른 실행 숨기기") : TL(L"Show quick access", L"빠른 실행 표시");
    if (IconButton(HashId(this, 121), sideB, L"\uE89F", sideTip + L"  " + HotkeyText(L"ToggleSidebar"), true, false, FONT_ICON)) {
        in.pressed[0] = false;
        RunCommand(L"ToggleSidebar");
    }
    }

    // window buttons
    f32 cbw = std::floor(46 * S);
    bool zoomed = IsZoomed(hwnd) != 0;
    RectF closeB(rc.Right() - cbw, rc.y, cbw, rc.h);
    RectF maxB(closeB.x - cbw, rc.y, cbw, rc.h);
    RectF minB(maxB.x - cbw, rc.y, cbw, rc.h);
    maxButtonRect = maxB;
    captionHits.push_back(minB);
    captionHits.push_back(closeB);
    TextOpts co; co.align = ALIGN_CENTER; co.ellipsis = false;
    Color glyphC = focusedWin ? theme.text : theme.textFaint;
    auto winButton = [&](RectF b, const wchar_t *glyph, bool danger, bool hot, bool down) {
        if (hot) r.FillRect(b, danger ? Color::Hex(0xC42B1C) : (down ? theme.selectionInactive : theme.hover));
        r.Text(FONT_ICON_SMALL, glyph, b, hot && danger ? Color(1, 1, 1, 1) : glyphC, co);
    };
    bool minHot = Hover(minB), closeHot = Hover(closeB);
    bool maxHot = ncHoverMax || Hover(maxB);
    winButton(minB, L"\uE921", false, minHot, minHot && in.down[0]);
    winButton(maxB, zoomed ? L"\uE923" : L"\uE922", false, maxHot, maxButtonPressed);
    winButton(closeB, L"\uE8BB", true, closeHot, closeHot && in.down[0]);
    if (minHot) SetTooltip(HashId(this, 130), TL(L"Minimize", L"최소화"));
    if (closeHot) SetTooltip(HashId(this, 132), TL(L"Close", L"닫기"));
    if (minHot && in.released[0]) ShowWindow(hwnd, SW_MINIMIZE);
    if (closeHot && in.released[0]) PostMessageW(hwnd, WM_CLOSE, 0, 0);

    if (setup.active) return;
    // tab list dropdown
    RectF listB(minB.x - bw - 8 * S, rc.y + 4 * S, bw, bw);
    captionHits.push_back(listB);
    Panel *tp = (root && root->leaf) ? root->panel.get() : active;
    if (IconButton(HashId(this, 122), listB, GLYPH_CHEVRON_D, TL(L"All tabs", L"모든 탭")) && tp) {
        in.pressed[0] = false;
        std::vector<MenuItem> m;
        for (int i = 0; i < (int)tp->tabs.size(); i++) {
            MenuItem it;
            Tab *t = tp->tabs[i].get();
            it.label = t->Title();
            it.hotkey = t->kind == VIEW_FOLDER ? t->path : L"";
            it.checked = i == tp->active;
            it.action = [this, tp, i] { tp->active = i; ClearInspector(); UpdateInspector(); Invalidate(); };
            m.push_back(it);
        }
        OpenMenu(std::move(m), listB.Right() - 360 * S, listB.Bottom() + 2 * S);
    }

    // tabs (single panel) or the app title (split layouts keep a tab strip per panel)
    RectF mid(left.Right() + 6 * S, rc.y, listB.x - left.Right() - 12 * S, rc.h);
    if (root && root->leaf && mid.w > 60 * S) {
        r.PushClip(mid);
        RectF strip(mid.x, rc.y + 2 * S, mid.w, rc.h - 2 * S);
        f32 right = DrawTabs(root->panel.get(), strip);
        r.PopClip();
        f32 used = std::min(right, mid.Right());
        r.FillSurface(RectF(used, rc.y, mid.Right() - used, rc.h), theme.headerBg);
        captionHits.push_back(RectF(mid.x, rc.y + 4 * S, used - mid.x, rc.h - 4 * S));
    } else {
        Tab *ct = CurTab();
        std::wstring title = std::wstring(T(S_AppTitle)) + L"  —  " + (ct ? ct->Title() : L"");
        r.Text(FONT_UI, title, RectF(mid.x + 6 * S, rc.y, mid.w, rc.h), focusedWin ? theme.textDim : theme.textFaint);
    }
}

// ================================================================== panel
void App::DrawPanel(Panel *p, RectF rc) {
    p->rect = rc;
    Tab *t = p->Cur();
    if (!t) return;
    if (t->rowsDirty) RebuildRows(t);
    if (in.pressed[0] || in.pressed[1] || in.pressed[2]) {
        if (Hover(rc)) FocusPanel(p);
    }
    r.FillSurface(rc, theme.panelBg);
    f32 lh = r.Metrics(FONT_UI).lineH;
    if (!(root && root->leaf)) {
        RectF tabs = rc.CutTop(std::floor(lh + 14 * S));
        DrawTabs(p, tabs);
    }
    RectF header = rc.CutTop(std::floor(lh + 16 * S));
    DrawHeader(p, t, header);
    RectF status = rc.CutBottom(std::floor(r.Metrics(FONT_SMALL).lineH + 10 * S));
    if (!IsGridView(t)) { // list / icon views have no columns to show
        RectF cols = rc.CutTop(std::floor(lh + 8 * S));
        DrawColumns(p, t, cols);
    }
    p->listRect = rc;
    DrawList(p, t, rc);
    DrawStatus(p, t, status);
    if (p == active && root && !root->leaf) {
        r.StrokeRect(p->rect, theme.accent.WithAlpha(0.55f), std::max(1.f, S));
    }
}

f32 App::DrawTabs(Panel *p, RectF rc) {
    r.FillSurface(rc, theme.headerBg);
    f32 plusW = rc.h;
    RectF area = rc;
    area.w -= plusW;
    int n = (int)p->tabs.size();
    f32 tw = std::min(220 * S, area.w / std::max(1, n));
    for (int i = 0; i < n; i++) {
        Tab *t = p->tabs[i].get();
        RectF tr(area.x + i * tw, rc.y + 3 * S, tw - 2 * S, rc.h - 3 * S);
        bool act = i == p->active;
        bool hov = Hover(tr);
        r.FillSurface(tr, act ? theme.tabActive : theme.tabInactive, act ? 6 * S : 4 * S);
        if (act) r.FillSurface(RectF(tr.x, tr.Bottom() - 6 * S, tr.w, 6 * S), theme.tabActive); // square bottom corners
        else if (hov) r.FillRect(tr, theme.hover, 4 * S);
        RectF closeR(tr.Right() - tr.h, tr.y, tr.h, tr.h);
        // tab icon + title (drive roots show their volume label like "로컬 디스크 (C:)")
        f32 isz = std::floor(r.Metrics(FONT_UI).lineH * 0.95f);
        ID2D1Bitmap *ticon = nullptr;
        std::wstring title = t->Title();
        if (t->kind == VIEW_DRIVES) ticon = icons.GetSpecial(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
        else if (t->kind == VIEW_RECYCLE) ticon = icons.GetSpecial(L"::{645FF040-5081-101B-9F08-00AA002F954E}");
        else if (PathIsRootDrive(t->path)) {
            ticon = icons.GetDrive(t->path.substr(0, 2) + L"\\");
            for (auto &d : sidebarDrives)
                if (_wcsnicmp(d.name.c_str(), t->path.c_str(), 2) == 0) title = DisplayName(d);
        } else if (_wcsicmp(t->path.c_str(), HomePath().c_str()) == 0) {
            ticon = HomeIcon();
        } else if (t->kind == VIEW_FOLDER) {
            ticon = icons.GetFolder(t->path);
        } else {
            FileEntry fe; fe.isDir = true; fe.name = PathFileName(t->path);
            ticon = icons.Get(t->path, fe);
        }
        if (ticon) r.DrawBitmap(ticon, RectF(tr.x + 10 * S, tr.y + (tr.h - isz) / 2, isz, isz), act ? 1.f : 0.8f);
        RectF label(tr.x + 16 * S + isz, tr.y, tr.w - 16 * S - isz - (hov || act ? closeR.w : 4 * S), tr.h);
        if (t->loading && GetTickCount64() - t->loadStartTick > 200) title = L"⟳ " + title;
        r.Text(act ? FONT_UI_BOLD : FONT_UI, title, label, act ? theme.text : theme.textDim);
        bool closeHit = false;
        if (hov || act) {
            closeHit = IconButton(HashId(t, 1), closeR.Shrink(4 * S), GLYPH_CLOSE, TL(L"Close tab", L"탭 닫기"));
            if (closeHit) { CloseTab(p, i); in.pressed[0] = false; return rc.Right(); }
        }
        if (hov) SetTooltip(HashId(t, 2), t->kind == VIEW_FOLDER ? t->path : t->Title());
        if (hov && in.pressed[0]) {
            if (p->active != i) { p->active = i; ClearInspector(); UpdateInspector(); }
            FocusPanel(p);
            Invalidate();
        }
        if (hov && in.pressed[1]) { CloseTab(p, i); return rc.Right(); }
        if (hov && in.pressed[2]) {
            p->active = i;
            std::vector<MenuItem> m;
            MenuItem a; a.label = TL(L"Duplicate Tab", L"탭 복제"); a.action = [this] { RunCommand(L"DuplicateTab"); }; m.push_back(a);
            MenuItem b; b.label = TL(L"Close Tab", L"탭 닫기"); b.hotkey = HotkeyText(L"CloseTab"); b.action = [this] { RunCommand(L"CloseTab"); }; m.push_back(b);
            MenuItem c; c.label = TL(L"Close Other Tabs", L"다른 탭 닫기"); c.action = [this] { RunCommand(L"CloseOtherTabs"); }; m.push_back(c);
            MenuItem d; d.label = TL(L"Duplicate Tab To New Window", L"새 창에서 탭 열기"); d.action = [this] { RunCommand(L"NewWindow"); }; m.push_back(d);
            OpenMenu(m, in.mx, in.my);
        }
    }
    RectF plus(area.x + n * tw, rc.y + 3 * S, plusW, rc.h - 3 * S);
    if (IconButton(HashId(p, 3), plus.Shrink(3 * S), GLYPH_ADD, TL(L"New tab", L"새 탭") + std::wstring(L"  ") + HotkeyText(L"NewTab"))) {
        FocusPanel(p);
        NewTab(p, NewTabPath());
    }
    if (Hover(rc) && in.pressed[0] && in.clickCount >= 2 && in.mx > plus.Right()) NewTab(p, NewTabPath());
    return plus.Right();
}

struct Crumb { std::wstring label, path; };

static std::vector<Crumb> BuildCrumbs(App *a, Tab *t) {
    std::vector<Crumb> v;
    if (t->kind == VIEW_DRIVES) { v.push_back({T(S_ThisPC), PATH_THIS_PC}); return v; }
    if (t->kind == VIEW_RECYCLE) { v.push_back({T(S_RecycleBin), PATH_RECYCLE}); return v; }
    std::wstring p = t->path;
    std::vector<std::wstring> parts;
    while (!p.empty()) {
        parts.push_back(p);
        std::wstring parent = PathParent(p);
        if (parent == p) break;
        p = parent;
    }
    std::reverse(parts.begin(), parts.end());
    v.push_back({T(S_ThisPC), PATH_THIS_PC});
    for (auto &q : parts) {
        std::wstring label = PathFileName(q);
        if (PathIsRootDrive(q)) {
            label = q.substr(0, 2);
        } else if (q.rfind(L"\\\\", 0) == 0 && PathParent(q).empty()) {
            label = q;
        }
        v.push_back({label, q});
    }
    (void)a;
    return v;
}

void App::DrawHeader(Panel *p, Tab *t, RectF rc) {
    r.FillSurface(rc, theme.panelBg);
    r.Line(rc.x, rc.Bottom() - 0.5f, rc.Right(), rc.Bottom() - 0.5f, theme.border);
    RectF in_ = rc.Shrink(4 * S);
    f32 bw = in_.h;
    RectF b1 = in_.CutLeft(bw), b2 = in_.CutLeft(bw), b3 = in_.CutLeft(bw);
    if (IconButton(HashId(p, 10), b1, GLYPH_BACK, std::wstring(T(S_Back)) + L"  " + HotkeyText(L"MoveFolderBack"), !t->back.empty())) {
        FocusPanel(p); GoBack(t);
    }
    if (IconButton(HashId(p, 11), b2, GLYPH_FORWARD, std::wstring(T(S_Forward)) + L"  " + HotkeyText(L"MoveFolderForward"), !t->fwd.empty())) {
        FocusPanel(p); GoForward(t);
    }
    if (IconButton(HashId(p, 12), b3, GLYPH_UP, std::wstring(T(S_Up)) + L"  " + HotkeyText(L"MoveFolderUp"), t->kind == VIEW_FOLDER)) {
        FocusPanel(p); GoUp(t);
    }
    in_.CutLeft(4 * S);
    // right side buttons
    RectF bMenu = in_.CutRight(bw), bInsp = in_.CutRight(bw), bSplit = in_.CutRight(bw), bRec = in_.CutRight(bw);
    if (IconButton(HashId(p, 13), bMenu, GLYPH_COMMAND, TL(L"Command palette", L"명령 팔레트") + std::wstring(L"  ") + HotkeyText(L"CommandsOpen"))) {
        FocusPanel(p); OpenPalette();
    }
    if (IconButton(HashId(p, 14), bInsp, GLYPH_PREVIEW, TL(L"Toggle inspector", L"미리 보기 창") + std::wstring(L"  ") + HotkeyText(L"InspectorToggle"), true, inspectorOpen)) {
        FocusPanel(p); RunCommand(L"InspectorToggle");
    }
    if (IconButton(HashId(p, 15), bSplit, GLYPH_SPLIT_R, TL(L"Split right", L"오른쪽으로 분할") + std::wstring(L"  ") + HotkeyText(L"PanelSplitRight"))) {
        FocusPanel(p); SplitActive(false);
    }
    if (IconButton(HashId(p, 16), bRec, GLYPH_SUBFOLDER, TL(L"Search in subfolders", L"하위 폴더에서 검색") + std::wstring(L"  ") + HotkeyText(L"SearchInSubfolders"),
                   t->kind == VIEW_FOLDER, t->recursive)) {
        FocusPanel(p); RunCommand(L"SearchInSubfolders");
    }
    in_.CutRight(4 * S);
    // filter box
    f32 fw = std::min(240 * S, in_.w * 0.35f);
    RectF fbox = in_.CutRight(fw);
    in_.CutRight(6 * S);
    bool filterFocus = p == active && filterEditing;
    r.FillRect(fbox, theme.inputBg, 5 * S);
    r.StrokeRect(fbox, filterFocus ? theme.accent : theme.border, std::max(1.f, S), 5 * S);
    RectF ficon = fbox;
    ficon = ficon.CutLeft(fbox.h);
    TextOpts io; io.align = ALIGN_CENTER; io.ellipsis = false;
    r.Text(FONT_ICON_SMALL, t->recursive ? GLYPH_SUBFOLDER : GLYPH_SEARCH, ficon, theme.textFaint, io);
    RectF ftext(ficon.Right(), fbox.y, fbox.w - ficon.w - 6 * S, fbox.h);
    std::wstring ph = t->recursive ? TL(L"Search subfolders…", L"하위 폴더 검색…") : T(S_FilterHint);
    if (filterFocus) {
        DrawTextEdit(filterEdit, ftext, FONT_UI, theme.text, ph, true);
    } else {
        if (t->filter.empty()) r.Text(FONT_UI, ph, ftext, theme.textFaint);
        else r.Text(FONT_UI, t->filter, ftext, theme.text);
        if (Hover(fbox)) cursorKind = 3;
        if (Hover(fbox) && in.pressed[0]) { FocusPanel(p); BeginFilter(L""); }
    }

    // path / breadcrumbs
    RectF pbox = in_;
    bool pathFocus = p == active && pathEditing;
    if (pathFocus) r.FillRect(pbox, theme.inputBg, 5 * S);
    else r.FillSurface(pbox, theme.headerBg, 5 * S);
    if (pathFocus) {
        r.StrokeRect(pbox, theme.accent, std::max(1.f, S), 5 * S);
        DrawTextEdit(pathEdit, RectF(pbox.x + 8 * S, pbox.y, pbox.w - 16 * S, pbox.h), FONT_UI, theme.text,
                     TL(L"Type a path…", L"경로 입력…"), true);
        return;
    }
    std::vector<Crumb> crumbs = BuildCrumbs(this, t);
    f32 chevW = 16 * S;
    std::vector<f32> widths;
    f32 total = 0;
    for (auto &c : crumbs) {
        f32 w = r.Measure(FONT_UI, c.label) + 12 * S;
        widths.push_back(w);
        total += w + chevW;
    }
    size_t first = 0;
    f32 avail = pbox.w - 8 * S;
    while (total > avail - (first ? 20 * S : 0) && first + 1 < crumbs.size()) {
        total -= widths[first] + chevW;
        first++;
    }
    r.PushClip(pbox);
    f32 x = pbox.x + 4 * S;
    bool anyCrumbHit = false;
    if (!settings.showBreadcrumbs) {
        std::wstring shown = t->kind == VIEW_FOLDER ? t->path : t->Title();
        r.Text(FONT_UI, shown, RectF(pbox.x + 8 * S, pbox.y, pbox.w - 16 * S, pbox.h), theme.text);
    } else {
        if (first > 0) {
            RectF er(x, pbox.y + 2 * S, 20 * S, pbox.h - 4 * S);
            TextOpts co; co.align = ALIGN_CENTER;
            r.Text(FONT_UI, L"…", er, theme.textDim, co);
            x += 20 * S;
        }
        for (size_t i = first; i < crumbs.size(); i++) {
            RectF cr(x, pbox.y + 2 * S, widths[i], pbox.h - 4 * S);
            bool hov = Hover(cr);
            bool last = i + 1 == crumbs.size();
            if (hov) r.FillRect(cr, theme.hover, 4 * S);
            TextOpts co; co.align = ALIGN_CENTER;
            r.Text(last ? FONT_UI_BOLD : FONT_UI, crumbs[i].label, cr, last ? theme.text : theme.textDim, co);
            if (hov) cursorKind = 4;
            if (hov && in.pressed[0]) { FocusPanel(p); Navigate(t, crumbs[i].path); anyCrumbHit = true; in.pressed[0] = false; }
            if (hov && in.pressed[1]) { NewTab(p, crumbs[i].path); anyCrumbHit = true; }
            x += widths[i];
            if (t->kind == VIEW_FOLDER || i + 1 < crumbs.size() || crumbs[i].path == PATH_THIS_PC) {
                RectF chev(x, pbox.y + 2 * S, chevW, pbox.h - 4 * S);
                bool ch = Hover(chev);
                if (ch) r.FillRect(chev, theme.hover, 3 * S);
                TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
                r.Text(FONT_ICON_SMALL, GLYPH_CHEVRON_R, chev, theme.textFaint, ko);
                if (ch && in.pressed[0]) {
                    FocusPanel(p);
                    ShowFolderDropdown(t, crumbs[i].path, chev.x, pbox.Bottom());
                    anyCrumbHit = true;
                    in.pressed[0] = false;
                }
                x += chevW;
            }
        }
    }
    r.PopClip();
    if (!anyCrumbHit && Hover(pbox)) {
        cursorKind = 3;
        if (in.pressed[0]) { FocusPanel(p); BeginPathEdit(); }
    }
    if (Hover(pbox) && t->kind == VIEW_FOLDER) SetTooltip(HashId(p, 17), t->path);
}

std::vector<ColumnId> App::Columns(Tab *t) const {
    switch (t->kind) {
    case VIEW_DRIVES: return {COL_NAME, COL_TYPE, COL_TOTAL, COL_FREE, COL_PERCENT, COL_FS};
    case VIEW_RECYCLE: return {COL_NAME, COL_ORIGLOC, COL_DELETED, COL_SIZE, COL_TYPE};
    default:
        if (t->cloud) return {COL_NAME, COL_STATUS, COL_MODIFIED, COL_TYPE, COL_SIZE};
        return {COL_NAME, COL_MODIFIED, COL_TYPE, COL_SIZE};
    }
}

const wchar_t *App::ColumnName(ColumnId c) const {
    switch (c) {
    case COL_NAME: return T(S_ColName);
    case COL_MODIFIED: return T(S_ColModified);
    case COL_CREATED: return T(S_ColCreated);
    case COL_TYPE: return T(S_ColType);
    case COL_SIZE: return T(S_ColSize);
    case COL_FREE: return T(S_ColFree);
    case COL_TOTAL: return T(S_ColTotal);
    case COL_PERCENT: return T(S_ColPercent);
    case COL_FS: return T(S_ColFileSystem);
    case COL_ORIGLOC: return T(S_ColOrigLocation);
    case COL_DELETED: return T(S_ColDeleted);
    case COL_STATUS: return TL(L"Status", L"상태");
    default: return L"";
    }
}

static SortKey SortForColumn(ColumnId c) {
    switch (c) {
    case COL_MODIFIED: return SORT_MODIFIED;
    case COL_CREATED: return SORT_CREATED;
    case COL_TYPE: return SORT_TYPE;
    case COL_SIZE: return SORT_SIZE;
    case COL_FREE: return SORT_FREE;
    case COL_TOTAL: return SORT_TOTAL;
    case COL_PERCENT: return SORT_PERCENT;
    case COL_FS: return SORT_FS;
    case COL_ORIGLOC: return SORT_ORIGLOC;
    case COL_DELETED: return SORT_DELETED;
    default: return SORT_NAME;
    }
}

static bool RightAligned(ColumnId c) { return c == COL_SIZE || c == COL_FREE || c == COL_TOTAL; }

// drops low-priority columns so the name column keeps a readable width in narrow panels
static std::vector<ColumnId> FitColumns(App *a, Panel *p, Tab *t, f32 width) {
    std::vector<ColumnId> cols = a->Columns(t);
    static const ColumnId dropOrder[] = {COL_TYPE, COL_CREATED, COL_FS, COL_ORIGLOC, COL_DELETED, COL_MODIFIED,
                                         COL_STATUS, COL_PERCENT, COL_TOTAL, COL_FREE, COL_SIZE};
    f32 minName = std::max(160 * a->S, width * 0.38f);
    for (ColumnId d : dropOrder) {
        f32 fixed = 0;
        for (size_t i = 1; i < cols.size(); i++) fixed += p->colW[cols[i]];
        if (width - fixed >= minName) break;
        auto it = std::find(cols.begin() + 1, cols.end(), d);
        if (it != cols.end()) cols.erase(it);
    }
    return cols;
}

// computes x/width per column for the given row rect
static void ColumnLayout(App *a, Panel *p, const std::vector<ColumnId> &cols, RectF rc, std::vector<RectF> &out) {
    out.clear();
    f32 fixed = 0;
    for (size_t i = 1; i < cols.size(); i++) fixed += p->colW[cols[i]];
    f32 nameW = std::max(120 * a->S, rc.w - fixed);
    f32 x = rc.x;
    out.push_back(RectF(x, rc.y, nameW, rc.h));
    x += nameW;
    for (size_t i = 1; i < cols.size(); i++) {
        out.push_back(RectF(x, rc.y, p->colW[cols[i]], rc.h));
        x += p->colW[cols[i]];
    }
}

// OneDrive-style sync status icon (same meaning as Explorer's "Status" column)
void App::DrawCloudState(int st, RectF cell, u64 tipId) {
    if (st <= CLOUD_NONE || st == CLOUD_EXCLUDED) return;
    f32 d = std::floor(r.Metrics(FONT_UI).lineH * 0.72f);
    RectF c(std::floor(cell.x + 10 * S), std::floor(cell.y + (cell.h - d) / 2), d, d);
    Color green = theme.dark ? Color::Hex(0x6ccb5f) : Color::Hex(0x0f7b0f);
    Color blue = theme.dark ? Color::Hex(0x60cdff) : Color::Hex(0x0067c0);
    auto check = [&](Color col) {
        f32 w = std::max(1.2f, 1.5f * S);
        r.Line(c.x + d * 0.27f, c.y + d * 0.52f, c.x + d * 0.44f, c.y + d * 0.68f, col, w);
        r.Line(c.x + d * 0.43f, c.y + d * 0.68f, c.x + d * 0.74f, c.y + d * 0.34f, col, w);
    };
    TextOpts go; go.align = ALIGN_CENTER; go.ellipsis = false;
    const wchar_t *tip = L"";
    switch (st) {
    case CLOUD_ONLINE_ONLY:
        r.Text(FONT_ICON_SMALL, L"", RectF(c.x - 2 * S, cell.y, d + 4 * S, cell.h), blue, go);
        tip = TL(L"Available when online", L"온라인일 때 사용 가능");
        break;
    case CLOUD_IN_SYNC:
        r.StrokeRect(c, green, std::max(1.f, 1.2f * S), d / 2);
        check(green);
        tip = TL(L"Available on this device", L"이 디바이스에서 사용 가능");
        break;
    case CLOUD_PINNED:
        r.FillRect(c, green, d / 2);
        check(theme.dark ? Color::Hex(0x1b1d22) : Color(1, 1, 1, 1));
        tip = TL(L"Always available on this device", L"항상 이 디바이스에서 사용 가능");
        break;
    case CLOUD_ERROR:
        r.FillRect(c, theme.danger, d / 2);
        r.Text(FONT_ICON_SMALL, GLYPH_CLOSE, c, Color(1, 1, 1, 1), go);
        tip = TL(L"Sync error", L"동기화 오류");
        break;
    case CLOUD_WARNING:
        r.Text(FONT_ICON_SMALL, L"", RectF(c.x - 2 * S, cell.y, d + 4 * S, cell.h), Color::Hex(0xf2b600), go);
        tip = TL(L"Needs attention", L"확인 필요");
        break;
    default: // pending upload / download, transferring
        r.Text(FONT_ICON_SMALL, L"", RectF(c.x - 2 * S, cell.y, d + 4 * S, cell.h), blue, go);
        tip = TL(L"Sync pending", L"동기화 보류 중");
        break;
    }
    if (Hover(RectF(c.x - 4 * S, cell.y, d + 8 * S, cell.h))) SetTooltip(tipId, tip);
}

void App::DrawColumns(Panel *p, Tab *t, RectF rc) {
    r.FillSurface(rc, theme.panelBg);
    r.Line(rc.x, rc.Bottom() - 0.5f, rc.Right(), rc.Bottom() - 0.5f, theme.border);
    RectF inner(rc.x, rc.y, rc.w - 10 * S, rc.h);
    auto cols = FitColumns(this, p, t, inner.w);
    std::vector<RectF> cr;
    ColumnLayout(this, p, cols, inner, cr);
    for (size_t i = 0; i < cols.size(); i++) {
        RectF c = cr[i];
        bool hov = Hover(c);
        if (hov) r.FillRect(c, theme.hover);
        bool sorted = cols[i] != COL_STATUS && SortForColumn(cols[i]) == t->sort;
        TextOpts o;
        o.align = RightAligned(cols[i]) ? ALIGN_RIGHT : ALIGN_LEFT;
        RectF tr(c.x + (i == 0 ? 12 * S : 8 * S), c.y, c.w - 16 * S - (sorted ? 14 * S : 0), c.h);
        r.Text(sorted ? FONT_UI_BOLD : FONT_SMALL, ColumnName(cols[i]), tr, sorted ? theme.text : theme.textDim, o);
        if (sorted) {
            TextOpts so; so.align = ALIGN_CENTER; so.ellipsis = false;
            r.Text(FONT_ICON_SMALL, t->desc ? GLYPH_SORT_DOWN : GLYPH_SORT_UP, RectF(c.Right() - 20 * S, c.y, 16 * S, c.h), theme.textDim, so);
        }
        if (i > 0) r.Line(c.x + 0.5f, c.y + 6 * S, c.x + 0.5f, c.Bottom() - 6 * S, theme.border);
        // resize handle at the left edge of fixed columns
        if (i > 0) {
            RectF h(c.x - 4 * S, c.y, 8 * S, c.h);
            if (Hover(h) || dragColumn == (int)cols[i]) cursorKind = 1;
            if (Hover(h) && in.pressed[0]) {
                dragColumn = cols[i];
                dragColumnStart = in.mx;
                dragColumnW = p->colW[cols[i]];
                in.pressed[0] = false;
            }
            if (dragColumn == (int)cols[i]) {
                if (!in.down[0]) dragColumn = -1;
                else { p->colW[cols[i]] = Clamp(dragColumnW - (in.mx - dragColumnStart), 50 * S, 600 * S); Invalidate(); }
            }
        }
        if (hov && in.pressed[0] && cols[i] != COL_STATUS) {
            FocusPanel(p);
            SortKey k = SortForColumn(cols[i]);
            if (t->sort == k) t->desc = !t->desc;
            else { t->sort = k; t->desc = k != SORT_NAME && k != SORT_TYPE && k != SORT_FS && k != SORT_ORIGLOC; }
            t->rowsDirty = true;
            sessionDirty = true;
        }
        if (hov && in.pressed[2]) {
            std::vector<MenuItem> m;
            auto add = [&](const wchar_t *en, const wchar_t *ko, const wchar_t *cmd, GroupKey g) {
                MenuItem it; it.label = TL(en, ko); it.checked = t->group == g;
                std::wstring c2 = cmd; it.action = [this, c2] { RunCommand(c2); }; m.push_back(it);
            };
            add(L"No Grouping", L"그룹 없음", L"GroupByNone", GROUP_NONE);
            add(L"Group by Name", L"이름(초성)으로 그룹화", L"GroupByName", GROUP_NAME);
            add(L"Group by Type", L"유형으로 그룹화", L"GroupByType", GROUP_TYPE);
            add(L"Group by Size", L"크기로 그룹화", L"GroupBySize", GROUP_SIZE);
            add(L"Group by Date Modified", L"수정한 날짜로 그룹화", L"GroupByDateModified", GROUP_MODIFIED);
            add(L"Group by Date Created", L"만든 날짜로 그룹화", L"GroupByDateCreated", GROUP_CREATED);
            if (t->kind == VIEW_RECYCLE) {
                add(L"Group by Date Deleted", L"삭제한 날짜로 그룹화", L"GroupByDateDeleted", GROUP_DELETED);
                add(L"Group by Original Location", L"원래 위치로 그룹화", L"GroupByOriginalLocation", GROUP_ORIGLOC);
            }
            if (t->kind == VIEW_DRIVES) {
                add(L"Group by File System", L"파일 시스템으로 그룹화", L"GroupByFileSystem", GROUP_FS);
                add(L"Group by Percent Full", L"사용률로 그룹화", L"GroupByPercentFull", GROUP_PERCENT);
            }
            OpenMenu(m, in.mx, in.my);
        }
    }
}

std::wstring App::CellText(Tab *t, const Row &row, ColumnId c) {
    const FileEntry &e = *row.e;
    switch (c) {
    case COL_MODIFIED: return FormatFileTime(e.modified);
    case COL_CREATED: return FormatFileTime(e.created);
    case COL_DELETED: return FormatFileTime(e.deleted);
    case COL_ORIGLOC: return e.origLocation;
    case COL_FS: return e.fsName;
    case COL_TYPE:
        if (e.isDrive) {
            switch (e.driveType) {
            case DRIVE_REMOVABLE: return T(S_Removable);
            case DRIVE_REMOTE: return T(S_Network);
            case DRIVE_CDROM: return T(S_CDROM);
            default: return T(S_Volume);
            }
        }
        return FileTypeName(e.name, e.isDir);
    case COL_SIZE:
        if (e.isDir && !e.isRecycled) {
            auto st = FolderSize(RowPath(row));
            if (!st) return L"";
            if (st->done) return FormatSize(st->size);
            if (GetTickCount64() - st->startTick > 300) return FormatSize(st->size) + L"…";
            return L"";
        }
        return FormatSize(e.size);
    case COL_FREE: return e.totalBytes ? FormatSize(e.freeBytes) : L"";
    case COL_TOTAL: return e.totalBytes ? FormatSize(e.totalBytes) : L"";
    case COL_PERCENT:
        if (!e.totalBytes) return L"";
        return Format(L"%.0f%%", 100.0 * (1.0 - (f64)e.freeBytes / e.totalBytes));
    default: return L"";
    }
    (void)t;
}

static std::vector<std::pair<u32, u32>> MatchRanges(const std::wstring &name, const std::wstring &filter) {
    std::vector<std::pair<u32, u32>> v;
    if (filter.empty()) return v;
    std::wstring ln = ToLower(name), lf = ToLower(filter);
    size_t pos = ln.find(lf);
    if (pos != std::wstring::npos) { v.push_back({(u32)pos, (u32)lf.size()}); return v; }
    // initial-consonant (초성) matches: highlight matched syllables one by one
    std::wstring cho = HangulChoseong(ln);
    pos = cho.find(lf);
    if (pos != std::wstring::npos) { v.push_back({(u32)pos, (u32)lf.size()}); return v; }
    // fuzzy subsequence
    size_t ti = 0;
    for (wchar_t pc : lf) {
        if (pc == L' ') continue;
        while (ti < ln.size() && ln[ti] != pc && cho[ti] != pc) ti++;
        if (ti >= ln.size()) break;
        v.push_back({(u32)ti, 1});
        ti++;
    }
    return v;
}

// mouse press on an item row/cell (shared by all view modes). Returns true when rows may have been rebuilt.
bool App::RowPress(Tab *t, int i, const std::wstring &path, bool sel, bool toggleSel) {
    const FileEntry &e = *t->rows[i].e;
    if (in.pressed[0]) {
        if (focus == FOCUS_RENAME && path != renamePath) CommitRename();
        if (pathEditing) EndPathEdit(false);
        g_pressTab = t;
        g_pressRow = i;
        g_pressWasSelected = sel;
        dragPending = true;
        if (in.ctrl || toggleSel) {
            if (sel) t->selected.erase(path); else t->selected.insert(path);
            t->cursor = i;
            t->anchor = i;
            UpdateInspector();
        } else if (in.shift) {
            SetCursor(t, i, true);
        } else if (!sel) {
            SelectOnly(t, i);
        } else {
            t->cursor = i;
        }
        if (in.clickCount >= 2 && !in.ctrl && !in.shift) {
            dragPending = false;
            OpenRow(t, i, 0);
            in.pressed[0] = false;
            return true;
        }
        in.pressed[0] = false;
    }
    if (in.pressed[1]) {
        if (e.isDir) OpenRow(t, i, 1);
        in.pressed[1] = false;
    }
    if (in.pressed[2]) {
        if (!sel) SelectOnly(t, i);
        if (focus == FOCUS_RENAME) CommitRename();
        ShowContextMenu(t, in.mx, in.my, false);
        in.pressed[2] = false;
    }
    return false;
}

// blank-area clicks, click-release and drag start (shared by all view modes)
void App::ListMouseTail(Tab *t, RectF rc, bool hovList, int hoverRow, f32 sbW) {
    int n = (int)t->rows.size();
    // blank area clicks
    if (hovList && hoverRow < 0 && in.mx < rc.Right() - sbW) {
        if (in.pressed[0]) {
            if (focus == FOCUS_RENAME) CommitRename();
            if (pathEditing) EndPathEdit(false);
            if (in.clickCount >= 2 && settings.doubleClickToParent) GoUp(t);
            else if (!in.ctrl && !in.shift) { t->selected.clear(); UpdateInspector(); }
        }
        if (in.pressed[2]) {
            t->selected.clear();
            ShowContextMenu(t, in.mx, in.my, true);
        }
    }

    // release / drag
    if (g_pressTab == t) {
        if (in.released[0]) {
            if (dragPending && g_pressRow >= 0 && g_pressRow < n && !in.ctrl && !in.shift) {
                const Row &pr = t->rows[g_pressRow];
                if (g_pressWasSelected && t->selected.size() > 1) SelectOnly(t, g_pressRow);
                bool single = settings.singleClick || (settings.singleClickFoldersOnly && pr.e && pr.e->isDir);
                if (single && !pr.header) OpenRow(t, g_pressRow, 0);
            }
            dragPending = false;
            g_pressTab = nullptr;
        } else if (dragPending && in.down[0]) {
            f32 dx = in.mx - in.pressX, dy = in.my - in.pressY;
            if (dx * dx + dy * dy > 36 * S * S && t->kind == VIEW_FOLDER) {
                dragPending = false;
                g_pressTab = nullptr;
                std::vector<std::wstring> paths = SelectedPaths(t);
                if (!paths.empty()) {
                    in.down[0] = false;
                    DWORD eff = DragFiles(hwnd, paths);
                    (void)eff;
                }
            }
        }
    }
}


// wheel (Ctrl+wheel zooms the view like Explorer), smooth scrolling
void App::ListScroll(Tab *t, RectF rc, f32 contentH, f32 step) {
    f32 maxScroll = std::max(0.f, contentH - rc.h);
    if (Hover(rc) && in.wheel != 0) {
        if (in.ctrl) {
            ZoomView(t, in.wheel);
            in.wheel = 0;
        } else {
            f32 d = in.wheel * step * (settings.reverseScroll ? -1 : 1);
            t->scrollTarget = Clamp(t->scrollTarget - d, 0.f, maxScroll);
        }
    }
    t->scrollTarget = Clamp(t->scrollTarget, 0.f, maxScroll);
    if (settings.animations) {
        f32 diff = t->scrollTarget - t->scroll;
        if (fabsf(diff) > 0.5f) { t->scroll += diff * 0.35f; Invalidate(); }
        else t->scroll = t->scrollTarget;
    } else t->scroll = t->scrollTarget;
    t->scroll = Clamp(t->scroll, 0.f, maxScroll);
}

// empty folder / loading / error message; returns true when there is nothing to list
static bool DrawEmptyState(App *a, Tab *t, RectF rc) {
    if (!t->rows.empty()) return false;
    std::wstring msg;
    if (t->loading) { if (GetTickCount64() - t->loadStartTick > 150) msg = T(S_Loading); else a->Invalidate(); }
    else if (t->error == LIST_ACCESS_DENIED) msg = T(S_AccessDenied);
    else if (t->error == LIST_NOT_FOUND) msg = T(S_PathNotFound);
    else if (t->recursive && t->search && !t->search->done) msg = T(S_SearchSubfolders);
    else if (!t->filter.empty()) msg = T(S_NoMatch);
    else msg = T(S_Empty);
    TextOpts o; o.align = ALIGN_CENTER;
    a->r.Text(FONT_UI, msg, RectF(rc.x, rc.y + 20 * a->S, rc.w, a->r.Metrics(FONT_UI).lineH * 2), a->theme.textFaint, o);
    return true;
}

void App::DrawList(Panel *p, Tab *t, RectF rc) {
    if (IsGridView(t)) { DrawGrid(p, t, rc); return; }
    t->viewH = rc.h;
    f32 rh = RowHeight();
    int n = (int)t->rows.size();
    f32 contentH = n * rh;
    bool hovList = Hover(rc);
    ListScroll(t, rc, contentH, rh * 3);
    if (IsGridView(t)) { DrawGrid(p, t, rc); return; } // Ctrl+wheel switched to a grid view
    if (t->ensureCursor) {
        t->ensureCursor = false;
        t->grid.width = rc.w;
        EnsureVisible(t, t->cursor);
        t->scroll = t->scrollTarget;
    }

    r.PushClip(rc);
    bool focused = p == active;
    f32 sbW = 10 * S;
    auto cols = FitColumns(this, p, t, rc.w - sbW);
    std::vector<RectF> cr;
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 iconSz = std::min(rh - 4 * S, std::floor(lh * 1.05f));

    DrawEmptyState(this, t, rc);

    int first = std::max(0, (int)(t->scroll / rh));
    int last = std::min(n - 1, (int)((t->scroll + rc.h) / rh) + 1);
    int hoverRow = -1;
    if (hovList && in.mx < rc.Right() - sbW) { // the scrollbar strip is not part of any row
        int hr = (int)((in.my - rc.y + t->scroll) / rh);
        if (hr >= 0 && hr < n) hoverRow = hr;
    }
    std::wstring filterShown = t->recursive ? L"" : t->filter;
    bool anySizeRunning = false;

    for (int i = first; i <= last; i++) {
        const Row &row = t->rows[i];
        f32 y = rc.y + i * rh - t->scroll;
        RectF rr(rc.x, std::floor(y), rc.w - sbW, rh);
        if (row.header) {
            GroupInfo &g = t->groups[row.group];
            if (i == hoverRow) r.FillRect(rr, theme.hover);
            if (i == t->cursor && focused && focus == FOCUS_LIST) r.StrokeRect(rr.Shrink(1 * S), theme.accent.WithAlpha(0.5f), 1 * S, 3 * S);
            TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
            r.Text(FONT_ICON_SMALL, g.collapsed ? GLYPH_CHEVRON_R : GLYPH_CHEVRON_D, RectF(rr.x + 6 * S, rr.y, 16 * S, rr.h), theme.textDim, ko);
            std::wstring label = Format(L"%s  ·  %d", g.label.c_str(), g.count);
            r.Text(FONT_UI_BOLD, label, RectF(rr.x + 26 * S, rr.y, rr.w - 30 * S, rr.h), theme.text);
            r.Line(rr.x + 26 * S + r.Measure(FONT_UI_BOLD, label) + 10 * S, rr.y + rr.h / 2, rr.Right() - 10 * S, rr.y + rr.h / 2, theme.border);
            if (i == hoverRow && in.pressed[0]) { t->cursor = i; ToggleExpand(t, i, 0); in.pressed[0] = false; break; }
            continue;
        }
        const FileEntry &e = *row.e;
        std::wstring path = RowPath(row);
        bool sel = t->selected.count(path) > 0;
        if (sel) r.FillRect(rr.Shrink(0.5f * S), focused ? theme.selection : theme.selectionInactive, settings.roundCorners ? 4 * S : 0);
        else if (i == hoverRow) r.FillRect(rr, theme.hover, settings.roundCorners ? 4 * S : 0);
        else if (settings.alternatingRowColors && (i & 1)) r.FillRect(rr, theme.rowAlt);
        if (i == t->cursor && focused && !sel && focus == FOCUS_LIST)
            r.StrokeRect(rr.Shrink(1 * S), theme.accent.WithAlpha(0.6f), 1 * S, settings.roundCorners ? 4 * S : 0);
        if (dropTab == t && dropRow == i) r.StrokeRect(rr.Shrink(1 * S), theme.accent, 2 * S, 4 * S);

        ColumnLayout(this, p, cols, rr, cr);
        RectF nameR = cr[0];
        f32 x = nameR.x + 6 * S + row.depth * 16 * S;
        // checkbox
        if (settings.showCheckboxes) {
            RectF cb(x, rr.y + (rh - 14 * S) / 2, 14 * S, 14 * S);
            r.StrokeRect(cb, sel ? theme.accent : theme.textFaint, std::max(1.f, S), 3 * S);
            if (sel) {
                r.FillRect(cb, theme.accent, 3 * S);
                TextOpts co; co.align = ALIGN_CENTER; co.ellipsis = false;
                r.Text(FONT_ICON_SMALL, GLYPH_CHECK, cb, theme.accentText, co);
            }
            x += 20 * S;
        }
        // tree chevron
        RectF chev(x, rr.y, 16 * S, rh);
        bool expanded = false;
        if (e.isDir && t->kind == VIEW_FOLDER) {
            expanded = t->expanded.count(path) > 0;
            if (expanded || i == hoverRow || row.depth > 0 || !t->expanded.empty() || t->viewMode == LV_TREE) {
                TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
                r.Text(FONT_ICON_SMALL, expanded ? GLYPH_CHEVRON_D : GLYPH_CHEVRON_R, chev,
                       Hover(chev) ? theme.text : theme.textFaint, ko);
            }
        }
        x += 16 * S;
        // icon
        ID2D1Bitmap *icon = icons.Get(path, e);
        bool cut = cutPaths.count(path) > 0;
        bool hidden = e.Hidden();
        if (icon) r.DrawBitmap(icon, RectF(x, rr.y + (rh - iconSz) / 2, iconSz, iconSz), (cut || hidden) ? 0.5f : 1.f);
        x += iconSz + 8 * S;
        RectF textR(x, rr.y, nameR.Right() - x - 6 * S, rh);
        if (focus == FOCUS_RENAME && path == renamePath && focused) {
            RectF er(textR.x - 4 * S, rr.y + 2 * S, textR.w + 4 * S, rh - 4 * S);
            r.FillRect(er, theme.inputBg, 3 * S);
            r.StrokeRect(er, theme.accent, std::max(1.f, S), 3 * S);
            DrawTextEdit(renameEdit, RectF(textR.x, rr.y, textR.w - 4 * S, rh), FONT_UI, theme.text, L"", true);
        } else {
            std::wstring name = DisplayName(e);
            if (!filterShown.empty()) r.TextHighlightRanges(FONT_UI, name, textR, MatchRanges(name, filterShown), theme.highlight);
            Color tc = (cut || hidden) ? theme.textDim : theme.text;
            f32 tw = r.Text(FONT_UI, name, textR, tc);
            if (!e.relDir.empty()) {
                // recursive search: show where the item lives
                RectF dr(textR.x + tw + 10 * S, rr.y, textR.Right() - textR.x - tw - 10 * S, rh);
                r.Text(FONT_SMALL, e.relDir, dr, theme.textFaint);
            }
            if (settings.underlineOnHover && i == hoverRow) {
                f32 uy = rr.y + rh / 2 + lh / 2 - 2 * S;
                r.Line(textR.x, uy, textR.x + std::min(tw, textR.w), uy, tc, 1 * S);
            }
        }
        // other cells
        for (size_t c = 1; c < cols.size(); c++) {
            RectF cell(cr[c].x + 8 * S, rr.y, cr[c].w - 16 * S, rh);
            if (cols[c] == COL_PERCENT && e.totalBytes) {
                f32 pct = (f32)(1.0 - (f64)e.freeBytes / e.totalBytes);
                RectF bar(cell.x, rr.y + rh / 2 - 3 * S, cell.w - 40 * S, 6 * S);
                r.FillRect(bar, theme.border, 3 * S);
                r.FillRect(RectF(bar.x, bar.y, bar.w * pct, bar.h), pct > 0.9f ? theme.danger : theme.accent, 3 * S);
                TextOpts po; po.align = ALIGN_RIGHT;
                r.Text(FONT_SMALL, CellText(t, row, COL_PERCENT), cell, theme.textDim, po);
                continue;
            }
            if (cols[c] == COL_STATUS) {
                DrawCloudState(cloud.Get(path), RectF(cr[c].x, rr.y, cr[c].w, rh), HashStr(path, 11));
                continue;
            }
            std::wstring s = CellText(t, row, cols[c]);
            if (s.empty()) continue;
            if (cols[c] == COL_SIZE && e.isDir && s.back() == L'…') anySizeRunning = true;
            TextOpts o;
            o.align = RightAligned(cols[c]) ? ALIGN_RIGHT : ALIGN_LEFT;
            r.Text(FONT_UI, s, cell, cols[c] == COL_SIZE && e.isDir ? theme.textFaint : theme.textDim, o);
        }

        // ---- mouse on this row
        if (i == hoverRow) {
            if (r.Measure(FONT_UI, DisplayName(e)) > textR.w && Hover(textR)) SetTooltip(HashStr(path), DisplayName(e));
            if (in.pressed[0] && e.isDir && t->kind == VIEW_FOLDER && Hover(chev)) {
                if (focus == FOCUS_RENAME && path != renamePath) CommitRename();
                if (pathEditing) EndPathEdit(false);
                ToggleExpand(t, i, 0);
                in.pressed[0] = false;
                break;
            }
            if (RowPress(t, i, path, sel, settings.showCheckboxes && in.mx < chev.x)) break;
        }
    }
    if (anySizeRunning) Invalidate();
    ListMouseTail(t, rc, hovList, hoverRow, sbW);
    if (dropTab == t && dropRow < 0) r.StrokeRect(rc.Shrink(1 * S), theme.accent, 2 * S, 4 * S);
    r.PopClip();
    DrawScrollbar(RectF(rc.Right() - sbW, rc.y, sbW, rc.h), contentH, rc.h, t->scrollTarget, HashId(t, 50));
    if (activeId == HashId(t, 50)) t->scroll = t->scrollTarget;
}

// two-line name for the icon view, ending in "…" when it does not fit (cached per name / width)
static std::wstring FitTwoLines(Renderer &r, const std::wstring &s, f32 w) {
    static std::unordered_map<std::wstring, std::wstring> cache;
    f32 maxH = r.Metrics(FONT_UI).lineH * 2 + 1;
    std::wstring key = s + L"\x1f" + std::to_wstring((int)w) + L"\x1f" + std::to_wstring((int)maxH);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::wstring out = s;
    if (r.MeasureWrapped(FONT_UI, s, w) > maxH) {
        size_t lo = 1, hi = s.size();
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (r.MeasureWrapped(FONT_UI, s.substr(0, mid) + L"…", w) <= maxH) lo = mid; else hi = mid - 1;
        }
        out = s.substr(0, lo) + L"…";
    }
    if (cache.size() > 4000) cache.clear();
    cache[key] = out;
    return out;
}

// list (small icons in columns) and icon views
void App::DrawGrid(Panel *p, Tab *t, RectF rc) {
    t->viewH = rc.h;
    f32 sbW = 10 * S;
    UpdateGrid(t, rc.w - sbW);
    bool hovList = Hover(rc);
    ListScroll(t, rc, ListContentHeight(t), std::max(t->grid.cellH, RowHeight() * 3));
    if (!IsGridView(t)) { DrawList(p, t, rc); return; } // Ctrl+wheel went back to details
    UpdateGrid(t, rc.w - sbW);
    const GridLayout &g = t->grid;
    if (t->ensureCursor) {
        t->ensureCursor = false;
        EnsureVisible(t, t->cursor);
        t->scroll = t->scrollTarget;
    }

    r.PushClip(rc);
    DrawEmptyState(this, t, rc);
    bool focused = p == active;
    bool iconsView = t->viewMode == LV_ICONS;
    int n = (int)t->rows.size();
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 smallSz = std::floor(lh * 1.05f);
    f32 rad = settings.roundCorners ? 5 * S : 0;
    int first = (int)(std::partition_point(g.items.begin(), g.items.end(),
                                           [&](const RectF &it) { return it.Bottom() <= t->scroll; }) - g.items.begin());
    int hoverRow = hovList && in.mx < rc.Right() - sbW ? ItemAtPoint(t, in.mx - rc.x, in.my - rc.y + t->scroll) : -1;
    std::wstring filterShown = t->recursive ? L"" : t->filter;

    for (int i = first; i < n && i < (int)g.items.size(); i++) {
        RectF cr = g.items[i];
        cr.x += rc.x;
        cr.y += rc.y - t->scroll;
        if (cr.y > rc.Bottom()) break;
        const Row &row = t->rows[i];
        if (row.header) {
            GroupInfo &gi = t->groups[row.group];
            if (i == hoverRow) r.FillRect(cr, theme.hover, rad);
            if (i == t->cursor && focused && focus == FOCUS_LIST) r.StrokeRect(cr.Shrink(1 * S), theme.accent.WithAlpha(0.5f), 1 * S, 3 * S);
            TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
            r.Text(FONT_ICON_SMALL, gi.collapsed ? GLYPH_CHEVRON_R : GLYPH_CHEVRON_D, RectF(cr.x + 6 * S, cr.y, 16 * S, cr.h), theme.textDim, ko);
            std::wstring label = Format(L"%s  ·  %d", gi.label.c_str(), gi.count);
            r.Text(FONT_UI_BOLD, label, RectF(cr.x + 26 * S, cr.y, cr.w - 30 * S, cr.h), theme.text);
            r.Line(cr.x + 26 * S + r.Measure(FONT_UI_BOLD, label) + 10 * S, cr.y + cr.h / 2, cr.Right() - 10 * S, cr.y + cr.h / 2, theme.border);
            if (i == hoverRow && in.pressed[0]) { t->cursor = i; ToggleExpand(t, i, 0); in.pressed[0] = false; break; }
            continue;
        }
        const FileEntry &e = *row.e;
        std::wstring path = RowPath(row);
        bool sel = t->selected.count(path) > 0;
        RectF cell = cr.Shrink(2 * S);
        if (sel) r.FillRect(cell, focused ? theme.selection : theme.selectionInactive, rad);
        else if (i == hoverRow) r.FillRect(cell, theme.hover, rad);
        if (i == t->cursor && focused && !sel && focus == FOCUS_LIST) r.StrokeRect(cell, theme.accent.WithAlpha(0.6f), 1 * S, rad);
        if (dropTab == t && dropRow == i) r.StrokeRect(cell, theme.accent, 2 * S, rad);
        bool cut = cutPaths.count(path) > 0;
        f32 alpha = (cut || e.Hidden()) ? 0.5f : 1.f;
        Color tc = (cut || e.Hidden()) ? theme.textDim : theme.text;
        bool renaming = focus == FOCUS_RENAME && path == renamePath && focused;
        std::wstring name = DisplayName(e);
        RectF nameR;
        bool truncated = false;
        if (iconsView) {
            f32 ip = g.iconPx;
            RectF imgR(std::floor(cell.x + (cell.w - ip) / 2), cell.y + 6 * S, ip, ip);
            ID2D1Bitmap *bmp = t->kind == VIEW_RECYCLE ? nullptr : thumbs.Get(path, e.modified, (u32)ip);
            if (bmp) {
                D2D1_SIZE_F bs = bmp->GetSize();
                f32 k = std::min(ip / std::max(1.f, bs.width), ip / std::max(1.f, bs.height));
                f32 w = std::floor(bs.width * k), h = std::floor(bs.height * k);
                // thumbnails sit on the bottom edge of the image box, like Explorer
                r.DrawBitmap(bmp, RectF(imgR.x + (ip - w) / 2, imgR.Bottom() - h, w, h), alpha);
            } else if (ID2D1Bitmap *ic = icons.Get(path, e)) {
                r.DrawBitmap(ic, RectF(imgR.x + (ip - smallSz) / 2, imgR.y + (ip - smallSz) / 2, smallSz, smallSz), alpha);
            }
            if (t->cloud) DrawCloudState(cloud.Get(path), RectF(cell.x - 4 * S, imgR.Bottom() - lh, lh * 2, lh), HashStr(path, 11));
            nameR = RectF(cell.x + 4 * S, imgR.Bottom() + 4 * S, cell.w - 8 * S, lh * 2);
            if (!renaming) {
                std::wstring shown = FitTwoLines(r, name, nameR.w);
                truncated = shown.size() != name.size();
                TextOpts o; o.align = ALIGN_CENTER; o.wrap = true; o.ellipsis = false; o.vcenter = false;
                r.PushClip(RectF(nameR.x, nameR.y, nameR.w, lh * 2 + 2 * S));
                r.Text(FONT_UI, shown, nameR, tc, o);
                r.PopClip();
            }
        } else {
            f32 x = cell.x + 6 * S;
            if (ID2D1Bitmap *ic = icons.Get(path, e)) r.DrawBitmap(ic, RectF(x, cell.y + (cell.h - smallSz) / 2, smallSz, smallSz), alpha);
            x += smallSz + 8 * S;
            nameR = RectF(x, cell.y, cell.Right() - x - 6 * S, cell.h);
            if (!renaming) {
                if (!filterShown.empty()) r.TextHighlightRanges(FONT_UI, name, nameR, MatchRanges(name, filterShown), theme.highlight);
                truncated = r.Measure(FONT_UI, name) > nameR.w;
                r.Text(FONT_UI, name, nameR, tc);
            }
        }
        if (renaming) {
            RectF er(nameR.x - 2 * S, nameR.y, nameR.w + 4 * S, std::min(nameR.h, lh + 6 * S));
            r.FillRect(er, theme.inputBg, 3 * S);
            r.StrokeRect(er, theme.accent, std::max(1.f, S), 3 * S);
            DrawTextEdit(renameEdit, RectF(er.x + 4 * S, er.y, er.w - 8 * S, er.h), FONT_UI, theme.text, L"", true);
        }
        if (i == hoverRow) {
            if (truncated) SetTooltip(HashStr(path), name);
            if (RowPress(t, i, path, sel, false)) break;
        }
    }
    ListMouseTail(t, rc, hovList, hoverRow, sbW);
    if (dropTab == t && dropRow < 0) r.StrokeRect(rc.Shrink(1 * S), theme.accent, 2 * S, 4 * S);
    r.PopClip();
    DrawScrollbar(RectF(rc.Right() - sbW, rc.y, sbW, rc.h), ListContentHeight(t), rc.h, t->scrollTarget, HashId(t, 50));
    if (activeId == HashId(t, 50)) t->scroll = t->scrollTarget;
}

void App::DrawStatus(Panel *p, Tab *t, RectF rc) {
    r.FillSurface(rc, theme.panelBg);
    r.Line(rc.x, rc.y + 0.5f, rc.Right(), rc.y + 0.5f, theme.border);
    RectF in_ = RectF(rc.x + 12 * S, rc.y, rc.w - 24 * S, rc.h);
    int items = 0;
    for (auto &row : t->rows) if (!row.header) items++;
    std::wstring left = Format(T(S_Items), items);
    if (!t->selected.empty()) {
        u64 bytes = 0;
        bool anyFile = false;
        for (auto &row : t->rows) {
            if (row.header) continue;
            if (t->selected.count(RowPath(row))) {
                if (row.e->isDir) {
                    auto st = sizes.Get(RowPath(row), false);
                    if (st) bytes += st->size;
                } else { bytes += row.e->size; anyFile = true; }
            }
        }
        left += L"   ·   " + Format(T(S_ItemsSelected), (int)t->selected.size());
        if (bytes || anyFile) left += L"  (" + FormatSize(bytes) + L")";
    }
    if (t->recursive && t->search && !t->search->done) left += L"   ·   " + std::wstring(T(S_SearchSubfolders));
    if (t->loading && GetTickCount64() - t->loadStartTick > 150) left += L"   ·   " + std::wstring(T(S_Loading));
    if (focus == FOCUS_FILTER && p == active && ime.composing) left += L"   ·   " + std::wstring(T(S_Composition)) + L": " + ime.comp;
    // view mode button (opens the size slider)
    {
        std::wstring label = ViewModeName(t);
        f32 bw = std::floor(r.Measure(FONT_SMALL, label) + 34 * S);
        RectF b(in_.Right() - bw + 6 * S, rc.y + 3 * S, bw, rc.h - 6 * S);
        bool open = popup.kind == POPUP_VIEW && popup.viewTabId == t->id;
        if (open) r.FillRect(b, theme.selectionInactive, 4 * S);
        else if (Hover(b)) r.FillRect(b, theme.hover, 4 * S);
        TextOpts go; go.align = ALIGN_CENTER; go.ellipsis = false;
        r.Text(FONT_ICON_SMALL, ViewModeGlyph(t), RectF(b.x + 4 * S, b.y, 20 * S, b.h), theme.textDim, go);
        r.Text(FONT_SMALL, label, RectF(b.x + 26 * S, b.y, b.w - 28 * S, b.h), theme.textDim);
        if (Hover(b)) SetTooltip(HashId(t, 60), TL(L"View (Ctrl+wheel to resize)", L"보기 (Ctrl+휠로 크기 조절)"));
        if (Clicked(b)) { FocusPanel(p); OpenViewPopup(t, b); in.pressed[0] = false; }
        in_.w -= bw + 6 * S;
    }
    r.Text(FONT_SMALL, left, in_, theme.textDim);
    if (t->kind == VIEW_FOLDER) {
        static std::unordered_map<std::wstring, std::pair<u64, std::wstring>> cache;
        std::wstring root = t->path.size() >= 3 && t->path[1] == L':' ? t->path.substr(0, 3) : t->path;
        auto &c = cache[root];
        if (GetTickCount64() - c.first > 3000) {
            u64 fb = 0, tb = 0;
            c.second = DiskFree(t->path, &fb, &tb) ? Format(T(S_FreeOf), FormatSize(fb).c_str(), FormatSize(tb).c_str()) : L"";
            c.first = GetTickCount64();
        }
        TextOpts o; o.align = ALIGN_RIGHT;
        r.Text(FONT_SMALL, c.second, in_, theme.textFaint, o);
    }
}

// ================================================================== sidebar
// Explorer's "Home" (Windows 11) icon, or the user folder icon on older systems
ID2D1Bitmap *App::HomeIcon() {
    ID2D1Bitmap *b = icons.GetSpecial(L"::{F874310E-B6B7-47DC-BC84-B9E6B38F5903}");
    return b ? b : icons.GetSpecial(HomePath());
}

void App::DrawSidebar(RectF rc) {
    r.FillSurface(rc, theme.sidebarBg);
    if (places.empty()) {
        places.push_back({S_Home, HomePath(), GLYPH_HOME});
        places.push_back({S_Desktop, KnownFolderPath(FOLDERID_Desktop), L""});
        places.push_back({S_Downloads, KnownFolderPath(FOLDERID_Downloads), L""});
        places.push_back({S_Documents, KnownFolderPath(FOLDERID_Documents), L""});
        places.push_back({S_Pictures, KnownFolderPath(FOLDERID_Pictures), L""});
        places.push_back({S_Music, KnownFolderPath(FOLDERID_Music), L""});
        places.push_back({S_Videos, KnownFolderPath(FOLDERID_Videos), L""});
        places.push_back({S_ThisPC, PATH_THIS_PC, GLYPH_PC});
        places.push_back({S_RecycleBin, PATH_RECYCLE, GLYPH_DELETE});
    }
    u64 now = GetTickCount64();
    if (!drivesLoading && now - drivesTick > 5000) {
        drivesLoading = true;
        JobSubmit([] {
            std::vector<FileEntry> d;
            ListDrives(d, true);
            auto sh = std::make_shared<std::vector<FileEntry>>(std::move(d));
            MainPost([sh] {
                if (!g_app) return;
                g_app->sidebarDrives = std::move(*sh);
                g_app->drivesLoading = false;
                g_app->drivesTick = GetTickCount64();
                g_app->Invalidate();
            });
        });
    }

    Tab *cur = CurTab();
    f32 rh = std::floor(r.Metrics(FONT_UI).lineH + 10 * S);
    f32 iconSz = std::floor(r.Metrics(FONT_UI).lineH * 1.05f);
    RectF view = rc.Shrink(6 * S);

    r.PushClip(view);
    if (Hover(view) && in.wheel != 0) sidebarScroll -= in.wheel * rh * 2;
    f32 y = view.y - sidebarScroll;
    f32 startY = y;

    auto section = [&](const std::wstring &title) {
        y += 6 * S;
        r.Text(FONT_SMALL, title, RectF(view.x + 8 * S, y, view.w - 16 * S, rh * 0.8f), theme.textFaint);
        y += rh * 0.8f;
    };
    auto item = [&](u64 id, const std::wstring &label, const std::wstring &path, ID2D1Bitmap *bmp, const wchar_t *glyph,
                    f32 usage, std::function<void(f32, f32)> rightClick) {
        RectF ir(view.x, y, view.w, rh);
        y += rh;
        if (ir.Bottom() < view.y || ir.y > view.Bottom()) return;
        bool isCur = cur && _wcsicmp(cur->path.c_str(), path.c_str()) == 0;
        bool hov = Hover(ir);
        if (isCur) r.FillRect(ir, theme.selectionInactive, 5 * S);
        else if (hov) r.FillRect(ir, theme.hover, 5 * S);
        f32 x = ir.x + 8 * S;
        if (bmp) r.DrawBitmap(bmp, RectF(x, ir.y + (rh - iconSz) / 2, iconSz, iconSz));
        else if (glyph) {
            TextOpts go; go.align = ALIGN_CENTER; go.ellipsis = false;
            r.Text(FONT_ICON_SMALL, glyph, RectF(x, ir.y, iconSz, rh), theme.textDim, go);
        }
        x += iconSz + 8 * S;
        RectF tr(x, ir.y, ir.Right() - x - 6 * S, usage >= 0 ? rh * 0.75f : rh);
        r.Text(FONT_UI, label, tr, theme.text);
        if (usage >= 0) {
            RectF bar(x, ir.Bottom() - 6 * S, ir.Right() - x - 10 * S, 3 * S);
            r.FillRect(bar, theme.border, 1.5f * S);
            r.FillRect(RectF(bar.x, bar.y, bar.w * usage, bar.h), usage > 0.9f ? theme.danger : theme.accent, 1.5f * S);
        }
        if (hov) {
            if (path != PATH_THIS_PC && path != PATH_RECYCLE) SetTooltip(id, path);
            if (in.pressed[0] && cur) { Navigate(cur, path); in.pressed[0] = false; }
            if (in.pressed[1] && active) { NewTab(active, path); in.pressed[1] = false; }
            if (in.pressed[2] && rightClick) { rightClick(in.mx, in.my); in.pressed[2] = false; }
        }
    };
    auto stdMenu = [this](const std::wstring &path, bool bookmark, bool recent) {
        return [this, path, bookmark, recent](f32 mx, f32 my) {
            std::vector<MenuItem> m;
            MenuItem a; a.label = TL(L"Open in New Tab", L"새 탭에서 열기"); a.action = [this, path] { if (active) NewTab(active, path); }; m.push_back(a);
            MenuItem b; b.label = TL(L"Open in Right Split", L"오른쪽 분할 창에서 열기");
            b.action = [this, path] { SplitActive(false); Navigate(CurTab(), path, false); }; m.push_back(b);
            if (bookmark) {
                MenuItem c; c.label = TL(L"Remove Bookmark", L"즐겨찾기 제거"); c.action = [this, path] { ToggleBookmark(path); }; m.push_back(c);
                MenuItem up; up.label = TL(L"Move Up", L"위로 이동");
                up.action = [this, path] {
                    for (size_t i = 1; i < user.bookmarks.size(); i++)
                        if (user.bookmarks[i] == path) { std::swap(user.bookmarks[i], user.bookmarks[i - 1]); SaveUserData(); break; }
                };
                m.push_back(up);
            } else if (path != PATH_THIS_PC && path != PATH_RECYCLE) {
                MenuItem c; c.label = IsBookmarked(path) ? TL(L"Remove Bookmark", L"즐겨찾기 제거") : TL(L"Add Bookmark", L"즐겨찾기 추가");
                c.action = [this, path] { ToggleBookmark(path); }; m.push_back(c);
            }
            if (recent) {
                MenuItem d; d.label = TL(L"Remove From Recents", L"최근 폴더에서 제거"); d.action = [this, path] { user.RemoveRecent(path); SaveUserData(); }; m.push_back(d);
            }
            if (path == PATH_RECYCLE) {
                MenuItem e; e.label = TL(L"Empty Recycle Bin", L"휴지통 비우기"); e.action = [this] { EmptyRecycleBin(); }; m.push_back(e);
            } else if (path != PATH_THIS_PC) {
                MenuItem e; e.label = TL(L"Properties", L"속성"); e.action = [this, path] { ShellProperties(hwnd, {path}); }; m.push_back(e);
            }
            OpenMenu(m, mx, my);
        };
    };

    section(T(S_Places));
    for (auto &pl : places) {
        if (pl.path.empty() && pl.name != S_ThisPC) continue;
        ID2D1Bitmap *bmp = nullptr;
        if (pl.path == PATH_RECYCLE) bmp = icons.GetSpecial(L"::{645FF040-5081-101B-9F08-00AA002F954E}");
        else if (pl.path == PATH_THIS_PC) bmp = icons.GetSpecial(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
        else if (pl.name == S_Home) bmp = HomeIcon();
        else bmp = icons.GetSpecial(pl.path); // Windows' own icons for the known folders
        item(HashStr(pl.path, 7), T(pl.name), pl.path, bmp, pl.glyph, -1, stdMenu(pl.path, false, false));
    }
    if (!user.bookmarks.empty()) {
        section(T(S_Bookmarks));
        std::vector<std::wstring> bms = user.bookmarks;
        for (auto &b : bms) {
            FileEntry fe; fe.isDir = true; fe.name = PathFileName(b);
            std::wstring label = PathFileName(b);
            if (label.empty()) label = b;
            item(HashStr(b, 8), label, b, icons.Get(b, fe), nullptr, -1, stdMenu(b, true, false));
        }
    }
    if (!sidebarDrives.empty()) {
        section(T(S_Drives));
        for (auto &d : sidebarDrives) {
            f32 usage = d.totalBytes ? (f32)(1.0 - (f64)d.freeBytes / d.totalBytes) : -1;
            item(HashStr(d.name, 9), DisplayName(d), d.name + L"\\", icons.GetDrive(d.name + L"\\"), nullptr, usage, stdMenu(d.name + L"\\", false, false));
        }
    }
    if (settings.showRecents && !user.recents.empty()) {
        section(T(S_Recents));
        auto rec = user.SortedRecents();
        int count = 0;
        for (auto &rc2 : rec) {
            if (count >= 12) break;
            bool isPlace = false;
            for (auto &pl : places) if (_wcsicmp(pl.path.c_str(), rc2.path.c_str()) == 0) isPlace = true;
            if (isPlace) continue;
            FileEntry fe; fe.isDir = true; fe.name = PathFileName(rc2.path);
            std::wstring label = PathFileName(rc2.path);
            if (label.empty()) label = rc2.path;
            item(HashStr(rc2.path, 10), label, rc2.path, icons.Get(rc2.path, fe), nullptr, -1, stdMenu(rc2.path, false, true));
            count++;
        }
    }
    f32 contentH = y + sidebarScroll - startY + 10 * S;
    sidebarScroll = Clamp(sidebarScroll, 0.f, std::max(0.f, contentH - view.h));
    r.PopClip();
}

// ================================================================== overlays
void App::DrawToasts() {
    u64 now = GetTickCount64();
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const ToastMsg &t) { return now - t.tick > 2600; }), toasts.end());
    f32 y = (f32)r.Height() - 50 * S;
    for (int i = (int)toasts.size() - 1; i >= 0; i--) {
        auto &t = toasts[i];
        f32 age = (f32)(now - t.tick);
        f32 alpha = age < 2200 ? 1.f : 1.f - (age - 2200) / 400.f;
        f32 w = r.Measure(FONT_UI, t.text) + 36 * S;
        f32 h = r.Metrics(FONT_UI).lineH + 16 * S;
        RectF tr(((f32)r.Width() - w) / 2, y - h, w, h);
        r.Shadow(tr, 8 * S, 10 * S, 0.25f * alpha);
        r.FillRect(tr, theme.popupBg.WithAlpha(alpha), 8 * S);
        r.StrokeRect(tr, theme.popupBorder.WithAlpha(alpha), 1 * S, 8 * S);
        TextOpts o; o.align = ALIGN_CENTER; o.opacity = alpha;
        r.Text(FONT_UI, t.text, tr, theme.text, o);
        y -= h + 8 * S;
    }
    if (!toasts.empty()) Invalidate();
}

void App::DrawTooltip() {
    if (tooltip.empty()) return;
    if (GetTickCount64() - hoverStartTick < 550) { Invalidate(); return; }
    f32 w = std::min(r.Measure(FONT_SMALL, tooltip) + 16 * S, 500 * S);
    f32 h = r.Metrics(FONT_SMALL).lineH + 10 * S;
    f32 x = Clamp(tooltipX + 12 * S, 4 * S, (f32)r.Width() - w - 4 * S);
    f32 y = tooltipY + 22 * S;
    if (y + h > r.Height()) y = tooltipY - h - 8 * S;
    RectF tr(x, y, w, h);
    r.Shadow(tr, 4 * S, 6 * S, 0.2f);
    r.FillRect(tr, theme.popupBg, 4 * S);
    r.StrokeRect(tr, theme.popupBorder, 1 * S, 4 * S);
    r.Text(FONT_SMALL, tooltip, RectF(x + 8 * S, y, w - 16 * S, h), theme.text);
}
