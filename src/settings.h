// Config (settings), UserData (bookmarks, recents, hotkeys), Session (window/layout) persisted as JSON.
#pragma once
#include "common.h"
#include "json.h"

enum ColorSchemeSetting { SCHEME_SYSTEM = 0, SCHEME_DARK, SCHEME_LIGHT };
enum LanguageSetting { LANGSET_AUTO = 0, LANGSET_EN, LANGSET_KO };
enum StartupSetting { STARTUP_HOME = 0, STARTUP_LAST_SESSION, STARTUP_THIS_PC };
enum CharKeySetting { CHARKEY_SELECT = 0, CHARKEY_FILTER };
enum InspectorLayoutSetting { INSPECTOR_RIGHT = 0, INSPECTOR_BOTTOM };
// window background material (Windows 11 system backdrops; Windows 10 falls back to an acrylic blur)
enum BackdropSetting { BACKDROP_NONE = 0, BACKDROP_CLEAR, BACKDROP_MICA, BACKDROP_ACRYLIC, BACKDROP_TABBED, BACKDROP_COUNT };

struct Settings {
    int language = LANGSET_AUTO;
    int colorScheme = SCHEME_SYSTEM;
    std::wstring fontName = L"Segoe UI";
    f32 fontSize = 10.0f;
    std::wstring inspectorFontName = L"Consolas";
    f32 inspectorFontSize = 10.0f;
    int spacing = 4;             // row padding (0..12)
    bool showHiddenFiles = false;
    bool showSystemFiles = false;
    bool showFileExtension = true;
    bool showCheckboxes = false;
    bool showBreadcrumbs = true;
    bool showShortenedPaths = false;
    bool showTooltips = true;
    bool showRecents = true;
    bool alternatingRowColors = false;
    bool roundCorners = true;
    int backdrop = BACKDROP_NONE;
    int backgroundOpacity = 60;  // % opacity of the window background when a backdrop is used
    bool animations = true;
    bool underlineOnHover = false;
    bool vsync = true;
    bool singleClick = false;
    bool singleClickFoldersOnly = false;
    bool doubleClickToParent = true;
    int characterKeyAction = CHARKEY_FILTER;
    bool calcFolderSizes = true;
    bool calcFolderSizesSSD = true;
    bool calcFolderSizesHDD = false;
    bool calcFolderSizesNetwork = false;
    bool loadThumbnails = true;
    int loadOnStartup = STARTUP_LAST_SESSION;
    int newTabShow = 0;          // 0 = home, 1 = current folder, 2 = This PC
    int inspectorLayout = INSPECTOR_RIGHT;
    bool storeRecents = true;
    bool sortRecents = true;
    bool foldersFirst = true;
    bool reverseScroll = false;
    int assetCacheMB = 256;
    bool setupPrompted = false;  // the first start showed the setup screen once

    Json ToJson() const;
    void FromJson(const Json &j);
};

struct RecentItem {
    std::wstring path;
    f64 score = 0;      // frecency score with exponential decay
    u64 lastUse = 0;    // unix seconds
};

struct HotkeyBinding {
    std::wstring keys;    // e.g. "Ctrl+Shift+P"
    std::wstring command; // command id
};

struct UserData {
    std::vector<std::wstring> bookmarks;
    std::vector<std::wstring> commandBookmarks;
    std::vector<RecentItem> recents;
    std::vector<HotkeyBinding> hotkeys; // user overrides
    void TouchRecent(const std::wstring &path);
    void RemoveRecent(const std::wstring &path);
    std::vector<RecentItem> SortedRecents() const;
    Json ToJson() const;
    void FromJson(const Json &j);
};

// session layout tree (serialized form)
struct SessionTab {
    std::wstring path;
    int sort = 0;
    bool desc = false;
    int group = 0;
    int view = 0;
    f32 iconSize = 96;
};
struct SessionNode {
    bool leaf = true;
    bool vertical = false;
    f32 ratio = 0.5f;
    std::vector<SessionTab> tabs;
    int activeTab = 0;
    bool folderColumns = false;
    std::unique_ptr<SessionNode> a, b;
};

struct SessionData {
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 0, h = 0;
    bool maximized = false;
    f32 sidebarWidth = 220;
    bool sidebarClosed = false;
    bool inspectorOpen = false;
    f32 inspectorSize = 360;
    int selectedPanel = 0;
    std::unique_ptr<SessionNode> layout;
    Json ToJson() const;
    void FromJson(const Json &j);
};

struct DataPaths {
    std::wstring dir;
    std::wstring config, userData, session, logs;
};

DataPaths ResolveDataPaths(); // portable (next to exe) when a config file exists there
bool LoadJsonLocked(const std::wstring &path, const wchar_t *mutexName, Json &out);
bool SaveJsonLocked(const std::wstring &path, const wchar_t *mutexName, const Json &j);
u64 UnixNow();
