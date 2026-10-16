// Entry point, window procedure, IME message handling and the render loop
#include "app.h"
#include "jobs.h"
#include <windowsx.h>
#include <imm.h>


static App g_appInstance;

static KeyEvent MakeKey(UINT vk, LPARAM lp) {
    KeyEvent k;
    k.vk = vk;
    k.ctrl = GetKeyState(VK_CONTROL) < 0;
    k.shift = GetKeyState(VK_SHIFT) < 0;
    k.alt = GetKeyState(VK_MENU) < 0;
    k.repeat = (lp & (1 << 30)) != 0;
    return k;
}

static std::wstring ImeString(HWND hwnd, DWORD type) {
    std::wstring s;
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return s;
    LONG bytes = ImmGetCompositionStringW(imc, type, nullptr, 0);
    if (bytes > 0) {
        s.resize(bytes / sizeof(wchar_t));
        ImmGetCompositionStringW(imc, type, &s[0], bytes);
    }
    ImmReleaseContext(hwnd, imc);
    return s;
}

static int ImeCursor(HWND hwnd) {
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) return 0;
    LONG c = ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
    ImmReleaseContext(hwnd, imc);
    return std::max(0L, c);
}

static void MouseButton(App *a, HWND hwnd, int b, bool down, bool dbl, LPARAM lp) {
    a->in.mx = (f32)GET_X_LPARAM(lp);
    a->in.my = (f32)GET_Y_LPARAM(lp);
    a->in.ctrl = GetKeyState(VK_CONTROL) < 0;
    a->in.shift = GetKeyState(VK_SHIFT) < 0;
    a->in.alt = GetKeyState(VK_MENU) < 0;
    if (down) {
        a->in.down[b] = true;
        a->in.pressed[b] = true;
        if (b == 0) {
            a->in.clickCount = dbl ? 2 : 1;
            a->in.pressX = a->in.mx;
            a->in.pressY = a->in.my;
        }
        SetCapture(hwnd);
    } else {
        a->in.down[b] = false;
        a->in.released[b] = true;
        if (!a->in.down[0] && !a->in.down[1] && !a->in.down[2]) ReleaseCapture();
    }
    a->Invalidate();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App *a = g_app;
    LRESULT cmResult = 0;
    if (a && a->hwnd && HandleContextMenuMessage(msg, wp, lp, &cmResult)) return cmResult;
    switch (msg) {
    case WM_CREATE:
        return 0;
    // ---------------- custom frame: client area extends over the caption ----------------
    case WM_NCCALCSIZE:
        if (wp) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
            LONG top = p->rgrc[0].top;
            DefWindowProcW(hwnd, msg, wp, lp); // keeps the native left/right/bottom resize borders
            p->rgrc[0].top = top;              // ...but removes the caption
            if (IsZoomed(hwnd)) {
                UINT dpi = GetDpiForWindow(hwnd);
                p->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit != HTCLIENT || !a || !a->ready) return hit;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (!IsZoomed(hwnd)) {
            UINT dpi = GetDpiForWindow(hwnd);
            int frame = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (pt.y < frame) return pt.x < frame * 2 ? HTTOPLEFT : (pt.x >= rc.right - frame * 2 ? HTTOPRIGHT : HTTOP);
        }
        return a->NonClientHitTest(pt.x, pt.y);
    }
    case WM_NCMOUSEMOVE:
        if (a) {
            bool overMax = wp == HTMAXBUTTON;
            if (overMax) {
                POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd, &pt);
                a->in.mx = (f32)pt.x;
                a->in.my = (f32)pt.y;
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            if (overMax != a->ncHoverMax) { a->ncHoverMax = overMax; a->Invalidate(); }
            if (!overMax) a->maxButtonPressed = false;
        }
        break;
    case WM_NCMOUSELEAVE:
        if (a) { a->ncHoverMax = false; a->maxButtonPressed = false; a->Invalidate(); }
        break;
    case WM_NCLBUTTONDOWN:
        if (a && wp == HTMAXBUTTON) { a->maxButtonPressed = true; a->Invalidate(); return 0; }
        break;
    case WM_NCLBUTTONUP:
        if (a && wp == HTMAXBUTTON) {
            if (a->maxButtonPressed) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            a->maxButtonPressed = false;
            a->Invalidate();
            return 0;
        }
        break;
    case WM_SIZE:
        if (a && a->hwnd && wp != SIZE_MINIMIZED) {
            a->r.Resize(LOWORD(lp), HIWORD(lp));
            a->sessionDirty = true;
            a->Frame(); // live resize
        }
        return 0;
    case WM_DPICHANGED:
        if (a) {
            a->ApplyDpi((f32)HIWORD(wp));
            RECT *rc = (RECT *)lp;
            SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        f32 s = a ? a->S : 1;
        mm->ptMinTrackSize.x = (LONG)(480 * s);
        mm->ptMinTrackSize.y = (LONG)(320 * s);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        if (a && a->hwnd) a->Frame();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        if (a) a->Invalidate();
        break;
    case WM_SETTINGCHANGE:
        if (a && lp && wcscmp((const wchar_t *)lp, L"ImmersiveColorSet") == 0 && a->settings.colorScheme == SCHEME_SYSTEM)
            a->ApplySettings();
        break;
    case WM_MOUSEMOVE:
        if (a) {
            if (!a->in.mouseInWindow) {
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                a->in.mouseInWindow = true;
            }
            a->in.mx = (f32)GET_X_LPARAM(lp);
            a->in.my = (f32)GET_Y_LPARAM(lp);
            a->in.ctrl = GetKeyState(VK_CONTROL) < 0;
            a->in.shift = GetKeyState(VK_SHIFT) < 0;
            a->Invalidate();
        }
        return 0;
    case WM_MOUSELEAVE:
        if (a) {
            a->in.mouseInWindow = false;
            if (!a->in.down[0]) { a->in.mx = a->in.my = -10000; }
            a->Invalidate();
        }
        return 0;
    case WM_LBUTTONDOWN: if (a) MouseButton(a, hwnd, 0, true, false, lp); return 0;
    case WM_LBUTTONDBLCLK: if (a) MouseButton(a, hwnd, 0, true, true, lp); return 0;
    case WM_LBUTTONUP: if (a) MouseButton(a, hwnd, 0, false, false, lp); return 0;
    case WM_MBUTTONDOWN: if (a) MouseButton(a, hwnd, 1, true, false, lp); return 0;
    case WM_MBUTTONUP: if (a) MouseButton(a, hwnd, 1, false, false, lp); return 0;
    case WM_RBUTTONDOWN: if (a) MouseButton(a, hwnd, 2, true, false, lp); return 0;
    case WM_RBUTTONUP: if (a) MouseButton(a, hwnd, 2, false, false, lp); return 0;
    case WM_CAPTURECHANGED:
        if (a && (HWND)lp != hwnd) {
            for (int i = 0; i < 3; i++) if (a->in.down[i]) { a->in.down[i] = false; a->in.released[i] = true; }
            a->Invalidate();
        }
        return 0;
    case WM_XBUTTONUP:
        if (a && a->popup.kind == POPUP_NONE) {
            if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) a->RunCommand(L"MoveFolderBack");
            else a->RunCommand(L"MoveFolderForward");
        }
        return TRUE;
    case WM_MOUSEWHEEL:
        if (a) { a->in.wheel += (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; a->Invalidate(); }
        return 0;
    case WM_MOUSEHWHEEL:
        if (a) { a->in.hwheel += (f32)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; a->Invalidate(); }
        return 0;
    case WM_SETCURSOR:
        if (a && LOWORD(lp) == HTCLIENT) {
            LPCWSTR c = IDC_ARROW;
            switch (a->cursorKind) {
            case 1: c = IDC_SIZEWE; break;
            case 2: c = IDC_SIZENS; break;
            case 3: c = IDC_IBEAM; break;
            case 4: c = IDC_HAND; break;
            }
            SetCursor(LoadCursorW(nullptr, c));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (!a) break;
        UINT vk = (UINT)wp;
        if (vk == VK_PROCESSKEY) return 0; // the IME owns this keystroke (한글 조합 중)
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_HANGUL || vk == VK_HANJA)
            break;
        KeyEvent k = MakeKey(vk, lp);
        if (msg == WM_SYSKEYDOWN && vk == VK_F4) break;               // Alt+F4
        if (msg == WM_SYSKEYDOWN && vk == VK_SPACE) break;            // system menu
        a->OnKey(k);
        return 0;
    }
    case WM_SYSCHAR:
        return 0; // suppress beep for Alt+key hotkeys
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) return 0; // a lone Alt tap must not steal focus into the system menu
        break;
    case WM_CHAR:
        if (a) {
            static wchar_t high = 0;
            wchar_t c = (wchar_t)wp;
            if (c >= 0xD800 && c <= 0xDBFF) { high = c; return 0; }
            std::wstring s;
            if (c >= 0xDC00 && c <= 0xDFFF && high) { s += high; s += c; high = 0; }
            else if (c >= 32 && c != 127) s += c;
            if (!s.empty() && GetKeyState(VK_CONTROL) >= 0) a->OnText(s);
        }
        return 0;
    // ---------------- IME (Korean composition) ----------------
    case WM_IME_SETCONTEXT:
        lp &= ~ISC_SHOWUICOMPOSITIONWINDOW; // we draw the composition inline ourselves
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_IME_STARTCOMPOSITION:
        if (a) a->OnImeComposition(L"", 0);
        return 0;
    case WM_IME_COMPOSITION:
        if (a) {
            if (lp & GCS_RESULTSTR) {
                std::wstring res = ImeString(hwnd, GCS_RESULTSTR);
                a->ime.composing = false;
                a->ime.comp.clear();
                a->OnText(res);
            }
            if (lp & GCS_COMPSTR) {
                std::wstring comp = ImeString(hwnd, GCS_COMPSTR);
                a->OnImeComposition(comp, (lp & GCS_CURSORPOS) ? ImeCursor(hwnd) : (int)comp.size());
            }
        }
        return 0;
    case WM_IME_ENDCOMPOSITION:
        if (a) a->OnImeEnd();
        return 0;
    case WM_IME_CHAR:
        return 0; // already delivered via GCS_RESULTSTR
    case WM_IME_NOTIFY:
        break;
    case WM_APP_MAINQUEUE:
        MainDrain();
        if (a) a->Invalidate();
        return 0;
    case WM_CLIPBOARDUPDATE:
        if (a) {
            if (!a->cutPaths.empty() && !ClipboardHasFiles()) a->cutPaths.clear();
            a->Invalidate();
        }
        return 0;
    case WM_CLOSE:
        if (a) {
            a->Shutdown();
            a->hwnd = nullptr;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    bool setupRequested = false;
    bool quiet = false;
    bool openVerb = false; // started by the folder/drive "open" verb (default file manager)
    for (int i = 1; i < argc; i++) if (std::wstring(argv[i]) == L"--quiet") quiet = true;
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"--install") {
            // unattended install: --install [--dir=PATH] [--desktop] [--default] [--no-menu] [--no-path] [--quiet]
            I18nSetLanguage(I18nDetectSystemLanguage());
            InstallOptions o = QueryInstall().opts;
            for (int j = 1; j < argc; j++) {
                std::wstring a = argv[j];
                if (a.rfind(L"--dir=", 0) == 0) o.dir = a.substr(6);
                else if (a == L"--desktop") o.desktopShortcut = true;
                else if (a == L"--default") o.setDefault = true;
                else if (a == L"--no-menu") o.contextMenu = false;
                else if (a == L"--no-path") o.addToPath = false;
            }
            std::wstring err;
            bool ok = PerformInstall(o, &err);
            if (!quiet) MessageBoxW(nullptr, ok ? TL(L"TwinDeck was installed.", L"TwinDeck을 설치했습니다.") : err.c_str(), APP_NAME,
                                    ok ? MB_ICONINFORMATION : MB_ICONERROR);
            LocalFree(argv);
            OleUninitialize();
            return ok ? 0 : 1;
        }
        if (arg == L"--uninstall") {
            // "Uninstall" in Windows Settings > Apps
            I18nSetLanguage(I18nDetectSystemLanguage());
            if (quiet || MessageBoxW(nullptr, TL(L"Uninstall TwinDeck? Your settings and bookmarks are kept.",
                                        L"TwinDeck을 제거할까요? 설정과 즐겨찾기는 남겨 둡니다."),
                            APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                bool self = false;
                PerformUninstall(&self);
                if (self) ScheduleSelfDelete();
                if (!quiet) MessageBoxW(nullptr, T(S_Uninstalled), APP_NAME, MB_ICONINFORMATION);
            }
            LocalFree(argv);
            OleUninitialize();
            return 0;
        }
        if (arg == L"--setup") { setupRequested = true; continue; }
        if (arg == L"--open") { openVerb = true; continue; }
        if (arg == L"--root" || arg == L"--quiet") continue;
        path = arg;
        // a shell verb's "%V" for a drive root is "C:\", and its closing \" comes through as a literal quote: C:"
        if (!path.empty() && path.back() == L'"') path.back() = L'\\';
    }
    LocalFree(argv);
    // with TwinDeck as the default file manager, a folder opened inside File Explorer stays in that window
    if (openVerb && !path.empty() && NavigateForegroundExplorer(path)) {
        OleUninitialize();
        return 0;
    }

    App &app = g_appInstance;
    g_app = &app;
    if (!app.Init(inst, path, setupRequested)) {
        OleUninitialize();
        return 1;
    }
    AddClipboardFormatListener(app.hwnd);

    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running || !app.hwnd) break;
        if (app.needRedraw) {
            app.needRedraw = false;
            app.Frame();
            continue;
        }
        DWORD timeout = INFINITE;
        if (app.focus != FOCUS_LIST || app.popup.kind == POPUP_PALETTE || app.popup.kind == POPUP_GOTO) timeout = 265; // caret blink
        if (app.sizes.AnyRunning() || JobsPending() > 0) timeout = std::min<DWORD>(timeout, 120);
        if (!app.toasts.empty()) timeout = std::min<DWORD>(timeout, 50);
        DWORD r = MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_TIMEOUT) app.needRedraw = true;
    }
    g_app = nullptr;
    OleUninitialize();
    return 0;
}
