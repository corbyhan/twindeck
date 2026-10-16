// TwinDeckOpen: the folder / drive "open" verb while TwinDeck is the default file manager.
// A separate tiny program because the Windows 11 context menu shows the icon of the verb's program next to "Open";
// this one's icon is blank, so the menu keeps looking like Explorer's own Open.
//   inside File Explorer -> that Explorer window navigates (TwinDeck is not started)
//   anywhere else        -> TwinDeck.exe next to this program opens the folder
#include "../src/explorernav.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring path;
    for (int i = 1; i < argc; i++) if (argv[i][0] != L'-') path = argv[i];
    LocalFree(argv);
    // "%V" of a drive root is "C:\", and its closing \" comes through as a literal quote: C:"
    if (!path.empty() && path.back() == L'"') path.back() = L'\\';
    if (path.empty()) return 1;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool handled = NavigateForegroundExplorer(path);
    CoUninitialize();
    if (handled) return 0;

    wchar_t self[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, self, (DWORD)(sizeof(self) / sizeof(self[0])));
    std::wstring exe(self, n);
    exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"TwinDeck.exe";
    std::wstring arg = path;
    if (arg.back() == L'\\') arg += L'\\'; // keep the closing quote a quote
    std::wstring cmd = L"\"" + exe + L"\" \"" + arg + L"\"";
    AllowSetForegroundWindow(ASFW_ANY); // we were started from a click, so TwinDeck may come to the front
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
