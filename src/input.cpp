// Keyboard, text editing and IME (한글 조합) handling
#include "app.h"
#include "shell.h"
#include <imm.h>

#pragma comment(lib, "imm32.lib")

// ================================================================== TextEdit
static bool IsWordChar(wchar_t c) { return iswalnum(c) || c == L'_' || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x3131 && c <= 0x318E); }

void TextEdit::DeleteSel() {
    if (!HasSel()) return;
    u32 a = SelMin(), b = SelMax();
    text.erase(a, b - a);
    caret = anchor = a;
}

void TextEdit::Insert(const std::wstring &sIn) {
    std::wstring s;
    for (wchar_t c : sIn) if (c >= 32 && c != 127) s += c;
    if (s.empty()) return;
    if (undo.empty() || undo.back() != text) undo.push_back(text);
    if (undo.size() > 100) undo.erase(undo.begin());
    DeleteSel();
    text.insert(caret, s);
    caret += (u32)s.size();
    anchor = caret;
    blinkStart = GetTickCount64();
}

bool TextEdit::OnKey(const KeyEvent &k, HWND hwnd, bool *changed) {
    *changed = false;
    blinkStart = GetTickCount64();
    u32 n = (u32)text.size();
    auto moveTo = [&](u32 pos) {
        caret = pos;
        if (!k.shift) anchor = caret;
    };
    auto wordLeft = [&](u32 p) {
        while (p > 0 && !IsWordChar(text[p - 1])) p--;
        while (p > 0 && IsWordChar(text[p - 1])) p--;
        return p;
    };
    auto wordRight = [&](u32 p) {
        while (p < n && IsWordChar(text[p])) p++;
        while (p < n && !IsWordChar(text[p])) p++;
        return p;
    };
    switch (k.vk) {
    case VK_LEFT:
        if (HasSel() && !k.shift) { moveTo(SelMin()); return true; }
        moveTo(k.ctrl ? wordLeft(caret) : (caret > 0 ? caret - 1 : 0));
        return true;
    case VK_RIGHT:
        if (HasSel() && !k.shift) { moveTo(SelMax()); return true; }
        moveTo(k.ctrl ? wordRight(caret) : std::min(n, caret + 1));
        return true;
    case VK_HOME: moveTo(0); return true;
    case VK_END: moveTo(n); return true;
    case VK_BACK:
        if (undo.empty() || undo.back() != text) undo.push_back(text);
        if (HasSel()) DeleteSel();
        else if (caret > 0) {
            u32 from = k.ctrl ? wordLeft(caret) : caret - 1;
            text.erase(from, caret - from);
            caret = anchor = from;
        }
        *changed = true;
        return true;
    case VK_DELETE:
        if (undo.empty() || undo.back() != text) undo.push_back(text);
        if (HasSel()) DeleteSel();
        else if (caret < n) {
            u32 to = k.ctrl ? wordRight(caret) : caret + 1;
            text.erase(caret, to - caret);
        }
        *changed = true;
        return true;
    }
    if (k.ctrl && !k.alt) {
        switch (k.vk) {
        case 'A': anchor = 0; caret = n; return true;
        case 'C':
            if (HasSel()) ClipboardSetText(hwnd, text.substr(SelMin(), SelMax() - SelMin()));
            return true;
        case 'X':
            if (HasSel()) {
                ClipboardSetText(hwnd, text.substr(SelMin(), SelMax() - SelMin()));
                if (undo.empty() || undo.back() != text) undo.push_back(text);
                DeleteSel();
                *changed = true;
            }
            return true;
        case 'V': {
            std::wstring s;
            if (ClipboardGetText(hwnd, s)) {
                for (auto &c : s) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
                Insert(s);
                *changed = true;
            }
            return true;
        }
        case 'Z':
            if (!undo.empty()) {
                text = undo.back();
                undo.pop_back();
                caret = anchor = (u32)text.size();
                *changed = true;
            }
            return true;
        }
    }
    return false;
}

// ================================================================== IME
TextEdit *App::FocusedEdit() {
    if (popup.kind == POPUP_PALETTE || popup.kind == POPUP_GOTO || ShortcutSearchFocused()) return &popup.edit;
    switch (focus) {
    case FOCUS_FILTER: return &filterEdit;
    case FOCUS_PATH: return &pathEdit;
    case FOCUS_RENAME: return &renameEdit;
    case FOCUS_SETUP: return &setup.pathEdit;
    default: return nullptr;
    }
}

void App::UpdateImePosition(f32 x, f32 y, f32 h) {
    static LONG lx = -1, ly = -1, lh = -1;
    if ((LONG)x == lx && (LONG)y == ly && (LONG)h == lh) return;
    lx = (LONG)x; ly = (LONG)y; lh = (LONG)h;
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return;
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = {lx, ly};
    ImmSetCompositionWindow(imc, &cf);
    CANDIDATEFORM cand = {};
    cand.dwIndex = 0;
    cand.dwStyle = CFS_EXCLUDE;
    cand.ptCurrentPos = {lx, ly + lh};
    cand.rcArea = {lx, ly, lx + 1, ly + lh};
    ImmSetCandidateWindow(imc, &cand);
    ImmReleaseContext(hwnd, imc);
}

void App::OnImeComposition(const std::wstring &comp, int cursor) {
    ime.composing = !comp.empty();
    ime.comp = comp;
    ime.cursor = cursor;
    if (!comp.empty() && !TextFocused() && popup.kind == POPUP_NONE) {
        Tab *t = CurTab();
        if (t && settings.characterKeyAction == CHARKEY_FILTER) BeginFilter(L"");
    }
    if (focus == FOCUS_FILTER) ApplyFilter();
    if (popup.kind == POPUP_PALETTE || popup.kind == POPUP_GOTO) RefreshPaletteItems();
    Invalidate();
}

void App::OnImeEnd() {
    ime.composing = false;
    ime.comp.clear();
    if (focus == FOCUS_FILTER) ApplyFilter();
    Invalidate();
}

// ================================================================== text input
void App::OnText(const std::wstring &s) {
    if (s.empty()) return;
    if (popup.kind == POPUP_PALETTE || popup.kind == POPUP_GOTO) {
        popup.edit.Insert(s);
        RefreshPaletteItems();
        Invalidate();
        return;
    }
    if (popup.kind == POPUP_OPTIONS && popup.subMenu) { DropdownTypeAhead(s); return; }
    if (ShortcutSearchFocused()) {
        popup.edit.Insert(s);
        popup.scroll = 0;
        Invalidate();
        return;
    }
    if (popup.kind == POPUP_OPTIONS || popup.kind == POPUP_MENU || popup.kind == POPUP_CONFIRM || popup.kind == POPUP_VIEW) return;
    if (TextEdit *e = FocusedEdit()) {
        e->Insert(s);
        if (focus == FOCUS_FILTER) ApplyFilter();
        Invalidate();
        return;
    }
    if (setup.active) return;
    Tab *t = CurTab();
    if (!t) return;
    if (s == L" ") return; // space is a hotkey in the list
    if (settings.characterKeyAction == CHARKEY_FILTER) BeginFilter(s);
    else TypeSelect(t, s);
}

static bool StartsWithNoCase(const std::wstring &s, const std::wstring &prefix) {
    if (prefix.size() > s.size()) return false;
    return _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

void App::TypeSelect(Tab *t, const std::wstring &s) {
    u64 now = GetTickCount64();
    if (now - t->typeTick > 1000) t->typeBuf.clear();
    t->typeTick = now;
    t->typeBuf += s;
    int n = (int)t->rows.size();
    for (int k = 0; k < n; k++) {
        int i = (t->cursor + (t->typeBuf.size() == 1 ? 1 : 0) + k) % std::max(1, n);
        const Row &r = t->rows[i];
        if (r.header) continue;
        std::wstring name = DisplayName(*r.e);
        // "ㅍㅇ" matches "파일" (초성 type-select)
        if (StartsWithNoCase(name, t->typeBuf) || StartsWithNoCase(HangulChoseong(name), t->typeBuf)) {
            SelectOnly(t, i);
            EnsureVisible(t, i);
            return;
        }
    }
}

// ================================================================== filter / path edit
void App::BeginFilter(const std::wstring &initial) {
    Tab *t = CurTab();
    if (!t) return;
    if (pathEditing) EndPathEdit(false);
    if (!filterEditing) {
        filterEditing = true;
        filterEdit.Set(t->filter, false);
    }
    focus = FOCUS_FILTER;
    if (!initial.empty()) filterEdit.Insert(initial);
    ApplyFilter();
    Invalidate();
}

void App::EndFilter(bool clear) {
    Tab *t = CurTab();
    filterEditing = false;
    if (focus == FOCUS_FILTER) focus = FOCUS_LIST;
    if (clear && t) {
        t->filter.clear();
        filterEdit.Set(L"");
        if (t->recursive) {
            DropRows(t);
            t->recursive = false;
            if (t->search) t->search->cancel = true;
            t->search.reset();
            t->searchResults.clear();
        }
        t->rowsDirty = true;
        RebuildRows(t);
        if (!t->selected.empty()) {
            int row = RowAt(t, *t->selected.begin());
            if (row >= 0) { t->cursor = row; EnsureVisible(t, row); }
        }
    }
    Invalidate();
}

void App::ApplyFilter() {
    Tab *t = CurTab();
    if (!t) return;
    std::wstring f = filterEdit.text;
    if (focus == FOCUS_FILTER && ime.composing) f.insert(std::min<size_t>(filterEdit.caret, f.size()), ime.comp);
    if (f == t->filter && !t->rowsDirty) return;
    t->filter = f;
    t->rowsDirty = true;
    if (t->recursive) { // a full rescan per keystroke is wasted: start once typing pauses (App::Frame)
        if (t->search) t->search->cancel = true;
        DropRows(t);
        t->searchResults.clear();
        if (!Trim(f).empty()) t->search = std::make_shared<RecursiveSearch>(); // shows "searching" until then
        else t->search.reset();
        t->searchDueTick = GetTickCount64() + 250;
    }
    RebuildRows(t);
    t->scrollTarget = t->scroll = 0;
    bool any = false;
    for (int i = 0; i < (int)t->rows.size(); i++) {
        if (!t->rows[i].header) { SelectOnly(t, i); any = true; break; }
    }
    if (!any) { t->selected.clear(); t->cursor = 0; UpdateInspector(); }
    Invalidate();
}

void App::BeginPathEdit() {
    Tab *t = CurTab();
    if (!t) return;
    if (filterEditing) EndFilter(false);
    pathEditing = true;
    focus = FOCUS_PATH;
    std::wstring p = t->path;
    if (t->kind == VIEW_DRIVES) p = L"";
    else if (t->kind == VIEW_RECYCLE) p = L"shell:RecycleBinFolder";
    pathEdit.Set(p, true);
    Invalidate();
}

void App::EndPathEdit(bool commit) {
    Tab *t = CurTab();
    pathEditing = false;
    if (focus == FOCUS_PATH) focus = FOCUS_LIST;
    if (commit && t) {
        std::wstring p = pathEdit.text;
        while (!p.empty() && p.back() == L' ') p.pop_back();
        while (!p.empty() && p.front() == L' ') p.erase(0, 1);
        if (p.empty()) Navigate(t, PATH_THIS_PC);
        else if (_wcsicmp(p.c_str(), L"shell:RecycleBinFolder") == 0) Navigate(t, PATH_RECYCLE);
        else {
            std::wstring n = NormalizePath(p);
            if (PathExists(n)) Navigate(t, n);
            else if (n.rfind(L"shell:", 0) == 0 || n.find(L"://") != std::wstring::npos) ShellOpen(hwnd, p);
            else {
                // not a path: treat as a GoTo query
                OpenGoTo(p);
            }
        }
    }
    Invalidate();
}

// ================================================================== keys
static int PageRows(App *a, Tab *t) { return a->PageItems(t); }

void App::ListKey(Tab *t, const KeyEvent &k) {
    if (t->rowsDirty) RebuildRows(t);
    int n = (int)t->rows.size();
    switch (k.vk) {
    case VK_UP: if (n) SetCursor(t, IsGridView(t) ? GridNeighbor(t, t->cursor, 0, -1) : t->cursor - 1, k.shift, k.ctrl); break;
    case VK_DOWN: if (n) SetCursor(t, IsGridView(t) ? GridNeighbor(t, t->cursor, 0, 1) : t->cursor + 1, k.shift, k.ctrl); break;
    case VK_PRIOR: if (n) SetCursor(t, t->cursor - PageRows(this, t), k.shift, k.ctrl); break;
    case VK_NEXT: if (n) SetCursor(t, t->cursor + PageRows(this, t), k.shift, k.ctrl); break;
    case VK_HOME: if (n) SetCursor(t, 0, k.shift, k.ctrl); break;
    case VK_END: if (n) SetCursor(t, n - 1, k.shift, k.ctrl); break;
    case VK_RIGHT:
        if (n && IsGridView(t)) { SetCursor(t, GridNeighbor(t, t->cursor, 1, 0), k.shift, k.ctrl); break; }
        if (n && t->cursor < n) {
            const Row &r = t->rows[t->cursor];
            if (r.header) ToggleExpand(t, t->cursor, 1);
            else if (r.e->isDir && t->kind == VIEW_FOLDER) {
                std::wstring p = RowPath(r);
                if (t->expanded.count(p)) SetCursor(t, t->cursor + 1, false);
                else ToggleExpand(t, t->cursor, 1);
            }
        }
        break;
    case VK_LEFT:
        if (n && IsGridView(t)) { SetCursor(t, GridNeighbor(t, t->cursor, -1, 0), k.shift, k.ctrl); break; }
        if (n && t->cursor < n) {
            const Row &r = t->rows[t->cursor];
            if (r.header) { ToggleExpand(t, t->cursor, -1); break; }
            std::wstring p = RowPath(r);
            if (r.e->isDir && t->expanded.count(p)) { ToggleExpand(t, t->cursor, -1); break; }
            if (r.depth > 0) {
                // jump to the parent row in the tree
                for (int i = t->cursor - 1; i >= 0; i--)
                    if (!t->rows[i].header && t->rows[i].depth < r.depth) { SetCursor(t, i, false); break; }
            } else if (t->group != GROUP_NONE) {
                for (int i = t->cursor - 1; i >= 0; i--)
                    if (t->rows[i].header) { SetCursor(t, i, false); break; }
            }
        }
        break;
    case VK_RETURN:
        if (n && t->cursor < n && t->rows[t->cursor].header) ToggleExpand(t, t->cursor, 0);
        else OpenSelection(k.ctrl ? 1 : (k.shift ? 2 : 0));
        break;
    case VK_BACK: GoUp(t); break;
    case VK_ESCAPE:
        if (!t->filter.empty() || t->recursive) EndFilter(true);
        else if (!t->selected.empty()) { t->selected.clear(); UpdateInspector(); Invalidate(); }
        break;
    case VK_SPACE:
        if (k.ctrl && n && !t->rows[t->cursor].header) {
            std::wstring p = RowPath(t->rows[t->cursor]);
            if (t->selected.count(p)) t->selected.erase(p);
            else t->selected.insert(p);
            Invalidate();
        }
        break;
    }
}

void App::OnKey(const KeyEvent &k) {
    Invalidate();
    if (popup.kind != POPUP_NONE) {
        PopupKey(k);
        return;
    }
    if (setup.active) { SetupKey(k); return; }
    if (focus == FOCUS_RENAME) {
        if (k.vk == VK_RETURN) { CommitRename(); return; }
        if (k.vk == VK_ESCAPE) { CancelRename(); return; }
        if (k.vk == VK_F2) {
            // cycle: name only -> whole -> extension
            std::wstring &s = renameEdit.text;
            size_t dot = s.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) {
                if (renameEdit.anchor == 0 && renameEdit.caret == dot) { renameEdit.anchor = 0; renameEdit.caret = (u32)s.size(); }
                else if (renameEdit.anchor == 0) { renameEdit.anchor = (u32)dot + 1; renameEdit.caret = (u32)s.size(); }
                else { renameEdit.anchor = 0; renameEdit.caret = (u32)dot; }
            }
            return;
        }
        if (k.vk == VK_TAB) { CommitRename(); return; }
        bool ch;
        renameEdit.OnKey(k, hwnd, &ch);
        return;
    }
    if (focus == FOCUS_PATH) {
        if (k.vk == VK_RETURN) { EndPathEdit(true); return; }
        if (k.vk == VK_ESCAPE) { EndPathEdit(false); return; }
        if (k.vk == VK_TAB) {
            // autocomplete first matching sub folder
            std::wstring p = pathEdit.text;
            std::wstring dir = PathParent(p), part = PathFileName(p);
            if (!p.empty() && (p.back() == L'\\')) { dir = p; part.clear(); }
            std::vector<FileEntry> list;
            if (!dir.empty() && ListDirectory(NormalizePath(dir), list) == LIST_OK) {
                std::vector<std::wstring> names;
                for (auto &e : list)
                    if (e.isDir && (part.empty() || _wcsnicmp(e.name.c_str(), part.c_str(), part.size()) == 0 ||
                                    StartsWithNoCase(HangulChoseong(e.name), part)))
                        names.push_back(e.name);
                std::sort(names.begin(), names.end(), [](const std::wstring &a, const std::wstring &b) { return NaturalCompare(a, b) < 0; });
                if (!names.empty()) pathEdit.Set(PathJoin(NormalizePath(dir), names[0]) + L"\\", false);
            }
            return;
        }
        bool ch;
        pathEdit.OnKey(k, hwnd, &ch);
        return;
    }
    Tab *t = CurTab();
    if (focus == FOCUS_FILTER) {
        if (k.vk == VK_ESCAPE) { EndFilter(true); return; }
        if (k.vk == VK_RETURN) {
            EndFilter(false);
            if (t && t->rows.size() == 1 + (t->group != GROUP_NONE ? 1 : 0)) OpenSelection(0);
            else if (t && k.ctrl) OpenSelection(1);
            else if (t && (t->rows.size() > 0)) OpenSelection(0);
            return;
        }
        if (t && (k.vk == VK_UP || k.vk == VK_DOWN || k.vk == VK_PRIOR || k.vk == VK_NEXT)) { ListKey(t, k); return; }
        if (k.vk == VK_BACK && filterEdit.text.empty() && !ime.composing) { EndFilter(true); return; }
        if (HandleHotkey(k)) return;
        bool changed = false;
        filterEdit.OnKey(k, hwnd, &changed);
        if (changed) ApplyFilter();
        return;
    }
    if (HandleHotkey(k)) return;
    if (t) ListKey(t, k);
}
