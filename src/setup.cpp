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
    f32 sh = r.Metrics(FONT_SMALL).lineH;
    bool installed = setup.info.installed;
    bool update = installed && setup.info.version != APP_VERSION;
    r.PushClip(rc);

    // ---- left: brand panel
    RectF left = rc;
    left = left.CutLeft(std::floor(Clamp(rc.w * 0.36f, 200 * S, 320 * S)));
    r.FillGradientV(left, Color::Hex(0x14b8a6), Color::Hex(0x0f4c81));
    if (!setup.logo) {
        HICON ic = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
        setup.logo = r.CreateBitmapFromHIcon(ic);
        if (ic) DestroyIcon(ic);
    }
    f32 logoSz = std::floor(88 * S);
    f32 by = std::floor(left.y + left.h * 0.22f);
    TextOpts center; center.align = ALIGN_CENTER;
    if (setup.logo) r.DrawBitmap(setup.logo, RectF(std::floor(left.x + (left.w - logoSz) / 2), by, logoSz, logoSz));
    by += logoSz + 16 * S;
    Color white(1, 1, 1, 1), whiteDim(1, 1, 1, 0.75f);
    f32 th = r.Metrics(FONT_TITLE).lineH;
    r.Text(FONT_TITLE, L"TwinDeck", RectF(left.x, by, left.w, th), white, center);
    by += th + 4 * S;
    std::wstring ver = installed ? Format(TL(L"Installed %s", L"설치됨 · %s"), setup.info.version.c_str())
                                 : Format(TL(L"Version %s", L"버전 %s"), APP_VERSION);
    if (update) ver += Format(L"  →  %s", APP_VERSION);
    r.Text(FONT_SMALL, ver, RectF(left.x, by, left.w, sh), whiteDim, center);
    by += sh + 22 * S;
    TextOpts wrap; wrap.align = ALIGN_CENTER; wrap.wrap = true; wrap.vcenter = false; wrap.ellipsis = false;
    r.Text(FONT_UI, TL(L"A fast file manager\nwith first-class Korean support", L"한글을 제대로 지원하는\n빠른 파일 관리자"),
           RectF(left.x + 20 * S, by, left.w - 40 * S, lh * 3), whiteDim, wrap);

    // ---- right: settings
    RectF right = rc;
    right.CutLeft(left.w);
    right = right.Shrink(std::floor(28 * S));
    f32 rowH = std::floor(lh + 18 * S);
    f32 y = right.y;
    r.Text(FONT_UI_BOLD, TL(L"Install location", L"설치 위치"), RectF(right.x, y, right.w, lh), theme.text);
    y += lh + 8 * S;
    // path field + browse button
    RectF browse(right.Right() - rowH, y, rowH, rowH);
    RectF field(right.x, y, right.w - rowH - 8 * S, rowH);
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
    r.FillRect(browse, Hover(browse) ? theme.hover : theme.inputBg, 5 * S);
    r.StrokeRect(browse, theme.border, std::max(1.f, S), 5 * S);
    TextOpts gc; gc.align = ALIGN_CENTER; gc.ellipsis = false;
    r.Text(FONT_ICON_SMALL, GLYPH_FOLDER, browse, theme.textDim, gc);
    if (Hover(browse)) SetTooltip(HashId(&setup, 1), TL(L"Choose folder", L"폴더 선택"));
    if (Clicked(browse)) {
        in.pressed[0] = false;
        std::wstring picked;
        if (BrowseFolder(hwnd, setup.pathEdit.text, picked)) {
            // picking a parent folder installs into "<parent>\TwinDeck", like most installers
            if (_wcsicmp(PathFileName(picked).c_str(), L"TwinDeck") != 0) picked = PathJoin(picked, L"TwinDeck");
            setup.pathEdit.Set(picked, false);
        }
    }
    y += rowH + 22 * S;

    // option cards
    r.Text(FONT_UI_BOLD, TL(L"Options", L"옵션"), RectF(right.x, y, right.w, lh), theme.text);
    y += lh + 8 * S;
    struct Opt { const wchar_t *en, *ko, *descEn, *descKo; bool *v; };
    Opt opts[5] = {
        {L"Right-click entries", L"우클릭 메뉴", L"\"Open in TwinDeck\" in Explorer's folder menus",
         L"탐색기 폴더 메뉴에 'TwinDeck에서 열기' 추가", &setup.opts.contextMenu},
        {L"Add to PATH", L"PATH에 추가", L"Run \"twindeck\" from any terminal", L"터미널에서 twindeck으로 실행", &setup.opts.addToPath},
        {L"Desktop shortcut", L"바탕 화면 바로 가기", L"An icon on the desktop (Start menu always)",
         L"바탕 화면에 아이콘 (시작 메뉴는 항상 추가)", &setup.opts.desktopShortcut},
        {L"Default file manager", L"기본 파일 관리자", L"Folders, drives and Win+E open in TwinDeck",
         L"폴더·드라이브 열기와 Win+E를 TwinDeck으로", &setup.opts.setDefault},
        {L"Win+E hotkey", L"Win+E 단축키", L"Catch Win+E itself, for Windows that ignores the above",
         L"위 설정이 안 먹는 Windows에서 Win+E를 직접 받기", &setup.opts.winEHotkey},
    };
    const int optCount = (int)std::size(opts);
    f32 cardRowH = std::floor(lh + sh + 22 * S);
    RectF card(right.x, y, right.w, cardRowH * optCount);
    r.FillRect(card, theme.inputBg, 8 * S);
    r.StrokeRect(card, theme.border, std::max(1.f, S), 8 * S);
    for (int i = 0; i < optCount; i++) {
        RectF row(card.x, card.y + i * cardRowH, card.w, cardRowH);
        if (i) r.Line(row.x + 14 * S, row.y + 0.5f, row.Right() - 14 * S, row.y + 0.5f, theme.border);
        bool v = *opts[i].v;
        bool hov = Hover(row);
        if (hov) r.FillRect(row.Shrink(2 * S), theme.hover, 6 * S);
        f32 textY = row.y + (row.h - lh - sh - 2 * S) / 2;
        RectF sw(row.Right() - 58 * S, row.y + (row.h - 22 * S) / 2, 44 * S, 22 * S);
        r.Text(FONT_UI, TL(opts[i].en, opts[i].ko), RectF(row.x + 16 * S, textY, sw.x - row.x - 26 * S, lh), theme.text);
        r.Text(FONT_SMALL, TL(opts[i].descEn, opts[i].descKo), RectF(row.x + 16 * S, textY + lh + 2 * S, sw.x - row.x - 26 * S, sh), theme.textDim);
        r.FillRect(sw, v ? theme.accent : theme.border, 11 * S);
        f32 kx = v ? sw.Right() - 20 * S : sw.x + 2 * S;
        r.FillRect(RectF(kx, sw.y + 2 * S, 18 * S, 18 * S), Color(1, 1, 1, 1), 9 * S);
        if (hov && in.pressed[0]) { *opts[i].v = !v; in.pressed[0] = false; }
    }
    y = card.Bottom() + 16 * S;

    // status line + buttons (bottom right)
    f32 btnH = std::floor(lh + 16 * S);
    f32 bx = right.Right();
    f32 btnY = std::max(y + sh + 12 * S, right.Bottom() - btnH);
    auto button = [&](const std::wstring &label, bool primary, bool danger, bool alignLeft = false) {
        f32 w = std::floor(r.Measure(FONT_UI, label) + 32 * S);
        RectF b(alignLeft ? right.x : bx - w, btnY, w, btnH);
        if (!alignLeft) bx -= w + 8 * S;
        bool hov = Hover(b);
        if (primary) r.FillRect(b, hov ? theme.accent.WithAlpha(0.85f) : theme.accent, 5 * S);
        else {
            r.FillRect(b, hov ? theme.hover : theme.inputBg, 5 * S);
            r.StrokeRect(b, danger && hov ? theme.danger : theme.border, std::max(1.f, S), 5 * S);
        }
        r.Text(FONT_UI, label, b, primary ? theme.accentText : (danger ? theme.danger : theme.text), center);
        bool hit = hov && in.pressed[0];
        if (hit) in.pressed[0] = false;
        return hit;
    };
    if (!setup.status.empty())
        r.Text(FONT_SMALL, setup.status, RectF(right.x, btnY - sh - 10 * S, right.w, sh), setup.statusError ? theme.danger : theme.ok);

    std::wstring primaryLabel = update ? TL(L"Update", L"업데이트") : (installed ? TL(L"Apply", L"적용") : TL(L"Install", L"설치"));
    if (button(primaryLabel, true, false)) {
        if (focus == FOCUS_SETUP) focus = FOCUS_LIST;
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
    bool firstRunInstaller = setup.installerLaunch && !installed;
    if (button(firstRunInstaller ? TL(L"Run without installing", L"설치하지 않고 실행") : TL(L"Close", L"닫기"), false, false))
        CloseSetup();
    if (installed && button(TL(L"Uninstall", L"제거"), false, true, true)) {
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
    r.PopClip();
}
