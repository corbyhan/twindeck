#include "fs.h"
#include "jobs.h"
#include <winternl.h>
#include <winioctl.h>

// ---------------- NT directory query ----------------
typedef NTSTATUS(NTAPI *NtQueryDirectoryFileFn)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG,
                                                 BOOLEAN, PUNICODE_STRING, BOOLEAN);
static NtQueryDirectoryFileFn g_NtQueryDirectoryFile = nullptr;

#define FileFullDirectoryInformationClass 2
#define FileIdExtdDirectoryInformationClass 60
#define NT_STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006L)
#define NT_STATUS_INVALID_INFO_CLASS ((NTSTATUS)0xC0000003L)
#define NT_STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define NT_STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)

#pragma pack(push, 8)
struct NtDirInfoExtd {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER EndOfFile, AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    ULONG ReparsePointTag;
    BYTE FileId[16];
    WCHAR FileName[1];
};
struct NtDirInfoFull {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER EndOfFile, AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize; // holds the reparse tag for reparse points
    WCHAR FileName[1];
};
#pragma pack(pop)

void FsInit() {
    OSVERSIONINFOEXW vi = {sizeof(vi)};
    vi.dwMajorVersion = 6;
    vi.dwMinorVersion = 2;
    DWORDLONG mask = 0;
    mask = VerSetConditionMask(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    mask = VerSetConditionMask(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    if (VerifyVersionInfoW(&vi, VER_MAJORVERSION | VER_MINORVERSION, mask)) {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) g_NtQueryDirectoryFile = (NtQueryDirectoryFileFn)GetProcAddress(nt, "NtQueryDirectoryFile");
    }
    Log(L"NtQueryDirectoryFile: %s", g_NtQueryDirectoryFile ? L"available" : L"unavailable");
}

bool FsHasNtQuery() { return g_NtQueryDirectoryFile != nullptr; }

std::wstring NormalizePath(const std::wstring &in) {
    if (in.empty()) return in;
    std::wstring p = in;
    // strip quotes
    if (p.size() >= 2 && p.front() == L'"' && p.back() == L'"') p = p.substr(1, p.size() - 2);
    if (p.find(L'%') != std::wstring::npos) {
        wchar_t buf[4096];
        DWORD n = ExpandEnvironmentStringsW(p.c_str(), buf, _countof(buf));
        if (n > 0 && n < _countof(buf)) p = buf;
    }
    for (auto &c : p) if (c == L'/') c = L'\\';
    if (p == L"~") {
        wchar_t buf[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) p = buf;
    }
    if (p.size() == 2 && p[1] == L':') p += L'\\';
    if (p.size() >= 2 && p[1] == L':') p[0] = (wchar_t)towupper(p[0]);
    // resolve relative components
    if (p.find(L"\\.") != std::wstring::npos && p.rfind(L"\\\\?\\", 0) != 0) {
        wchar_t buf[4096];
        DWORD n = GetFullPathNameW(p.c_str(), _countof(buf), buf, nullptr);
        if (n > 0 && n < _countof(buf)) p = buf;
    }
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();
    return p;
}

std::wstring LongPathPrefix(const std::wstring &p) {
    if (p.size() < 240 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}

static inline u64 LI(const LARGE_INTEGER &l) { return (u64)l.QuadPart; }

static ListError MapError(DWORD e) {
    if (e == ERROR_ACCESS_DENIED) return LIST_ACCESS_DENIED;
    if (e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND || e == ERROR_INVALID_NAME || e == ERROR_BAD_NETPATH ||
        e == ERROR_DIRECTORY || e == ERROR_NOT_READY)
        return LIST_NOT_FOUND;
    return LIST_OTHER;
}

static ListError ListWin32(const std::wstring &dir, std::vector<FileEntry> &out) {
    WIN32_FIND_DATAW fd;
    std::wstring pat = LongPathPrefix(PathJoin(dir, L"*"));
    HANDLE h = FindFirstFileExW(pat.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        return e == ERROR_FILE_NOT_FOUND ? LIST_OK : MapError(e);
    }
    do {
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
        FileEntry e;
        e.name = fd.cFileName;
        e.attrs = fd.dwFileAttributes;
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = e.isDir ? 0 : (((u64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        e.created = ((u64)fd.ftCreationTime.dwHighDateTime << 32) | fd.ftCreationTime.dwLowDateTime;
        e.modified = ((u64)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        e.accessed = ((u64)fd.ftLastAccessTime.dwHighDateTime << 32) | fd.ftLastAccessTime.dwLowDateTime;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) e.reparseTag = fd.dwReserved0;
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return LIST_OK;
}

static HANDLE OpenDirHandle(const std::wstring &dir, DWORD *err) {
    HANDLE h = CreateFileW(LongPathPrefix(dir).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE && err) *err = GetLastError();
    return h;
}

// Iterates NT directory records; returns false if the info class is unsupported (caller falls back)
template <typename F>
static bool NtIterate(HANDLE h, void *buf, ULONG bufSize, F &&fn, NTSTATUS *statusOut) {
    IO_STATUS_BLOCK iosb;
    ULONG cls = FileIdExtdDirectoryInformationClass;
    bool restart = true;
    for (;;) {
        memset(&iosb, 0, sizeof(iosb));
        NTSTATUS st = g_NtQueryDirectoryFile(h, nullptr, nullptr, nullptr, &iosb, buf, bufSize, cls, FALSE, nullptr, restart ? TRUE : FALSE);
        restart = false;
        if (st == NT_STATUS_NO_MORE_FILES) { *statusOut = 0; return true; }
        if (st < 0) {
            if (cls == FileIdExtdDirectoryInformationClass &&
                (st == NT_STATUS_INVALID_INFO_CLASS || st == NT_STATUS_INVALID_PARAMETER || st == NT_STATUS_NOT_SUPPORTED)) {
                cls = FileFullDirectoryInformationClass;
                restart = true;
                continue;
            }
            *statusOut = st;
            return false;
        }
        BYTE *p = (BYTE *)buf;
        for (;;) {
            FileEntry e;
            ULONG next;
            if (cls == FileIdExtdDirectoryInformationClass) {
                auto *d = (NtDirInfoExtd *)p;
                next = d->NextEntryOffset;
                ULONG len = d->FileNameLength / 2;
                if (!((len == 1 && d->FileName[0] == L'.') || (len == 2 && d->FileName[0] == L'.' && d->FileName[1] == L'.'))) {
                    e.name.assign(d->FileName, len);
                    e.attrs = d->FileAttributes;
                    e.isDir = (d->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    e.size = e.isDir ? 0 : LI(d->EndOfFile);
                    e.created = LI(d->CreationTime);
                    e.modified = LI(d->LastWriteTime);
                    e.accessed = LI(d->LastAccessTime);
                    e.reparseTag = (d->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? d->ReparsePointTag : 0;
                    fn(std::move(e));
                }
            } else {
                auto *d = (NtDirInfoFull *)p;
                next = d->NextEntryOffset;
                ULONG len = d->FileNameLength / 2;
                if (!((len == 1 && d->FileName[0] == L'.') || (len == 2 && d->FileName[0] == L'.' && d->FileName[1] == L'.'))) {
                    e.name.assign(d->FileName, len);
                    e.attrs = d->FileAttributes;
                    e.isDir = (d->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    e.size = e.isDir ? 0 : LI(d->EndOfFile);
                    e.created = LI(d->CreationTime);
                    e.modified = LI(d->LastWriteTime);
                    e.accessed = LI(d->LastAccessTime);
                    e.reparseTag = (d->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? d->EaSize : 0;
                    fn(std::move(e));
                }
            }
            if (!next) break;
            p += next;
        }
    }
}

ListError ListDirectory(const std::wstring &dir, std::vector<FileEntry> &out) {
    out.clear();
    if (g_NtQueryDirectoryFile) {
        DWORD err = 0;
        HANDLE h = OpenDirHandle(dir, &err);
        if (h == INVALID_HANDLE_VALUE) return MapError(err);
        thread_local std::vector<BYTE> buf(256 * 1024);
        NTSTATUS st = 0;
        bool ok = NtIterate(h, buf.data(), (ULONG)buf.size(), [&](FileEntry &&e) { out.push_back(std::move(e)); }, &st);
        CloseHandle(h);
        if (ok) return LIST_OK;
        out.clear();
        Log(L"NtQueryDirectoryFile failed (0x%08x) on %s, falling back", (u32)st, dir.c_str());
    }
    return ListWin32(dir, out);
}

static bool ShouldDescend(u32 attrs, u32 tag) {
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        // symlinks / junctions / app exec links are not followed (prevents cycles)
        if (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT || tag == 0x8000001B) return false;
    }
    return true;
}

u64 EnumerateRecursive(const std::wstring &root, const std::atomic<bool> &cancel,
                       const std::function<bool(const FileEntry &)> &accept,
                       const std::function<void(std::vector<FileEntry> &&)> &onChunk, u64 maxResults,
                       std::atomic<u64> *progress) {
    std::vector<std::wstring> stack;
    stack.push_back(L"");
    std::vector<FileEntry> chunk, list;
    u64 scanned = 0, results = 0;
    u64 lastFlush = GetTickCount64();
    while (!stack.empty() && !cancel.load()) {
        std::wstring rel = stack.back();
        stack.pop_back();
        std::wstring dir = rel.empty() ? root : PathJoin(root, rel);
        if (ListDirectory(dir, list) != LIST_OK) continue;
        for (auto &e : list) {
            scanned++;
            if (ShouldDescend(e.attrs, e.reparseTag)) stack.push_back(rel.empty() ? e.name : PathJoin(rel, e.name));
            if (accept(e)) {
                e.relDir = rel;
                chunk.push_back(std::move(e));
                results++;
            }
        }
        if (progress) progress->store(scanned);
        // a few matches in a big tree must show up right away, not when 2000 have piled up
        u64 now = GetTickCount64();
        if (chunk.size() >= 2000 || (!chunk.empty() && now - lastFlush >= 150)) {
            onChunk(std::move(chunk));
            chunk.clear();
            lastFlush = now;
        }
        if (results >= maxResults) break;
    }
    if (!chunk.empty()) onChunk(std::move(chunk));
    if (progress) progress->store(scanned);
    return scanned;
}

void ListDrives(std::vector<FileEntry> &out, bool queryVolumeInfo) {
    out.clear();
    wchar_t buf[512];
    DWORD n = GetLogicalDriveStringsW(_countof(buf), buf);
    for (wchar_t *p = buf; p < buf + n && *p; p += wcslen(p) + 1) {
        FileEntry e;
        e.isDrive = true;
        e.isDir = true;
        e.name = std::wstring(p).substr(0, 2); // "C:"
        e.driveType = GetDriveTypeW(p);
        if (queryVolumeInfo && e.driveType != DRIVE_CDROM) {
            wchar_t label[MAX_PATH] = {}, fs[64] = {};
            DWORD serial = 0, maxLen = 0, flags = 0;
            UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
            if (GetVolumeInformationW(p, label, MAX_PATH, &serial, &maxLen, &flags, fs, 64)) {
                e.label = label;
                e.fsName = fs;
            }
            ULARGE_INTEGER freeAvail, total, totalFree;
            if (GetDiskFreeSpaceExW(p, &freeAvail, &total, &totalFree)) {
                e.totalBytes = total.QuadPart;
                e.freeBytes = freeAvail.QuadPart;
            }
            SetErrorMode(oldMode);
        }
        if (e.driveType == DRIVE_REMOTE) {
            wchar_t remote[MAX_PATH]; DWORD len = MAX_PATH;
            if (WNetGetConnectionW(e.name.c_str(), remote, &len) == NO_ERROR && e.label.empty()) e.label = remote;
        }
        out.push_back(std::move(e));
    }
}

bool PathExists(const std::wstring &p) {
    return GetFileAttributesW(LongPathPrefix(p).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsDirectory(const std::wstring &p) {
    DWORD a = GetFileAttributesW(LongPathPrefix(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

u32 DriveTypeOf(const std::wstring &path) {
    if (path.rfind(L"\\\\", 0) == 0) return DRIVE_REMOTE;
    if (path.size() < 2 || path[1] != L':') return DRIVE_UNKNOWN;
    wchar_t root[4] = {path[0], L':', L'\\', 0};
    return GetDriveTypeW(root);
}

bool DiskFree(const std::wstring &path, u64 *freeBytes, u64 *totalBytes) {
    ULARGE_INTEGER fa, t, tf;
    if (!GetDiskFreeSpaceExW(path.c_str(), &fa, &t, &tf)) return false;
    if (freeBytes) *freeBytes = fa.QuadPart;
    if (totalBytes) *totalBytes = t.QuadPart;
    return true;
}

bool IsSSD(const std::wstring &path) {
    static std::mutex m;
    static std::unordered_map<wchar_t, bool> cache;
    if (path.size() < 2 || path[1] != L':') return false;
    wchar_t letter = (wchar_t)towupper(path[0]);
    {
        std::lock_guard<std::mutex> lk(m);
        auto it = cache.find(letter);
        if (it != cache.end()) return it->second;
    }
    bool ssd = false;
    wchar_t vol[] = {L'\\', L'\\', L'.', L'\\', letter, L':', 0};
    HANDLE h = CreateFileW(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        STORAGE_PROPERTY_QUERY q = {};
        q.PropertyId = StorageDeviceSeekPenaltyProperty;
        q.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR d = {};
        DWORD ret = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &ret, nullptr)) ssd = !d.IncursSeekPenalty;
        CloseHandle(h);
    }
    std::lock_guard<std::mutex> lk(m);
    cache[letter] = ssd;
    return ssd;
}

// ---------------- folder size calculation ----------------
static void SizeTask(std::shared_ptr<FolderStats> st, std::vector<std::wstring> batch);

static void SubmitSizeBatch(const std::shared_ptr<FolderStats> &st, std::vector<std::wstring> &&batch) {
    st->pending++;
    JobSubmit([st, b = std::move(batch)]() mutable { SizeTask(st, std::move(b)); });
}

static void SizeTask(std::shared_ptr<FolderStats> st, std::vector<std::wstring> batch) {
    std::vector<std::wstring> next;
    thread_local std::vector<BYTE> buf(64 * 1024);
    for (auto &dir : batch) {
        if (st->cancel.load()) break;
        auto onEntry = [&](FileEntry &&e) {
            if (ShouldDescend(e.attrs, e.reparseTag)) {
                st->dirs++;
                next.push_back(PathJoin(dir, e.name));
                if (next.size() == 16) { // batch of 16 sub-directories per pool task
                    SubmitSizeBatch(st, std::move(next));
                    next.clear();
                }
            } else {
                st->files++;
                st->size += e.size;
            }
        };
        if (g_NtQueryDirectoryFile) {
            DWORD err = 0;
            HANDLE h = OpenDirHandle(dir, &err);
            if (h == INVALID_HANDLE_VALUE) { st->errors++; continue; }
            NTSTATUS s;
            NtIterate(h, buf.data(), (ULONG)buf.size(), onEntry, &s);
            CloseHandle(h);
        } else {
            std::vector<FileEntry> list;
            if (ListWin32(dir, list) != LIST_OK) { st->errors++; continue; }
            for (auto &e : list) onEntry(std::move(e));
        }
    }
    if (!next.empty() && !st->cancel.load()) SubmitSizeBatch(st, std::move(next));
    if (--st->pending == 0) {
        st->done = true;
        MainPost([] {}); // wake UI
    }
}

std::shared_ptr<FolderStats> FolderSizeCalc::Get(const std::wstring &path, bool start) {
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    if (!start) return nullptr;
    auto st = std::make_shared<FolderStats>();
    st->startTick = GetTickCount64();
    cache[path] = st;
    std::vector<std::wstring> b{path};
    SubmitSizeBatch(st, std::move(b));
    return st;
}

void FolderSizeCalc::Invalidate(const std::wstring &path) {
    // invalidate the folder and every cached ancestor
    for (auto it = cache.begin(); it != cache.end();) {
        const std::wstring &k = it->first;
        bool related = k == path || (path.size() > k.size() && path.compare(0, k.size(), k) == 0 &&
                                     (k.back() == L'\\' || path[k.size()] == L'\\'));
        if (related) {
            it->second->cancel = true;
            it = cache.erase(it);
        } else ++it;
    }
}

void FolderSizeCalc::CancelAll() {
    for (auto &kv : cache) kv.second->cancel = true;
    cache.clear();
}

void FolderSizeCalc::CancelUnless(const std::function<bool(const std::wstring &)> &keep) {
    // finished results stay cached; unfinished ones are dropped so they restart if shown again
    for (auto it = cache.begin(); it != cache.end();) {
        if (!it->second->done.load() && !keep(it->first)) {
            it->second->cancel = true;
            it = cache.erase(it);
        } else ++it;
    }
}

bool FolderSizeCalc::AnyRunning() {
    for (auto &kv : cache)
        if (!kv.second->done.load()) return true;
    return false;
}

// ---------------- DirWatcher ----------------
struct DirWatcher::Watch {
    std::wstring path;
    HANDLE dir = INVALID_HANDLE_VALUE;
    OVERLAPPED ov = {};
    alignas(DWORD) BYTE buf[8192];
};

void DirWatcher::Start(std::function<void(const std::wstring &)> cb) {
    callback = std::move(cb);
    wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
}

void DirWatcher::Stop() {
    if (!thread) return;
    quit = true;
    SetEvent(wake);
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    CloseHandle(wake);
    thread = nullptr;
}

void DirWatcher::SetWatched(const std::set<std::wstring> &dirs) {
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (dirs == wanted) return;
        wanted = dirs;
    }
    if (wake) SetEvent(wake);
}

DWORD WINAPI DirWatcher::ThreadProc(void *p) {
    ((DirWatcher *)p)->Run();
    return 0;
}

static bool Issue(DirWatcher *, void *wp) {
    auto *w = (DirWatcher::Watch *)wp;
    return ReadDirectoryChangesW(w->dir, w->buf, sizeof(w->buf), FALSE,
                                 FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
                                 nullptr, &w->ov, nullptr) != 0;
}

void DirWatcher::Run() {
    std::vector<std::unique_ptr<Watch>> watches;
    auto closeWatch = [](Watch *w) {
        CancelIoEx(w->dir, &w->ov);
        DWORD n;
        GetOverlappedResult(w->dir, &w->ov, &n, TRUE);
        CloseHandle(w->dir);
        CloseHandle(w->ov.hEvent);
    };
    while (!quit) {
        // sync watch set
        std::set<std::wstring> want;
        {
            std::lock_guard<std::mutex> lk(mtx);
            want = wanted;
        }
        for (size_t i = 0; i < watches.size();) {
            if (!want.count(watches[i]->path)) {
                closeWatch(watches[i].get());
                watches.erase(watches.begin() + i);
            } else i++;
        }
        for (auto &p : want) {
            if (watches.size() >= MAXIMUM_WAIT_OBJECTS - 1) break;
            bool have = false;
            for (auto &w : watches) if (w->path == p) { have = true; break; }
            if (have) continue;
            auto w = std::make_unique<Watch>();
            w->path = p;
            w->dir = CreateFileW(LongPathPrefix(p).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (w->dir == INVALID_HANDLE_VALUE) continue;
            w->ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!Issue(this, w.get())) {
                CloseHandle(w->dir);
                CloseHandle(w->ov.hEvent);
                continue;
            }
            watches.push_back(std::move(w));
        }
        std::vector<HANDLE> handles;
        handles.push_back(wake);
        for (auto &w : watches) handles.push_back(w->ov.hEvent);
        DWORD r = WaitForMultipleObjects((DWORD)handles.size(), handles.data(), FALSE, INFINITE);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) continue;
        size_t idx = r - WAIT_OBJECT_0 - 1;
        if (idx >= watches.size()) continue;
        Watch *w = watches[idx].get();
        DWORD bytes = 0;
        BOOL ok = GetOverlappedResult(w->dir, &w->ov, &bytes, FALSE);
        ResetEvent(w->ov.hEvent);
        std::wstring path = w->path;
        if (callback) callback(path);
        if (!ok || !Issue(this, w)) {
            closeWatch(w);
            watches.erase(watches.begin() + idx);
        }
    }
    for (auto &w : watches) closeWatch(w.get());
}
