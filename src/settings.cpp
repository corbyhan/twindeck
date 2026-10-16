#include "settings.h"
#include <shlobj.h>
#include <math.h>

u64 UnixNow() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    u64 t = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) / 10000000ULL;
}

// ------------------------------------------------ Settings
#define SETTING_FIELDS(X) \
    X(L"Language", language, Int) \
    X(L"ColorScheme", colorScheme, Int) \
    X(L"FontName", fontName, Str) \
    X(L"FontSize", fontSize, Float) \
    X(L"InspectorFontName", inspectorFontName, Str) \
    X(L"InspectorFontSize", inspectorFontSize, Float) \
    X(L"Spacing", spacing, Int) \
    X(L"ShowHiddenFiles", showHiddenFiles, Bool) \
    X(L"ShowSystemFiles", showSystemFiles, Bool) \
    X(L"ShowFileExtension", showFileExtension, Bool) \
    X(L"ShowCheckboxes", showCheckboxes, Bool) \
    X(L"ShowBreadcrumbs", showBreadcrumbs, Bool) \
    X(L"ShowShortenedPaths", showShortenedPaths, Bool) \
    X(L"ShowTooltips", showTooltips, Bool) \
    X(L"ShowRecents", showRecents, Bool) \
    X(L"AlternatingRowColors", alternatingRowColors, Bool) \
    X(L"RoundCorners", roundCorners, Bool) \
    X(L"Backdrop", backdrop, Int) \
    X(L"BackgroundOpacity", backgroundOpacity, Int) \
    X(L"Animations", animations, Bool) \
    X(L"UnderlineOnHover", underlineOnHover, Bool) \
    X(L"VSync", vsync, Bool) \
    X(L"SingleClick", singleClick, Bool) \
    X(L"SingleClickFoldersOnly", singleClickFoldersOnly, Bool) \
    X(L"DoubleClickToParent", doubleClickToParent, Bool) \
    X(L"CharacterKeyAction", characterKeyAction, Int) \
    X(L"CalcFolderSizes", calcFolderSizes, Bool) \
    X(L"CalcFolderSizesSSD", calcFolderSizesSSD, Bool) \
    X(L"CalcFolderSizesHDD", calcFolderSizesHDD, Bool) \
    X(L"CalcFolderSizesNetwork", calcFolderSizesNetwork, Bool) \
    X(L"LoadThumbnails", loadThumbnails, Bool) \
    X(L"LoadOnStartup", loadOnStartup, Int) \
    X(L"NewTabShow", newTabShow, Int) \
    X(L"InspectorLayout", inspectorLayout, Int) \
    X(L"StoreRecents", storeRecents, Bool) \
    X(L"SortRecents", sortRecents, Bool) \
    X(L"FoldersFirst", foldersFirst, Bool) \
    X(L"ReverseScroll", reverseScroll, Bool) \
    X(L"AssetCacheSize", assetCacheMB, Int) \
    X(L"SetupPrompted", setupPrompted, Bool)

Json Settings::ToJson() const {
    Json j = Json::MakeObject();
    j[L"Version"] = Json(1);
#define Int(v) Json((int)(v))
#define Float(v) Json((f64)(v))
#define Str(v) Json(v)
#define Bool(v) Json((bool)(v))
#define X(key, field, type) j[key] = type(field);
    SETTING_FIELDS(X)
#undef X
#undef Int
#undef Float
#undef Str
#undef Bool
    return j;
}

void Settings::FromJson(const Json &j) {
    if (!j.IsObject()) return;
#define Int(field, key) field = j.GetInt(key, field)
#define Float(field, key) field = (f32)j.GetNum(key, field)
#define Str(field, key) field = j.GetStr(key, field)
#define Bool(field, key) field = j.GetBool(key, field)
#define X(key, field, type) type(field, key);
    SETTING_FIELDS(X)
#undef X
#undef Int
#undef Float
#undef Str
#undef Bool
    fontSize = Clamp(fontSize, 6.f, 32.f);
    inspectorFontSize = Clamp(inspectorFontSize, 6.f, 32.f);
    spacing = Clamp(spacing, 0, 12);
}

// ------------------------------------------------ UserData
static const f64 kRecentHalfLifeDays = 7.0;

static f64 DecayedScore(const RecentItem &r, u64 now) {
    f64 days = (f64)(now - std::min(now, r.lastUse)) / 86400.0;
    return r.score * pow(0.5, days / kRecentHalfLifeDays);
}

void UserData::TouchRecent(const std::wstring &path) {
    if (path.empty()) return;
    u64 now = UnixNow();
    for (auto &r : recents) {
        if (_wcsicmp(r.path.c_str(), path.c_str()) == 0) {
            r.score = DecayedScore(r, now) + 1.0;
            r.lastUse = now;
            return;
        }
    }
    recents.push_back({path, 1.0, now});
    if (recents.size() > 200) {
        auto s = SortedRecents();
        s.resize(150);
        recents = s;
    }
}

void UserData::RemoveRecent(const std::wstring &path) {
    recents.erase(std::remove_if(recents.begin(), recents.end(),
                                 [&](const RecentItem &r) { return _wcsicmp(r.path.c_str(), path.c_str()) == 0; }),
                  recents.end());
}

std::vector<RecentItem> UserData::SortedRecents() const {
    u64 now = UnixNow();
    std::vector<RecentItem> v = recents;
    std::sort(v.begin(), v.end(), [&](const RecentItem &a, const RecentItem &b) {
        return DecayedScore(a, now) > DecayedScore(b, now);
    });
    return v;
}

Json UserData::ToJson() const {
    Json j = Json::MakeObject();
    j[L"Version"] = Json(1);
    Json bm = Json::MakeArray();
    for (auto &b : bookmarks) bm.Push(Json(b));
    j[L"PathBookmarks"] = bm;
    Json cb = Json::MakeArray();
    for (auto &b : commandBookmarks) cb.Push(Json(b));
    j[L"CommandBookmarks"] = cb;
    Json rc = Json::MakeArray();
    for (auto &r : recents) {
        Json o = Json::MakeObject();
        o[L"Path"] = Json(r.path);
        o[L"Score"] = Json(r.score);
        o[L"Decay"] = Json((f64)r.lastUse);
        rc.Push(o);
    }
    j[L"Recents"] = rc;
    Json hk = Json::MakeArray();
    for (auto &h : hotkeys) {
        Json o = Json::MakeObject();
        o[L"Keys"] = Json(h.keys);
        o[L"Command"] = Json(h.command);
        hk.Push(o);
    }
    j[L"Hotkeys"] = hk;
    return j;
}

void UserData::FromJson(const Json &j) {
    if (!j.IsObject()) return;
    if (auto *a = j.Find(L"PathBookmarks")) for (auto &v : a->arr) if (v.type == Json::String) bookmarks.push_back(v.str);
    if (auto *a = j.Find(L"CommandBookmarks")) for (auto &v : a->arr) if (v.type == Json::String) commandBookmarks.push_back(v.str);
    if (auto *a = j.Find(L"Recents"))
        for (auto &v : a->arr) {
            RecentItem r;
            r.path = v.GetStr(L"Path", L"");
            r.score = v.GetNum(L"Score", 1);
            r.lastUse = (u64)v.GetNum(L"Decay", 0);
            if (!r.path.empty()) recents.push_back(r);
        }
    if (auto *a = j.Find(L"Hotkeys"))
        for (auto &v : a->arr) {
            HotkeyBinding h{v.GetStr(L"Keys", L""), v.GetStr(L"Command", L"")};
            if (!h.command.empty()) hotkeys.push_back(h);
        }
}

// ------------------------------------------------ Session
static Json NodeToJson(const SessionNode *n) {
    Json j = Json::MakeObject();
    if (!n) return j;
    if (n->leaf) {
        Json tabs = Json::MakeArray();
        for (auto &t : n->tabs) {
            Json o = Json::MakeObject();
            o[L"Path"] = Json(t.path);
            o[L"SortIndex"] = Json(t.sort);
            o[L"Descending"] = Json(t.desc);
            o[L"Group"] = Json(t.group);
            o[L"View"] = Json(t.view);
            o[L"IconSize"] = Json((f64)t.iconSize);
            tabs.Push(o);
        }
        j[L"Panel"] = tabs;
        j[L"SelectedTab"] = Json(n->activeTab);
    } else {
        j[L"Vertical"] = Json(n->vertical);
        j[L"SplitRatio"] = Json((f64)n->ratio);
        j[L"Child"] = NodeToJson(n->a.get());
        j[L"Next"] = NodeToJson(n->b.get());
    }
    return j;
}

static std::unique_ptr<SessionNode> NodeFromJson(const Json &j, int depth) {
    if (!j.IsObject() || depth > 16) return nullptr;
    auto n = std::make_unique<SessionNode>();
    if (auto *tabs = j.Find(L"Panel")) {
        n->leaf = true;
        for (auto &t : tabs->arr) {
            SessionTab st;
            st.path = t.GetStr(L"Path", L"");
            st.sort = t.GetInt(L"SortIndex", 0);
            st.desc = t.GetBool(L"Descending", false);
            st.group = t.GetInt(L"Group", 0);
            st.view = t.GetInt(L"View", 0);
            st.iconSize = (f32)t.GetNum(L"IconSize", 96);
            n->tabs.push_back(st);
        }
        n->activeTab = j.GetInt(L"SelectedTab", 0);
        if (n->tabs.empty()) return nullptr;
        return n;
    }
    n->leaf = false;
    n->vertical = j.GetBool(L"Vertical", false);
    n->ratio = Clamp((f32)j.GetNum(L"SplitRatio", 0.5), 0.1f, 0.9f);
    const Json *a = j.Find(L"Child");
    const Json *b = j.Find(L"Next");
    if (a) n->a = NodeFromJson(*a, depth + 1);
    if (b) n->b = NodeFromJson(*b, depth + 1);
    if (!n->a && !n->b) return nullptr;
    if (!n->a) return std::move(n->b);
    if (!n->b) return std::move(n->a);
    return n;
}

Json SessionData::ToJson() const {
    Json j = Json::MakeObject();
    j[L"Version"] = Json(1);
    j[L"WindowX"] = Json(x);
    j[L"WindowY"] = Json(y);
    j[L"WindowWidth"] = Json(w);
    j[L"WindowHeight"] = Json(h);
    j[L"WindowMaximized"] = Json(maximized);
    j[L"QuickAccessWidth"] = Json((f64)sidebarWidth);
    j[L"QuickAccessClosed"] = Json(sidebarClosed);
    j[L"InspectorOpen"] = Json(inspectorOpen);
    j[L"InspectorSplitRatio"] = Json((f64)inspectorSize);
    j[L"SelectedPanel"] = Json(selectedPanel);
    j[L"Layout"] = NodeToJson(layout.get());
    return j;
}

void SessionData::FromJson(const Json &j) {
    if (!j.IsObject()) return;
    x = j.GetInt(L"WindowX", x);
    y = j.GetInt(L"WindowY", y);
    w = j.GetInt(L"WindowWidth", w);
    h = j.GetInt(L"WindowHeight", h);
    maximized = j.GetBool(L"WindowMaximized", false);
    sidebarWidth = (f32)j.GetNum(L"QuickAccessWidth", sidebarWidth);
    sidebarClosed = j.GetBool(L"QuickAccessClosed", false);
    inspectorOpen = j.GetBool(L"InspectorOpen", false);
    inspectorSize = (f32)j.GetNum(L"InspectorSplitRatio", inspectorSize);
    selectedPanel = j.GetInt(L"SelectedPanel", 0);
    if (auto *l = j.Find(L"Layout")) layout = NodeFromJson(*l, 0);
}

// ------------------------------------------------ paths & locking
DataPaths ResolveDataPaths() {
    DataPaths d;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exeDir = PathParent(exe);
    if (GetFileAttributesW(PathJoin(exeDir, L"TwinDeck-Config.json").c_str()) != INVALID_FILE_ATTRIBUTES) {
        d.dir = exeDir; // portable mode
    } else {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
            d.dir = PathJoin(p, APP_DIR_NAME);
            CoTaskMemFree(p);
        } else {
            d.dir = exeDir;
        }
        SHCreateDirectoryExW(nullptr, d.dir.c_str(), nullptr);
    }
    d.config = PathJoin(d.dir, L"TwinDeck-Config.json");
    d.userData = PathJoin(d.dir, L"TwinDeck-UserData.json");
    d.session = PathJoin(d.dir, L"TwinDeck-Session.json");
    d.logs = PathJoin(d.dir, L"Logs");
    return d;
}

struct MutexLock {
    HANDLE m;
    explicit MutexLock(const wchar_t *name) {
        m = CreateMutexW(nullptr, FALSE, name);
        if (m) WaitForSingleObject(m, 3000);
    }
    ~MutexLock() {
        if (m) { ReleaseMutex(m); CloseHandle(m); }
    }
};

bool LoadJsonLocked(const std::wstring &path, const wchar_t *mutexName, Json &out) {
    MutexLock lk(mutexName);
    return JsonLoadFile(path, out);
}

bool SaveJsonLocked(const std::wstring &path, const wchar_t *mutexName, const Json &j) {
    MutexLock lk(mutexName);
    return JsonSaveFile(path, j);
}
