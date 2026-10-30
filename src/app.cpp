#include "app.h"
#include "everything.h"
#include "jobs.h"
#include <dwmapi.h>
#include <shlobj.h>

App *g_app = nullptr;
extern LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// ================================================================== theme
bool SystemUsesDarkMode() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v == 0;
    return false;
}

Theme MakeTheme(bool dark) {
    Theme t;
    t.dark = dark;
    if (dark) {
        t.bg = Color::Hex(0x1b1d22);
        t.sidebarBg = Color::Hex(0x17191d);
        t.panelBg = Color::Hex(0x202329);
        t.headerBg = Color::Hex(0x1b1d22);
        t.tabActive = Color::Hex(0x2a2e36);
        t.tabInactive = Color::Hex(0x1b1d22);
        t.text = Color::Hex(0xe6e8ec);
        t.textDim = Color::Hex(0xa3a9b4);
        t.textFaint = Color::Hex(0x6b7280);
        t.accent = Color::Hex(0x4c8dff);
        t.accentText = Color::Hex(0xffffff);
        t.selection = Color::Hex(0x2f5fb3, 0.55f);
        t.selectionInactive = Color::Hex(0x4b5563, 0.45f);
        t.hover = Color::Hex(0xffffff, 0.05f);
        t.border = Color::Hex(0x30343c);
        t.rowAlt = Color::Hex(0xffffff, 0.022f);
        t.danger = Color::Hex(0xef5350);
        t.ok = Color::Hex(0x4caf50);
        t.popupBg = Color::Hex(0x262a31);
        t.popupBorder = Color::Hex(0x3a3f48);
        t.inputBg = Color::Hex(0x15171b);
        t.scrollbar = Color::Hex(0xffffff, 0.18f);
        t.highlight = Color::Hex(0xffc107, 0.35f);
        t.folderIcon = Color::Hex(0xf2c46d);
        t.cut = Color::Hex(0xffffff, 0.45f);
    } else {
        t.bg = Color::Hex(0xeef0f3);
        t.sidebarBg = Color::Hex(0xe6e9ed);
        t.panelBg = Color::Hex(0xfbfbfc);
        t.headerBg = Color::Hex(0xeef0f3);
        t.tabActive = Color::Hex(0xfbfbfc);
        t.tabInactive = Color::Hex(0xe2e5ea);
        t.text = Color::Hex(0x1f2328);
        t.textDim = Color::Hex(0x57606a);
        t.textFaint = Color::Hex(0x8c959f);
        t.accent = Color::Hex(0x2f6fe0);
        t.accentText = Color::Hex(0xffffff);
        t.selection = Color::Hex(0x2f6fe0, 0.22f);
        t.selectionInactive = Color::Hex(0x8c959f, 0.22f);
        t.hover = Color::Hex(0x000000, 0.045f);
        t.border = Color::Hex(0xd0d7de);
        t.rowAlt = Color::Hex(0x000000, 0.025f);
        t.danger = Color::Hex(0xd32f2f);
        t.ok = Color::Hex(0x2e7d32);
        t.popupBg = Color::Hex(0xffffff);
        t.popupBorder = Color::Hex(0xc8cfd6);
        t.inputBg = Color::Hex(0xffffff);
        t.scrollbar = Color::Hex(0x000000, 0.22f);
        t.highlight = Color::Hex(0xffb300, 0.35f);
        t.folderIcon = Color::Hex(0xe0a526);
        t.cut = Color::Hex(0x000000, 0.4f);
    }
    return t;
}

// ================================================================== lifecycle
void App::LoadAll() {
    Json j;
    if (LoadJsonLocked(paths.config, L"TwinDeck_Config_Mutex", j)) settings.FromJson(j);
    Json u;
    if (LoadJsonLocked(paths.userData, L"TwinDeck_UserData_Mutex", u)) user.FromJson(u);
    Json s;
    if (LoadJsonLocked(paths.session, L"TwinDeck_Session_Mutex", s)) session.FromJson(s);
}

void App::SaveConfig() { SaveJsonLocked(paths.config, L"TwinDeck_Config_Mutex", settings.ToJson()); }
void App::SaveUserData() { SaveJsonLocked(paths.userData, L"TwinDeck_UserData_Mutex", user.ToJson()); }

static std::unique_ptr<SessionNode> SerializeNode(LayoutNode *n) {
    auto s = std::make_unique<SessionNode>();
    s->leaf = n->leaf;
    if (n->leaf) {
        for (auto &t : n->panel->tabs) {
            SessionTab st;
            st.path = t->path;
            st.sort = t->sort;
            st.desc = t->desc;
            st.group = t->group;
            st.view = t->viewMode;
            st.iconSize = t->iconSize;
            s->tabs.push_back(st);
        }
        s->activeTab = n->panel->active;
        s->folderColumns = n->panel->folderColumns;
    } else {
        s->vertical = n->vertical;
        s->ratio = n->ratio;
        s->a = SerializeNode(n->a.get());
        s->b = SerializeNode(n->b.get());
    }
    return s;
}

void App::SaveSession() {
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (hwnd && !setup.resizedWindow && GetWindowPlacement(hwnd, &wp)) { // keep the real size while the installer window is shown
        session.x = wp.rcNormalPosition.left;
        session.y = wp.rcNormalPosition.top;
        session.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        session.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        session.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    }
    session.sidebarWidth = sidebarW / S;
    session.sidebarClosed = !sidebarOpen;
    session.inspectorOpen = inspectorOpen;
    session.inspectorSize = inspectorSize / S;
    if (root) session.layout = SerializeNode(root.get());
    int idx = 0, sel = 0;
    ForEachPanel([&](Panel *p) { if (p == active) sel = idx; idx++; });
    session.selectedPanel = sel;
    SaveJsonLocked(paths.session, L"TwinDeck_Session_Mutex", session.ToJson());
    sessionDirty = false;
    lastSessionSave = GetTickCount64();
}

static std::unique_ptr<LayoutNode> BuildNode(App *app, const SessionNode *s, LayoutNode *parent) {
    auto n = std::make_unique<LayoutNode>();
    n->parent = parent;
    n->leaf = s->leaf;
    if (s->leaf) {
        n->panel = std::make_unique<Panel>();
        for (auto &st : s->tabs) {
            Tab *t = app->NewTab(n->panel.get(), L"\x01", true); // placeholder, navigated below
            t->sort = (SortKey)Clamp(st.sort, 0, (int)SORT_COUNT - 1);
            t->desc = st.desc;
            t->group = (GroupKey)Clamp(st.group, 0, (int)GROUP_COUNT - 1);
            t->viewMode = Clamp(st.view, 0, (int)LV_COUNT - 1);
            t->iconSize = Clamp(st.iconSize, 32.f, 256.f);
            std::wstring p = st.path;
            if (p != PATH_THIS_PC && p != PATH_RECYCLE && !IsDirectory(p)) p = app->HomePath();
            t->path = L"";
            app->Navigate(t, p, false);
        }
        n->panel->active = Clamp(s->activeTab, 0, (int)n->panel->tabs.size() - 1);
        n->panel->folderColumns = s->folderColumns;
    } else {
        n->vertical = s->vertical;
        n->ratio = s->ratio;
        n->a = BuildNode(app, s->a.get(), n.get());
        n->b = BuildNode(app, s->b.get(), n.get());
    }
    return n;
}

bool App::Init(HINSTANCE inst, const std::wstring &cmdPath, bool setupRequested) {
    hinst = inst;
    paths = ResolveDataPaths();
    LogInit(paths.logs);
    InstallCrashHandler(paths.logs);
    Log(L"TwinDeck %s starting, data dir: %s", APP_VERSION, paths.dir.c_str());
    LoadAll();
    I18nSetLanguage(settings.language == LANGSET_AUTO ? I18nDetectSystemLanguage()
                                                      : (settings.language == LANGSET_KO ? LANG_KO : LANG_EN));
    FsInit();

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"TwinDeck";
    RegisterClassExW(&wc);

    int w = session.w > 200 ? session.w : 1280;
    int h = session.h > 200 ? session.h : 800;
    int x = session.x, y = session.y;
    if (x != (int)CW_USEDEFAULT) {
        RECT rc = {x, y, x + w, y + h};
        if (!MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) x = y = CW_USEDEFAULT;
    }
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, APP_NAME, WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd) return false;
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER); // apply custom frame

    dpi = (f32)GetDpiForWindow(hwnd);
    S = dpi / 96.f;
    if (session.w <= 200) {
        // first start: scale default size by DPI and center
        int sw = (int)(1280 * S), sh = (int)(800 * S);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        sw = std::min(sw, (int)(mi.rcWork.right - mi.rcWork.left));
        sh = std::min(sh, (int)(mi.rcWork.bottom - mi.rcWork.top));
        SetWindowPos(hwnd, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - sw) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - sh) / 2, sw, sh, SWP_NOZORDER);
    }

    if (!r.Init(hwnd)) {
        MessageBoxW(hwnd, L"Direct3D 11 / Direct2D 초기화에 실패했습니다.", APP_NAME, MB_ICONERROR);
        return false;
    }
    JobsInit(hwnd);
    icons.Init(&r);
    thumbs.Init(&r);
    sidebarOpen = !session.sidebarClosed;
    inspectorOpen = session.inspectorOpen;
    ApplySettings();
    sidebarW = Clamp(session.sidebarWidth, 120.f, 600.f) * S;
    inspectorSize = Clamp(session.inspectorSize, 160.f, 2000.f) * S;

    watcher.Start([](const std::wstring &dir) {
        std::wstring d = dir;
        MainPost([d] { if (g_app) g_app->OnDirChanged(d); });
    });

    DropCallbacks cb;
    cb.targetAt = [this](int x, int y) -> std::wstring {
        dropTab = nullptr;
        dropRow = -1;
        std::wstring dest;
        ForEachPanel([&](Panel *p) {
            if (!dest.empty() || !p->listRect.Contains((f32)x, (f32)y)) {
                if (dest.empty() && p->rect.Contains((f32)x, (f32)y)) {
                    Tab *t = p->Cur();
                    if (t && t->kind == VIEW_FOLDER) { dest = t->path; dropTab = t; }
                }
                return;
            }
            Tab *t = p->Cur();
            if (!t || t->kind == VIEW_RECYCLE) return;
            int row = ItemAtPoint(t, x - p->listRect.x, y - p->listRect.y + t->scroll);
            if (row >= 0 && row < (int)t->rows.size() && !t->rows[row].header && t->rows[row].e->isDir) {
                dest = RowPath(t->rows[row]);
                dropRow = row;
            } else if (t->kind == VIEW_FOLDER) {
                dest = t->path;
            }
            dropTab = t;
        });
        // sidebar drives / bookmarks are handled as plain folders
        dropHighlight = dest;
        Invalidate();
        return dest;
    };
    cb.hover = [this](int, int, bool on) {
        if (!on) { dropHighlight.clear(); dropTab = nullptr; dropRow = -1; }
        Invalidate();
    };
    cb.drop = [this](const std::vector<std::wstring> &files, const std::wstring &dest, DWORD effect) {
        FileOpKind k = (effect & DROPEFFECT_MOVE) ? FOP_MOVE : FOP_COPY;
        FileOperation(hwnd, k, files, dest, nullptr);
    };
    DragDropRegister(hwnd, cb);

    RegisterCommands();
    BuildHotkeys();

    // layout
    if (session.layout && settings.loadOnStartup == STARTUP_LAST_SESSION) {
        root = BuildNode(this, session.layout.get(), nullptr);
    }
    bool fresh = false;
    if (!root) {
        fresh = true;
        root = std::make_unique<LayoutNode>();
        root->panel = std::make_unique<Panel>();
        NewTab(root->panel.get(), !cmdPath.empty() ? PATH_THIS_PC
                                  : (settings.loadOnStartup == STARTUP_THIS_PC ? PATH_THIS_PC : HomePath()));
    }
    int idx = 0;
    active = nullptr;
    ForEachPanel([&](Panel *p) {
        if (idx == session.selectedPanel || !active) active = p;
        idx++;
    });
    if (!cmdPath.empty()) {
        std::wstring p = NormalizePath(cmdPath);
        bool exists = PathExists(p);
        if (exists) {
            Tab *t = fresh ? active->Cur() : NewTab(active, PATH_THIS_PC);
            if (IsDirectory(p)) Navigate(t, p, false);
            else Navigate(t, PathParent(p), false, PathFileName(p));
        }
    }

    ready = true;
    lastSessionSave = GetTickCount64();
    CleanupAfterUpdate();
    // installer: TwinDeck-Setup.exe, --setup, or the very first start of a copy that is not installed
    std::wstring exeName = ToLower(PathFileName(SelfPath()));
    bool setupExe = exeName.find(L"setup") != std::wstring::npos;
    bool firstStart = !settings.setupPrompted && !IsInstalled();
    if (setupRequested || setupExe || firstStart) {
        if (firstStart) { settings.setupPrompted = true; SaveConfig(); }
        OpenSetup(true);
        int sw = (int)(880 * S), sh = (int)(600 * S);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        sw = std::min(sw, (int)(mi.rcWork.right - mi.rcWork.left));
        sh = std::min(sh, (int)(mi.rcWork.bottom - mi.rcWork.top));
        SetWindowPos(hwnd, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - sw) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - sh) / 2, sw, sh, SWP_NOZORDER);
        setup.resizedWindow = true;
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
        return true;
    }
    ShowWindow(hwnd, session.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    ApplyScreenshotScene();
    UpdateWindow(hwnd);
    return true;
}

// README screenshots (tools/shots.py): TWINDECK_SHOT="select=name;filter=ㅎㄱ;view;palette=text;setup"
// puts the window in a state that would otherwise need clicks or typing.
void App::ApplyScreenshotScene() {
    wchar_t buf[512];
    DWORD n = GetEnvironmentVariableW(L"TWINDECK_SHOT", buf, (DWORD)std::size(buf));
    if (!n || n >= std::size(buf)) return;
    std::wstring spec(buf, n);
    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(L';', start);
        std::wstring item = spec.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        start = end == std::wstring::npos ? spec.size() : end + 1;
        size_t eq = item.find(L'=');
        std::wstring key = item.substr(0, eq), val = eq == std::wstring::npos ? L"" : item.substr(eq + 1);
        Tab *t = CurTab();
        if (key == L"select") shotSelect = val; // applied once the folder has loaded (OnLoaded)
        else if (key == L"filter") BeginFilter(val);
        else if (key == L"palette") OpenPalette(val);
        else if (key == L"options") { OpenOptions(); popup.optionsTab = Clamp(_wtoi(val.c_str()), 0, kOptionsTabs - 1); }
        else if (key == L"view" && t) OpenViewPopup(t, RectF((f32)r.Width() - 60 * S, (f32)r.Height() - 30 * S, 50 * S, 24 * S));
        else if (key == L"setup") {
            OpenSetup(false);
            setup.info = InstallInfo();  // show a fresh install, without this machine's paths
            setup.opts = setup.info.opts;
            setup.pathEdit.Set(L"C:\\Users\\me\\AppData\\Local\\Programs\\TwinDeck", false);
        }
    }
}

void App::ApplySettings() {
    bool dark = settings.colorScheme == SCHEME_DARK || (settings.colorScheme == SCHEME_SYSTEM && SystemUsesDarkMode());
    theme = MakeTheme(dark);
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &d, sizeof(d));
    ApplyBackdrop();
    int corner = settings.roundCorners ? 2 : 1; // DWMWCP_ROUND / DWMWCP_DONOTROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    I18nSetLanguage(settings.language == LANGSET_AUTO ? I18nDetectSystemLanguage()
                                                      : (settings.language == LANGSET_KO ? LANG_KO : LANG_EN));
    r.SetScale(S);
    r.SetFonts(settings.fontName, settings.fontSize, settings.inspectorFontName, settings.inspectorFontSize);
    icons.SetLarge(r.Metrics(FONT_UI).lineH > 26);
    thumbs.SetBudget((size_t)Clamp(settings.assetCacheMB, 32, 4096) << 20);
    ForEachPanel([](Panel *p) { for (auto &t : p->tabs) t->rowsDirty = true; });
    Invalidate();
}

void App::ApplyBackdrop() {
    int bd = r.SupportsTransparency() ? Clamp(settings.backdrop, 0, BACKDROP_COUNT - 1) : BACKDROP_NONE;
    bool material = bd >= BACKDROP_MICA;
    // no DwmExtendFrameIntoClientArea: the window has no redirection bitmap, so the system material already
    // shows through the translucent swap chain, and an extended frame would make DWM paint its own caption
    // buttons underneath ours
    static const int kSystemBackdrop[BACKDROP_COUNT] = {1, 1, 2, 3, 4}; // DWMSBT_NONE, MAINWINDOW, TRANSIENTWINDOW, TABBEDWINDOW
    int type = kSystemBackdrop[bd];
    bool systemOk = SUCCEEDED(DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &type, sizeof(type)));

    // Windows 10 / early Windows 11: no system backdrop API, use the (undocumented) accent blur instead
    static bool accentOn = false;
    bool wantAccent = material && !systemOk;
    if (wantAccent || accentOn) {
        struct AccentPolicy { int state; int flags; DWORD gradient; int animation; };
        struct CompositionData { int attribute; void *data; SIZE_T size; };
        typedef BOOL(WINAPI * SetWca)(HWND, CompositionData *);
        static SetWca setWca = (SetWca)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
        if (setWca) {
            AccentPolicy ap = {wantAccent ? 4 /*ACCENT_ENABLE_ACRYLICBLURBEHIND*/ : 0, 0, 0x01000000, 0};
            CompositionData cd = {19 /*WCA_ACCENT_POLICY*/, &ap, sizeof(ap)};
            setWca(hwnd, &cd);
            accentOn = wantAccent;
        }
    }

    f32 alpha = bd == BACKDROP_NONE ? 1.f : Clamp(settings.backgroundOpacity, 0, 100) / 100.f;
    if (alpha < 1) {
        for (Color *c : {&theme.bg, &theme.sidebarBg, &theme.panelBg, &theme.headerBg, &theme.tabActive, &theme.tabInactive})
            c->a = alpha;
        theme.inputBg.a = 0.4f + 0.6f * alpha; // input boxes stay a bit more solid for legibility
    }
    r.SetTranslucent(alpha < 1);
    Log(L"Backdrop %d (system %s), background alpha %.2f, translucent %d", bd, systemOk ? L"ok" : L"unsupported", alpha, r.Translucent());
}

void App::ApplyDpi(f32 newDpi) {
    f32 old = S;
    dpi = newDpi;
    S = dpi / 96.f;
    sidebarW = sidebarW / old * S;
    inspectorSize = inspectorSize / old * S;
    ForEachPanel([&](Panel *p) { for (auto &w : p->colW) w = w / old * S; });
    ApplySettings();
}

void App::Shutdown() {
    ready = false;
    SaveSession();
    SaveUserData();
    DragDropRevoke(hwnd);
    watcher.Stop();
    sizes.CancelAll();
    ForEachPanel([](Panel *p) { for (auto &t : p->tabs) if (t->search) t->search->cancel = true; });
    JobsShutdown();
    ClearInspector();
    thumbs.Clear();
    icons.Shutdown();
    r.Shutdown();
    Log(L"Shutdown complete");
}

// ================================================================== layout
static void ForEachNode(LayoutNode *n, const std::function<void(Panel *)> &fn) {
    if (!n) return;
    if (n->leaf) { if (n->panel) fn(n->panel.get()); return; }
    ForEachNode(n->a.get(), fn);
    ForEachNode(n->b.get(), fn);
}

void App::ForEachPanel(const std::function<void(Panel *)> &fn) { ForEachNode(root.get(), fn); }

LayoutNode *App::FindNode(Panel *p, LayoutNode *n) {
    if (!n) n = root.get();
    if (!n) return nullptr;
    if (n->leaf) return n->panel.get() == p ? n : nullptr;
    if (auto *r1 = FindNode(p, n->a.get())) return r1;
    return FindNode(p, n->b.get());
}

Panel *App::NewPanelWith(const std::wstring &path) {
    auto *p = new Panel();
    NewTab(p, path);
    return p;
}

void App::SplitActive(bool vertical) {
    if (!active) return;
    LayoutNode *n = FindNode(active);
    if (!n) return;
    Tab *t = CurTab();
    std::wstring path = t ? t->path : HomePath();
    // build the new panel first: Navigate() may drain queued callbacks that walk the layout tree
    std::unique_ptr<Panel> fresh(NewPanelWith(path));
    auto a = std::make_unique<LayoutNode>();
    a->leaf = true;
    a->panel = std::move(n->panel);
    a->parent = n;
    auto b = std::make_unique<LayoutNode>();
    b->leaf = true;
    b->panel = std::move(fresh);
    b->parent = n;
    Panel *np = b->panel.get();
    if (t) {
        Tab *nt = np->Cur();
        nt->sort = t->sort; nt->desc = t->desc; nt->group = t->group;
    }
    n->leaf = false;
    n->vertical = vertical;
    n->ratio = 0.5f;
    n->a = std::move(a);
    n->b = std::move(b);
    active = np;
    sessionDirty = true;
    UpdateWatches();
    Invalidate();
}

void App::ClosePanel(Panel *p) {
    LayoutNode *n = FindNode(p);
    if (!n || !n->parent) return; // last panel stays
    LayoutNode *parent = n->parent;
    std::unique_ptr<LayoutNode> sibling = (parent->a.get() == n) ? std::move(parent->b) : std::move(parent->a);
    // replace parent content with sibling
    parent->leaf = sibling->leaf;
    parent->vertical = sibling->vertical;
    parent->ratio = sibling->ratio;
    parent->panel = std::move(sibling->panel);
    parent->a = std::move(sibling->a);
    parent->b = std::move(sibling->b);
    if (parent->a) parent->a->parent = parent;
    if (parent->b) parent->b->parent = parent;
    active = nullptr;
    ForEachNode(parent, [&](Panel *q) { if (!active) active = q; });
    sessionDirty = true;
    UpdateWatches();
    Invalidate();
}

void App::FocusPanel(Panel *p) {
    if (active != p) {
        active = p;
        if (filterEditing) EndFilter(false);
        if (pathEditing) EndPathEdit(false);
        Invalidate();
    }
}

Panel *App::NextPanel(Panel *p, int dir) {
    std::vector<Panel *> list;
    ForEachPanel([&](Panel *q) { list.push_back(q); });
    if (list.size() < 2) return p;
    int i = 0;
    for (int k = 0; k < (int)list.size(); k++) if (list[k] == p) i = k;
    i = (i + dir + (int)list.size()) % (int)list.size();
    return list[i];
}

void App::CycleFocus(int dir) { FocusPanel(NextPanel(active, dir)); }

void App::FocusNeighbor(int dx, int dy) {
    if (!active) return;
    f32 cx = active->rect.x + active->rect.w / 2, cy = active->rect.y + active->rect.h / 2;
    Panel *best = nullptr;
    f32 bestD = 1e30f;
    ForEachPanel([&](Panel *p) {
        if (p == active) return;
        f32 px = p->rect.x + p->rect.w / 2, py = p->rect.y + p->rect.h / 2;
        f32 ddx = px - cx, ddy = py - cy;
        if (dx && ddx * dx <= 0) return;
        if (dy && ddy * dy <= 0) return;
        f32 d = ddx * ddx + ddy * ddy;
        if (d < bestD) { bestD = d; best = p; }
    });
    if (best) FocusPanel(best);
}

static void EvenAll(LayoutNode *n) {
    if (!n || n->leaf) return;
    n->ratio = 0.5f;
    EvenAll(n->a.get());
    EvenAll(n->b.get());
}

void App::EvenPanels(bool siblingsOnly) {
    if (siblingsOnly) {
        LayoutNode *n = FindNode(active);
        if (n && n->parent) n->parent->ratio = 0.5f;
    } else {
        EvenAll(root.get());
    }
    sessionDirty = true;
    Invalidate();
}

// ================================================================== tabs & navigation
// id -> Tab registry: async results find their tab even while the layout tree is being (re)built
static std::unordered_map<int, Tab *> g_tabsById;
static int g_nextTabId = 1;

Tab::Tab() {
    id = g_nextTabId++;
    g_tabsById[id] = this;
}

Tab::~Tab() {
    if (search) search->cancel = true;
    g_tabsById.erase(id);
}

std::wstring Tab::Title() const {
    if (path == PATH_THIS_PC) return T(S_ThisPC);
    if (path == PATH_RECYCLE) return T(S_RecycleBin);
    std::wstring n = PathFileName(path);
    return n.empty() ? path : n;
}

std::wstring App::HomePath() {
    wchar_t buf[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) return buf;
    return L"C:\\";
}

std::wstring App::NewTabPath() {
    if (settings.newTabShow == 1 && CurTab()) return CurTab()->path;
    if (settings.newTabShow == 2) return PATH_THIS_PC;
    return HomePath();
}

Tab *App::NewTab(Panel *p, const std::wstring &path, bool activate) {
    auto t = std::make_unique<Tab>();
    Tab *raw = t.get();
    if (p->tabs.empty()) {
        // default column widths
        f32 base[COL_COUNT] = {0, 150, 150, 160, 90, 100, 100, 110, 90, 260, 150, 56};
        for (int i = 0; i < COL_COUNT; i++) p->colW[i] = base[i] * S;
    }
    if (Tab *from = p->Cur()) { raw->viewMode = from->viewMode; raw->iconSize = from->iconSize; } // new tabs keep the view
    int insertAt = p->tabs.empty() ? 0 : p->active + 1;
    p->tabs.insert(p->tabs.begin() + insertAt, std::move(t));
    if (activate) p->active = insertAt;
    else if (insertAt <= p->active && p->tabs.size() > 1) p->active++;
    if (path != L"\x01") Navigate(raw, path, false);
    sessionDirty = true;
    return raw;
}

void App::CloseTab(Panel *p, int index) {
    if (index < 0 || index >= (int)p->tabs.size()) return;
    Tab *t = p->tabs[index].get();
    if (t->search) t->search->cancel = true;
    closedTabs.push_back({t->path, L""});
    if (closedTabs.size() > 20) closedTabs.erase(closedTabs.begin());
    if (p->tabs.size() == 1) {
        if (FindNode(p) && FindNode(p)->parent) { ClosePanel(p); return; }
        // last tab of last panel: closing it closes the window, like a browser
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return;
    }
    p->tabs.erase(p->tabs.begin() + index);
    if (p->active >= (int)p->tabs.size()) p->active = (int)p->tabs.size() - 1;
    else if (index < p->active) p->active--;
    if (insp.path.size()) ClearInspector();
    CancelHiddenSizes();
    sessionDirty = true;
    UpdateWatches();
    Invalidate();
}

Tab *App::FindTab(int id) {
    auto it = g_tabsById.find(id);
    return it == g_tabsById.end() ? nullptr : it->second;
}

static void StartLoad(App *app, Tab *t) {
    t->loading = true;
    t->loadGen++;
    t->loadStartTick = GetTickCount64();
    int id = t->id;
    u64 gen = t->loadGen;
    std::wstring path = t->path;
    ViewKind kind = t->kind;
    JobSubmitListing([id, gen, path, kind] {
        std::vector<FileEntry> list;
        ListError err = LIST_OK;
        if (kind == VIEW_DRIVES) ListDrives(list, true);
        else if (kind == VIEW_RECYCLE) { if (!RecycleBinList(list)) err = LIST_OTHER; }
        else err = ListDirectory(path, list);
        auto shared = std::make_shared<std::vector<FileEntry>>(std::move(list));
        MainPost([id, gen, shared, err] {
            if (g_app) g_app->OnLoaded(id, gen, std::move(*shared), err);
        });
    });
    // brief synchronous wait so fast local folders appear without a "loading" flash
    u64 until = GetTickCount64() + 30;
    while (t->loading && GetTickCount64() < until) {
        Sleep(1);
        MainDrain();
        t = app->FindTab(id);
        if (!t) return;
    }
}

void App::Navigate(Tab *t, const std::wstring &pathIn, bool pushHistory, const std::wstring &selectName) {
    if (!t) return;
    std::wstring path = pathIn;
    std::wstring sel = selectName;
    if (path != PATH_THIS_PC && path != PATH_RECYCLE) {
        path = NormalizePath(path);
        if (path.empty()) path = PATH_THIS_PC;
        else {
            DWORD a = GetFileAttributesW(LongPathPrefix(path).c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                if (PathExtension(path) == L"lnk") {
                    std::wstring target = ResolveShortcut(path);
                    if (!target.empty() && IsDirectory(target)) path = target;
                    else { sel = PathFileName(path); path = PathParent(path); }
                } else {
                    sel = PathFileName(path);
                    path = PathParent(path);
                }
            }
        }
    }
    if (pushHistory && t->path != path) {
        t->back.push_back(t->path);
        if (t->back.size() > 100) t->back.erase(t->back.begin());
        t->fwd.clear();
    }
    bool same = t->path == path;
    t->path = path;
    t->kind = path == PATH_THIS_PC ? VIEW_DRIVES : (path == PATH_RECYCLE ? VIEW_RECYCLE : VIEW_FOLDER);
    DropRows(t);
    if (!same) {
        t->cursorKeep.clear();
        t->entries.clear();
        t->children.clear();
        t->expanded.clear();
        t->selected.clear();
        t->cursor = 0;
        t->anchor = -1;
        t->scroll = t->scrollTarget = 0;
        t->filter.clear();
        if (t->search) t->search->cancel = true;
        t->search.reset();
        t->recursive = false;
        t->searchResults.clear();
        if (active && active->Cur() == t && filterEditing) EndFilter(true);
    }
    t->pendingSelect = sel;
    t->rows.clear();
    t->rowsDirty = true;
    if (t->kind == VIEW_FOLDER && settings.storeRecents) user.TouchRecent(path);
    StartLoad(this, t);
    CancelHiddenSizes();
    sessionDirty = true;
    UpdateWatches();
    Invalidate();
}

void App::GoBack(Tab *t) {
    if (!t || t->back.empty()) return;
    std::wstring cur = t->path;
    std::wstring p = t->back.back();
    t->back.pop_back();
    t->fwd.push_back(cur);
    std::wstring sel;
    if (!cur.empty() && PathParent(cur) == p) sel = PathFileName(cur);
    Navigate(t, p, false, sel);
}

void App::GoForward(Tab *t) {
    if (!t || t->fwd.empty()) return;
    std::wstring p = t->fwd.back();
    t->fwd.pop_back();
    t->back.push_back(t->path);
    Navigate(t, p, false);
}

void App::GoUp(Tab *t) {
    if (!t) return;
    if (t->kind != VIEW_FOLDER) return;
    std::wstring parent = PathParent(t->path);
    std::wstring sel = PathFileName(t->path);
    if (parent.empty()) {
        // drive root -> This PC
        Navigate(t, PATH_THIS_PC, true, t->path.substr(0, 2));
        return;
    }
    Navigate(t, parent, true, sel);
}

void App::Reload(Tab *t, bool keepSelection) {
    if (!t) return;
    if (!keepSelection) t->selected.clear();
    DropRows(t);
    for (auto &kv : t->children) {
        std::vector<FileEntry> list;
        ListDirectory(kv.first, list);
        kv.second = std::move(list);
    }
    StartLoad(this, t);
    if (t->recursive) StartRecursiveSearch(t);
    Invalidate();
}

void App::OnLoaded(int tabId, u64 gen, std::vector<FileEntry> &&list, ListError err) {
    Tab *t = FindTab(tabId);
    if (!t || t->loadGen != gen) return;
    DropRows(t);
    t->entries = std::move(list);
    t->error = err;
    t->cloud = t->kind == VIEW_FOLDER && IsUnderSyncRoot(t->path);
    if (t->cloud) cloud.InvalidateDir(t->path);
    t->loading = false;
    t->rowsDirty = true;
    RebuildRows(t);
    // drop selections that no longer exist
    if (!t->selected.empty()) {
        std::unordered_set<std::wstring> keep;
        for (auto &r : t->rows) if (!r.header) {
            std::wstring p = RowPath(r);
            if (t->selected.count(p)) keep.insert(p);
        }
        t->selected.swap(keep);
    }
    if (!t->pendingSelect.empty()) {
        std::wstring want = t->pendingSelect;
        t->pendingSelect.clear();
        for (int i = 0; i < (int)t->rows.size(); i++) {
            const Row &r = t->rows[i];
            if (r.header) continue;
            if (_wcsicmp(r.e->name.c_str(), want.c_str()) == 0 || _wcsicmp(RowPath(r).c_str(), want.c_str()) == 0) {
                SelectOnly(t, i);
                EnsureVisible(t, i);
                if (t->pendingRename) {
                    t->pendingRename = false;
                    if (active && active->Cur() == t) BeginRename();
                }
                break;
            }
        }
    }
    if (!shotSelect.empty() && active && active->Cur() == t) {
        for (int i = 0; i < (int)t->rows.size(); i++)
            if (!t->rows[i].header && t->rows[i].e->name == shotSelect) { SelectOnly(t, i); shotSelect.clear(); break; }
    }
    t->cursor = Clamp(t->cursor, 0, std::max(0, (int)t->rows.size() - 1));
    if (active && active->Cur() == t) UpdateInspector();
    Invalidate();
}

// stop folder-size scans no open tab can show any more (switching drives would otherwise pile up whole-drive scans)
void App::CancelHiddenSizes() {
    std::vector<std::wstring> roots;
    ForEachPanel([&](Panel *p) {
        for (auto &t : p->tabs) if (t->kind == VIEW_FOLDER) roots.push_back(t->path);
    });
    std::wstring inspPath = insp.path;
    sizes.CancelUnless([&](const std::wstring &k) {
        if (_wcsicmp(k.c_str(), inspPath.c_str()) == 0) return true;
        for (auto &r : roots) {
            if (k.size() < r.size() || _wcsnicmp(k.c_str(), r.c_str(), r.size()) != 0) continue;
            if (k.size() == r.size() || r.back() == L'\\' || k[r.size()] == L'\\') return true;
        }
        return false;
    });
}

void App::UpdateWatches() {
    std::set<std::wstring> dirs;
    ForEachPanel([&](Panel *p) {
        for (auto &t : p->tabs) {
            if (t->kind != VIEW_FOLDER) continue;
            dirs.insert(t->path);
            for (auto &e : t->expanded) if (dirs.size() < 60) dirs.insert(e);
        }
    });
    watcher.SetWatched(dirs);
}

void App::OnDirChanged(const std::wstring &dir) {
    pendingReloads[dir] = GetTickCount64() + 150;
    if (folderLists.count(dir)) LoadFolderList(dir);
    sizes.Invalidate(dir);
    cloud.InvalidateDir(dir);
    Invalidate();
}

void App::StartRecursiveSearch(Tab *t) {
    t->searchDueTick = 0;
    if (t->search) t->search->cancel = true;
    DropRows(t);
    t->searchResults.clear();
    if (!t->recursive || t->kind != VIEW_FOLDER) { t->search.reset(); return; }
    if (Trim(t->filter).empty()) { t->search.reset(); t->rowsDirty = true; return; } // nothing to look for yet
    auto s = std::make_shared<RecursiveSearch>();
    s->pattern = t->filter;
    t->search = s;
    int id = t->id;
    std::wstring root = t->path;
    std::wstring pattern = t->filter;
    bool showHidden = settings.showHiddenFiles;
    bool useEverything = settings.useEverything;
    JobSubmit([s, id, root, pattern, showHidden, useEverything]() mutable {
        // Everything answers from its index at once; otherwise walk the folders
        std::vector<FileEntry> found;
        if (useEverything && EverythingSearch(root, pattern, showHidden, 200000, found)) {
            s->everything = true;
            s->scanned = found.size();
            auto sh = std::make_shared<std::vector<FileEntry>>(std::move(found));
            MainPost([s, id, sh] {
                if (s->cancel || !g_app) return;
                Tab *t = g_app->FindTab(id);
                if (!t || t->search != s) return;
                g_app->DropRows(t);
                t->searchResults = std::move(*sh);
                t->rowsDirty = true;
                g_app->Invalidate();
            });
            s->done = true;
            MainPost([] { if (g_app) g_app->Invalidate(); });
            return;
        }
        EnumerateRecursive(
            root, s->cancel,
            [&](const FileEntry &e) {
                if (!showHidden && e.Hidden()) return false;
                return pattern.empty() || FuzzyMatch(e.name, pattern);
            },
            [&](std::vector<FileEntry> &&chunk) {
                auto sh = std::make_shared<std::vector<FileEntry>>(std::move(chunk));
                MainPost([s, id, sh] {
                    if (s->cancel || !g_app) return;
                    Tab *t = g_app->FindTab(id);
                    if (!t || t->search != s) return;
                    g_app->DropRows(t);
                    for (auto &e : *sh) t->searchResults.push_back(std::move(e));
                    t->rowsDirty = true;
                    g_app->Invalidate();
                });
            },
            200000, &s->scanned);
        s->done = true;
        MainPost([] { if (g_app) g_app->Invalidate(); });
    });
}

// ================================================================== rows
void App::DropRows(Tab *t) {
    // Rows hold raw pointers into entries/children/searchResults. Call this BEFORE mutating those containers.
    if (!t->rows.empty() && t->cursor >= 0 && t->cursor < (int)t->rows.size() && !t->rows[t->cursor].header)
        t->cursorKeep = RowPath(t->rows[t->cursor]);
    t->rows.clear();
    t->rowsDirty = true;
    t->rowsGen++;
}

f32 App::RowHeight() const { return std::floor(r.Metrics(FONT_UI).lineH + (6 + settings.spacing * 1.5f) * S); }

std::wstring App::EntryPath(const FileEntry &e, const std::wstring &dir) const {
    if (e.isDrive) return e.name + L"\\";
    if (e.isRecycled) return e.parsingName;
    if (!e.relDir.empty()) return PathJoin(PathJoin(dir, e.relDir), e.name);
    return PathJoin(dir, e.name);
}

std::wstring App::RowPath(const Row &r) const {
    if (!r.e) return L"";
    return EntryPath(*r.e, r.dir ? *r.dir : std::wstring());
}

std::wstring App::DisplayName(const FileEntry &e) const {
    if (e.isDrive) {
        std::wstring label = e.label;
        if (label.empty()) {
            switch (e.driveType) {
            case DRIVE_REMOVABLE: label = T(S_Removable); break;
            case DRIVE_REMOTE: label = T(S_Network); break;
            case DRIVE_CDROM: label = T(S_CDROM); break;
            default: label = T(S_Volume);
            }
        }
        return label + L" (" + e.name + L")";
    }
    if (!settings.showFileExtension && !e.isDir) {
        size_t dot = e.name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) return e.name.substr(0, dot);
    }
    return e.name;
}

bool App::Visible(const FileEntry &e) const {
    if (e.isDrive || e.isRecycled) return true;
    if (e.System() && !settings.showSystemFiles) return false;
    if (e.Hidden() && !settings.showHiddenFiles) return false;
    return true;
}

static int CmpU64(u64 a, u64 b) { return a < b ? -1 : (a > b ? 1 : 0); }

struct SortCtx {
    App *app;
    SortKey key;
    bool desc;
    bool foldersFirst;
    const std::wstring *dir;
};

static int CompareEntries(const SortCtx &c, const FileEntry &a, const FileEntry &b) {
    if (c.foldersFirst && a.isDir != b.isDir) return a.isDir ? -1 : 1; // folders always first regardless of direction
    int r = 0;
    switch (c.key) {
    case SORT_NAME: break;
    case SORT_MODIFIED: r = CmpU64(a.modified, b.modified); break;
    case SORT_CREATED: r = CmpU64(a.created, b.created); break;
    case SORT_ACCESSED: r = CmpU64(a.accessed, b.accessed); break;
    case SORT_TYPE:
        if (!a.isDir) r = NaturalCompare(PathExtension(a.name), PathExtension(b.name));
        break;
    case SORT_SIZE: {
        u64 sa = a.size, sb = b.size;
        if (a.isDir && b.isDir && c.dir) {
            auto fa = c.app->sizes.Get(c.app->EntryPath(a, *c.dir), false);
            auto fb = c.app->sizes.Get(c.app->EntryPath(b, *c.dir), false);
            sa = fa ? fa->size.load() : 0;
            sb = fb ? fb->size.load() : 0;
        }
        r = CmpU64(sa, sb);
    } break;
    case SORT_FREE: r = CmpU64(a.freeBytes, b.freeBytes); break;
    case SORT_TOTAL: r = CmpU64(a.totalBytes, b.totalBytes); break;
    case SORT_PERCENT: {
        f64 pa = a.totalBytes ? 1.0 - (f64)a.freeBytes / a.totalBytes : 0;
        f64 pb = b.totalBytes ? 1.0 - (f64)b.freeBytes / b.totalBytes : 0;
        r = pa < pb ? -1 : (pa > pb ? 1 : 0);
    } break;
    case SORT_FS: r = NaturalCompare(a.fsName, b.fsName); break;
    case SORT_ORIGLOC: r = NaturalCompare(a.origLocation, b.origLocation); break;
    case SORT_DELETED: r = CmpU64(a.deleted, b.deleted); break;
    default: break;
    }
    if (r == 0) r = NaturalCompare(a.name, b.name);
    if (r == 0) r = NaturalCompare(a.relDir, b.relDir);
    return c.desc ? -r : r;
}

// --- grouping
static SYSTEMTIME LocalFromFileTime(u64 ft) {
    FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME u, l;
    FileTimeToSystemTime(&f, &u);
    SystemTimeToTzSpecificLocalTime(nullptr, &u, &l);
    return l;
}

static int DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void DateGroup(u64 ft, int *rank, std::wstring *label) {
    if (!ft) { *rank = 1000; *label = T(S_LongAgo); return; }
    SYSTEMTIME now; GetLocalTime(&now);
    SYSTEMTIME t = LocalFromFileTime(ft);
    int dn = DaysFromCivil(now.wYear, now.wMonth, now.wDay);
    int dt = DaysFromCivil(t.wYear, t.wMonth, t.wDay);
    int diff = dn - dt;
    int dow = (now.wDayOfWeek + 6) % 7; // monday = 0
    if (diff <= 0) { *rank = 0; *label = T(S_Today); return; }
    if (diff == 1) { *rank = 1; *label = T(S_Yesterday); return; }
    if (diff <= dow) { *rank = 2; *label = T(S_ThisWeek); return; }
    if (diff <= dow + 7) { *rank = 3; *label = T(S_LastWeek); return; }
    if (t.wYear == now.wYear && t.wMonth == now.wMonth) { *rank = 4; *label = T(S_ThisMonth); return; }
    int pm = now.wMonth == 1 ? 12 : now.wMonth - 1;
    int py = now.wMonth == 1 ? now.wYear - 1 : now.wYear;
    if (t.wYear == py && t.wMonth == pm) { *rank = 5; *label = T(S_LastMonth); return; }
    if (t.wYear == now.wYear) { *rank = 6; *label = T(S_ThisYear); return; }
    if (now.wYear - t.wYear <= 10) { *rank = 10 + (now.wYear - t.wYear); *label = Format(L"%d", t.wYear); return; }
    *rank = 1000;
    *label = T(S_LongAgo);
}

static const wchar_t *kChoseongLabels = L"ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ";

static void EntryGroup(App *app, GroupKey g, const FileEntry &e, int *rank, std::wstring *label) {
    *rank = 0;
    switch (g) {
    case GROUP_NAME: {
        wchar_t c = e.name.empty() ? L'#' : e.name[0];
        if (c >= 0xAC00 && c <= 0xD7A3) {
            int idx = (c - 0xAC00) / (21 * 28);
            *rank = 200 + idx; // Korean groups after Latin, by initial consonant
            *label = std::wstring(1, kChoseongLabels[idx]);
        } else if (c >= 0x3131 && c <= 0x314E && wcschr(kChoseongLabels, c)) {
            int idx = (int)(wcschr(kChoseongLabels, c) - kChoseongLabels); // name starting with a bare consonant (ㅎㅎ)
            *rank = 200 + idx;
            *label = std::wstring(1, c);
        } else if (iswdigit(c)) {
            *rank = 0; *label = L"0 - 9";
        } else if (iswalpha(c) && c < 0x80) {
            wchar_t u = (wchar_t)towupper(c);
            *rank = 10 + (u - L'A'); *label = std::wstring(1, u);
        } else {
            *rank = 500; *label = L"#";
        }
    } break;
    case GROUP_TYPE:
        *label = FileTypeName(e.name, e.isDir);
        *rank = e.isDir ? 0 : 1;
        break;
    case GROUP_SIZE: {
        if (e.isDir) { *rank = -1; *label = T(S_Folders); break; }
        u64 s = e.size;
        if (s == 0) { *rank = 0; *label = T(S_SizeEmpty); }
        else if (s < 16 * 1024) { *rank = 1; *label = T(S_SizeTiny); }
        else if (s < 1024 * 1024) { *rank = 2; *label = T(S_SizeSmall); }
        else if (s < 128ull * 1024 * 1024) { *rank = 3; *label = T(S_SizeMedium); }
        else if (s < 1024ull * 1024 * 1024) { *rank = 4; *label = T(S_SizeLarge); }
        else if (s < 4096ull * 1024 * 1024) { *rank = 5; *label = T(S_SizeHuge); }
        else { *rank = 6; *label = T(S_SizeGigantic); }
    } break;
    case GROUP_MODIFIED: DateGroup(e.modified, rank, label); break;
    case GROUP_CREATED: DateGroup(e.created, rank, label); break;
    case GROUP_ACCESSED: DateGroup(e.accessed, rank, label); break;
    case GROUP_DELETED: DateGroup(e.deleted, rank, label); break;
    case GROUP_ORIGLOC: *label = e.origLocation; break;
    case GROUP_FS: *label = e.fsName.empty() ? T(S_Others) : e.fsName; break;
    case GROUP_FREE: case GROUP_PERCENT: {
        f64 pct = e.totalBytes ? 100.0 * (1.0 - (f64)e.freeBytes / e.totalBytes) : 0;
        int b = (int)(pct / 25);
        *rank = b;
        *label = Format(L"%d%% - %d%%", b * 25, std::min(100, b * 25 + 25));
    } break;
    default: break;
    }
    (void)app;
}

struct ItemRef {
    const FileEntry *e;
    int rank;
    std::wstring glabel;
};

static void AddChildren(App *app, Tab *t, const std::wstring &dir, int depth, const SortCtx &sc, std::vector<Row> &rows) {
    auto it = t->children.find(dir);
    if (it == t->children.end()) return;
    std::vector<const FileEntry *> v;
    for (auto &e : it->second) if (app->Visible(e)) v.push_back(&e);
    std::sort(v.begin(), v.end(), [&](const FileEntry *a, const FileEntry *b) { return CompareEntries(sc, *a, *b) < 0; });
    const std::wstring *dirKey = &it->first;
    SortCtx sc2 = sc;
    sc2.dir = dirKey;
    (void)sc2;
    for (auto *e : v) {
        Row r;
        r.e = e;
        r.dir = dirKey;
        r.depth = depth;
        rows.push_back(r);
        if (e->isDir) {
            std::wstring p = PathJoin(*dirKey, e->name);
            if (t->expanded.count(p)) AddChildren(app, t, p, depth + 1, sc, rows);
        }
    }
}

void App::RebuildRows(Tab *t) {
    if (!t) return;
    t->rowsDirty = false;
    t->rowsGen++;
    // remember cursor row path so it stays put (rows may already be dropped by DropRows)
    std::wstring cursorPath = t->cursorKeep;
    if (t->cursor >= 0 && t->cursor < (int)t->rows.size() && !t->rows[t->cursor].header) cursorPath = RowPath(t->rows[t->cursor]);
    t->cursorKeep.clear();

    t->rows.clear();
    t->groups.clear();
    const std::vector<FileEntry> &src = t->recursive ? t->searchResults : t->entries;
    SortCtx sc{this, t->sort, t->desc, settings.foldersFirst && t->kind == VIEW_FOLDER, &t->path};
    GroupKey g = t->group;
    if (t->kind == VIEW_DRIVES && (g != GROUP_NONE && g != GROUP_FS && g != GROUP_FREE && g != GROUP_PERCENT && g != GROUP_NAME)) g = GROUP_NONE;

    std::vector<ItemRef> items;
    items.reserve(src.size());
    bool filtering = !t->filter.empty() && !t->recursive;
    for (auto &e : src) {
        if (!Visible(e)) continue;
        if (t->hideFiles && !e.isDir) continue;
        if (t->hideDirs && e.isDir) continue;
        if (filtering && !FuzzyMatch(DisplayName(e), t->filter)) continue;
        ItemRef ir{&e, 0, L""};
        if (g != GROUP_NONE) EntryGroup(this, g, e, &ir.rank, &ir.glabel);
        items.push_back(std::move(ir));
    }
    std::stable_sort(items.begin(), items.end(), [&](const ItemRef &a, const ItemRef &b) {
        if (g != GROUP_NONE) {
            if (a.rank != b.rank) return a.rank < b.rank;
            if (a.glabel != b.glabel) return NaturalCompare(a.glabel, b.glabel) < 0;
        }
        return CompareEntries(sc, *a.e, *b.e) < 0;
    });

    std::wstring lastLabel;
    int lastRank = INT32_MIN;
    bool collapsed = false;
    for (auto &ir : items) {
        if (g != GROUP_NONE && (ir.rank != lastRank || ir.glabel != lastLabel || t->groups.empty())) {
            GroupInfo gi;
            gi.label = ir.glabel;
            gi.collapsed = t->collapsedGroups.count(ir.glabel) > 0;
            t->groups.push_back(gi);
            Row hr;
            hr.header = true;
            hr.group = (int)t->groups.size() - 1;
            t->rows.push_back(hr);
            lastLabel = ir.glabel;
            lastRank = ir.rank;
            collapsed = gi.collapsed;
        }
        if (g != GROUP_NONE) {
            t->groups.back().count++;
            t->groups.back().bytes += ir.e->size;
        }
        if (collapsed) continue;
        Row r;
        r.e = ir.e;
        r.dir = &t->path;
        r.depth = 0;
        r.group = g != GROUP_NONE ? (int)t->groups.size() - 1 : -1;
        t->rows.push_back(r);
        if (ir.e->isDir && !t->expanded.empty() && ir.e->relDir.empty()) {
            std::wstring p = PathJoin(t->path, ir.e->name);
            if (t->expanded.count(p)) AddChildren(this, t, p, 1, sc, t->rows);
        }
    }
    if (!cursorPath.empty()) {
        for (int i = 0; i < (int)t->rows.size(); i++)
            if (!t->rows[i].header && RowPath(t->rows[i]) == cursorPath) { t->cursor = i; break; }
    }
    t->cursor = Clamp(t->cursor, 0, std::max(0, (int)t->rows.size() - 1));
}

int App::RowAt(Tab *t, const std::wstring &path) {
    for (int i = 0; i < (int)t->rows.size(); i++)
        if (!t->rows[i].header && _wcsicmp(RowPath(t->rows[i]).c_str(), path.c_str()) == 0) return i;
    return -1;
}

std::vector<std::wstring> App::SelectedPaths(Tab *t, bool fallbackToCursor) {
    std::vector<std::wstring> out;
    if (!t) return out;
    if (t->rowsDirty) RebuildRows(t);
    for (auto &r : t->rows) {
        if (r.header) continue;
        std::wstring p = RowPath(r);
        if (t->selected.count(p)) out.push_back(p);
    }
    if (out.empty() && fallbackToCursor && t->cursor >= 0 && t->cursor < (int)t->rows.size() && !t->rows[t->cursor].header)
        out.push_back(RowPath(t->rows[t->cursor]));
    return out;
}

void App::SelectOnly(Tab *t, int row) {
    t->selected.clear();
    if (row >= 0 && row < (int)t->rows.size() && !t->rows[row].header) t->selected.insert(RowPath(t->rows[row]));
    t->cursor = row;
    t->anchor = row;
    if (active && active->Cur() == t) UpdateInspector();
    Invalidate();
}

void App::SetCursor(Tab *t, int row, bool extend, bool keepSel) {
    if (t->rows.empty()) return;
    row = Clamp(row, 0, (int)t->rows.size() - 1);
    if (extend) {
        if (t->anchor < 0) t->anchor = t->cursor;
        int a = std::min(t->anchor, row), b = std::max(t->anchor, row);
        if (!keepSel) t->selected.clear();
        for (int i = a; i <= b; i++) if (!t->rows[i].header) t->selected.insert(RowPath(t->rows[i]));
        t->cursor = row;
        if (active && active->Cur() == t) UpdateInspector();
    } else if (keepSel) {
        t->cursor = row;
    } else {
        SelectOnly(t, row);
    }
    EnsureVisible(t, row);
    Invalidate();
}

void App::EnsureVisible(Tab *t, int row) {
    RectF ir = ItemRect(t, row);
    f32 y = ir.y, rh = ir.h;
    f32 vh = t->viewH > 0 ? t->viewH : 400 * S;
    if (y < t->scrollTarget) t->scrollTarget = y;
    else if (y + rh > t->scrollTarget + vh) t->scrollTarget = y + rh - vh;
    if (!settings.animations) t->scroll = t->scrollTarget;
}

void App::ToggleExpand(Tab *t, int row, int mode) {
    if (row < 0 || row >= (int)t->rows.size()) return;
    const Row &r = t->rows[row];
    if (r.header) {
        GroupInfo &g = t->groups[r.group];
        bool collapse = mode == 0 ? !g.collapsed : mode < 0;
        if (collapse) t->collapsedGroups.insert(g.label);
        else t->collapsedGroups.erase(g.label);
        t->rowsDirty = true;
        Invalidate();
        return;
    }
    if (!r.e->isDir || t->kind != VIEW_FOLDER) return;
    std::wstring p = RowPath(r);
    bool isExp = t->expanded.count(p) > 0;
    bool expand = mode == 0 ? !isExp : mode > 0;
    if (expand == isExp) return;
    DropRows(t);
    if (expand) {
        std::vector<FileEntry> list;
        ListDirectory(p, list);
        t->children[p] = std::move(list);
        t->expanded.insert(p);
    } else {
        // collapse this and descendants
        for (auto it = t->expanded.begin(); it != t->expanded.end();) {
            if (*it == p || (it->size() > p.size() && it->compare(0, p.size(), p) == 0 && (*it)[p.size()] == L'\\')) {
                t->children.erase(*it);
                it = t->expanded.erase(it);
            } else ++it;
        }
    }
    t->rowsDirty = true;
    RebuildRows(t);
    UpdateWatches();
    Invalidate();
}

// ================================================================== sizes
bool App::CalcSizesFor(const std::wstring &path) const {
    if (!settings.calcFolderSizes || path.empty() || path == PATH_RECYCLE) return false;
    u32 type = DriveTypeOf(path);
    if (type == DRIVE_REMOTE) return settings.calcFolderSizesNetwork;
    if (type == DRIVE_FIXED) return IsSSD(path) ? settings.calcFolderSizesSSD : settings.calcFolderSizesHDD;
    if (type == DRIVE_REMOVABLE) return settings.calcFolderSizesHDD;
    return false;
}

std::shared_ptr<FolderStats> App::FolderSize(const std::wstring &path) {
    return sizes.Get(path, CalcSizesFor(path));
}
