// File system access: fast directory listing, drives, change watching, folder size calculation.
#pragma once
#include "common.h"
#include <set>

struct FileEntry {
    std::wstring name;
    std::wstring relDir;      // for recursive search results: path relative to the listed root ("" = direct child)
    u64 size = 0;
    u64 created = 0, modified = 0, accessed = 0;
    u32 attrs = 0;
    u32 reparseTag = 0;
    bool isDir = false;

    // drive view
    bool isDrive = false;
    u32 driveType = 0;
    u64 totalBytes = 0, freeBytes = 0;
    std::wstring fsName, label;

    // recycle bin view
    bool isRecycled = false;
    std::wstring origLocation;
    std::wstring parsingName;  // shell parsing name for restore/delete
    u64 deleted = 0;

    bool Hidden() const { return (attrs & FILE_ATTRIBUTE_HIDDEN) != 0; }
    bool System() const { return (attrs & FILE_ATTRIBUTE_SYSTEM) != 0 && (attrs & FILE_ATTRIBUTE_HIDDEN) != 0; }
};

enum ListError { LIST_OK = 0, LIST_ACCESS_DENIED, LIST_NOT_FOUND, LIST_OTHER };

void FsInit(); // resolves NtQueryDirectoryFile (Windows 8+)
bool FsHasNtQuery();

std::wstring NormalizePath(const std::wstring &p); // expands env vars, fixes slashes, removes trailing sep
std::wstring LongPathPrefix(const std::wstring &p); // \\?\ form when needed

// Lists direct children. Thread-safe.
ListError ListDirectory(const std::wstring &dir, std::vector<FileEntry> &out);

// Recursive enumeration for "Search In Subfolders". `cancel` checked periodically.
// `onChunk` is called with batches of entries (relDir filled). Returns entries scanned.
u64 EnumerateRecursive(const std::wstring &root, const std::atomic<bool> &cancel,
                       const std::function<bool(const FileEntry &)> &accept,
                       const std::function<void(std::vector<FileEntry> &&)> &onChunk, u64 maxResults,
                       std::atomic<u64> *scanned = nullptr); // matches arrive in chunks, at least every 150 ms

void ListDrives(std::vector<FileEntry> &out, bool queryVolumeInfo);
bool PathExists(const std::wstring &p);
bool IsDirectory(const std::wstring &p);
u32  DriveTypeOf(const std::wstring &path);
bool DiskFree(const std::wstring &path, u64 *freeBytes, u64 *totalBytes);
bool IsSSD(const std::wstring &path); // seek penalty query (cached)

// ---- folder size calculation (parallel recursive) ----
struct FolderStats {
    std::atomic<u64> size{0}, files{0}, dirs{0};
    std::atomic<int> pending{0};
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<u32> errors{0};
    u64 startTick = 0;
};

class FolderSizeCalc {
public:
    std::shared_ptr<FolderStats> Get(const std::wstring &path, bool start); // returns cached/started stats
    void Invalidate(const std::wstring &path);
    void CancelAll();
    void CancelUnless(const std::function<bool(const std::wstring &)> &keep); // stops running ones keep() rejects
    bool AnyRunning();
private:
    std::unordered_map<std::wstring, std::shared_ptr<FolderStats>> cache;
};

// ---- change notifications ----
class DirWatcher {
public:
    void Start(std::function<void(const std::wstring &dir)> onChange);
    void Stop();
    void SetWatched(const std::set<std::wstring> &dirs); // replace watched set
    struct Watch;
private:
    static DWORD WINAPI ThreadProc(void *p);
    void Run();
    std::mutex mtx;
    std::set<std::wstring> wanted;
    HANDLE thread = nullptr;
    HANDLE wake = nullptr;
    std::atomic<bool> quit{false};
    std::function<void(const std::wstring &)> callback;
};
