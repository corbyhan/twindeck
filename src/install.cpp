// Per-user install / modify / uninstall (HKCU only, no admin required).
// The program itself is the installer: TwinDeck-Setup.exe (or --setup, or the first start) shows the setup screen.
#include "app.h"
#include "install.h"
#include <shlobj.h>

static const wchar_t *kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TwinDeck";
static const wchar_t *kVerb = L"OpenInTwinDeck";
static const wchar_t *kHereVerb = L"TwinDeckHere";
// "This PC" — Win+E and the Explorer taskbar button open it through this verb
static const wchar_t *kThisPcNewWindow =
    L"Software\\Classes\\CLSID\\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}\\shell\\opennewwindow";

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return std::wstring(buf, n);
}

std::wstring DefaultInstallDir() { return PathJoin(KnownFolderPath(FOLDERID_LocalAppData), L"Programs\\TwinDeck"); }

static std::wstring InstalledExe(const std::wstring &dir) { return PathJoin(dir, L"TwinDeck.exe"); }
static std::wstring InstalledOpener(const std::wstring &dir) { return PathJoin(dir, L"TwinDeckOpen.exe"); }

// TwinDeckOpen.exe is embedded in TwinDeck.exe (res/app.rc) so that the installer stays a single file
static const int kOpenerResource = 3;

static bool WriteOpener(const std::wstring &path) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(kOpenerResource), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    const void *data = mem ? LockResource(mem) : nullptr;
    DWORD size = res ? SizeofResource(nullptr, res) : 0;
    if (!data || !size) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data, size, &written, nullptr) && written == size;
    CloseHandle(f);
    return ok;
}

// ------------------------------------------------------------------ registry helpers
static bool SetStr(HKEY root, const std::wstring &key, const wchar_t *name, const std::wstring &value, DWORD type = REG_SZ) {
    HKEY h;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS s = RegSetValueExW(h, name, 0, type, (const BYTE *)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return s == ERROR_SUCCESS;
}

static void SetDword(HKEY root, const std::wstring &key, const wchar_t *name, DWORD v) {
    HKEY h;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(h, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(h);
}

static bool GetStr(HKEY root, const std::wstring &key, const wchar_t *name, std::wstring &out, DWORD *typeOut = nullptr) {
    HKEY h;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    bool ok = false;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(h, name, nullptr, &type, (BYTE *)&s[0], &size) == ERROR_SUCCESS) {
            s.resize(wcsnlen(s.c_str(), s.size()));
            out = s;
            ok = true;
            if (typeOut) *typeOut = type;
        }
    }
    RegCloseKey(h);
    return ok;
}

static DWORD GetDword(HKEY root, const std::wstring &key, const wchar_t *name, DWORD def) {
    DWORD v = def, size = sizeof(v), type = 0;
    HKEY h;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return def;
    if (RegQueryValueExW(h, name, nullptr, &type, (BYTE *)&v, &size) != ERROR_SUCCESS || type != REG_DWORD) v = def;
    RegCloseKey(h);
    return v;
}

static void DeleteValue(HKEY root, const std::wstring &key, const wchar_t *name) {
    HKEY h;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_WRITE, &h) != ERROR_SUCCESS) return;
    RegDeleteValueW(h, name);
    RegCloseKey(h);
}

// removes a key that we created only if nothing else lives in it (no subkeys, no values)
static void DeleteKeyIfEmpty(const std::wstring &key) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    DWORD subkeys = 0, values = 0;
    RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(h);
    if (subkeys == 0 && values == 0) RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
}

static void RegisterVerb(const std::wstring &cls, const std::wstring &verb, const std::wstring &label, const std::wstring &exe) {
    std::wstring key = L"Software\\Classes\\" + cls + L"\\shell\\" + verb;
    SetStr(HKEY_CURRENT_USER, key, L"MUIVerb", label);
    SetStr(HKEY_CURRENT_USER, key, L"Icon", L"\"" + exe + L"\",0");
    SetStr(HKEY_CURRENT_USER, key + L"\\command", nullptr, L"\"" + exe + L"\" \"%V\"");
}

static void RemoveVerb(const std::wstring &cls, const std::wstring &verb) {
    RegDeleteTreeW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + cls + L"\\shell\\" + verb).c_str());
}

// ------------------------------------------------------------------ shortcuts
static bool CreateShortcut(const std::wstring &lnk, const std::wstring &target) {
    IShellLinkW *sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(PathParent(target).c_str());
    sl->SetDescription(L"TwinDeck - 빠른 파일 관리자");
    IPersistFile *pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

static std::wstring StartMenuLink() { return PathJoin(KnownFolderPath(FOLDERID_Programs), L"TwinDeck.lnk"); }
static std::wstring DesktopLink() { return PathJoin(KnownFolderPath(FOLDERID_Desktop), L"TwinDeck.lnk"); }

// ------------------------------------------------------------------ user PATH
static std::wstring TrimSlash(std::wstring s) {
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

static void BroadcastEnvironment() {
    DWORD_PTR r;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &r);
}

static bool UpdateUserPath(const std::wstring &dir, bool add) {
    std::wstring cur;
    DWORD type = REG_EXPAND_SZ;
    GetStr(HKEY_CURRENT_USER, L"Environment", L"Path", cur, &type);
    std::vector<std::wstring> parts;
    bool found = false;
    size_t start = 0;
    while (start <= cur.size()) {
        size_t semi = cur.find(L';', start);
        std::wstring p = cur.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
        if (!p.empty()) {
            if (_wcsicmp(TrimSlash(p).c_str(), TrimSlash(dir).c_str()) == 0) { found = true; if (!add) p.clear(); }
            if (!p.empty()) parts.push_back(p);
        }
        if (semi == std::wstring::npos) break;
        start = semi + 1;
    }
    if (add == found) return true; // already in the wanted state
    if (add) parts.push_back(dir);
    std::wstring out;
    for (auto &p : parts) { if (!out.empty()) out += L";"; out += p; }
    bool ok = SetStr(HKEY_CURRENT_USER, L"Environment", L"Path", out, type == REG_SZ ? REG_SZ : REG_EXPAND_SZ);
    BroadcastEnvironment();
    return ok;
}

// ------------------------------------------------------------------ query
bool IsInstalled() {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}

InstallInfo QueryInstall() {
    InstallInfo info;
    info.opts.dir = DefaultInstallDir();
    if (!IsInstalled()) return info;
    info.installed = true;
    GetStr(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", info.opts.dir);
    GetStr(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", info.version);
    info.opts.desktopShortcut = GetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptDesktopShortcut", 0) != 0;
    info.opts.setDefault = GetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptDefaultFileManager", 0) != 0;
    info.opts.contextMenu = GetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptContextMenu", 1) != 0;
    info.opts.addToPath = GetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptAddToPath", 0) != 0;
    return info;
}

bool IsRunningInstalledCopy() {
    InstallInfo info = QueryInstall();
    return info.installed && _wcsicmp(SelfPath().c_str(), InstalledExe(info.opts.dir).c_str()) == 0;
}

// ------------------------------------------------------------------ integration on / off
static void ApplyIntegration(const InstallOptions &o, const std::wstring &exe) {
    // folder / drive verb shown in the right-click menu
    for (const wchar_t *cls : {L"Directory", L"Drive"}) {
        if (o.contextMenu) RegisterVerb(cls, kVerb, T(S_OpenInApp), exe);
        else RemoveVerb(cls, kVerb);
    }
    for (const wchar_t *cls : {L"Directory\\Background", L"DesktopBackground"}) {
        if (o.contextMenu) RegisterVerb(cls, kHereVerb, T(S_AppHere), exe);
        else RemoveVerb(cls, kHereVerb);
    }

    // default file manager: folders/drives open in TwinDeck through their "open" verb (it takes the place of
    // Explorer's own Open in the menu), and Win+E opens TwinDeck. The verb runs TwinDeckOpen.exe, which hands
    // double-clicks inside File Explorer back to it; its blank icon keeps our app icon off the Windows 11 menu's Open.
    std::wstring opener = InstalledOpener(PathParent(exe));
    std::wstring openVerbCmd = L"\"" + exe + L"\" --open \"%V\""; // same behavior, only with our icon in the menu
    if (o.setDefault && WriteOpener(opener)) openVerbCmd = L"\"" + opener + L"\" \"%V\"";
    else if (!o.setDefault) DeleteFileW(opener.c_str());
    for (const wchar_t *cls : {L"Directory", L"Drive"}) {
        std::wstring shellKey = std::wstring(L"Software\\Classes\\") + cls + L"\\shell";
        std::wstring openKey = shellKey + L"\\open";
        std::wstring cur, openCmd;
        bool ours = GetStr(HKEY_CURRENT_USER, openKey + L"\\command", nullptr, openCmd) &&
                    (ContainsNoCase(openCmd, L"\\TwinDeck.exe") || ContainsNoCase(openCmd, L"\\TwinDeckOpen.exe"));
        // the default verb is named explicitly: the system's "none" would let ShellExecute fall through to Folder's open
        // (older versions made our menu verb the default instead)
        bool oursDefault = GetStr(HKEY_CURRENT_USER, shellKey, nullptr, cur) && (cur == kVerb || (cur == L"open" && ours));
        if (o.setDefault) {
            SetStr(HKEY_CURRENT_USER, openKey + L"\\command", nullptr, openVerbCmd);
            SetStr(HKEY_CURRENT_USER, openKey + L"\\command", L"DelegateExecute", L"");
            SetStr(HKEY_CURRENT_USER, shellKey, nullptr, L"open");
        } else {
            if (oursDefault) DeleteValue(HKEY_CURRENT_USER, shellKey, nullptr);
            if (ours) RegDeleteTreeW(HKEY_CURRENT_USER, openKey.c_str());
        }
    }
    std::wstring cmd;
    bool oursWinE = GetStr(HKEY_CURRENT_USER, std::wstring(kThisPcNewWindow) + L"\\command", nullptr, cmd) &&
                    ContainsNoCase(cmd, L"TwinDeck.exe");
    if (o.setDefault) {
        SetStr(HKEY_CURRENT_USER, std::wstring(kThisPcNewWindow) + L"\\command", nullptr, L"\"" + exe + L"\"");
        SetStr(HKEY_CURRENT_USER, std::wstring(kThisPcNewWindow) + L"\\command", L"DelegateExecute", L"");
    } else if (oursWinE) {
        RegDeleteTreeW(HKEY_CURRENT_USER, kThisPcNewWindow);
        std::wstring shellKey = PathParent(kThisPcNewWindow);
        DeleteKeyIfEmpty(shellKey);
        DeleteKeyIfEmpty(PathParent(shellKey));
    }

    if (o.desktopShortcut) CreateShortcut(DesktopLink(), exe);
    else DeleteFileW(DesktopLink().c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// ------------------------------------------------------------------ install / modify
static bool CopyExe(const std::wstring &self, const std::wstring &exe, std::wstring *err) {
    if (_wcsicmp(self.c_str(), exe.c_str()) == 0) return true;
    if (CopyFileW(self.c_str(), exe.c_str(), FALSE)) return true;
    // the installed copy may be running: a running exe can still be renamed, so move it aside
    std::wstring old = exe + L".old";
    DeleteFileW(old.c_str());
    if (MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) && CopyFileW(self.c_str(), exe.c_str(), FALSE)) return true;
    if (err) *err = Format(TL(L"Could not copy the program (error %u).", L"프로그램을 복사하지 못했습니다 (오류 %u)."), GetLastError());
    return false;
}

static void RemoveProgramFiles(const std::wstring &dir) {
    std::wstring exe = InstalledExe(dir);
    if (_wcsicmp(SelfPath().c_str(), exe.c_str()) != 0) DeleteFileW(exe.c_str());
    DeleteFileW((exe + L".old").c_str());
    DeleteFileW(InstalledOpener(dir).c_str());
    RemoveDirectoryW(dir.c_str()); // only succeeds when empty
}

bool PerformInstall(const InstallOptions &in, std::wstring *err) {
    InstallOptions o = in;
    o.dir = TrimSlash(NormalizePath(o.dir));
    if (o.dir.size() < 3 || o.dir[1] != L':') {
        if (err) *err = TL(L"Choose a full folder path, e.g. C:\\Tools\\TwinDeck.", L"전체 폴더 경로를 입력하세요. 예: C:\\Tools\\TwinDeck");
        return false;
    }
    InstallInfo prev = QueryInstall();
    int rc = SHCreateDirectoryExW(nullptr, o.dir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) {
        if (err) *err = Format(TL(L"Could not create the folder (error %d).", L"폴더를 만들지 못했습니다 (오류 %d)."), rc);
        return false;
    }
    std::wstring exe = InstalledExe(o.dir);
    if (!CopyExe(SelfPath(), exe, err)) return false;

    // moved to another folder: clean up the old location
    bool moved = prev.installed && _wcsicmp(TrimSlash(prev.opts.dir).c_str(), o.dir.c_str()) != 0;
    if (moved) {
        UpdateUserPath(prev.opts.dir, false);
        RemoveProgramFiles(prev.opts.dir);
    }

    ApplyIntegration(o, exe);
    UpdateUserPath(o.dir, o.addToPath);
    CreateShortcut(StartMenuLink(), exe);

    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"DisplayName", L"TwinDeck");
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", APP_VERSION);
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"Publisher", L"TwinDeck");
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"DisplayIcon", L"\"" + exe + L"\",0");
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", o.dir);
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"UninstallString", L"\"" + exe + L"\" --uninstall");
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"ModifyPath", L"\"" + exe + L"\" --setup");
    SYSTEMTIME st;
    GetLocalTime(&st);
    SetStr(HKEY_CURRENT_USER, kUninstallKey, L"InstallDate", Format(L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay));
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &fa))
        SetDword(HKEY_CURRENT_USER, kUninstallKey, L"EstimatedSize", (DWORD)((((u64)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) / 1024));
    SetDword(HKEY_CURRENT_USER, kUninstallKey, L"NoRepair", 1);
    SetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptDesktopShortcut", o.desktopShortcut);
    SetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptDefaultFileManager", o.setDefault);
    SetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptContextMenu", o.contextMenu);
    SetDword(HKEY_CURRENT_USER, kUninstallKey, L"OptAddToPath", o.addToPath);
    Log(L"Installed to %s (desktop %d, default %d, menu %d, path %d)", o.dir.c_str(), o.desktopShortcut, o.setDefault,
        o.contextMenu, o.addToPath);
    return true;
}

// ------------------------------------------------------------------ uninstall
bool PerformUninstall(bool *selfWasInstalled) {
    InstallInfo info = QueryInstall();
    InstallOptions none;
    none.contextMenu = none.setDefault = none.desktopShortcut = none.addToPath = false;
    std::wstring exe = InstalledExe(info.opts.dir);
    ApplyIntegration(none, exe);
    UpdateUserPath(info.opts.dir, false);
    DeleteFileW(StartMenuLink().c_str());
    DeleteFileW(DesktopLink().c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    bool self = _wcsicmp(SelfPath().c_str(), exe.c_str()) == 0;
    if (selfWasInstalled) *selfWasInstalled = self;
    RemoveProgramFiles(info.opts.dir);
    Log(L"Uninstalled from %s", info.opts.dir.c_str());
    return true;
}

// deletes the running installed copy once this process has exited
void ScheduleSelfDelete() {
    std::wstring self = SelfPath();
    std::wstring dir = PathParent(self);
    std::wstring cmd = Format(L"cmd.exe /C ping 127.0.0.1 -n 3 > nul & del /F /Q \"%s\" \"%s.old\" & rmdir \"%s\"",
                              self.c_str(), self.c_str(), dir.c_str());
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void CleanupAfterUpdate() { DeleteFileW((SelfPath() + L".old").c_str()); }

bool LaunchInstalled() {
    InstallInfo info = QueryInstall();
    if (!info.installed) return false;
    std::wstring exe = InstalledExe(info.opts.dir);
    return (INT_PTR)ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, info.opts.dir.c_str(), SW_SHOWNORMAL) > 32;
}
