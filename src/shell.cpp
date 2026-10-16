#include "shell.h"
#include "render.h"
#include "jobs.h"
#include "i18n.h"
#include <commoncontrols.h>
#include <shlwapi.h>
#include <propkey.h>
#include <shobjidl.h>

// ============================================================ icons
void IconCache::Init(Renderer *r) {
    rnd = r;
    SetLarge(false);
}

void IconCache::SetLarge(bool l) {
    if (imageList && l == large) return;
    Clear();
    large = l;
    if (imageList) ((IImageList *)imageList)->Release();
    imageList = nullptr;
    SHGetImageList(large ? SHIL_LARGE : SHIL_SMALL, IID_IImageList, &imageList);
}

void IconCache::Clear() {
    for (auto &kv : byIndex) if (kv.second) kv.second->Release();
    byIndex.clear();
}

void IconCache::Shutdown() {
    Clear();
    if (imageList) ((IImageList *)imageList)->Release();
    imageList = nullptr;
}

ID2D1Bitmap *IconCache::FromIndex(int idx) {
    if (idx < 0 || !imageList) return nullptr;
    auto it = byIndex.find(idx);
    if (it != byIndex.end()) return it->second;
    HICON icon = nullptr;
    ((IImageList *)imageList)->GetIcon(idx, ILD_TRANSPARENT, &icon);
    ID2D1Bitmap *bmp = rnd->CreateBitmapFromHIcon(icon);
    if (icon) DestroyIcon(icon);
    byIndex[idx] = bmp;
    return bmp;
}

int IconCache::IndexForExtension(const std::wstring &ext, bool dir) {
    std::wstring key = dir ? L"<dir>" : ext;
    auto it = extIndex.find(key);
    if (it != extIndex.end()) return it->second;
    SHFILEINFOW sfi = {};
    std::wstring probe = dir ? L"folder" : (L"file." + ext);
    SHGetFileInfoW(probe.c_str(), dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                   SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES);
    extIndex[key] = sfi.iIcon;
    return sfi.iIcon;
}

static bool HasPerFileIcon(const std::wstring &ext) {
    static const wchar_t *exts[] = {L"exe", L"ico", L"lnk", L"url", L"cur", L"ani", L"msc", L"scr", L"cpl", L"appref-ms", L"msi"};
    for (auto e : exts) if (ext == e) return true;
    return false;
}

ID2D1Bitmap *IconCache::Get(const std::wstring &fullPath, const FileEntry &e) {
    if (e.isDrive) return GetDrive(e.name + L"\\");
    std::wstring ext = e.isDir ? L"" : PathExtension(e.name);
    if (!e.isDir && HasPerFileIcon(ext) && !e.isRecycled) {
        auto it = pathIndex.find(fullPath);
        if (it != pathIndex.end()) return FromIndex(it->second);
        if (!pathPending[fullPath]) {
            pathPending[fullPath] = true;
            std::wstring p = fullPath;
            JobSubmit([this, p] {
                SHFILEINFOW sfi = {};
                SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
                int idx = sfi.iIcon;
                MainPost([this, p, idx] { pathIndex[p] = idx; pathPending.erase(p); });
            });
        }
    }
    return FromIndex(IndexForExtension(ext, e.isDir));
}

ID2D1Bitmap *IconCache::GetFolder(const std::wstring &path) {
    auto it = pathIndex.find(path);
    if (it != pathIndex.end() && it->second >= 0) return FromIndex(it->second);
    if (it == pathIndex.end() && !pathPending[path]) {
        pathPending[path] = true;
        std::wstring p = path;
        JobSubmit([this, p] {
            SHFILEINFOW sfi = {};
            int idx = SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX) ? sfi.iIcon : -1;
            MainPost([this, p, idx] { pathIndex[p] = idx; pathPending.erase(p); });
        });
    }
    return FromIndex(IndexForExtension(L"", true));
}

ID2D1Bitmap *IconCache::GetDrive(const std::wstring &root) {
    auto it = pathIndex.find(root);
    if (it != pathIndex.end()) return FromIndex(it->second);
    SHFILEINFOW sfi = {};
    SHGetFileInfoW(root.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
    pathIndex[root] = sfi.iIcon;
    return FromIndex(sfi.iIcon);
}

ID2D1Bitmap *IconCache::GetSpecial(const std::wstring &parsingName) {
    auto it = pathIndex.find(parsingName);
    if (it != pathIndex.end()) return FromIndex(it->second);
    int idx = -1;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(parsingName.c_str(), nullptr, &pidl, 0, nullptr))) {
        SHFILEINFOW sfi = {};
        SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX);
        idx = sfi.iIcon;
        CoTaskMemFree(pidl);
    }
    pathIndex[parsingName] = idx;
    return FromIndex(idx);
}

std::wstring FileTypeName(const std::wstring &name, bool isDir) {
    if (isDir) return T(S_Folder);
    static std::unordered_map<std::wstring, std::wstring> cache;
    std::wstring ext = PathExtension(name);
    auto it = cache.find(ext);
    if (it != cache.end()) return it->second;
    SHFILEINFOW sfi = {};
    std::wstring probe = ext.empty() ? L"file" : L"file." + ext;
    SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES);
    std::wstring r = sfi.szTypeName;
    if (r.empty()) r = ext.empty() ? T(S_File) : Format(T(S_FileOfType), ToLower(ext).c_str());
    cache[ext] = r;
    return r;
}

// ============================================================ images
bool IsImageExtension(const std::wstring &ext) {
    static const wchar_t *exts[] = {L"png", L"jpg", L"jpeg", L"bmp", L"gif", L"tif", L"tiff", L"ico", L"webp", L"heic", L"heif",
                                    L"jxr", L"dds", L"avif", L"jfif"};
    for (auto e : exts) if (ext == e) return true;
    return false;
}

bool DecodeImageFile(const std::wstring &path, u32 maxSide, DecodedImage &out) {
    IWICImagingFactory *wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    bool ok = false;
    IWICBitmapDecoder *dec = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICBitmapScaler *scaler = nullptr;
    IWICFormatConverter *conv = nullptr;
    do {
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) break;
        if (FAILED(dec->GetFrame(0, &frame))) break;
        UINT w, h;
        frame->GetSize(&w, &h);
        out.srcW = w; out.srcH = h;
        IWICBitmapSource *src = frame;
        if (maxSide && (w > maxSide || h > maxSide)) {
            f64 s = (f64)maxSide / std::max(w, h);
            UINT nw = std::max(1u, (UINT)(w * s)), nh = std::max(1u, (UINT)(h * s));
            if (FAILED(wic->CreateBitmapScaler(&scaler))) break;
            if (FAILED(scaler->Initialize(frame, nw, nh, WICBitmapInterpolationModeFant))) break;
            src = scaler;
            w = nw; h = nh;
        }
        if (FAILED(wic->CreateFormatConverter(&conv))) break;
        if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut))) break;
        out.w = w; out.h = h;
        out.pixels.resize((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)out.pixels.size(), out.pixels.data()))) break;
        ok = true;
    } while (0);
    SafeRelease(conv);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(dec);
    SafeRelease(wic);
    out.ok = ok;
    return ok;
}

bool ShellThumbnail(const std::wstring &path, u32 size, DecodedImage &out) {
    IShellItemImageFactory *f = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&f)))) return false;
    HBITMAP hb = nullptr;
    SIZE sz = {(LONG)size, (LONG)size};
    HRESULT hr = f->GetImage(sz, SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT, &hb);
    f->Release();
    if (FAILED(hr) || !hb) return false;
    BITMAP bm = {};
    GetObjectW(hb, sizeof(bm), &bm);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out.w = bm.bmWidth; out.h = bm.bmHeight;
    out.srcW = out.w; out.srcH = out.h;
    out.pixels.resize((size_t)out.w * out.h * 4);
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, hb, 0, out.h, out.pixels.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(hb);
    if (lines <= 0) return false;
    bool anyAlpha = false;
    for (size_t i = 3; i < out.pixels.size(); i += 4) if (out.pixels[i]) { anyAlpha = true; break; }
    if (!anyAlpha) for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
    out.ok = true;
    return true;
}

// ============================================================ file operations
static IShellItemArray *MakeItemArray(const std::vector<std::wstring> &paths) {
    std::vector<PIDLIST_ABSOLUTE> pidls;
    for (auto &p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr))) pidls.push_back(pidl);
    }
    IShellItemArray *arr = nullptr;
    if (!pidls.empty()) SHCreateShellItemArrayFromIDLists((UINT)pidls.size(), (PCIDLIST_ABSOLUTE_ARRAY)pidls.data(), &arr);
    for (auto p : pidls) CoTaskMemFree(p);
    return arr;
}

struct FileOpCtx {
    HWND owner;
    FileOpKind kind;
    std::vector<std::wstring> src;
    std::wstring dest;
    std::function<void(bool)> done;
};

static DWORD WINAPI FileOpThread(void *p) {
    FileOpCtx *c = (FileOpCtx *)p;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool ok = false;
    IFileOperation *op = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))) {
        op->SetOwnerWindow(c->owner);
        DWORD flags = FOF_NOCONFIRMMKDIR | FOFX_ADDUNDORECORD;
        switch (c->kind) {
        case FOP_RECYCLE: flags |= FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOFX_RECYCLEONDELETE; break;
        case FOP_DELETE: flags |= FOF_NOCONFIRMATION; flags &= ~FOFX_ADDUNDORECORD; break;
        case FOP_DUPLICATE: flags |= FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION; break;
        default: flags |= FOF_ALLOWUNDO; break;
        }
        if (c->kind == FOP_COPY) {
            // pasting into the source folder makes "name - 복사본" like Explorer instead of a collision prompt
            bool sameFolder = true;
            for (auto &s : c->src) if (_wcsicmp(PathParent(s).c_str(), c->dest.c_str()) != 0) sameFolder = false;
            if (sameFolder) flags |= FOF_RENAMEONCOLLISION;
        }
        op->SetOperationFlags(flags);
        HRESULT hr = E_FAIL;
        if (c->kind == FOP_RENAME) {
            IShellItem *item = nullptr;
            if (!c->src.empty() && SUCCEEDED(SHCreateItemFromParsingName(c->src[0].c_str(), nullptr, IID_PPV_ARGS(&item)))) {
                hr = op->RenameItem(item, c->dest.c_str(), nullptr);
                item->Release();
            }
        } else {
            IShellItemArray *arr = MakeItemArray(c->src);
            if (arr) {
                if (c->kind == FOP_DELETE || c->kind == FOP_RECYCLE) {
                    hr = op->DeleteItems(arr);
                } else {
                    IShellItem *dst = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(c->dest.c_str(), nullptr, IID_PPV_ARGS(&dst)))) {
                        hr = c->kind == FOP_MOVE ? op->MoveItems(arr, dst) : op->CopyItems(arr, dst);
                        dst->Release();
                    }
                }
                arr->Release();
            }
        }
        if (SUCCEEDED(hr)) {
            hr = op->PerformOperations();
            BOOL aborted = FALSE;
            op->GetAnyOperationsAborted(&aborted);
            ok = SUCCEEDED(hr) && !aborted;
            if (FAILED(hr)) Log(L"PerformOperations failed: 0x%08x", hr);
        }
        op->Release();
    }
    CoUninitialize();
    auto done = c->done;
    delete c;
    if (done) MainPost([done, ok] { done(ok); });
    else MainPost([] {});
    return 0;
}

void FileOperation(HWND owner, FileOpKind kind, const std::vector<std::wstring> &src, const std::wstring &dest,
                   std::function<void(bool)> done) {
    if (src.empty()) return;
    auto *c = new FileOpCtx{owner, kind, src, kind == FOP_DUPLICATE ? PathParent(src[0]) : dest, std::move(done)};
    HANDLE t = CreateThread(nullptr, 0, FileOpThread, c, 0, nullptr);
    if (t) CloseHandle(t);
    else FileOpThread(c);
}

static std::wstring UniqueName(const std::wstring &dir, const std::wstring &base) {
    std::wstring stem = base, ext;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) { stem = base.substr(0, dot); ext = base.substr(dot); }
    std::wstring p = PathJoin(dir, base);
    for (int i = 2; PathExists(p) && i < 10000; i++) p = PathJoin(dir, Format(L"%s (%d)%s", stem.c_str(), i, ext.c_str()));
    return p;
}

bool CreateNewFolder(const std::wstring &dir, const std::wstring &baseName, std::wstring *created) {
    std::wstring p = UniqueName(dir, baseName);
    if (!CreateDirectoryW(LongPathPrefix(p).c_str(), nullptr)) return false;
    if (created) *created = p;
    SHChangeNotify(SHCNE_MKDIR, SHCNF_PATHW, p.c_str(), nullptr);
    return true;
}

bool CreateNewFile(const std::wstring &dir, const std::wstring &baseName, std::wstring *created) {
    std::wstring p = UniqueName(dir, baseName);
    HANDLE h = CreateFileW(LongPathPrefix(p).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    if (created) *created = p;
    SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, p.c_str(), nullptr);
    return true;
}

// ============================================================ clipboard
static bool OpenClip(HWND hwnd) {
    for (int i = 0; i < 10; i++) {
        if (OpenClipboard(hwnd)) return true;
        Sleep(15);
    }
    return false;
}

bool ClipboardSetFiles(HWND hwnd, const std::vector<std::wstring> &paths, bool cut) {
    if (paths.empty() || !OpenClip(hwnd)) return false;
    EmptyClipboard();
    size_t chars = 1;
    for (auto &p : paths) chars += p.size() + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    if (h) {
        DROPFILES *df = (DROPFILES *)GlobalLock(h);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        wchar_t *dst = (wchar_t *)((BYTE *)df + sizeof(DROPFILES));
        for (auto &p : paths) {
            memcpy(dst, p.c_str(), p.size() * sizeof(wchar_t));
            dst += p.size() + 1;
        }
        GlobalUnlock(h);
        SetClipboardData(CF_HDROP, h);
    }
    HGLOBAL he = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (he) {
        *(DWORD *)GlobalLock(he) = cut ? DROPEFFECT_MOVE : (DROPEFFECT_COPY | DROPEFFECT_LINK);
        GlobalUnlock(he);
        SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), he);
    }
    CloseClipboard();
    return true;
}

bool ClipboardHasFiles() { return IsClipboardFormatAvailable(CF_HDROP) != 0; }

bool ClipboardGetFiles(HWND hwnd, std::vector<std::wstring> &paths, bool *cut) {
    paths.clear();
    if (cut) *cut = false;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClip(hwnd)) return false;
    HDROP hd = (HDROP)GetClipboardData(CF_HDROP);
    if (hd) {
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(hd, i, nullptr, 0);
            std::wstring s(len, L'\0');
            DragQueryFileW(hd, i, &s[0], len + 1);
            paths.push_back(s);
        }
    }
    HANDLE he = GetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    if (he && cut) {
        DWORD *eff = (DWORD *)GlobalLock(he);
        if (eff) { *cut = (*eff & DROPEFFECT_MOVE) != 0; GlobalUnlock(he); }
    }
    CloseClipboard();
    return !paths.empty();
}

bool ClipboardSetText(HWND hwnd, const std::wstring &text) {
    if (!OpenClip(hwnd)) return false;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    return true;
}

bool ClipboardGetText(HWND hwnd, std::wstring &text) {
    text.clear();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClip(hwnd)) return false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *s = (const wchar_t *)GlobalLock(h);
        if (s) text = s;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return true;
}

// ============================================================ context menu
static IContextMenu2 *g_cm2 = nullptr;
static IContextMenu3 *g_cm3 = nullptr;

bool HandleContextMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT *result) {
    if (g_cm3) {
        if (SUCCEEDED(g_cm3->HandleMenuMsg2(msg, wp, lp, result))) return true;
    } else if (g_cm2) {
        if (SUCCEEDED(g_cm2->HandleMenuMsg(msg, wp, lp))) { *result = 0; return true; }
    }
    return false;
}

static IShellFolder *BindFolder(const std::wstring &folder, PIDLIST_ABSOLUTE *outPidl) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &pidl, 0, nullptr))) return nullptr;
    IShellFolder *sf = nullptr;
    if (FAILED(SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&sf)))) {
        IShellFolder *desk = nullptr;
        SHGetDesktopFolder(&desk);
        if (desk && pidl->mkid.cb == 0) sf = desk;
        else SafeRelease(desk);
    }
    if (outPidl) *outPidl = pidl;
    else CoTaskMemFree(pidl);
    return sf;
}

std::wstring ShowShellContextMenu(HWND hwnd, const std::wstring &folder, const std::vector<std::wstring> &items, POINT pt) {
    std::wstring verbResult;
    IShellFolder *sf = nullptr;
    IContextMenu *cm = nullptr;
    std::vector<PITEMID_CHILD> children;

    std::wstring parent = items.empty() ? folder : PathParent(items[0]);
    if (items.size() == 1 && PathIsRootDrive(items[0])) parent = L"";
    if (parent.empty()) {
        SHGetDesktopFolder(&sf);
        // drives live under "This PC"
        PIDLIST_ABSOLUTE pc = nullptr;
        if (SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pc) == S_OK) {
            IShellFolder *pcf = nullptr;
            if (SUCCEEDED(sf->BindToObject(pc, nullptr, IID_PPV_ARGS(&pcf)))) { sf->Release(); sf = pcf; }
            CoTaskMemFree(pc);
        }
    } else {
        sf = BindFolder(parent, nullptr);
    }
    if (!sf) return verbResult;

    HRESULT hr;
    if (items.empty()) {
        hr = sf->CreateViewObject(hwnd, IID_PPV_ARGS(&cm));
    } else {
        for (auto &it : items) {
            if (PathParent(it) != parent && !(parent.empty() && PathIsRootDrive(it))) continue;
            std::wstring name = parent.empty() ? it : PathFileName(it);
            if (parent.empty() && name.size() == 2) name += L"\\";
            PIDLIST_RELATIVE child = nullptr;
            if (SUCCEEDED(sf->ParseDisplayName(hwnd, nullptr, (LPWSTR)name.c_str(), nullptr, &child, nullptr)))
                children.push_back((PITEMID_CHILD)child);
        }
        hr = children.empty() ? E_FAIL
                              : sf->GetUIObjectOf(hwnd, (UINT)children.size(), (PCUITEMID_CHILD_ARRAY)children.data(),
                                                  IID_IContextMenu, nullptr, (void **)&cm);
    }
    if (SUCCEEDED(hr) && cm) {
        HMENU menu = CreatePopupMenu();
        UINT flags = CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME;
        if (GetKeyState(VK_SHIFT) < 0) flags |= CMF_EXTENDEDVERBS;
        const UINT first = 1, last = 0x7FFF;
        if (SUCCEEDED(cm->QueryContextMenu(menu, 0, first, last, flags))) {
            cm->QueryInterface(IID_PPV_ARGS(&g_cm2));
            cm->QueryInterface(IID_PPV_ARGS(&g_cm3));
            UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd, nullptr);
            SafeRelease(g_cm2);
            SafeRelease(g_cm3);
            if (cmd >= first) {
                wchar_t verb[128] = {};
                cm->GetCommandString(cmd - first, GCS_VERBW, nullptr, (LPSTR)verb, _countof(verb));
                verbResult = verb;
                if (_wcsicmp(verb, L"rename") != 0) {
                    CMINVOKECOMMANDINFOEX ici = {sizeof(ici)};
                    ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                    if (GetKeyState(VK_CONTROL) < 0) ici.fMask |= CMIC_MASK_CONTROL_DOWN;
                    if (GetKeyState(VK_SHIFT) < 0) ici.fMask |= CMIC_MASK_SHIFT_DOWN;
                    ici.hwnd = hwnd;
                    ici.lpVerb = MAKEINTRESOURCEA(cmd - first);
                    ici.lpVerbW = MAKEINTRESOURCEW(cmd - first);
                    std::wstring dirW = folder;
                    ici.lpDirectoryW = dirW.empty() ? nullptr : dirW.c_str();
                    ici.nShow = SW_SHOWNORMAL;
                    ici.ptInvoke = pt;
                    cm->InvokeCommand((CMINVOKECOMMANDINFO *)&ici);
                }
            }
        }
        DestroyMenu(menu);
        cm->Release();
    }
    for (auto c : children) CoTaskMemFree(c);
    sf->Release();
    return verbResult;
}

// ============================================================ misc
void ShellOpen(HWND hwnd, const std::wstring &path, const wchar_t *verb) {
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_ASYNCOK | SEE_MASK_FLAG_LOG_USAGE;
    sei.hwnd = hwnd;
    sei.lpVerb = verb;
    sei.lpFile = path.c_str();
    std::wstring dir = PathParent(path);
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) Log(L"ShellExecuteEx failed (%u) for %s", GetLastError(), path.c_str());
}

void ShellOpenWith(HWND hwnd, const std::wstring &path) {
    OPENASINFO oi = {};
    oi.pcszFile = path.c_str();
    oi.oaifInFlags = OAIF_EXEC | OAIF_ALLOW_REGISTRATION;
    SHOpenWithDialog(hwnd, &oi);
}

void ShellProperties(HWND hwnd, const std::vector<std::wstring> &paths) {
    if (paths.empty()) return;
    if (paths.size() == 1) {
        SHObjectProperties(hwnd, SHOP_FILEPATH, paths[0].c_str(), nullptr);
        return;
    }
    IShellItemArray *arr = MakeItemArray(paths);
    if (!arr) return;
    IDataObject *dobj = nullptr;
    if (SUCCEEDED(arr->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&dobj)))) {
        SHMultiFileProperties(dobj, 0);
        dobj->Release();
    }
    arr->Release();
}

void OpenConsoleAt(const std::wstring &dir) {
    wchar_t wt[MAX_PATH];
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd;
    if (SearchPathW(nullptr, L"wt.exe", nullptr, MAX_PATH, wt, nullptr)) {
        cmd = Format(L"\"%s\" -d \"%s\"", wt, dir.c_str());
    } else {
        cmd = L"cmd.exe /K";
    }
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void OpenExplorerAt(const std::wstring &dir, const std::wstring &select) {
    std::wstring args = select.empty() ? Format(L"\"%s\"", dir.c_str()) : Format(L"/select,\"%s\"", select.c_str());
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

std::wstring KnownFolderPath(const KNOWNFOLDERID &id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

std::wstring ResolveShortcut(const std::wstring &lnk) {
    std::wstring r;
    IShellLinkW *sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return r;
    IPersistFile *pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
            wchar_t buf[MAX_PATH * 4] = {};
            if (SUCCEEDED(sl->GetPath(buf, _countof(buf), nullptr, SLGP_RAWPATH))) r = buf;
        }
        pf->Release();
    }
    sl->Release();
    return r;
}

// ============================================================ recycle bin
static const PROPERTYKEY PK_DeletedFrom = {{0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 2};
static const PROPERTYKEY PK_DateDeleted = {{0x9B174B33, 0x40FF, 0x11D2, {0xA2, 0x7E, 0x00, 0xC0, 0x4F, 0xC3, 0x08, 0x71}}, 3};

bool RecycleBinList(std::vector<FileEntry> &out) {
    out.clear();
    IShellItem *bin = nullptr;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin)))) return false;
    IEnumShellItems *en = nullptr;
    if (SUCCEEDED(bin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) {
        IShellItem *item = nullptr;
        while (en->Next(1, &item, nullptr) == S_OK) {
            FileEntry e;
            e.isRecycled = true;
            PWSTR s = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &s))) { e.name = s; CoTaskMemFree(s); }
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &s))) { e.parsingName = s; CoTaskMemFree(s); }
            SFGAOF attrs = 0;
            item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attrs);
            e.isDir = (attrs & SFGAO_FOLDER) && !(attrs & SFGAO_STREAM);
            e.attrs = e.isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            IShellItem2 *i2 = nullptr;
            if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&i2)))) {
                ULONGLONG sz = 0;
                if (SUCCEEDED(i2->GetUInt64(PKEY_Size, &sz))) e.size = sz;
                FILETIME ft;
                if (SUCCEEDED(i2->GetFileTime(PKEY_DateModified, &ft))) e.modified = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                if (SUCCEEDED(i2->GetFileTime(PK_DateDeleted, &ft))) e.deleted = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                PWSTR loc = nullptr;
                if (SUCCEEDED(i2->GetString(PK_DeletedFrom, &loc))) { e.origLocation = loc; CoTaskMemFree(loc); }
                i2->Release();
            }
            // Windows 11 reports the full original path as the display name of recycled items
            if (e.name.find(L'\\') != std::wstring::npos) {
                if (e.origLocation.empty()) e.origLocation = PathParent(e.name);
                e.name = PathFileName(e.name);
            }
            out.push_back(std::move(e));
            item->Release();
        }
        en->Release();
    }
    bin->Release();
    return true;
}

static bool InvokeVerbOnItem(HWND hwnd, const std::wstring &parsingName, const char *verb) {
    IShellItem *item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(parsingName.c_str(), nullptr, IID_PPV_ARGS(&item)))) return false;
    bool ok = false;
    IContextMenu *cm = nullptr;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&cm)))) {
        HMENU m = CreatePopupMenu();
        cm->QueryContextMenu(m, 0, 1, 0x7FFF, CMF_NORMAL);
        CMINVOKECOMMANDINFO ici = {sizeof(ici)};
        ici.hwnd = hwnd;
        ici.lpVerb = verb;
        ici.nShow = SW_SHOWNORMAL;
        ok = SUCCEEDED(cm->InvokeCommand(&ici));
        DestroyMenu(m);
        cm->Release();
    }
    item->Release();
    return ok;
}

bool RecycleBinRestore(HWND hwnd, const std::vector<std::wstring> &names) {
    bool ok = true;
    for (auto &n : names) ok &= InvokeVerbOnItem(hwnd, n, "undelete");
    return ok;
}

bool RecycleBinEmpty(HWND hwnd) {
    return SUCCEEDED(SHEmptyRecycleBinW(hwnd, nullptr, SHERB_NOCONFIRMATION));
}

bool RecycleBinDelete(HWND hwnd, const std::vector<std::wstring> &names) {
    bool ok = true;
    for (auto &n : names) ok &= InvokeVerbOnItem(hwnd, n, "delete");
    return ok;
}

// ============================================================ drag & drop
class DropTarget : public IDropTarget {
public:
    DropTarget(HWND h, DropCallbacks c) : hwnd(h), cb(std::move(c)) {
        CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper));
    }
    ~DropTarget() { SafeRelease(helper); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = --ref; if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *dobj, DWORD keys, POINTL pt, DWORD *effect) override {
        files.clear();
        FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm = {};
        if (SUCCEEDED(dobj->GetData(&fe, &sm))) {
            HDROP hd = (HDROP)GlobalLock(sm.hGlobal);
            if (hd) {
                UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < n; i++) {
                    UINT len = DragQueryFileW(hd, i, nullptr, 0);
                    std::wstring s(len, L'\0');
                    DragQueryFileW(hd, i, &s[0], len + 1);
                    files.push_back(s);
                }
                GlobalUnlock(sm.hGlobal);
            }
            ReleaseStgMedium(&sm);
        }
        POINT p = {pt.x, pt.y};
        if (helper) helper->DragEnter(hwnd, dobj, &p, *effect);
        return DragOver(keys, pt, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD *effect) override {
        POINT p = {pt.x, pt.y};
        if (helper) helper->DragOver(&p, *effect);
        ScreenToClient(hwnd, &p);
        dest = files.empty() ? L"" : cb.targetAt(p.x, p.y);
        if (cb.hover) cb.hover(p.x, p.y, !dest.empty());
        if (dest.empty()) { *effect = DROPEFFECT_NONE; return S_OK; }
        DWORD allowed = *effect;
        DWORD e;
        if (keys & MK_CONTROL) e = DROPEFFECT_COPY;
        else if (keys & MK_SHIFT) e = DROPEFFECT_MOVE;
        else if (keys & MK_ALT) e = DROPEFFECT_LINK;
        else {
            bool sameVolume = !files.empty() && files[0].size() > 2 && dest.size() > 2 &&
                              towupper(files[0][0]) == towupper(dest[0]) && files[0][1] == L':' && dest[1] == L':';
            e = sameVolume ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        if (!(allowed & e)) e = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : (allowed & DROPEFFECT_MOVE);
        // dropping a folder into itself is refused
        for (auto &f : files) if (dest == f || (dest.size() > f.size() && dest.compare(0, f.size(), f) == 0 && dest[f.size()] == L'\\')) e = DROPEFFECT_NONE;
        *effect = e;
        lastEffect = e;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (helper) helper->DragLeave();
        if (cb.hover) cb.hover(0, 0, false);
        files.clear();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *dobj, DWORD keys, POINTL pt, DWORD *effect) override {
        DragOver(keys, pt, effect);
        POINT p = {pt.x, pt.y};
        if (helper) helper->Drop(dobj, &p, *effect);
        if (cb.hover) cb.hover(0, 0, false);
        if (!dest.empty() && *effect != DROPEFFECT_NONE && !files.empty()) cb.drop(files, dest, *effect);
        files.clear();
        return S_OK;
    }
private:
    std::atomic<ULONG> ref{1};
    HWND hwnd;
    DropCallbacks cb;
    IDropTargetHelper *helper = nullptr;
    std::vector<std::wstring> files;
    std::wstring dest;
    DWORD lastEffect = 0;
};

static DropTarget *g_dropTarget = nullptr;

void DragDropRegister(HWND hwnd, DropCallbacks cb) {
    g_dropTarget = new DropTarget(hwnd, std::move(cb));
    RegisterDragDrop(hwnd, g_dropTarget);
}

void DragDropRevoke(HWND hwnd) {
    RevokeDragDrop(hwnd);
    if (g_dropTarget) g_dropTarget->Release();
    g_dropTarget = nullptr;
}

DWORD DragFiles(HWND hwnd, const std::vector<std::wstring> &paths) {
    IShellItemArray *arr = MakeItemArray(paths);
    if (!arr) return DROPEFFECT_NONE;
    IDataObject *dobj = nullptr;
    DWORD effect = DROPEFFECT_NONE;
    if (SUCCEEDED(arr->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&dobj)))) {
        SHDoDragDrop(hwnd, dobj, nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
        dobj->Release();
    }
    arr->Release();
    return effect;
}

// ------------------------------------------------------------------ cloud files status
bool IsUnderSyncRoot(const std::wstring &path) {
    static std::unordered_map<std::wstring, bool> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    // cldapi.dll exists on Windows 10 1709+; loaded dynamically so older systems still start
    typedef HRESULT(WINAPI * GetInfoFn)(LPCWSTR, int, PVOID, DWORD, DWORD *);
    static GetInfoFn getInfo = [] {
        HMODULE m = LoadLibraryExW(L"cldapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m ? (GetInfoFn)GetProcAddress(m, "CfGetSyncRootInfoByPath") : nullptr;
    }();
    bool under = false;
    if (getInfo && path.size() > 3 && path.compare(0, 2, L"\\\\") != 0) {
        BYTE buf[512];
        DWORD len = 0;
        HRESULT hr = getInfo(path.c_str(), 0 /*CF_SYNC_ROOT_INFO_BASIC*/, buf, sizeof(buf), &len);
        under = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_MORE_DATA);
    }
    cache[path] = under;
    return under;
}

int CloudStatusCache::Get(const std::wstring &path) {
    auto it = states.find(path);
    bool known = it != states.end();
    if ((!known || stale.count(path)) && !pending.count(path)) {
        pending.insert(path);
        stale.erase(path);
        queue.push_back(path);
    }
    return known ? it->second : -1;
}

void CloudStatusCache::InvalidateDir(const std::wstring &dir) {
    for (auto &kv : states)
        if (_wcsicmp(PathParent(kv.first).c_str(), dir.c_str()) == 0) stale.insert(kv.first);
}

void CloudStatusCache::Flush() {
    if (queue.empty()) return;
    auto batch = std::make_shared<std::vector<std::wstring>>(std::move(queue));
    queue.clear();
    JobSubmit([this, batch] {
        auto out = std::make_shared<std::vector<int>>();
        for (auto &p : *batch) {
            int st = CLOUD_NONE;
            IShellItem2 *item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(p.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
                ULONG v = 0;
                if (SUCCEEDED(item->GetUInt32(PKEY_StorageProviderState, &v))) st = (int)v;
                item->Release();
            }
            out->push_back(st);
        }
        MainPost([this, batch, out] {
            for (size_t i = 0; i < batch->size(); i++) {
                states[(*batch)[i]] = (*out)[i];
                pending.erase((*batch)[i]);
            }
        });
    });
}

// ------------------------------------------------------------------ thumbnails for the icon views
static u32 ThumbBucket(u32 px) {
    static const u32 buckets[] = {48, 96, 160, 256};
    for (u32 b : buckets) if (px <= b) return b;
    return 256;
}

ID2D1Bitmap *ThumbCache::Get(const std::wstring &path, u64 stamp, u32 px) {
    u32 size = ThumbBucket(px);
    std::wstring key = path + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(stamp);
    auto it = map.find(key);
    if (it != map.end()) { it->second.used = frame; return it->second.bmp; }
    if (!pending.count(key)) queue.push_back({key, path, size});
    return nullptr;
}

void ThumbCache::Flush() {
    frame++;
    // newest requests first: what was requested last frame is what is on screen now
    const int kMaxInflight = 6;
    while (!queue.empty() && inflight < kMaxInflight) {
        Req q = std::move(queue.back());
        queue.pop_back();
        if (pending.count(q.key) || map.count(q.key)) continue;
        pending.insert(q.key);
        inflight++;
        JobSubmit([this, q] {
            auto img = std::make_shared<DecodedImage>();
            ShellThumbnail(q.path, q.size, *img);
            MainPost([this, q, img] {
                inflight--;
                pending.erase(q.key);
                Entry e;
                if (img->ok && rnd) {
                    e.bmp = rnd->CreateBitmapBGRA(img->pixels.data(), img->w, img->h, img->w * 4);
                    e.bytes = img->pixels.size();
                }
                e.used = frame;
                total += e.bytes;
                map[q.key] = e;
            });
        });
    }
    queue.clear(); // misses that did not fit are asked for again on the next frame
    if (total > maxBytes) {
        std::vector<std::pair<u64, std::wstring>> order;
        for (auto &kv : map) if (kv.second.used + 1 < frame) order.push_back({kv.second.used, kv.first});
        std::sort(order.begin(), order.end());
        for (auto &o : order) {
            if (total <= maxBytes * 3 / 4) break;
            auto it = map.find(o.second);
            if (it->second.bmp) it->second.bmp->Release();
            total -= it->second.bytes;
            map.erase(it);
        }
    }
}

void ThumbCache::Clear() {
    for (auto &kv : map) if (kv.second.bmp) kv.second.bmp->Release();
    map.clear();
    total = 0;
}
