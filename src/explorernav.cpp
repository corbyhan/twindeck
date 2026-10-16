// Hands a folder opened inside File Explorer back to that window.
// Shared by TwinDeck and TwinDeckOpen (the small program behind the folder "open" verb), so plain Win32 only.
#include "explorernav.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shlguid.h>
#include <propvarutil.h>
#include <iterator>

// A folder double-clicked inside File Explorer reaches us through the "open" verb while that window is still in
// front: move its active tab to the folder, as Explorer itself would.
bool NavigateForegroundExplorer(const std::wstring &path) {
    HWND fg = GetForegroundWindow();
    wchar_t cls[64];
    if (!fg || !GetClassNameW(fg, cls, (int)std::size(cls)) || wcscmp(cls, L"CabinetWClass") != 0) return false;
    HWND activeTab = FindWindowExW(fg, nullptr, L"ShellTabWindowClass", nullptr); // Windows 11 tabs: the front one comes first
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return false;
    IShellWindows *windows = nullptr;
    bool done = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        long count = 0;
        windows->get_Count(&count);
        for (long i = 0; i < count && !done; i++) {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;
            IDispatch *disp = nullptr;
            if (windows->Item(index, &disp) != S_OK || !disp) continue;
            IWebBrowser2 *browser = nullptr;
            if (SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&browser)))) {
                SHANDLE_PTR h = 0;
                bool match = SUCCEEDED(browser->get_HWND(&h)) && (HWND)h == fg;
                if (match && activeTab) {
                    IShellBrowser *sb = nullptr;
                    HWND tab = nullptr;
                    if (SUCCEEDED(IUnknown_QueryService(browser, SID_STopLevelBrowser, IID_PPV_ARGS(&sb)))) {
                        sb->GetWindow(&tab);
                        sb->Release();
                    }
                    match = tab == activeTab;
                }
                if (match) {
                    VARIANT target, empty;
                    VariantInit(&empty);
                    if (SUCCEEDED(InitVariantFromBuffer(pidl, ILGetSize(pidl), &target))) {
                        done = SUCCEEDED(browser->Navigate2(&target, &empty, &empty, &empty, &empty));
                        VariantClear(&target);
                    }
                }
                browser->Release();
            }
            disp->Release();
        }
        windows->Release();
    }
    CoTaskMemFree(pidl);
    return done;
}
