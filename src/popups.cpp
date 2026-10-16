// Command palette, GoTo, menus, confirm dialog, options window
#include "app.h"
#include "jobs.h"


void App::ClosePopup() {
    popup = Popup();
    if (focus == FOCUS_POPUP) focus = filterEditing ? FOCUS_FILTER : FOCUS_LIST;
    if (ime.composing) OnImeEnd();
    Invalidate();
}

void App::OpenPalette(const std::wstring &initial) {
    popup = Popup();
    popup.kind = POPUP_PALETTE;
    popup.edit.Set(initial, false);
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    RefreshPaletteItems();
    Invalidate();
}

void App::OpenGoTo(const std::wstring &initial) {
    popup = Popup();
    popup.kind = POPUP_GOTO;
    popup.edit.Set(initial, false);
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    RefreshPaletteItems();
    Invalidate();
}

void App::OpenMenu(std::vector<MenuItem> items, f32 x, f32 y) {
    popup = Popup();
    popup.kind = POPUP_MENU;
    popup.menu = std::move(items);
    popup.x = x;
    popup.y = y;
    popup.sel = -1;
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    Invalidate();
}

void App::OpenOptions() {
    popup = Popup();
    popup.kind = POPUP_OPTIONS;
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    Invalidate();
}

static bool LooksLikePath(const std::wstring &q) {
    return (q.size() >= 2 && q[1] == L':') || q.rfind(L"\\", 0) == 0 || q.rfind(L"~", 0) == 0 || q.rfind(L"%", 0) == 0;
}

void App::RefreshPaletteItems() {
    popup.items.clear();
    std::wstring q = popup.edit.text;
    if (ime.composing) q.insert(std::min<size_t>(popup.edit.caret, q.size()), ime.comp);
    if (popup.kind == POPUP_PALETTE) {
        bool ko = I18nGetLanguage() == LANG_KO;
        for (auto &c : commands) {
            std::wstring name = ko ? c.nameKo : c.nameEn;
            std::wstring hay = name + L" " + (ko ? c.nameEn : c.nameKo) + L" " + c.aliases + L" " + c.id;
            int score = 0;
            if (!q.empty()) {
                int s1 = 0, s2 = 0;
                bool m1 = FuzzyMatch(name, q, &s1);
                bool m2 = !m1 && FuzzyMatch(hay, q, &s2);
                if (!m1 && !m2) continue;
                score = m1 ? s1 + 500 : s2;
            }
            PaletteItem it;
            it.label = name;
            it.key = c.id;
            it.detail = HotkeyText(c.id);
            if (c.checked) it.detail = (c.checked() ? L"● " : L"○ ") + it.detail;
            for (auto &b : user.commandBookmarks) if (b == c.id) { it.bookmarked = true; score += 2000; }
            it.score = score;
            popup.items.push_back(it);
        }
        std::stable_sort(popup.items.begin(), popup.items.end(), [](const PaletteItem &a, const PaletteItem &b) { return a.score > b.score; });
    } else if (popup.kind == POPUP_GOTO) {
        std::vector<PaletteItem> cand;
        std::set<std::wstring> seen;
        auto add = [&](const std::wstring &path, const std::wstring &label, int bonus) {
            std::wstring key = path.empty() ? L"::thispc" : ToLower(path); // "" is This PC
            if (seen.count(key) || (path.empty() && label.empty())) return;
            int s = 0;
            if (!q.empty()) {
                int s1 = 0, s2 = 0;
                bool m1 = FuzzyMatch(label, q, &s1);
                bool m2 = !path.empty() && FuzzyMatch(path, q, &s2);
                if (!m1 && !m2) return;
                s = std::max(m1 ? s1 + 300 : -100000, m2 ? s2 : -100000);
            }
            seen.insert(key);
            PaletteItem it;
            it.label = label.empty() ? path : label;
            it.detail = path == PATH_THIS_PC ? T(S_ThisPC) : (path == PATH_RECYCLE ? T(S_RecycleBin) : path);
            it.key = path;
            it.score = s + bonus;
            FileEntry fe; fe.isDir = true; fe.name = PathFileName(path);
            if (path.size() == 3 && path[1] == L':') it.icon = icons.GetDrive(path);
            else if (path != PATH_THIS_PC && path != PATH_RECYCLE) it.icon = icons.Get(path, fe);
            cand.push_back(it);
        };
        if (LooksLikePath(q)) {
            std::wstring n = NormalizePath(q);
            std::wstring dir = (!q.empty() && (q.back() == L'\\' || q.back() == L'/')) ? n : PathParent(n);
            std::wstring part = (!q.empty() && (q.back() == L'\\' || q.back() == L'/')) ? L"" : PathFileName(n);
            if (IsDirectory(n)) add(n, PathFileName(n), 10000);
            std::vector<FileEntry> list;
            if (!dir.empty() && ListDirectory(dir, list) == LIST_OK) {
                for (auto &e : list) {
                    if (!e.isDir || !Visible(e)) continue;
                    int s = 0;
                    if (!part.empty() && !FuzzyMatch(e.name, part, &s)) continue;
                    PaletteItem it;
                    it.label = e.name;
                    it.key = PathJoin(dir, e.name);
                    it.detail = it.key;
                    it.score = s;
                    it.icon = icons.Get(it.key, e);
                    cand.push_back(it);
                }
            }
        } else {
            int b = 400;
            for (auto &bm : user.bookmarks) add(bm, PathFileName(bm), b);
            for (auto &pl : places) add(pl.path, T(pl.name), 300);
            for (auto &d : sidebarDrives) add(d.name + L"\\", DisplayName(d), 250);
            auto rec = user.SortedRecents();
            int rank = 0;
            for (auto &rc : rec) add(rc.path, PathFileName(rc.path), 200 - std::min(rank++, 150));
            Tab *t = CurTab();
            if (t && t->kind == VIEW_FOLDER && !q.empty()) {
                for (auto &e : t->entries) if (e.isDir && Visible(e)) add(PathJoin(t->path, e.name), e.name, 100);
            }
        }
        std::stable_sort(cand.begin(), cand.end(), [](const PaletteItem &a, const PaletteItem &b) { return a.score > b.score; });
        if (cand.size() > 200) cand.resize(200);
        popup.items = std::move(cand);
    }
    popup.sel = popup.items.empty() ? -1 : 0;
    popup.scroll = 0;
}

void App::ActivatePaletteItem(int idx, bool alt) {
    if (idx < 0 || idx >= (int)popup.items.size()) return;
    PaletteItem it = popup.items[idx];
    PopupKind kind = popup.kind;
    ClosePopup();
    if (kind == POPUP_PALETTE) {
        RunCommand(it.key);
    } else if (kind == POPUP_GOTO) {
        if (alt && active) NewTab(active, it.key);
        else if (Tab *t = CurTab()) Navigate(t, it.key);
    }
}

static int NextMenuSel(const std::vector<MenuItem> &m, int from, int dir) {
    int n = (int)m.size();
    if (!n) return -1;
    int i = from;
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (!m[i].separator && m[i].enabled) return i;
    }
    return from;
}

void App::PopupKey(const KeyEvent &k) {
    switch (popup.kind) {
    case POPUP_CONFIRM:
        if (k.vk == VK_RETURN || k.vk == 'Y') {
            auto fn = popup.onConfirm;
            ClosePopup();
            if (fn) fn();
        } else if (k.vk == VK_ESCAPE || k.vk == 'N') ClosePopup();
        return;
    case POPUP_VIEW:
        if (k.vk == VK_ESCAPE || k.vk == VK_RETURN) ClosePopup();
        else if (k.vk == VK_UP) ZoomView(FindTab(popup.viewTabId), 1);
        else if (k.vk == VK_DOWN) ZoomView(FindTab(popup.viewTabId), -1);
        return;
    case POPUP_MENU:
    case POPUP_FOLDER_DROPDOWN:
        if (k.vk == VK_ESCAPE) ClosePopup();
        else if (k.vk == VK_DOWN) popup.sel = NextMenuSel(popup.menu, popup.sel < 0 ? -1 : popup.sel, 1);
        else if (k.vk == VK_UP) popup.sel = NextMenuSel(popup.menu, popup.sel < 0 ? 0 : popup.sel, -1);
        else if (k.vk == VK_RETURN && popup.sel >= 0) {
            auto fn = popup.menu[popup.sel].action;
            ClosePopup();
            if (fn) fn();
        }
        return;
    case POPUP_OPTIONS:
        if (popup.subMenu) {
            if (k.vk == VK_ESCAPE) popup.subMenu = false;
            else if (k.vk == VK_DOWN) { popup.sel = NextMenuSel(popup.menu, popup.sel < 0 ? -1 : popup.sel, 1); popup.menuEnsureSel = true; }
            else if (k.vk == VK_UP) { popup.sel = NextMenuSel(popup.menu, popup.sel < 0 ? 0 : popup.sel, -1); popup.menuEnsureSel = true; }
            else if (k.vk == VK_RETURN && popup.sel >= 0) {
                auto fn = popup.menu[popup.sel].action;
                popup.subMenu = false;
                if (fn) fn();
            } else if (k.vk == VK_BACK && !popup.typeAhead.empty()) {
                popup.typeAhead.pop_back();
            }
            return;
        }
        if (!popup.captureCmd.empty()) { // Shortcuts: recording a key for a command
            if (k.vk == VK_ESCAPE) { popup.captureCmd.clear(); return; }
            std::wstring keys = KeyEventHotkey(k);
            if (keys.empty()) return; // a key without a name: keep waiting
            std::wstring msg = AssignHotkey(popup.captureCmd, keys);
            popup.captureCmd.clear();
            if (!msg.empty()) Toast(msg);
            return;
        }
        if (k.vk == VK_TAB) {
            popup.optionsTab = (popup.optionsTab + (k.shift ? kOptionsTabs - 1 : 1)) % kOptionsTabs;
            popup.scroll = 0;
            return;
        }
        if (popup.optionsTab == kShortcutsTab && k.vk != VK_ESCAPE) { // the search box has the keyboard
            bool changed = false;
            popup.edit.OnKey(k, hwnd, &changed);
            if (changed) popup.scroll = 0;
            return;
        }
        if (k.vk == VK_ESCAPE) {
            if (popup.optionsTab == kShortcutsTab && !popup.edit.text.empty()) { popup.edit.Set(L"", false); popup.scroll = 0; }
            else ClosePopup();
        }
        return;
    case POPUP_PALETTE:
    case POPUP_GOTO: {
        int n = (int)popup.items.size();
        if (k.vk == VK_ESCAPE) { ClosePopup(); return; }
        if (k.vk == VK_DOWN) { if (n) popup.sel = (popup.sel + 1) % n; popup.menuEnsureSel = true; return; }
        if (k.vk == VK_UP) { if (n) popup.sel = (popup.sel - 1 + n) % n; popup.menuEnsureSel = true; return; }
        if (k.vk == VK_NEXT) { if (n) popup.sel = std::min(n - 1, popup.sel + 10); popup.menuEnsureSel = true; return; }
        if (k.vk == VK_PRIOR) { if (n) popup.sel = std::max(0, popup.sel - 10); popup.menuEnsureSel = true; return; }
        if (k.vk == VK_RETURN) { ActivatePaletteItem(popup.sel, k.ctrl); return; }
        if (k.vk == VK_TAB && popup.kind == POPUP_GOTO && popup.sel >= 0 && popup.sel < n) {
            std::wstring key = popup.items[popup.sel].key;
            if (key != PATH_THIS_PC && key != PATH_RECYCLE) {
                popup.edit.Set(key + L"\\", false);
                RefreshPaletteItems();
            }
            return;
        }
        if (k.vk == 'B' && k.ctrl && popup.kind == POPUP_PALETTE && popup.sel >= 0) {
            // bookmark command (pin to top)
            std::wstring id = popup.items[popup.sel].key;
            auto &cb = user.commandBookmarks;
            auto it = std::find(cb.begin(), cb.end(), id);
            if (it == cb.end()) cb.push_back(id); else cb.erase(it);
            SaveUserData();
            RefreshPaletteItems();
            return;
        }
        bool changed = false;
        popup.edit.OnKey(k, hwnd, &changed);
        if (changed) RefreshPaletteItems();
        return;
    }
    default: return;
    }
}

// ------------------------------------------------------------------ drawing
static void Dim(App *a) {
    a->r.FillRect(RectF(0, 0, (f32)a->r.Width(), (f32)a->r.Height()), Color(0, 0, 0, a->theme.dark ? 0.35f : 0.18f));
}

void App::DrawPopup() {
    if (popup.kind == POPUP_NONE) return;
    f32 W = (f32)r.Width(), H = (f32)r.Height();
    f32 lh = r.Metrics(FONT_UI).lineH;
    bool anyPress = in.pressed[0] || in.pressed[1] || in.pressed[2];

    if (popup.kind == POPUP_PALETTE || popup.kind == POPUP_GOTO) {
        Dim(this);
        f32 w = std::min(680 * S, W - 40 * S);
        bool twoLine = popup.kind == POPUP_GOTO;
        f32 rowH = twoLine ? std::floor(lh * 2 + 8 * S) : std::floor(lh + 14 * S);
        int maxRows = std::max(3, std::min(12, (int)((H - 160 * S) / rowH)));
        int n = (int)popup.items.size();
        int vis = std::min(n, maxRows);
        f32 inputH = std::floor(lh + 22 * S);
        f32 h = inputH + vis * rowH + (vis ? 8 * S : 0) + 4 * S;
        RectF pr((W - w) / 2, std::min(70 * S, H * 0.1f), w, h);
        popup.rect = pr;
        r.Shadow(pr, 10 * S, 16 * S, 0.35f);
        r.FillRect(pr, theme.popupBg, 10 * S);
        r.StrokeRect(pr, theme.popupBorder, 1 * S, 10 * S);
        RectF ir = RectF(pr.x + 8 * S, pr.y + 6 * S, pr.w - 16 * S, inputH - 8 * S);
        r.FillRect(ir, theme.inputBg, 6 * S);
        RectF ic = ir;
        ic = ic.CutLeft(ir.h);
        TextOpts io; io.align = ALIGN_CENTER; io.ellipsis = false;
        r.Text(FONT_ICON_SMALL, popup.kind == POPUP_PALETTE ? GLYPH_COMMAND : GLYPH_FOLDER, ic, theme.textFaint, io);
        DrawTextEdit(popup.edit, RectF(ic.Right(), ir.y, ir.w - ic.w - 8 * S, ir.h), FONT_UI, theme.text,
                     T(popup.kind == POPUP_PALETTE ? S_CommandHint : S_GoToHint), true);
        // bring the selection into view after a keyboard move (not every frame: the wheel scrolls freely)
        if (popup.menuEnsureSel && popup.sel >= 0) {
            popup.menuEnsureSel = false;
            if (popup.sel < popup.scroll) popup.scroll = (f32)popup.sel;
            if (popup.sel >= popup.scroll + vis) popup.scroll = (f32)(popup.sel - vis + 1);
        }
        RectF list(pr.x + 6 * S, pr.y + inputH + 2 * S, pr.w - 12 * S, vis * rowH);
        if (Hover(list) && in.wheel != 0 && n > vis) popup.scroll = Clamp(popup.scroll - in.wheel * 3, 0.f, (f32)(n - vis));
        int start = (int)popup.scroll;
        for (int i = 0; i < vis && start + i < n; i++) {
            int idx = start + i;
            PaletteItem &it = popup.items[idx];
            RectF rr(list.x, list.y + i * rowH, list.w, rowH);
            bool hov = Hover(rr);
            if (hov && (in.mx != popup.x || in.my != popup.y)) { popup.sel = idx; }
            if (idx == popup.sel) r.FillRect(rr, theme.selection, 6 * S);
            f32 x = rr.x + 10 * S;
            if (it.icon) {
                f32 isz = std::floor(lh * 1.05f);
                r.DrawBitmap(it.icon, RectF(x, rr.y + (twoLine ? 6 * S : (rowH - isz) / 2), isz, isz));
                x += isz + 10 * S;
            }
            if (it.bookmarked) {
                TextOpts so; so.align = ALIGN_CENTER; so.ellipsis = false;
                r.Text(FONT_ICON_SMALL, GLYPH_STAR_FILL, RectF(x, rr.y, 16 * S, twoLine ? lh + 8 * S : rowH), theme.highlight.WithAlpha(1), so);
                x += 20 * S;
            }
            f32 detailW = twoLine ? 0 : std::min(rr.w * 0.4f, r.Measure(FONT_SMALL, it.detail) + 12 * S);
            if (twoLine) {
                r.Text(FONT_UI, it.label, RectF(x, rr.y + 4 * S, rr.Right() - x - 10 * S, lh), theme.text);
                r.Text(FONT_SMALL, it.detail, RectF(x, rr.y + 4 * S + lh, rr.Right() - x - 10 * S, lh), theme.textFaint);
            } else {
                r.Text(FONT_UI, it.label, RectF(x, rr.y, rr.Right() - x - detailW - 10 * S, rowH), theme.text);
                TextOpts dop; dop.align = ALIGN_RIGHT;
                r.Text(FONT_SMALL, it.detail, RectF(rr.Right() - detailW - 10 * S, rr.y, detailW, rowH), theme.textDim, dop);
            }
            if (hov && in.pressed[0]) { ActivatePaletteItem(idx, in.ctrl); return; }
            if (hov && in.pressed[1]) { ActivatePaletteItem(idx, true); return; }
        }
        popup.x = in.mx;
        popup.y = in.my;
        if (anyPress && !Hover(pr)) ClosePopup();
        return;
    }

    if (popup.kind == POPUP_VIEW) {
        DrawViewPopup();
        return;
    }
    if (popup.kind == POPUP_MENU || popup.kind == POPUP_FOLDER_DROPDOWN) {
        RectF pr;
        int hit = DrawMenuBox(popup.x, popup.y, popup.scroll, &pr);
        popup.rect = pr;
        if (hit >= 0) {
            auto fn = popup.menu[hit].action;
            ClosePopup();
            if (fn) fn();
        } else if (hit == -2) {
            ClosePopup();
        }
        return;
    }
    if (popup.kind == POPUP_CONFIRM) {
        Dim(this);
        f32 w = std::min(460 * S, W - 40 * S);
        f32 msgH = r.MeasureWrapped(FONT_UI, popup.message, w - 40 * S);
        f32 h = msgH + lh + 90 * S;
        RectF pr((W - w) / 2, (H - h) / 2.5f, w, h);
        popup.rect = pr;
        r.Shadow(pr, 10 * S, 16 * S, 0.35f);
        r.FillRect(pr, theme.popupBg, 10 * S);
        r.StrokeRect(pr, theme.popupBorder, 1 * S, 10 * S);
        TextOpts mo; mo.wrap = true; mo.vcenter = false;
        r.Text(FONT_UI, popup.message, RectF(pr.x + 20 * S, pr.y + 22 * S, w - 40 * S, msgH + 4), theme.text, mo);
        f32 bw = 100 * S, bh = lh + 14 * S;
        RectF yes(pr.Right() - 20 * S - bw * 2 - 10 * S, pr.Bottom() - bh - 18 * S, bw, bh);
        RectF no(pr.Right() - 20 * S - bw, pr.Bottom() - bh - 18 * S, bw, bh);
        TextOpts bo; bo.align = ALIGN_CENTER;
        r.FillRect(yes, Hover(yes) ? theme.accent.WithAlpha(0.85f) : theme.accent, 6 * S);
        r.Text(FONT_UI_BOLD, T(S_Yes), yes, theme.accentText, bo);
        r.FillRect(no, Hover(no) ? theme.hover : theme.inputBg, 6 * S);
        r.StrokeRect(no, theme.border, 1 * S, 6 * S);
        r.Text(FONT_UI, T(S_No), no, theme.text, bo);
        if (Clicked(yes)) { auto fn = popup.onConfirm; ClosePopup(); if (fn) fn(); return; }
        if (Clicked(no)) { ClosePopup(); return; }
        return;
    }

    if (popup.kind == POPUP_OPTIONS) {
        Dim(this);
        f32 w = std::min(760 * S, W - 40 * S), h = std::min(620 * S, H - 40 * S);
        RectF pr((W - w) / 2, (H - h) / 2, w, h);
        popup.rect = pr;
        r.Shadow(pr, 10 * S, 16 * S, 0.35f);
        r.FillRect(pr, theme.popupBg, 10 * S);
        r.StrokeRect(pr, theme.popupBorder, 1 * S, 10 * S);
        bool sub = popup.subMenu;
        mouseBlocked = sub;
        DrawOptions(pr);
        mouseBlocked = false;
        if (sub && popup.kind == POPUP_OPTIONS && popup.subMenu) {
            int hit = DrawMenuBox(popup.x, popup.y, popup.subScroll, nullptr);
            if (hit >= 0) {
                auto fn = popup.menu[hit].action;
                popup.subMenu = false;
                if (fn) fn();
            } else if (hit == -2) {
                popup.subMenu = false;
            }
            Invalidate();
            return;
        }
        if (anyPress && !Hover(pr) && popup.kind == POPUP_OPTIONS) ClosePopup();
        return;
    }
}

// ------------------------------------------------------------------ options window
void App::DrawOptions(RectF pr) {
    f32 lh = r.Metrics(FONT_UI).lineH;
    RectF head = pr;
    head = head.CutTop(lh + 24 * S);
    r.Text(FONT_TITLE, T(S_Options), RectF(head.x + 20 * S, head.y + 4 * S, head.w - 80 * S, head.h), theme.text);
    if (IconButton(HashId(&popup, 1), RectF(head.Right() - head.h, head.y + 6 * S, head.h - 12 * S, head.h - 12 * S), GLYPH_CLOSE, T(S_Cancel))) {
        ClosePopup();
        return;
    }
    RectF body = RectF(pr.x, head.Bottom(), pr.w, pr.h - head.h);
    RectF tabs = body.CutLeft(170 * S);
    const wchar_t *tabNames[kOptionsTabs] = {T(S_SetGeneral), T(S_SetAppearance), T(S_SetBehavior), T(S_SetSystem),
                                             TL(L"Shortcuts", L"단축키")};
    for (int i = 0; i < kOptionsTabs; i++) {
        RectF tr(tabs.x + 10 * S, tabs.y + 4 * S + i * (lh + 16 * S), tabs.w - 20 * S, lh + 12 * S);
        bool act = popup.optionsTab == i;
        if (act) r.FillRect(tr, theme.selection, 6 * S);
        else if (Hover(tr)) r.FillRect(tr, theme.hover, 6 * S);
        r.Text(act ? FONT_UI_BOLD : FONT_UI, tabNames[i], RectF(tr.x + 12 * S, tr.y, tr.w - 16 * S, tr.h), theme.text);
        if (Clicked(tr) && popup.optionsTab != i) {
            popup.optionsTab = i;
            popup.scroll = 0;
            popup.captureCmd.clear();
        }
    }
    r.Line(tabs.Right(), body.y + 4 * S, tabs.Right(), body.Bottom() - 10 * S, theme.border);
    RectF content = body.Shrink(16 * S);
    content.x += 4 * S;
    if (popup.optionsTab == kShortcutsTab) { DrawShortcutOptions(content); return; }
    r.PushClip(content);
    f32 rowH = std::floor(lh + 18 * S);
    f32 y = content.y - popup.scroll;
    bool changed = false, appearance = false;
    int rowIndex = 0;

    auto labelRow = [&](const std::wstring &label, RectF *ctrl) {
        RectF rr(content.x, y, content.w, rowH);
        y += rowH;
        if (rowIndex++ & 1) r.FillRect(rr, theme.rowAlt, 4 * S);
        r.Text(FONT_UI, label, RectF(rr.x + 10 * S, rr.y, rr.w * 0.55f, rr.h), theme.text);
        *ctrl = RectF(rr.x + rr.w * 0.58f, rr.y + 5 * S, rr.w * 0.42f - 10 * S, rr.h - 10 * S);
    };
    auto toggle = [&](const std::wstring &label, bool &v, bool isAppearance = false) {
        RectF c;
        labelRow(label, &c);
        RectF sw(c.Right() - 40 * S, c.y + (c.h - 20 * S) / 2, 40 * S, 20 * S);
        r.FillRect(sw, v ? theme.accent : theme.border, 10 * S);
        f32 kx = v ? sw.Right() - 18 * S : sw.x + 2 * S;
        r.FillRect(RectF(kx, sw.y + 2 * S, 16 * S, 16 * S), Color(1, 1, 1, 1), 8 * S);
        TextOpts o; o.align = ALIGN_RIGHT;
        r.Text(FONT_SMALL, T(v ? S_On : S_Off), RectF(c.x, c.y, c.w - 50 * S, c.h), theme.textDim, o);
        if (Clicked(RectF(c.x, c.y, c.w, c.h))) { v = !v; changed = true; appearance |= isAppearance; }
    };
    auto choice = [&](const std::wstring &label, int &v, std::vector<std::wstring> opts, bool isAppearance = false) {
        RectF c;
        labelRow(label, &c);
        f32 segW = c.w / opts.size();
        r.FillRect(c, theme.inputBg, 5 * S);
        r.StrokeRect(c, theme.border, 1 * S, 5 * S);
        for (size_t i = 0; i < opts.size(); i++) {
            RectF s(c.x + i * segW, c.y, segW, c.h);
            if ((int)i == v) r.FillRect(s.Shrink(2 * S), theme.accent, 4 * S);
            else if (Hover(s)) r.FillRect(s.Shrink(2 * S), theme.hover, 4 * S);
            TextOpts o; o.align = ALIGN_CENTER;
            r.Text(FONT_SMALL, opts[i], s, (int)i == v ? theme.accentText : theme.text, o);
            if (Clicked(s) && v != (int)i) { v = (int)i; changed = true; appearance |= isAppearance; }
        }
    };
    auto stepper = [&](const std::wstring &label, f32 &v, f32 step, f32 lo, f32 hi, const wchar_t *fmt) {
        RectF c;
        labelRow(label, &c);
        RectF minus(c.Right() - 110 * S, c.y, 30 * S, c.h), plus(c.Right() - 30 * S, c.y, 30 * S, c.h);
        RectF val(minus.Right(), c.y, plus.x - minus.Right(), c.h);
        r.FillRect(RectF(minus.x, c.y, 110 * S, c.h), theme.inputBg, 5 * S);
        r.StrokeRect(RectF(minus.x, c.y, 110 * S, c.h), theme.border, 1 * S, 5 * S);
        TextOpts o; o.align = ALIGN_CENTER;
        r.Text(FONT_UI, L"−", minus, Hover(minus) ? theme.accent : theme.text, o);
        r.Text(FONT_UI, L"+", plus, Hover(plus) ? theme.accent : theme.text, o);
        r.Text(FONT_UI, Format(fmt, v), val, theme.text, o);
        if (Clicked(minus)) { v = std::max(lo, v - step); changed = true; appearance = true; }
        if (Clicked(plus)) { v = std::min(hi, v + step); changed = true; appearance = true; }
    };
    auto fontPick = [&](const std::wstring &label, std::wstring &v, std::vector<std::wstring> recommended) {
        RectF c;
        labelRow(label, &c);
        r.FillRect(c, theme.inputBg, 5 * S);
        r.StrokeRect(c, Hover(c) ? theme.accent : theme.border, 1 * S, 5 * S);
        r.Text(FONT_UI, v, RectF(c.x + 10 * S, c.y, c.w - 34 * S, c.h), theme.text);
        TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
        r.Text(FONT_ICON_SMALL, GLYPH_CHEVRON_D, RectF(c.Right() - 26 * S, c.y, 20 * S, c.h), theme.textDim, ko);
        if (Clicked(c)) {
            std::vector<MenuItem> m;
            const auto &all = r.SystemFontFamilies();
            std::vector<std::wstring> fonts;
            for (auto &f : recommended)
                if (std::find(all.begin(), all.end(), f) != all.end()) fonts.push_back(f); // only installed ones
            size_t recCount = fonts.size();
            for (auto &f : all) if (std::find(fonts.begin(), fonts.begin() + recCount, f) == fonts.begin() + recCount) fonts.push_back(f);
            for (size_t fi = 0; fi < fonts.size(); fi++) {
                const std::wstring &f = fonts[fi];
                if (fi == recCount && recCount > 0) { MenuItem sepItem; sepItem.separator = true; m.push_back(sepItem); }
                MenuItem it;
                it.label = f;
                it.checked = _wcsicmp(f.c_str(), v.c_str()) == 0;
                std::wstring *target = &v;
                it.action = [this, target, f] {
                    *target = f;
                    SaveConfig();
                    ApplySettings();
                };
                m.push_back(it);
            }
            OpenOptionsDropdown(std::move(m), c.x, c.Bottom() + 2 * S, c.w);
        }
    };
    auto button = [&](const std::wstring &label, const std::wstring &btn, std::function<void()> fn) {
        RectF c;
        labelRow(label, &c);
        f32 bw = std::min(c.w, r.Measure(FONT_UI, btn) + 30 * S);
        RectF b(c.Right() - bw, c.y, bw, c.h);
        r.FillRect(b, Hover(b) ? theme.accent.WithAlpha(0.85f) : theme.accent, 5 * S);
        TextOpts o; o.align = ALIGN_CENTER;
        r.Text(FONT_UI, btn, b, theme.accentText, o);
        if (Clicked(b)) fn();
    };
    auto info = [&](const std::wstring &label, const std::wstring &value) {
        RectF c;
        labelRow(label, &c);
        TextOpts o; o.align = ALIGN_RIGHT;
        r.Text(FONT_UI, value, c, theme.textDim, o);
    };

    Settings &s = settings;
    switch (popup.optionsTab) {
    case 0:
        choice(T(S_Language), s.language, {T(S_Auto), L"English", L"한국어"}, true);
        choice(TL(L"Load on startup", L"시작 시 열기"), s.loadOnStartup, {T(S_StartHome), T(S_StartLastSession), T(S_StartThisPC)});
        choice(TL(L"New tab shows", L"새 탭 위치"), s.newTabShow, {T(S_Home), TL(L"Current", L"현재 폴더"), T(S_ThisPC)});
        toggle(TL(L"Store recent folders", L"최근 폴더 기록"), s.storeRecents);
        toggle(TL(L"Show recents in sidebar", L"사이드바에 최근 폴더 표시"), s.showRecents);
        toggle(TL(L"Calculate folder sizes", L"폴더 크기 계산"), s.calcFolderSizes);
        toggle(TL(L"   …on SSD", L"   …SSD에서"), s.calcFolderSizesSSD);
        toggle(TL(L"   …on HDD / removable", L"   …HDD / 이동식 디스크에서"), s.calcFolderSizesHDD);
        toggle(TL(L"   …on network drives", L"   …네트워크 드라이브에서"), s.calcFolderSizesNetwork);
        toggle(TL(L"Load thumbnails in inspector", L"미리 보기에서 썸네일 불러오기"), s.loadThumbnails);
        break;
    case 1: {
        choice(TL(L"Color scheme", L"색 구성"), s.colorScheme, {TL(L"System", L"시스템"), T(S_Dark), T(S_Light)}, true);
        choice(TL(L"Background effect", L"배경 효과"), s.backdrop,
               {TL(L"None", L"없음"), TL(L"Clear", L"투명"), TL(L"Mica", L"미카"), TL(L"Acrylic", L"아크릴"), TL(L"Tabbed", L"탭")}, true);
        if (s.backdrop != BACKDROP_NONE) {
            f32 op = (f32)s.backgroundOpacity;
            stepper(TL(L"   …background opacity", L"   …배경 불투명도"), op, 5, 0, 100, L"%.0f%%");
            s.backgroundOpacity = (int)op;
        }
        fontPick(TL(L"UI font", L"UI 글꼴"), s.fontName,
                 {L"Segoe UI", L"Segoe UI Variable Text", L"맑은 고딕", L"Pretendard", L"Noto Sans KR", L"나눔고딕", L"나눔바른고딕", L"Spoqa Han Sans Neo", L"IBM Plex Sans KR"});
        stepper(TL(L"Font size", L"글꼴 크기"), s.fontSize, 0.5f, 6, 32, L"%.1f pt");
        f32 sp = (f32)s.spacing;
        stepper(TL(L"Row spacing", L"행 간격"), sp, 1, 0, 12, L"%.0f");
        if ((int)sp != s.spacing) s.spacing = (int)sp;
        fontPick(TL(L"Inspector font", L"미리 보기 글꼴"), s.inspectorFontName,
                 {L"Consolas", L"Cascadia Mono", L"Cascadia Code", L"D2Coding", L"나눔고딕코딩", L"JetBrains Mono", L"맑은 고딕", L"Courier New"});
        stepper(TL(L"Inspector font size", L"미리 보기 글꼴 크기"), s.inspectorFontSize, 0.5f, 6, 32, L"%.1f pt");
        choice(TL(L"Inspector position", L"미리 보기 위치"), s.inspectorLayout, {TL(L"Right", L"오른쪽"), TL(L"Bottom", L"아래쪽")});
        toggle(TL(L"Show file extensions", L"파일 확장자 표시"), s.showFileExtension);
        toggle(TL(L"Show hidden files", L"숨김 파일 표시"), s.showHiddenFiles);
        toggle(TL(L"Show protected system files", L"보호된 시스템 파일 표시"), s.showSystemFiles);
        toggle(TL(L"Show checkboxes", L"확인란 표시"), s.showCheckboxes);
        toggle(TL(L"Show path breadcrumbs", L"경로 이동 경로 표시"), s.showBreadcrumbs);
        toggle(TL(L"Alternating row colors", L"행 색상 교차"), s.alternatingRowColors);
        toggle(TL(L"Round corners", L"둥근 모서리"), s.roundCorners, true);
        toggle(TL(L"Animations", L"애니메이션"), s.animations);
        toggle(TL(L"Underline on hover", L"마우스를 올리면 밑줄"), s.underlineOnHover);
        toggle(TL(L"Show tooltips", L"도구 설명 표시"), s.showTooltips);
        toggle(TL(L"VSync", L"수직 동기화"), s.vsync);
    } break;
    case 2:
        toggle(TL(L"Single click to open", L"한 번 클릭으로 열기"), s.singleClick);
        toggle(TL(L"Single click opens folders only", L"폴더만 한 번 클릭으로 열기"), s.singleClickFoldersOnly);
        toggle(TL(L"Double click empty space goes up", L"빈 곳 더블 클릭 시 상위 폴더로"), s.doubleClickToParent);
        choice(TL(L"Typing in the file list", L"파일 목록에서 입력 시"), s.characterKeyAction, {T(S_CharSelect), T(S_CharFilter)});
        toggle(TL(L"Folders first", L"폴더를 먼저 정렬"), s.foldersFirst);
        toggle(TL(L"Reverse scroll", L"스크롤 방향 반대로"), s.reverseScroll);
        toggle(TL(L"Search subfolders with Everything (when it runs)", L"하위 폴더 검색에 Everything 사용 (실행 중일 때)"), s.useEverything);
        info(TL(L"Hangul input", L"한글 입력"), TL(L"IME composition + initial consonant search", L"IME 조합 표시 + 초성 검색 지원"));
        break;
    case 3: {
        bool inst = IsInstalled();
        button(inst ? TL(L"Installed (shortcuts, default app, right-click menu, PATH)", L"설치됨 (바로 가기, 기본 앱, 우클릭 메뉴, PATH)")
                    : TL(L"Not installed", L"설치되지 않음"),
               inst ? TL(L"Setup…", L"설치 설정…") : TL(L"Install…", L"설치…"), [this] { OpenSetup(false); });
        button(TL(L"Config file", L"설정 파일"), TL(L"Open", L"열기"), [this] { RunCommand(L"ConfigOpen"); });
        button(TL(L"Data folder", L"데이터 폴더"), TL(L"Open", L"열기"), [this] { OpenExplorerAt(paths.dir, L""); });
        info(TL(L"Version", L"버전"), APP_VERSION);
        info(TL(L"Directory engine", L"디렉터리 엔진"), FsHasNtQuery() ? L"NtQueryDirectoryFile" : L"FindFirstFileEx");
        info(TL(L"Renderer", L"렌더러"), L"Direct3D 11 + Direct2D / DirectWrite");
        info(TL(L"Data location", L"데이터 위치"), paths.dir);
    } break;
    }
    f32 contentH = y + popup.scroll - content.y;
    if (Hover(content) && in.wheel != 0) popup.scroll -= in.wheel * rowH * 2;
    popup.scroll = Clamp(popup.scroll, 0.f, std::max(0.f, contentH - content.h));
    r.PopClip();
    if (changed) {
        SaveConfig();
        if (appearance) ApplySettings();
        ForEachPanel([](Panel *p) { for (auto &t : p->tabs) t->rowsDirty = true; });
        Invalidate();
    } else if (popup.kind == POPUP_OPTIONS) {
        static f32 lastFont = 0, lastInspFont = 0;
        if (lastFont != settings.fontSize || lastInspFont != settings.inspectorFontSize) {
            if (lastFont != 0) { SaveConfig(); ApplySettings(); }
            lastFont = settings.fontSize;
            lastInspFont = settings.inspectorFontSize;
        }
    }
}

// ------------------------------------------------------------------ options > Shortcuts
// Search box, then one row per command: its keys as chips (× removes one), + records a new key, ↺ restores defaults.
void App::DrawShortcutOptions(RectF c) {
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 rowH = std::floor(lh + 18 * S);
    bool capturing = !popup.captureCmd.empty();

    // ---- search box and "reset all"
    RectF top = c.CutTop(rowH);
    std::wstring resetLabel = GetTickCount64() - popup.resetArmedTick < 3000 ? TL(L"Click again to reset", L"한 번 더 누르면 초기화")
                                                                              : TL(L"Reset all", L"모두 기본값으로");
    f32 rbW = r.Measure(FONT_UI, resetLabel) + 28 * S;
    RectF rb(top.Right() - rbW, top.y + 4 * S, rbW, top.h - 8 * S);
    RectF sb(top.x, top.y + 4 * S, std::max(120 * S, rb.x - top.x - 12 * S), top.h - 8 * S);
    r.FillRect(sb, theme.inputBg, 5 * S);
    r.StrokeRect(sb, capturing ? theme.border : theme.accent, 1 * S, 5 * S);
    TextOpts io; io.align = ALIGN_CENTER; io.ellipsis = false;
    r.Text(FONT_ICON_SMALL, GLYPH_SEARCH, RectF(sb.x, sb.y, sb.h, sb.h), theme.textFaint, io);
    DrawTextEdit(popup.edit, RectF(sb.x + sb.h, sb.y, sb.w - sb.h - 8 * S, sb.h), FONT_UI, theme.text,
                 TL(L"Search commands or keys", L"명령 또는 키 검색"), !capturing);
    r.FillRect(rb, Hover(rb) ? theme.hover : theme.inputBg, 5 * S);
    r.StrokeRect(rb, theme.border, 1 * S, 5 * S);
    TextOpts co; co.align = ALIGN_CENTER;
    r.Text(FONT_UI, resetLabel, rb, theme.text, co);
    if (Clicked(rb)) {
        in.pressed[0] = false;
        if (GetTickCount64() - popup.resetArmedTick < 3000) {
            ResetAllHotkeys();
            popup.resetArmedTick = 0;
            Toast(TL(L"All shortcuts are back to their defaults.", L"모든 단축키를 기본값으로 되돌렸습니다."));
        } else {
            popup.resetArmedTick = GetTickCount64();
        }
    }
    if (popup.resetArmedTick && GetTickCount64() - popup.resetArmedTick < 3100) Invalidate(); // label times out
    RectF hint = c.CutTop(lh + 8 * S);
    r.Text(FONT_SMALL, capturing ? TL(L"Press the new key combination. Esc cancels.", L"새 키 조합을 누르세요. Esc를 누르면 취소합니다.")
                                 : TL(L"+ adds a key, × removes one, the arrow at the right restores the defaults.",
                                      L"+로 키 추가, ×로 삭제, 오른쪽 화살표로 기본값 복원"),
           hint, capturing ? theme.accent : theme.textFaint);

    // ---- commands matching the search
    std::wstring q = popup.edit.text;
    if (ime.composing) q += ime.comp;
    std::vector<const Command *> list;
    for (auto &cmd : commands) {
        if (!q.empty()) {
            std::wstring keys;
            for (auto &k : CommandHotkeys(cmd.id)) keys += k + L" ";
            bool hit = ContainsNoCase(cmd.nameKo, q) || ContainsNoCase(cmd.nameEn, q) || ContainsNoCase(cmd.id, q) ||
                       ContainsNoCase(keys, q) || ContainsNoCase(HangulChoseong(cmd.nameKo), q);
            if (!hit) continue;
        }
        list.push_back(&cmd);
    }

    r.PushClip(c);
    f32 y = c.y - popup.scroll;
    int rowIndex = 0;
    TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
    for (const Command *cmd : list) {
        RectF rr(c.x, y, c.w, rowH);
        y += rowH;
        if (rr.Bottom() < c.y || rr.y > c.Bottom()) { rowIndex++; continue; }
        if (rowIndex++ & 1) r.FillRect(rr, theme.rowAlt, 4 * S);
        bool rowCapturing = popup.captureCmd == cmd->id;
        if (rowCapturing) r.FillRect(rr, theme.selection, 4 * S);
        r.Text(FONT_UI, TL(cmd->nameEn, cmd->nameKo), RectF(rr.x + 10 * S, rr.y, rr.w * 0.42f - 10 * S, rr.h), theme.text);
        auto canClick = [&](const RectF &b) { return Clicked(b) && Hover(c); };

        f32 chipH = rowH - 12 * S;
        f32 x = rr.x + rr.w * 0.42f;
        f32 right = rr.Right() - 10 * S - chipH; // the reset button sits at the far right
        for (const std::wstring &k : CommandHotkeys(cmd->id)) {
            f32 cw = r.Measure(FONT_SMALL, k) + 16 * S + chipH * 0.7f;
            if (x + cw > right) break;
            RectF chip(x, rr.y + 6 * S, cw, chipH);
            bool hov = Hover(chip) && Hover(c) && !capturing;
            r.FillRect(chip, hov ? theme.hover : theme.inputBg, 4 * S);
            r.StrokeRect(chip, theme.border, 1 * S, 4 * S);
            r.Text(FONT_SMALL, k, RectF(chip.x + 8 * S, chip.y, cw - chipH * 0.7f - 8 * S, chip.h), theme.text);
            RectF xb(chip.Right() - chipH * 0.8f, chip.y, chipH * 0.8f, chip.h);
            r.Text(FONT_ICON_SMALL, GLYPH_CLOSE, xb, hov ? theme.text : theme.textFaint, ko);
            if (hov) SetTooltip(HashStr(cmd->id + k, 41), TL(L"Remove this key", L"이 키 삭제"));
            if (!capturing && canClick(chip)) {
                std::vector<std::wstring> rest = CommandHotkeys(cmd->id);
                rest.erase(std::remove(rest.begin(), rest.end(), k), rest.end());
                SetCommandHotkeys(cmd->id, rest);
                in.pressed[0] = false;
                break; // the list changed under us
            }
            x += cw + 6 * S;
        }
        if (rowCapturing) {
            std::wstring wait = TL(L"Press keys…", L"키를 누르세요…");
            RectF chip(x, rr.y + 6 * S, r.Measure(FONT_SMALL, wait) + 20 * S, chipH);
            r.FillRect(chip, theme.inputBg, 4 * S);
            r.StrokeRect(chip, theme.accent, std::max(1.5f, 1.5f * S), 4 * S);
            r.Text(FONT_SMALL, wait, chip, theme.accent, ko);
            if (canClick(rr)) in.pressed[0] = false; // clicks while waiting do nothing; Esc cancels
        } else if (x + chipH <= right + chipH) {
            RectF add(x, rr.y + 6 * S, chipH, chipH);
            bool hov = Hover(add) && Hover(c) && !capturing;
            r.FillRect(add, hov ? theme.hover : Color(0, 0, 0, 0), 4 * S);
            r.Text(FONT_ICON_SMALL, GLYPH_ADD, add, hov ? theme.accent : theme.textDim, ko);
            if (hov) SetTooltip(HashStr(cmd->id, 42), TL(L"Add a key", L"키 추가"));
            if (!capturing && canClick(add)) { popup.captureCmd = cmd->id; in.pressed[0] = false; }
        }
        if (HotkeysChanged(cmd->id) && !rowCapturing) {
            RectF reset(rr.Right() - 6 * S - chipH, rr.y + 6 * S, chipH, chipH);
            bool hov = Hover(reset) && Hover(c) && !capturing;
            r.FillRect(reset, hov ? theme.hover : Color(0, 0, 0, 0), 4 * S);
            r.Text(FONT_ICON_SMALL, GLYPH_REFRESH, reset, hov ? theme.accent : theme.textDim, ko);
            if (hov) SetTooltip(HashStr(cmd->id, 43), TL(L"Restore the default keys", L"기본 키로 되돌리기"));
            if (!capturing && canClick(reset)) {
                std::wstring msg = ResetCommandHotkeys(cmd->id);
                if (!msg.empty()) Toast(msg);
                in.pressed[0] = false;
            }
        }
    }
    if (list.empty()) r.Text(FONT_UI, TL(L"No matching command.", L"일치하는 명령이 없습니다."), RectF(c.x + 10 * S, c.y, c.w, rowH), theme.textFaint);
    f32 contentH = rowH * list.size();
    if (Hover(c) && in.wheel != 0) popup.scroll -= in.wheel * rowH * 3;
    popup.scroll = Clamp(popup.scroll, 0.f, std::max(0.f, contentH - c.h));
    r.PopClip();
}

// ------------------------------------------------------------------ shared menu box
void App::OpenOptionsDropdown(std::vector<MenuItem> items, f32 x, f32 y, f32 minW) {
    popup.menuMinW = minW;
    popup.menu = std::move(items);
    popup.subMenu = true;
    popup.subScroll = 0;
    popup.sel = -1;
    popup.typeAhead.clear();
    for (int i = 0; i < (int)popup.menu.size(); i++) if (popup.menu[i].checked) popup.sel = i;
    popup.menuEnsureSel = popup.sel >= 0;
    popup.x = x;
    popup.y = y;
    popup.openTick = GetTickCount64();
    in.pressed[0] = false;
    Invalidate();
}

int App::DrawMenuBox(f32 px, f32 py, f32 &scroll, RectF *outRect) {
    f32 W = (f32)r.Width(), H = (f32)r.Height();
    f32 lh = r.Metrics(FONT_UI).lineH;
    bool anyPress = in.pressed[0] || in.pressed[1] || in.pressed[2];
    f32 itemH = std::floor(lh + 12 * S), sepH = 9 * S;
    f32 iconSz = std::floor(lh * 1.05f);
    f32 labelW = 0, hkW = 0;
    bool anyIcon = false, anyCheck = false;
    for (auto &m : popup.menu) {
        if (m.separator) continue;
        labelW = std::max(labelW, r.Measure(FONT_UI, m.label));
        if (!m.hotkey.empty()) hkW = std::max(hkW, r.Measure(FONT_SMALL, m.hotkey));
        if (m.icon) anyIcon = true;
        if (m.checked) anyCheck = true;
    }
    f32 lead = 12 * S + (anyIcon ? iconSz + 8 * S : 0) + (anyCheck ? 18 * S : 0);
    f32 w = std::min(W - 16 * S, lead + labelW + (hkW > 0 ? hkW + 30 * S : 0) + 20 * S);
    w = std::max(w, std::max(180 * S, popup.menuMinW));
    f32 contentH = 8 * S;
    for (auto &m : popup.menu) contentH += m.separator ? sepH : itemH;
    f32 x = px, y = py;
    if (x + w > W - 8 * S) x = W - 8 * S - w;
    // prefer opening below the anchor (scrolling if needed); flip above only when that side has clearly more room
    f32 below = H - 8 * S - py, above = py - 8 * S;
    f32 h;
    if (contentH <= below || below >= above * 0.8f || below >= 300 * S) {
        h = std::min(contentH, below);
    } else {
        h = std::min(contentH, above);
        y = py - h;
    }
    h = std::max(h, std::min(contentH, itemH * 3));
    if (y + h > H - 8 * S) y = H - 8 * S - h;
    RectF pr(std::max(8 * S, x), y, w, h);
    if (outRect) *outRect = pr;
    // hovering only picks an item when the mouse actually moved (so keyboard/type-ahead selection sticks)
    static f32 lastMx = -1, lastMy = -1;
    bool mouseMoved = in.mx != lastMx || in.my != lastMy;
    lastMx = in.mx;
    lastMy = in.my;
    r.Shadow(pr, 8 * S, 12 * S, 0.3f);
    r.FillRect(pr, theme.popupBg, 8 * S);
    r.StrokeRect(pr, theme.popupBorder, 1 * S, 8 * S);
    if (Hover(pr) && in.wheel != 0) scroll = Clamp(scroll - in.wheel * itemH * 3, 0.f, std::max(0.f, contentH - h));
    if (popup.menuEnsureSel && popup.sel >= 0) {
        f32 sy = 4 * S;
        for (int i = 0; i < popup.sel; i++) sy += popup.menu[i].separator ? sepH : itemH;
        if (sy < scroll) scroll = sy;
        if (sy + itemH > scroll + h - 8 * S) scroll = sy + itemH - h + 8 * S;
        scroll = Clamp(scroll, 0.f, std::max(0.f, contentH - h));
        popup.menuEnsureSel = false;
    }
    r.PushClip(pr.Shrink(1 * S));
    f32 cy = pr.y + 4 * S - scroll;
    int result = -1;
    for (int i = 0; i < (int)popup.menu.size(); i++) {
        MenuItem &m = popup.menu[i];
        if (m.separator) {
            r.Line(pr.x + 10 * S, cy + sepH / 2, pr.Right() - 10 * S, cy + sepH / 2, theme.border);
            cy += sepH;
            continue;
        }
        RectF ir(pr.x + 4 * S, cy, pr.w - 8 * S, itemH);
        cy += itemH;
        if (ir.Bottom() < pr.y || ir.y > pr.Bottom()) continue;
        bool hov = Hover(ir) && m.enabled;
        if (hov && (mouseMoved || in.released[0])) popup.sel = i;
        if (i == popup.sel && m.enabled) r.FillRect(ir, theme.selection, 5 * S);
        f32 tx = ir.x + 8 * S;
        if (anyCheck) {
            if (m.checked) {
                TextOpts co; co.align = ALIGN_CENTER; co.ellipsis = false;
                r.Text(FONT_ICON_SMALL, GLYPH_CHECK, RectF(tx, ir.y, 14 * S, ir.h), theme.accent, co);
            }
            tx += 18 * S;
        }
        if (anyIcon) {
            if (m.icon) r.DrawBitmap(m.icon, RectF(tx, ir.y + (itemH - iconSz) / 2, iconSz, iconSz));
            tx += iconSz + 8 * S;
        }
        f32 hkSpace = hkW > 0 ? hkW + 20 * S : 8 * S;
        r.Text(FONT_UI, m.label, RectF(tx, ir.y, ir.Right() - tx - hkSpace, ir.h), m.enabled ? theme.text : theme.textFaint);
        if (!m.hotkey.empty()) {
            TextOpts ho; ho.align = ALIGN_RIGHT;
            r.Text(FONT_SMALL, m.hotkey, RectF(ir.Right() - hkW - 12 * S, ir.y, hkW, ir.h), theme.textFaint, ho);
        }
        if (hov && in.released[0] && GetTickCount64() - popup.openTick > 150) result = i;
    }
    r.PopClip();
    if (result < 0 && anyPress && !Hover(pr)) {
        result = -2;
        in.pressed[0] = in.pressed[1] = in.pressed[2] = false; // the click only dismisses the menu
    }
    return result;
}

// type-to-find in an options dropdown: prefix match, then substring; Hangul initial consonants work too
void App::DropdownTypeAhead(const std::wstring &str) {
    u64 now = GetTickCount64();
    if (now - popup.typeAheadTick > 1200) popup.typeAhead.clear();
    popup.typeAheadTick = now;
    popup.typeAhead += str;
    std::wstring q = ToLower(popup.typeAhead);
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int i = 0; i < (int)popup.menu.size(); i++) {
            const MenuItem &m = popup.menu[i];
            if (m.separator || !m.enabled) continue;
            std::wstring l = ToLower(m.label), cho = HangulChoseong(l);
            bool hit = pass == 0 ? (l.rfind(q, 0) == 0 || cho.rfind(q, 0) == 0)
                                 : (l.find(q) != std::wstring::npos || cho.find(q) != std::wstring::npos);
            if (hit) { best = i; break; }
        }
    }
    if (best >= 0) {
        popup.sel = best;
        popup.menuEnsureSel = true;
    }
    Invalidate();
}
