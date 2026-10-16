// Setup screen: install / modify / uninstall with options (install path, desktop shortcut, default file manager,
// right-click entries, PATH). Shown by TwinDeck-Setup.exe, --setup, the first start, or Options > System.
#include "app.h"
#include "install.h"
#include <shobjidl.h>

static bool BrowseFolder(HWND owner, const std::wstring &start, std::wstring &out) {
    IFileOpenDialog *dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(TL(L"Install folder", L"설치 폴더"));
    std::wstring s = start;
    while (!s.empty() && !IsDirectory(s)) s = PathParent(s);
    IShellItem *folder = nullptr;
    if (!s.empty() && SUCCEEDED(SHCreateItemFromParsingName(s.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        dlg->SetFolder(folder);
        folder->Release();
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
                ok = true;
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

void App::OpenSetup(bool installerLaunch) {
    setup.active = true;
    setup.installerLaunch = installerLaunch;
    setup.info = QueryInstall();
    setup.opts = setup.info.opts;
    // a fresh install keeps the program name as the last folder ("…\TwinDeck")
    setup.pathEdit.Set(setup.opts.dir, false);
    setup.status.clear();
    setup.statusError = false;
    ClosePopup();
    if (focus == FOCUS_FILTER || focus == FOCUS_PATH || focus == FOCUS_RENAME) focus = FOCUS_LIST;
    Invalidate();
}

void App::CloseSetup() {
    setup.active = false;
    if (focus == FOCUS_SETUP) focus = FOCUS_LIST;
    if (setup.resizedWindow) {
        // the installer window was small: go back to the normal window size
        setup.resizedWindow = false;
        if (session.w > 200) SetWindowPos(hwnd, nullptr, session.x, session.y, session.w, session.h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (session.maximized) ShowWindow(hwnd, SW_MAXIMIZE);
    }
    Invalidate();
}

void App::SetupKey(const KeyEvent &k) {
    if (focus == FOCUS_SETUP) {
        if (k.vk == VK_ESCAPE || k.vk == VK_RETURN || k.vk == VK_TAB) { focus = FOCUS_LIST; return; }
        bool changed = false;
        setup.pathEdit.OnKey(k, hwnd, &changed);
        return;
    }
    if (k.vk == VK_ESCAPE && !(setup.installerLaunch && !setup.info.installed)) CloseSetup();
}

void App::DrawSetup(RectF rc) {
    r.FillSurface(rc, theme.panelBg);
    f32 lh = r.Metrics(FONT_UI).lineH;
    f32 colW = std::min(560 * S, rc.w - 40 * S);
    f32 x0 = std::floor(rc.x + (rc.w - colW) / 2);
    // total height of the column, so it can be centered vertically
    f32 rowH = std::floor(lh + 18 * S);
    f32 logoSz = std::floor(96 * S);
    bool installed = setup.info.installed;
    f32 total = logoSz + 14 * S + r.Metrics(FONT_TITLE).lineH + 16 * S + lh + 28 * S + lh + 8 * S + rowH + 22 * S +
                2 * (lh + 20 * S) + 26 * S + rowH + 20 * S + (installed ? rowH : 0) + rowH + lh * 2;
    f32 y = std::floor(rc.y + std::max(16 * S, (rc.h - total) / 2));
    r.PushClip(rc);

    // logo + name
    if (!setup.logo) {
        HICON ic = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
        setup.logo = r.CreateBitmapFromHIcon(ic);
        if (ic) DestroyIcon(ic);
    }
    if (setup.logo) r.DrawBitmap(setup.logo, RectF(std::floor(rc.x + (rc.w - logoSz) / 2), y, logoSz, logoSz));
    y += logoSz + 14 * S;
    TextOpts center; center.align = ALIGN_CENTER;
    f32 th = r.Metrics(FONT_TITLE).lineH;
    r.Text(FONT_TITLE, L"TwinDeck", RectF(x0, y, colW, th), theme.text, center);
    y += th + 16 * S;
    std::wstring ver;
    if (!installed) ver = Format(TL(L"Install TwinDeck %s", L"TwinDeck %s 설치"), APP_VERSION);
    else if (setup.info.version != APP_VERSION)
        ver = Format(TL(L"Installed version %s  →  this file %s", L"설치된 버전 %s  →  이 파일 %s"), setup.info.version.c_str(), APP_VERSION);
    else ver = Format(TL(L"Installed version %s.", L"설치된 버전 %s."), setup.info.version.c_str());
    r.Text(FONT_UI, ver, RectF(x0, y, colW, lh), theme.textDim, center);
    y += lh + 14 * S;
    r.Line(x0 - 60 * S, y, x0 + colW + 60 * S, y, theme.border);
    y += 14 * S;

    // install path
    r.Text(FONT_UI, installed ? TL(L"Installed path:", L"설치 경로:") : TL(L"Install path:", L"설치 경로:"), RectF(x0, y, colW, lh), theme.textDim);
    RectF browse(x0 + colW - lh - 8 * S, y - 4 * S, lh + 8 * S, lh + 8 * S);
    if (IconButton(HashId(&setup, 1), browse, GLYPH_FOLDER, TL(L"Choose folder", L"폴더 선택"))) {
        std::wstring picked;
        if (BrowseFolder(hwnd, setup.pathEdit.text, picked)) {
            // picking a parent folder installs into "<parent>\TwinDeck", like most installers
            if (_wcsicmp(PathFileName(picked).c_str(), L"TwinDeck") != 0) picked = PathJoin(picked, L"TwinDeck");
            setup.pathEdit.Set(picked, false);
        }
        in.pressed[0] = false;
    }
    y += lh + 8 * S;
    RectF field(x0, y, colW, rowH);
    bool editing = focus == FOCUS_SETUP;
    r.FillRect(field, theme.inputBg, 5 * S);
    r.StrokeRect(field, editing ? theme.accent : (Hover(field) ? theme.textFaint : theme.border), std::max(1.f, S), 5 * S);
    DrawTextEdit(setup.pathEdit, RectF(field.x + 10 * S, field.y, field.w - 20 * S, field.h), FONT_UI, theme.text, L"", editing);
    if (Clicked(field)) {
        focus = FOCUS_SETUP;
        setup.pathEdit.caret = setup.pathEdit.anchor = r.HitTest(FONT_UI, setup.pathEdit.text, in.mx - field.x - 10 * S + setup.pathEdit.scrollX);
        in.pressed[0] = false;
    } else if (in.pressed[0] && editing) {
        focus = FOCUS_LIST;
    }
    if (Hover(field)) cursorKind = 3;
    y += rowH + 22 * S;

    // options (2 x 2)
    struct Opt { const wchar_t *en, *ko, *tipEn, *tipKo; bool *v; };
    Opt opts[4] = {
        {L"Desktop shortcut", L"바탕 화면 바로 가기", L"Adds a TwinDeck icon to the desktop.", L"바탕 화면에 TwinDeck 아이콘을 만듭니다."},
        {L"Set as default", L"기본 파일 관리자", L"Folders, drives and Win+E open in TwinDeck instead of File Explorer.",
         L"폴더·드라이브를 열거나 Win+E를 누르면 탐색기 대신 TwinDeck이 열립니다."},
        {L"Right-click entries", L"우클릭 메뉴", L"Adds \"Open in TwinDeck\" to folder and background menus in Explorer.",
         L"탐색기의 폴더·빈 곳 우클릭 메뉴에 \"TwinDeck에서 열기\"를 추가합니다."},
        {L"Add to PATH", L"PATH에 추가", L"Run \"twindeck\" from any terminal.", L"터미널 어디서나 \"twindeck\"으로 실행할 수 있습니다."},
    };
    opts[0].v = &setup.opts.desktopShortcut;
    opts[1].v = &setup.opts.setDefault;
    opts[2].v = &setup.opts.contextMenu;
    opts[3].v = &setup.opts.addToPath;
    f32 cellW = colW / 2, optH = lh + 20 * S;
    for (int i = 0; i < 4; i++) {
        RectF c(x0 + (i % 2) * cellW, y + (i / 2) * optH, cellW, optH);
        RectF hit = c.Shrink(3 * S);
        bool v = *opts[i].v;
        if (Hover(hit)) {
            r.FillRect(hit, theme.hover, 5 * S);
            SetTooltip(HashId(&setup, 10 + i), TL(opts[i].tipEn, opts[i].tipKo));
        }
        RectF sw(c.Right() - 56 * S, c.y + (c.h - 22 * S) / 2, 44 * S, 22 * S);
        r.FillRect(sw, v ? theme.accent : theme.border, 11 * S);
        f32 kx = v ? sw.Right() - 20 * S : sw.x + 2 * S;
        RectF knob(kx, sw.y + 2 * S, 18 * S, 18 * S);
        r.FillRect(knob, Color(1, 1, 1, 1), 9 * S);
        TextOpts ko; ko.align = ALIGN_CENTER; ko.ellipsis = false;
        r.Text(FONT_ICON_SMALL, v ? GLYPH_CHECK : GLYPH_CLOSE, knob, v ? theme.accent : theme.textFaint, ko);
        TextOpts ro; ro.align = ALIGN_RIGHT;
        r.Text(FONT_UI, std::wstring(TL(opts[i].en, opts[i].ko)) + L":", RectF(c.x, c.y, sw.x - c.x - 10 * S, c.h), theme.textDim, ro);
        if (Clicked(hit)) { *opts[i].v = !v; in.pressed[0] = false; }
    }
    y += 2 * optH + 26 * S;

    // actions
    auto action = [&](const std::wstring &label, bool primary, Color col) {
        f32 w = r.Measure(FONT_TITLE, label) + 48 * S;
        RectF b(std::floor(rc.x + (rc.w - w) / 2), y, w, rowH);
        y += rowH;
        bool hov = Hover(b);
        if (primary) r.FillRect(b, hov ? theme.accent.WithAlpha(0.85f) : theme.accent, 6 * S);
        else if (hov) r.FillRect(b, theme.hover, 6 * S);
        r.Text(FONT_TITLE, label, b, primary ? theme.accentText : (hov ? theme.text : col), center);
        bool hit = hov && in.pressed[0];
        if (hit) in.pressed[0] = false;
        return hit;
    };
    if (action(installed ? (setup.info.version != APP_VERSION ? TL(L"Update", L"업데이트") : TL(L"Modify", L"수정"))
                         : TL(L"Install", L"설치"), true, theme.text)) {
        focus = focus == FOCUS_SETUP ? FOCUS_LIST : focus;
        InstallOptions o = setup.opts;
        o.dir = setup.pathEdit.text;
        std::wstring err;
        if (PerformInstall(o, &err)) {
            bool wasInstalled = installed;
            setup.info = QueryInstall();
            setup.opts = setup.info.opts;
            setup.pathEdit.Set(setup.opts.dir, false);
            setup.status = wasInstalled ? TL(L"Settings applied.", L"설정을 적용했습니다.") : TL(L"Installed.", L"설치했습니다.");
            setup.statusError = false;
            // started as an installer from elsewhere: hand over to the installed copy
            if (setup.installerLaunch && !IsRunningInstalledCopy() && LaunchInstalled()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            setup.status = err;
            setup.statusError = true;
        }
    }
    y += 10 * S;
    r.Line(x0 - 60 * S, y, x0 + colW + 60 * S, y, theme.border);
    y += 10 * S;
    if (installed && action(TL(L"Uninstall", L"제거"), false, theme.textDim)) {
        Confirm(TL(L"Uninstall TwinDeck? Your settings and bookmarks are kept.", L"TwinDeck을 제거할까요? 설정과 즐겨찾기는 남겨 둡니다."), [this] {
            bool self = false;
            PerformUninstall(&self);
            setup.info = QueryInstall();
            setup.opts = setup.info.opts;
            setup.pathEdit.Set(setup.opts.dir, false);
            setup.status = TL(L"Uninstalled.", L"제거했습니다.");
            setup.statusError = false;
            if (self) {
                // the running copy was the installed one: delete it after exit
                ScheduleSelfDelete();
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        });
    }
    bool firstRunInstaller = setup.installerLaunch && !installed;
    if (action(firstRunInstaller ? TL(L"Run without installing", L"설치하지 않고 실행") : TL(L"Back", L"뒤로"), false, theme.textDim))
        CloseSetup();
    if (!setup.status.empty()) {
        r.Text(FONT_UI, setup.status, RectF(x0 - 60 * S, y + 6 * S, colW + 120 * S, lh), setup.statusError ? theme.danger : theme.ok, center);
    }
    r.PopClip();
}
