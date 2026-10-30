// TwinDeckOpen: the folder / drive "open" verb while TwinDeck is the default file manager.
// A separate tiny program because the Windows 11 context menu shows the icon of the verb's program next to "Open";
// this one's icon is blank, so the menu keeps looking like Explorer's own Open.
//   inside File Explorer -> that Explorer window navigates (TwinDeck is not started)
//   anywhere else        -> TwinDeck.exe next to this program opens the folder
// With --winkey it instead stays running (started at sign-in) and opens TwinDeck for Win+E: newer Windows 11 builds
// open File Explorer for Win+E without looking at the registry verb TwinDeck sets, so only a keyboard hook can.
#include "../src/explorernav.h"
#include <shellapi.h>

#define WINKEY_CLASS L"TwinDeckWinKey" // install.cpp finds the running instance by this class to stop it
#define WM_APP_LAUNCH (WM_APP + 1)

// starts TwinDeck.exe next to this program; arg is the already quoted command-line tail ("" for none)
static bool LaunchTwinDeck(const std::wstring &arg) {
    wchar_t self[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, self, (DWORD)(sizeof(self) / sizeof(self[0])));
    std::wstring exe(self, n);
    exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"TwinDeck.exe";
    std::wstring cmd = L"\"" + exe + L"\"" + (arg.empty() ? L"" : L" " + arg);
    AllowSetForegroundWindow(ASFW_ANY); // we were started from a click or key, so TwinDeck may come to the front
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ------------------------------------------------------------------ Win+E hook
static HWND g_wnd;
static bool g_eSwallowed; // E went down as part of Win+E: its repeats and release are ours too

static bool KeyHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
    if (code == HC_ACTION && k->vkCode == 'E' && !(k->flags & LLKHF_INJECTED)) {
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        if (g_eSwallowed) {
            if (!down) g_eSwallowed = false;
            return 1;
        }
        if (down && (KeyHeld(VK_LWIN) || KeyHeld(VK_RWIN)) && !KeyHeld(VK_CONTROL) && !KeyHeld(VK_MENU) && !KeyHeld(VK_SHIFT)) {
            g_eSwallowed = true;
            // Win released without another key in between opens the Start menu: slip in an unassigned key first
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wVk = in[1].ki.wVk = 0xE8;
            in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
            PostMessageW(g_wnd, WM_APP_LAUNCH, 0, 0); // keep the hook fast: Windows drops hooks that stall
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK WinKeyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_LAUNCH:
        LaunchTwinDeck(L"");
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int RunWinKey(HINSTANCE inst) {
    HANDLE once = CreateMutexW(nullptr, FALSE, L"Local\\TwinDeckWinKey");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WinKeyProc;
    wc.hInstance = inst;
    wc.lpszClassName = WINKEY_CLASS;
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, WINKEY_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    HHOOK hook = g_wnd ? SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, inst, 0) : nullptr;
    if (!hook) return 1;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    UnhookWindowsHookEx(hook);
    CloseHandle(once);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    bool winKey = false;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != L'-') path = argv[i];
        else if (wcscmp(argv[i], L"--winkey") == 0) winKey = true;
    }
    LocalFree(argv);
    if (winKey) return RunWinKey(inst);
    // "%V" of a drive root is "C:\", and its closing \" comes through as a literal quote: C:"
    if (!path.empty() && path.back() == L'"') path.back() = L'\\';
    if (path.empty()) return 1;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool handled = NavigateForegroundExplorer(path);
    CoUninitialize();
    if (handled) return 0;

    std::wstring arg = path;
    if (arg.back() == L'\\') arg += L'\\'; // keep the closing quote a quote
    return LaunchTwinDeck(L"\"" + arg + L"\"") ? 0 : 1;
}
