// File actions: open, clipboard, delete, rename, create, bookmarks, context menus
#include "app.h"
#include "jobs.h"

void App::Toast(const std::wstring &s) {
    toasts.push_back({s, GetTickCount64()});
    if (toasts.size() > 4) toasts.erase(toasts.begin());
    Invalidate();
}

void App::Confirm(const std::wstring &msg, std::function<void()> onYes) {
    popup = Popup();
    popup.kind = POPUP_CONFIRM;
    popup.message = msg;
    popup.onConfirm = std::move(onYes);
    popup.openTick = GetTickCount64();
    focus = FOCUS_POPUP;
    Invalidate();
}

// ------------------------------------------------------------------ open
void App::OpenRow(Tab *t, int row, int where) {
    if (!t || row < 0 || row >= (int)t->rows.size()) return;
    const Row &r = t->rows[row];
    if (r.header) { ToggleExpand(t, row, 0); return; }
    std::wstring path = RowPath(r);
    if (r.e->isRecycled) { ShellProperties(hwnd, {path}); return; }
    bool dir = r.e->isDir || r.e->isDrive;
    if (!dir && PathExtension(path) == L"lnk") {
        std::wstring target = ResolveShortcut(path);
        if (!target.empty() && IsDirectory(target)) { path = target; dir = true; }
    }
    if (!dir) {
        ShellOpen(hwnd, path);
        return;
    }
    if (where == 1) {
        Tab *nt = NewTab(active, path, !GetAsyncKeyState(VK_SHIFT));
        (void)nt;
    } else if (where == 2) {
        Panel *src = active;
        SplitActive(false);
        if (active != src) Navigate(CurTab(), path, false);
    } else {
        Navigate(t, path);
    }
}

void App::OpenSelection(int where) {
    Tab *t = CurTab();
    if (!t) return;
    if (t->rowsDirty) RebuildRows(t);
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (sel.size() <= 1) {
        int row = t->cursor;
        if (sel.size() == 1) row = RowAt(t, sel[0]);
        OpenRow(t, row, where);
        return;
    }
    // multiple: open files with shell, folders as new tabs
    for (auto &p : sel) {
        if (IsDirectory(p)) NewTab(active, p, false);
        else ShellOpen(hwnd, p);
    }
}

// ------------------------------------------------------------------ clipboard
void App::CopySelection(bool cut) {
    Tab *t = CurTab();
    if (!t || t->kind == VIEW_DRIVES) return;
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (sel.empty()) return;
    if (t->kind == VIEW_RECYCLE) return;
    ClipboardSetFiles(hwnd, sel, cut);
    cutPaths.clear();
    if (cut) for (auto &p : sel) cutPaths.insert(p);
    Toast(Format(T(cut ? S_Cut : S_Copied), (int)sel.size()));
}

void App::Paste() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_FOLDER) return;
    std::vector<std::wstring> files;
    bool cut = false;
    if (!ClipboardGetFiles(hwnd, files, &cut)) return;
    std::wstring dest = t->path;
    if (cut) {
        // moving into the folder the files already live in is a no-op
        bool same = true;
        for (auto &f : files) if (_wcsicmp(PathParent(f).c_str(), dest.c_str()) != 0) same = false;
        if (same) return;
    }
    FileOperation(hwnd, cut ? FOP_MOVE : FOP_COPY, files, dest, [this, cut](bool ok) {
        if (ok && cut) {
            cutPaths.clear();
            if (OpenClipboard(hwnd)) { EmptyClipboard(); CloseClipboard(); }
        }
    });
    if (!files.empty()) t->pendingSelect = PathFileName(files[0]);
}

void App::CopyPathToClipboard(bool smart) {
    Tab *t = CurTab();
    if (!t) return;
    std::vector<std::wstring> sel = SelectedPaths(t, false);
    if (sel.empty()) sel.push_back(t->path);
    std::wstring out;
    for (size_t i = 0; i < sel.size(); i++) {
        std::wstring p = sel[i];
        bool quote = !smart || p.find(L' ') != std::wstring::npos;
        if (smart && sel.size() == 1 && p.find(L' ') == std::wstring::npos) quote = false;
        if (!smart) quote = sel.size() > 1;
        out += quote ? L"\"" + p + L"\"" : p;
        if (i + 1 < sel.size()) out += L"\r\n";
    }
    ClipboardSetText(hwnd, out);
    Toast(T(S_PathCopied));
}

void App::CopyToNeighbor(int dir, bool move) {
    Tab *t = CurTab();
    if (!t) return;
    Panel *np = NextPanel(active, dir);
    if (!np || np == active || !np->Cur() || np->Cur()->kind != VIEW_FOLDER) return;
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (sel.empty()) return;
    FileOperation(hwnd, move ? FOP_MOVE : FOP_COPY, sel, np->Cur()->path, nullptr);
}

// ------------------------------------------------------------------ delete / rename / create
static std::wstring NextUnselectedName(App *a, Tab *t) {
    int last = -1;
    for (int i = 0; i < (int)t->rows.size(); i++)
        if (!t->rows[i].header && t->selected.count(a->RowPath(t->rows[i]))) last = i;
    if (last < 0) last = t->cursor;
    for (int i = last + 1; i < (int)t->rows.size(); i++)
        if (!t->rows[i].header && !t->selected.count(a->RowPath(t->rows[i]))) return t->rows[i].e->name;
    for (int i = last - 1; i >= 0; i--)
        if (!t->rows[i].header && !t->selected.count(a->RowPath(t->rows[i]))) return t->rows[i].e->name;
    return L"";
}

void App::DeleteSelection(bool permanent) {
    Tab *t = CurTab();
    if (!t || t->kind == VIEW_DRIVES) return;
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (sel.empty()) return;
    if (t->kind == VIEW_RECYCLE) {
        Confirm(Format(T(S_ConfirmDelete), (int)sel.size()), [this, sel] {
            RecycleBinDelete(hwnd, sel);
            if (Tab *c = CurTab()) Reload(c);
        });
        return;
    }
    if (t->selected.empty() && t->cursor < (int)t->rows.size()) t->selected.insert(sel[0]);
    std::wstring next = NextUnselectedName(this, t);
    int id = t->id;
    auto done = [this, id, next](bool) {
        if (Tab *tt = FindTab(id)) { tt->pendingSelect = next; Reload(tt, false); }
    };
    if (permanent) {
        Confirm(Format(T(S_ConfirmDelete), (int)sel.size()), [this, sel, done] { FileOperation(hwnd, FOP_DELETE, sel, L"", done); });
    } else {
        FileOperation(hwnd, FOP_RECYCLE, sel, L"", done);
    }
}

void App::BeginRename() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_FOLDER) return;
    if (t->rowsDirty) RebuildRows(t);
    if (t->cursor < 0 || t->cursor >= (int)t->rows.size() || t->rows[t->cursor].header) return;
    const Row &r = t->rows[t->cursor];
    renamePath = RowPath(r);
    renameEdit.Set(r.e->name, false);
    size_t dot = r.e->name.find_last_of(L'.');
    renameEdit.anchor = 0;
    renameEdit.caret = (!r.e->isDir && dot != std::wstring::npos && dot > 0) ? (u32)dot : (u32)r.e->name.size();
    if (filterEditing) EndFilter(false);
    focus = FOCUS_RENAME;
    EnsureVisible(t, t->cursor);
    Invalidate();
}

void App::CancelRename() {
    focus = FOCUS_LIST;
    renamePath.clear();
    Invalidate();
}

void App::CommitRename() {
    Tab *t = CurTab();
    std::wstring name = renameEdit.text;
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    while (!name.empty() && name.front() == L' ') name.erase(0, 1);
    std::wstring path = renamePath;
    focus = FOCUS_LIST;
    renamePath.clear();
    Invalidate();
    if (name.empty() || name == PathFileName(path) || !t) return;
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        Toast(Format(T(S_ErrorOp), L"\\ / : * ? \" < > |"));
        return;
    }
    int id = t->id;
    t->pendingSelect = name;
    FileOperation(hwnd, FOP_RENAME, {path}, name, [this, id, name](bool ok) {
        if (Tab *tt = FindTab(id)) { tt->pendingSelect = name; Reload(tt); }
    });
}

void App::CreateFolder() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_FOLDER) return;
    std::wstring created;
    if (CreateNewFolder(t->path, T(S_NewFolder), &created)) {
        t->pendingSelect = PathFileName(created);
        t->pendingRename = true;
        Reload(t);
    } else {
        Toast(Format(T(S_ErrorOp), T(S_AccessDenied)));
    }
}

void App::CreateFileHere() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_FOLDER) return;
    std::wstring created;
    if (CreateNewFile(t->path, T(S_NewFile), &created)) {
        t->pendingSelect = PathFileName(created);
        t->pendingRename = true;
        Reload(t);
    } else {
        Toast(Format(T(S_ErrorOp), T(S_AccessDenied)));
    }
}

void App::Duplicate() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_FOLDER) return;
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (!sel.empty()) FileOperation(hwnd, FOP_DUPLICATE, sel, L"", nullptr);
}

// ------------------------------------------------------------------ bookmarks
bool App::IsBookmarked(const std::wstring &path) const {
    for (auto &b : user.bookmarks) if (_wcsicmp(b.c_str(), path.c_str()) == 0) return true;
    return false;
}

void App::ToggleBookmark(const std::wstring &path) {
    if (path.empty() || path == PATH_RECYCLE) return;
    for (size_t i = 0; i < user.bookmarks.size(); i++) {
        if (_wcsicmp(user.bookmarks[i].c_str(), path.c_str()) == 0) {
            user.bookmarks.erase(user.bookmarks.begin() + i);
            SaveUserData();
            Toast(T(S_BookmarkRemoved));
            return;
        }
    }
    user.bookmarks.push_back(path);
    SaveUserData();
    Toast(T(S_BookmarkAdded));
}

// ------------------------------------------------------------------ recycle bin
void App::RestoreRecycled() {
    Tab *t = CurTab();
    if (!t || t->kind != VIEW_RECYCLE) return;
    std::vector<std::wstring> sel = SelectedPaths(t);
    if (sel.empty()) return;
    RecycleBinRestore(hwnd, sel);
    Reload(t, false);
}

void App::EmptyRecycleBin() {
    Confirm(T(S_ConfirmEmptyBin), [this] {
        RecycleBinEmpty(hwnd);
        ForEachPanel([this](Panel *p) { for (auto &t : p->tabs) if (t->kind == VIEW_RECYCLE) Reload(t.get(), false); });
    });
}

// ------------------------------------------------------------------ context menus
void App::ShowShellMenu(Tab *t, bool background, POINT screen) {
    if (!t) return;
    std::vector<std::wstring> items;
    if (!background) items = SelectedPaths(t);
    if (t->kind == VIEW_DRIVES && background) return;
    std::wstring folder = t->kind == VIEW_FOLDER ? t->path : L"";
    std::wstring verb = ShowShellContextMenu(hwnd, folder, items, screen);
    if (_wcsicmp(verb.c_str(), L"rename") == 0) BeginRename();
    Invalidate();
}

void App::ShowContextMenu(Tab *t, f32 x, f32 y, bool background) {
    if (!t) return;
    std::vector<MenuItem> m;
    auto add = [&](const wchar_t *glyph, const std::wstring &label, const std::wstring &cmd, std::function<void()> fn = nullptr) {
        MenuItem it;
        it.glyph = glyph;
        it.label = label;
        it.hotkey = cmd.empty() ? L"" : HotkeyText(cmd);
        std::wstring c = cmd;
        it.action = fn ? fn : [this, c] { RunCommand(c); };
        m.push_back(it);
    };
    auto sep = [&] { MenuItem it; it.separator = true; m.push_back(it); };
    std::vector<std::wstring> sel = background ? std::vector<std::wstring>() : SelectedPaths(t);
    bool anyDir = false;
    for (auto &p : sel) if (IsDirectory(p)) anyDir = true;

    if (t->kind == VIEW_RECYCLE) {
        if (!sel.empty()) {
            add(L"\xE7A7", TL(L"Restore", L"복원"), L"RestoreTrashItems");
            add(GLYPH_CLOSE, TL(L"Delete Permanently", L"영구 삭제"), L"DeletePermanently");
            sep();
        }
        add(GLYPH_DELETE, TL(L"Empty Recycle Bin", L"휴지통 비우기"), L"EmptyTrashItems");
        add(GLYPH_REFRESH, TL(L"Refresh", L"새로 고침"), L"Refresh");
    } else if (!sel.empty()) {
        add(L"\xE8E5", TL(L"Open", L"열기"), L"Open");
        if (anyDir) {
            add(L"\xE8A7", TL(L"Open in New Tab", L"새 탭에서 열기"), L"OpenInTab");
            add(GLYPH_SPLIT_R, TL(L"Open in Right Split", L"오른쪽 분할 창에서 열기"), L"OpenInRightSplit");
        } else if (sel.size() == 1) {
            std::wstring p = sel[0];
            add(L"\xE7AC", TL(L"Open With…", L"연결 프로그램…"), L"", [this, p] { ShellOpenWith(hwnd, p); });
        }
        sep();
        if (t->kind == VIEW_FOLDER) {
            add(L"\xE8C6", TL(L"Cut", L"잘라내기"), L"Cut");
            add(L"\xE8C8", TL(L"Copy", L"복사"), L"Copy");
            if (ClipboardHasFiles() && anyDir && sel.size() == 1) {
                std::wstring dest = sel[0];
                add(L"\xE77F", TL(L"Paste Into Folder", L"폴더에 붙여넣기"), L"", [this, dest] {
                    std::vector<std::wstring> files; bool cut = false;
                    if (ClipboardGetFiles(hwnd, files, &cut)) FileOperation(hwnd, cut ? FOP_MOVE : FOP_COPY, files, dest, nullptr);
                });
            }
            add(L"\xE7C4", TL(L"Duplicate", L"복제"), L"Duplicate");
        }
        add(L"\xE71B", TL(L"Copy Path", L"경로 복사"), L"CopyPath");
        if (t->kind == VIEW_FOLDER) {
            sep();
            add(L"\xE8AC", TL(L"Rename", L"이름 바꾸기"), L"Rename");
            add(GLYPH_DELETE, TL(L"Move to Recycle Bin", L"휴지통으로 이동"), L"Trash");
            add(GLYPH_CLOSE, TL(L"Delete Permanently", L"영구 삭제"), L"DeletePermanently");
        }
        sep();
        if (anyDir && sel.size() == 1) {
            std::wstring p = sel[0];
            add(IsBookmarked(p) ? GLYPH_STAR_FILL : GLYPH_STAR, IsBookmarked(p) ? TL(L"Remove Bookmark", L"즐겨찾기 제거") : TL(L"Add Bookmark", L"즐겨찾기 추가"), L"",
                [this, p] { ToggleBookmark(p); });
        }
        add(L"\xE838", TL(L"Show in Explorer", L"탐색기에서 보기"), L"", [this, sel] { OpenExplorerAt(PathParent(sel[0]), sel[0]); });
        add(L"\xE946", TL(L"Properties", L"속성"), L"", [this, sel] { ShellProperties(hwnd, sel); });
    } else {
        if (t->kind == VIEW_FOLDER) {
            add(L"\xE8F4", TL(L"New Folder", L"새 폴더"), L"CreateFolder");
            add(L"\xE7C3", TL(L"New File", L"새 파일"), L"CreateFile");
            if (ClipboardHasFiles()) add(L"\xE77F", TL(L"Paste", L"붙여넣기"), L"Paste");
            sep();
        }
        add(GLYPH_REFRESH, TL(L"Refresh", L"새로 고침"), L"Refresh");
        add(L"\xE8CB", TL(L"Sort by Name", L"이름순 정렬"), L"SortByName");
        add(nullptr, TL(L"Sort by Date Modified", L"수정한 날짜순 정렬"), L"SortByDateModified");
        add(nullptr, TL(L"Sort by Size", L"크기순 정렬"), L"SortBySize");
        add(nullptr, TL(L"Sort by Type", L"유형순 정렬"), L"SortByType");
        if (t->kind == VIEW_FOLDER) {
            sep();
            std::wstring p = t->path;
            add(IsBookmarked(p) ? GLYPH_STAR_FILL : GLYPH_STAR, IsBookmarked(p) ? TL(L"Remove Bookmark", L"즐겨찾기 제거") : TL(L"Add Bookmark", L"즐겨찾기 추가"), L"BookmarksToggle");
            add(L"\xE756", TL(L"Open Terminal Here", L"여기서 터미널 열기"), L"OpenConsole");
            add(L"\xE838", TL(L"Open in File Explorer", L"파일 탐색기에서 열기"), L"OpenFileExplorer");
            add(L"\xE946", TL(L"Properties", L"속성"), L"", [this, p] { ShellProperties(hwnd, {p}); });
        }
    }
    if (t->kind != VIEW_RECYCLE) {
        sep();
        bool bg = background;
        add(GLYPH_MORE, T(S_MoreOptions), L"", [this, t, bg] {
            POINT pt;
            GetCursorPos(&pt);
            ShowShellMenu(t, bg, pt);
        });
    }
    OpenMenu(std::move(m), x, y);
}

void App::ShowFolderDropdown(Tab *t, const std::wstring &dir, f32 x, f32 y) {
    std::vector<FileEntry> list;
    if (dir.empty()) ListDrives(list, false);
    else ListDirectory(dir, list);
    std::vector<const FileEntry *> v;
    for (auto &e : list) if (e.isDir && Visible(e)) v.push_back(&e);
    std::sort(v.begin(), v.end(), [](const FileEntry *a, const FileEntry *b) { return NaturalCompare(a->name, b->name) < 0; });
    std::vector<MenuItem> m;
    int id = t->id;
    for (size_t i = 0; i < v.size() && i < 400; i++) {
        MenuItem it;
        std::wstring p = EntryPath(*v[i], dir);
        it.label = DisplayName(*v[i]);
        it.icon = icons.Get(p, *v[i]);
        it.action = [this, id, p] { if (Tab *tt = FindTab(id)) Navigate(tt, p); };
        m.push_back(it);
    }
    if (m.empty()) {
        MenuItem it;
        it.label = T(S_Empty);
        it.enabled = false;
        m.push_back(it);
    }
    OpenMenu(std::move(m), x, y);
    popup.kind = POPUP_FOLDER_DROPDOWN;
}

void App::InsertDate(int which) {
    TextEdit *e = FocusedEdit();
    if (!e) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (which != 0) {
        Tab *t = CurTab();
        if (t && t->cursor < (int)t->rows.size() && !t->rows[t->cursor].header) {
            const FileEntry *fe = t->rows[t->cursor].e;
            u64 ft = which == 1 ? fe->created : fe->modified;
            FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32);
            SYSTEMTIME u;
            FileTimeToSystemTime(&f, &u);
            SystemTimeToTzSpecificLocalTime(nullptr, &u, &st);
        }
    }
    e->Insert(Format(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay));
    if (focus == FOCUS_FILTER) ApplyFilter();
    Invalidate();
}
