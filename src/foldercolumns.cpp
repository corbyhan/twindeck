// Folder columns strip (like OneCommander): This PC, then one column per folder from the drive down to the
// current folder. Parent columns list subfolders with the path's next folder highlighted; the last column is the
// current folder with its files. A click on a folder navigates the panel there.
#include "app.h"
#include "jobs.h"

static f32 ColumnWidth(f32 S) { return std::floor(200 * S); }
static f32 TitleHeight(const App *a) { return std::floor(a->r.Metrics(FONT_SMALL).lineH + 10 * a->S); }
static f32 ItemHeight(const App *a) { return std::floor(a->r.Metrics(FONT_UI).lineH + 6 * a->S); }
static const int kVisibleItems = 7;

f32 App::FolderColumnsHeight() const { return TitleHeight(this) + ItemHeight(this) * kVisibleItems + 4 * S; }

// folders from the drive (or share) root down to path
static std::vector<std::wstring> FolderChain(const std::wstring &path) {
    std::vector<std::wstring> v;
    for (std::wstring p = path; !p.empty();) {
        v.push_back(p);
        std::wstring parent = PathParent(p);
        if (parent == p) break;
        p = parent;
    }
    std::reverse(v.begin(), v.end());
    return v;
}

static std::wstring FolderTitle(const std::wstring &dir) {
    if (dir.empty()) return T(S_ThisPC);
    if (PathIsRootDrive(dir)) return dir.substr(0, 2);
    std::wstring n = PathFileName(dir);
    return n.empty() ? dir : n;
}

void App::LoadFolderList(const std::wstring &dir) {
    FolderColumnList &fl = folderLists[dir];
    if (fl.loading) return;
    fl.loading = true;
    JobSubmit([dir] {
        std::vector<FileEntry> all;
        ListError err = ListDirectory(dir, all);
        auto dirs = std::make_shared<std::vector<FileEntry>>();
        for (auto &e : all) if (e.isDir) dirs->push_back(std::move(e));
        std::sort(dirs->begin(), dirs->end(), [](const FileEntry &a, const FileEntry &b) { return NaturalCompare(a.name, b.name) < 0; });
        MainPost([dir, dirs, err] {
            if (!g_app) return;
            FolderColumnList &fl = g_app->folderLists[dir];
            fl.dirs = std::move(*dirs);
            fl.err = err;
            fl.loading = false;
            fl.loaded = true;
            g_app->Invalidate();
        });
    });
}

namespace {
// one row of a column, whatever the source (drive list, subfolder list or the tab's own entries)
struct ColumnItem {
    const FileEntry *e;
    std::wstring path;
};
struct Column {
    std::wstring dir;             // L"" = This PC
    std::vector<ColumnItem> items;
    std::wstring highlight;       // path of the next folder down (parent columns)
    bool current = false;         // the panel's own folder (shows files, highlights the selection)
    bool loading = false;
    ListError err = LIST_OK;
};
}

void App::DrawFolderColumns(Panel *p, Tab *t, RectF rc) {
    r.FillSurface(rc, theme.headerBg);
    r.Line(rc.x, rc.Bottom() - 0.5f, rc.Right(), rc.Bottom() - 0.5f, theme.border);

    // ---- columns for the current path
    std::vector<std::wstring> chain = t->kind == VIEW_FOLDER ? FolderChain(t->path) : std::vector<std::wstring>();
    bool relayout = p->fcPath != t->path;
    if (relayout) {
        // refresh parent folders on every navigation (cheap, in the background; the old list shows until then)
        for (size_t i = 0; i + 1 < chain.size(); i++) LoadFolderList(chain[i]);
        if (folderLists.size() > 256) {
            std::unordered_set<std::wstring> keep(chain.begin(), chain.end());
            for (auto it = folderLists.begin(); it != folderLists.end();)
                it = it->second.loading || keep.count(it->first) ? std::next(it) : folderLists.erase(it);
        }
    }
    if (p->fcTabId != t->id || p->fcRowsGen != t->rowsGen) {
        p->fcTabId = t->id;
        p->fcRowsGen = t->rowsGen;
        p->fcCurrent.clear();
        for (int i = 0; i < (int)t->entries.size(); i++) if (Visible(t->entries[i])) p->fcCurrent.push_back(i);
        std::sort(p->fcCurrent.begin(), p->fcCurrent.end(), [t](int a, int b) {
            const FileEntry &x = t->entries[a], &y = t->entries[b];
            if (x.isDir != y.isDir) return x.isDir;
            return NaturalCompare(x.name, y.name) < 0;
        });
    }

    std::vector<Column> cols;
    {
        Column pc; // This PC
        pc.dir = PATH_THIS_PC;
        if (t->kind == VIEW_DRIVES) {
            pc.current = true;
            for (int i : p->fcCurrent) pc.items.push_back({&t->entries[i], EntryPath(t->entries[i], L"")});
        } else {
            for (auto &d : sidebarDrives) pc.items.push_back({&d, d.name + L"\\"});
            if (!chain.empty()) pc.highlight = chain[0];
        }
        cols.push_back(std::move(pc));
    }
    for (size_t i = 0; i < chain.size(); i++) {
        Column c;
        c.dir = chain[i];
        if (i + 1 == chain.size()) {
            c.current = true;
            c.loading = t->loading && t->entries.empty();
            c.err = t->error;
            for (int k : p->fcCurrent) c.items.push_back({&t->entries[k], PathJoin(c.dir, t->entries[k].name)});
        } else {
            auto it = folderLists.find(c.dir);
            if (it != folderLists.end()) {
                c.loading = !it->second.loaded;
                c.err = it->second.err;
                for (auto &e : it->second.dirs) if (Visible(e)) c.items.push_back({&e, PathJoin(c.dir, e.name)});
            } else {
                c.loading = true;
            }
            c.highlight = chain[i + 1];
        }
        cols.push_back(std::move(c));
    }

    // ---- geometry: columns side by side, scrolled so the deepest ones show
    f32 colW = std::min(ColumnWidth(S), std::max(120 * S, rc.w / 2));
    f32 titleH = TitleHeight(this), itemH = ItemHeight(this);
    f32 totalW = colW * cols.size();
    f32 maxScrollX = std::max(0.f, totalW - rc.w);
    bool hovStrip = Hover(rc);
    if (relayout) {
        p->fcPath = t->path;
        p->fcScrollX = maxScrollX;
        p->fcReveal.clear();
        for (auto &c : cols) if (!c.highlight.empty()) p->fcReveal.insert(c.dir);
    }
    if (hovStrip && (in.hwheel != 0 || (in.wheel != 0 && in.shift))) {
        p->fcScrollX -= (in.hwheel != 0 ? -in.hwheel : in.wheel) * colW / 2;
        in.hwheel = in.wheel = 0;
    }
    p->fcScrollX = Clamp(p->fcScrollX, 0.f, maxScrollX);

    bool panelActive = p == active;
    Color selColor = panelActive ? theme.selection : theme.selectionInactive;
    f32 iconSz = std::floor(std::min(itemH - 6 * S, r.Metrics(FONT_UI).lineH * 1.05f));
    r.PushClip(rc);
    for (size_t ci = 0; ci < cols.size(); ci++) {
        Column &c = cols[ci];
        RectF cr(rc.x + ci * colW - p->fcScrollX, rc.y, colW, rc.h - 1 * S);
        if (cr.Right() < rc.x || cr.x > rc.Right()) continue;
        if (ci > 0) r.Line(cr.x + 0.5f, cr.y + 4 * S, cr.x + 0.5f, cr.Bottom() - 4 * S, theme.border);
        RectF title = cr.CutTop(titleH);
        TextOpts to;
        r.Text(c.current ? FONT_UI_BOLD : FONT_SMALL, FolderTitle(c.dir), RectF(title.x + 10 * S, title.y, title.w - 16 * S, title.h),
               c.current ? theme.text : theme.textDim, to);

        // vertical scroll; after a move the highlighted folder is brought into view (once its list is there)
        f32 &scroll = p->fcScroll[c.dir];
        f32 contentH = itemH * c.items.size();
        f32 maxScroll = std::max(0.f, contentH - cr.h);
        if (!c.loading && p->fcReveal.erase(c.dir)) {
            for (size_t k = 0; k < c.items.size(); k++) {
                if (_wcsicmp(c.items[k].path.c_str(), c.highlight.c_str()) != 0) continue;
                f32 y = k * itemH;
                if (y < scroll || y + itemH > scroll + cr.h) scroll = y - (cr.h - itemH) / 2;
                break;
            }
        }
        bool hovCol = Hover(cr);
        if (hovCol && in.wheel != 0) { scroll -= in.wheel * itemH * 3; in.wheel = 0; }
        scroll = Clamp(scroll, 0.f, maxScroll);

        if (c.items.empty()) {
            const wchar_t *msg = c.loading ? T(S_Loading) : c.err == LIST_ACCESS_DENIED ? T(S_AccessDenied) : nullptr;
            if (msg) r.Text(FONT_SMALL, msg, RectF(cr.x + 10 * S, cr.y, cr.w - 16 * S, itemH), theme.textFaint, to);
            continue;
        }
        r.PushClip(cr);
        int first = std::max(0, (int)(scroll / itemH));
        int last = std::min((int)c.items.size() - 1, (int)((scroll + cr.h) / itemH));
        for (int k = first; k <= last; k++) {
            const ColumnItem &it = c.items[k];
            RectF ir(cr.x + 4 * S, cr.y + k * itemH - scroll, cr.w - 8 * S, itemH);
            bool hl = c.current ? t->selected.count(it.path) > 0 : _wcsicmp(it.path.c_str(), c.highlight.c_str()) == 0;
            bool hov = hovCol && Hover(ir);
            if (hl) r.FillRect(ir, selColor, 4 * S);
            else if (hov) r.FillRect(ir, theme.hover, 4 * S);
            f32 x = ir.x + 6 * S;
            ID2D1Bitmap *ic = it.e->isDrive ? icons.GetDrive(it.path) : icons.Get(it.path, *it.e);
            if (ic) r.DrawBitmap(ic, RectF(x, ir.y + (ir.h - iconSz) / 2, iconSz, iconSz));
            x += iconSz + 6 * S;
            std::wstring name = DisplayName(*it.e);
            RectF nr(x, ir.y, ir.Right() - x - 4 * S, ir.h);
            r.Text(FONT_UI, name, nr, it.e->isDir || it.e->isDrive ? theme.text : theme.textDim);
            if (hov && r.Measure(FONT_UI, name) > nr.w) SetTooltip(HashStr(it.path, 31), name);
            if (hov) cursorKind = 4;

            if (hov && in.pressed[0]) {
                in.pressed[0] = false;
                if (focus == FOCUS_RENAME) CommitRename();
                if (pathEditing) EndPathEdit(false);
                if (it.e->isDir || it.e->isDrive) {
                    // the first click already moved there and the columns shifted: the second is not a new target
                    if (in.clickCount >= 2) { r.PopClip(); r.PopClip(); return; }
                    // going up to a folder already on the path selects the child we came from
                    std::wstring child;
                    for (size_t q = 0; q + 1 < chain.size(); q++)
                        if (_wcsicmp(chain[q].c_str(), it.path.c_str()) == 0) child = chain[q + 1];
                    if (_wcsicmp(it.path.c_str(), t->path.c_str()) != 0) Navigate(t, it.path, true, child);
                } else {
                    int row = RowAt(t, it.path);
                    if (row >= 0) {
                        SelectOnly(t, row);
                        EnsureVisible(t, row);
                        if (in.clickCount >= 2) OpenRow(t, row, 0);
                    }
                }
                Invalidate();
                r.PopClip();
                r.PopClip();
                return; // the columns change under us; the next frame redraws them
            }
        }
        r.PopClip();
        if (maxScroll > 0) {
            f32 th = std::max(16 * S, cr.h * cr.h / contentH);
            f32 ty = cr.y + (cr.h - th) * (scroll / maxScroll);
            r.FillRect(RectF(cr.Right() - 4 * S, ty, 3 * S, th), theme.scrollbar, 1.5f * S);
        }
    }
    r.PopClip();
    if (maxScrollX > 0) {
        f32 tw = std::max(30 * S, rc.w * rc.w / totalW);
        f32 tx = rc.x + (rc.w - tw) * (p->fcScrollX / maxScrollX);
        r.FillRect(RectF(tx, rc.Bottom() - 4 * S, tw, 3 * S), theme.scrollbar, 1.5f * S);
    }
    if (hovStrip && (in.pressed[0] || in.pressed[2])) in.pressed[0] = in.pressed[2] = false; // empty space: nothing behind it
}
