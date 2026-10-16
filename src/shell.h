// Windows shell integration: icons, thumbnails, file operations, clipboard, context menus, recycle bin, drag & drop
#pragma once
#include "common.h"
#include "fs.h"
#include <unordered_set>
#include <shlobj.h>
#include <d2d1.h>

class Renderer;

// ---------- icons ----------
class IconCache {
public:
    void Init(Renderer *r);
    void Shutdown();
    void SetLarge(bool large); // DPI dependent: 16px vs 32px system image list
    // Returns a bitmap for the entry (may return a generic icon while the specific one loads)
    ID2D1Bitmap *Get(const std::wstring &fullPath, const FileEntry &e);
    ID2D1Bitmap *GetDrive(const std::wstring &root);
    ID2D1Bitmap *GetSpecial(const std::wstring &parsingName); // e.g. recycle bin
    // the folder's own shell icon (Desktop, Downloads, desktop.ini icons...); generic folder until resolved
    ID2D1Bitmap *GetFolder(const std::wstring &path);
    ID2D1Bitmap *FromIndex(int sysIndex);
    void Clear();
private:
    int IndexForExtension(const std::wstring &ext, bool dir);
    Renderer *rnd = nullptr;
    void *imageList = nullptr; // IImageList*
    bool large = false;
    std::unordered_map<int, ID2D1Bitmap *> byIndex;
    std::unordered_map<std::wstring, int> extIndex;       // ".txt" -> sys index
    std::unordered_map<std::wstring, int> pathIndex;      // per-file icons (exe, lnk, ico...)
    std::unordered_map<std::wstring, bool> pathPending;
};

std::wstring FileTypeName(const std::wstring &name, bool isDir); // localized via shell, cached

// ---------- thumbnails / image decode (async) ----------
struct DecodedImage {
    std::vector<u8> pixels; // BGRA premultiplied
    u32 w = 0, h = 0;
    u32 srcW = 0, srcH = 0;
    bool ok = false;
};
// Decodes an image file with WIC (or shell thumbnail fallback) scaled to fit maxSide. Thread-safe (call from job).
bool DecodeImageFile(const std::wstring &path, u32 maxSide, DecodedImage &out);
bool ShellThumbnail(const std::wstring &path, u32 size, DecodedImage &out);
bool IsImageExtension(const std::wstring &ext);

// shell thumbnails (images, videos, documents; plain icons for the rest) for the icon views.
// Requests are re-issued every frame, so only what is on screen gets loaded; LRU by bytes.
class ThumbCache {
public:
    void Init(Renderer *r) { rnd = r; }
    void SetBudget(size_t bytes) { maxBytes = bytes; }
    // nullptr while loading (or when the shell has nothing); stamp = modified time so edits refresh
    ID2D1Bitmap *Get(const std::wstring &path, u64 stamp, u32 px);
    void Flush();     // once per frame: submits this frame's misses, evicts over budget
    void Clear();
private:
    struct Entry { ID2D1Bitmap *bmp = nullptr; size_t bytes = 0; u64 used = 0; };
    struct Req { std::wstring key, path; u32 size; };
    Renderer *rnd = nullptr;
    std::unordered_map<std::wstring, Entry> map;
    std::unordered_set<std::wstring> pending;
    std::vector<Req> queue;
    size_t total = 0, maxBytes = 256u << 20;
    u64 frame = 0;
    int inflight = 0;
};

// ---------- file operations (run on a background STA thread with Windows progress UI) ----------
enum FileOpKind { FOP_COPY, FOP_MOVE, FOP_DELETE, FOP_RECYCLE, FOP_RENAME, FOP_DUPLICATE };
void FileOperation(HWND owner, FileOpKind kind, const std::vector<std::wstring> &src, const std::wstring &destDirOrName,
                   std::function<void(bool ok)> done);
bool CreateNewFolder(const std::wstring &dir, const std::wstring &baseName, std::wstring *created);
bool CreateNewFile(const std::wstring &dir, const std::wstring &baseName, std::wstring *created);

// ---------- clipboard ----------
bool ClipboardSetFiles(HWND hwnd, const std::vector<std::wstring> &paths, bool cut);
bool ClipboardGetFiles(HWND hwnd, std::vector<std::wstring> &paths, bool *cut);
bool ClipboardSetText(HWND hwnd, const std::wstring &text);
bool ClipboardGetText(HWND hwnd, std::wstring &text);
bool ClipboardHasFiles();

// ---------- shell context menu ----------
// Shows the native Windows context menu for items (or the folder background when items is empty).
// Returns the canonical verb chosen. "rename" is NOT invoked (the app renames inline).
std::wstring ShowShellContextMenu(HWND hwnd, const std::wstring &folder, const std::vector<std::wstring> &items, POINT screenPt);
bool HandleContextMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT *result);

// ---------- misc shell ----------
void ShellOpen(HWND hwnd, const std::wstring &path, const wchar_t *verb = nullptr);
void ShellOpenWith(HWND hwnd, const std::wstring &path);
void ShellProperties(HWND hwnd, const std::vector<std::wstring> &paths);
void OpenConsoleAt(const std::wstring &dir);
void OpenExplorerAt(const std::wstring &dir, const std::wstring &select);
std::wstring KnownFolderPath(const KNOWNFOLDERID &id);
std::wstring ResolveShortcut(const std::wstring &lnkPath); // returns target or ""

// ---------- recycle bin ----------
bool RecycleBinList(std::vector<FileEntry> &out);
bool RecycleBinRestore(HWND hwnd, const std::vector<std::wstring> &parsingNames);
bool RecycleBinEmpty(HWND hwnd);
bool RecycleBinDelete(HWND hwnd, const std::vector<std::wstring> &parsingNames);

// ---------- drag & drop ----------
struct DropCallbacks {
    // returns destination folder for a client point ("" = refuse)
    std::function<std::wstring(int x, int y)> targetAt;
    std::function<void(int x, int y, bool active)> hover; // for highlight
    std::function<void(const std::vector<std::wstring> &files, const std::wstring &dest, DWORD effect)> drop;
};
void DragDropRegister(HWND hwnd, DropCallbacks cb);
void DragDropRevoke(HWND hwnd);
// Starts a modal drag with the given files (STA UI thread). Returns resulting effect.
DWORD DragFiles(HWND hwnd, const std::vector<std::wstring> &paths);

// ---- cloud files (OneDrive and other Cloud Files API providers): Explorer's "Status" column
bool IsUnderSyncRoot(const std::wstring &path); // cached per path
enum CloudState { CLOUD_NONE = 0, CLOUD_ONLINE_ONLY, CLOUD_IN_SYNC, CLOUD_PINNED, CLOUD_PENDING_UPLOAD,
                  CLOUD_PENDING_DOWNLOAD, CLOUD_TRANSFERRING, CLOUD_ERROR, CLOUD_WARNING, CLOUD_EXCLUDED,
                  CLOUD_PENDING }; // = STORAGEPROVIDERSTATE_* (System.StorageProviderState)
class CloudStatusCache {
public:
    int Get(const std::wstring &path);           // CloudState, or -1 until the first lookup finishes
    void InvalidateDir(const std::wstring &dir);  // re-query items of a changed folder (keeps old values meanwhile)
    void Flush();                                 // submits the queued lookups; call once per frame
private:
    std::unordered_map<std::wstring, int> states;
    std::unordered_set<std::wstring> pending, stale;
    std::vector<std::wstring> queue;
};
