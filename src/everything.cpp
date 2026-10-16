// Subfolder search through Everything (voidtools, https://www.voidtools.com) when it is running: its NTFS index
// answers at once where walking the folders takes seconds to minutes. The SDK's client source is compiled in
// (third_party/everything, MIT); it talks to Everything over window messages, so nothing is installed for it.
#include "everything.h"
#include <mutex>

#define EVERYTHINGUSERAPI // compiled into the program, not imported from Everything64.dll
#include "../third_party/everything/include/Everything.h"

// only where Everything indexes by default: local fixed NTFS volumes
static bool LocalNtfs(const std::wstring &root) {
    if (root.size() < 3 || root[1] != L':') return false;
    std::wstring drive = root.substr(0, 3);
    if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) return false;
    wchar_t fs[32] = {};
    return GetVolumeInformationW(drive.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, 32) && _wcsicmp(fs, L"NTFS") == 0;
}

static bool HasHangulJamo(const std::wstring &s) {
    for (wchar_t c : s) if (c >= 0x3131 && c <= 0x318E) return true; // ㄱ…ㅣ: initials search is ours only
    return false;
}

static u64 FileTimeU64(const FILETIME &ft) { return ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }

bool EverythingSearch(const std::wstring &root, const std::wstring &pattern, bool showHidden, u32 maxResults,
                      std::vector<FileEntry> &out) {
    if (pattern.empty() || HasHangulJamo(pattern) || !LocalNtfs(root)) return false;
    if (!FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION", nullptr)) return false; // Everything is not running

    static std::mutex lock; // the SDK keeps one query state per process
    std::lock_guard<std::mutex> guard(lock);
    if (!Everything_IsDBLoaded()) return false;

    // "E:\dir\" limits the search to that folder's subtree (a term with a backslash matches the full path);
    // the user's words match file names, wildcards whole names, and ';' becomes Everything's OR
    std::wstring dir = root;
    if (dir.back() != L'\\') dir += L'\\';
    std::wstring terms = pattern;
    for (auto &c : terms) if (c == L';') c = L'|';
    std::wstring query = L"\"" + dir + L"\" " + terms;

    Everything_SetSearchW(query.c_str());
    Everything_SetMatchPath(FALSE);
    Everything_SetMatchCase(FALSE);
    Everything_SetMatchWholeWord(FALSE);
    Everything_SetRegex(FALSE);
    Everything_SetOffset(0);
    Everything_SetMax(maxResults);
    Everything_SetRequestFlags(EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_PATH | EVERYTHING_REQUEST_SIZE |
                               EVERYTHING_REQUEST_DATE_CREATED | EVERYTHING_REQUEST_DATE_MODIFIED |
                               EVERYTHING_REQUEST_ATTRIBUTES);
    if (!Everything_QueryW(TRUE)) return false;

    DWORD n = Everything_GetNumResults();
    out.clear();
    out.reserve(n);
    std::wstring rootNoSlash = dir.substr(0, dir.size() - 1);
    for (DWORD i = 0; i < n; i++) {
        const wchar_t *name = Everything_GetResultFileNameW(i), *path = Everything_GetResultPathW(i);
        if (!name || !path) continue;
        FileEntry e;
        e.name = name;
        e.isDir = Everything_IsFolderResult(i) != FALSE;
        DWORD attrs = Everything_GetResultAttributes(i);
        e.attrs = attrs == INVALID_FILE_ATTRIBUTES ? (e.isDir ? FILE_ATTRIBUTE_DIRECTORY : 0) : attrs;
        if (!showHidden && e.Hidden()) continue;
        LARGE_INTEGER size;
        if (!e.isDir && Everything_GetResultSize(i, &size) && size.QuadPart > 0) e.size = (u64)size.QuadPart;
        FILETIME ft;
        if (Everything_GetResultDateModified(i, &ft)) e.modified = FileTimeU64(ft);
        if (Everything_GetResultDateCreated(i, &ft)) e.created = FileTimeU64(ft);
        // the folder the result is in, relative to root ("" = directly in root)
        std::wstring parent = path;
        if (_wcsicmp(parent.c_str(), rootNoSlash.c_str()) == 0 || _wcsicmp(parent.c_str(), dir.c_str()) == 0) e.relDir = L"";
        else if (parent.size() > dir.size() && _wcsnicmp(parent.c_str(), dir.c_str(), dir.size()) == 0) e.relDir = parent.substr(dir.size());
        else continue; // outside the folder (the path term also matches elsewhere in a full path)
        out.push_back(std::move(e));
    }
    return true;
}
