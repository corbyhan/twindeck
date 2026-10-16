// Application state & UI model
#pragma once
#include "common.h"
#include "render.h"
#include "fs.h"
#include "shell.h"
#include "settings.h"
#include "i18n.h"
#include "install.h"
#include <set>
#include <unordered_set>

// ---------------------------------------------------------------- theme
struct Theme {
    bool dark = true;
    Color bg, sidebarBg, panelBg, headerBg, tabActive, tabInactive;
    Color text, textDim, textFaint, accent, accentText;
    Color selection, selectionInactive, hover, border, rowAlt;
    Color danger, ok, popupBg, popupBorder, inputBg, scrollbar, highlight, folderIcon, cut;
};
Theme MakeTheme(bool dark);
bool SystemUsesDarkMode();

// ---------------------------------------------------------------- input
struct KeyEvent {
    u32 vk;
    bool ctrl, shift, alt;
    bool repeat;
};

struct Input {
    f32 mx = -1, my = -1;
    bool down[3] = {};
    bool pressed[3] = {};
    bool released[3] = {};
    int clickCount = 0;      // 1 = single, 2 = double (for pressed[0])
    f32 wheel = 0;           // notches (positive = up)
    f32 hwheel = 0;
    std::vector<KeyEvent> keys;
    std::wstring chars;      // committed text (WM_CHAR + IME results)
    bool ctrl = false, shift = false, alt = false;
    f32 pressX = 0, pressY = 0;
    bool mouseInWindow = false;
    void EndFrame() {
        for (int i = 0; i < 3; i++) pressed[i] = released[i] = false;
        wheel = hwheel = 0;
        keys.clear();
        chars.clear();
    }
};

// IME composition state (한글 조합)
struct ImeState {
    bool composing = false;
    std::wstring comp;       // current composition string (e.g. "한")
    int cursor = 0;
};

// ---------------------------------------------------------------- text edit
struct TextEdit {
    std::wstring text;
    u32 caret = 0;
    u32 anchor = 0;          // selection anchor
    f32 scrollX = 0;
    u64 blinkStart = 0;
    std::vector<std::wstring> undo;

    void Set(const std::wstring &s, bool selectAll = true) {
        text = s;
        caret = (u32)s.size();
        anchor = selectAll ? 0 : caret;
        scrollX = 0;
        undo.clear();
    }
    bool HasSel() const { return caret != anchor; }
    u32 SelMin() const { return std::min(caret, anchor); }
    u32 SelMax() const { return std::max(caret, anchor); }
    void Insert(const std::wstring &s);
    void DeleteSel();
    // returns true if consumed; sets *changed when text changed
    bool OnKey(const KeyEvent &k, HWND hwnd, bool *changed);
};

// ---------------------------------------------------------------- views
enum SortKey {
    SORT_NAME, SORT_MODIFIED, SORT_CREATED, SORT_ACCESSED, SORT_TYPE, SORT_SIZE,
    SORT_FREE, SORT_TOTAL, SORT_PERCENT, SORT_FS, SORT_ORIGLOC, SORT_DELETED, SORT_COUNT
};
enum GroupKey {
    GROUP_NONE, GROUP_NAME, GROUP_TYPE, GROUP_SIZE, GROUP_MODIFIED, GROUP_CREATED, GROUP_ACCESSED,
    GROUP_DELETED, GROUP_ORIGLOC, GROUP_FS, GROUP_FREE, GROUP_PERCENT, GROUP_COUNT
};
enum ViewKind { VIEW_FOLDER, VIEW_DRIVES, VIEW_RECYCLE };

#define PATH_THIS_PC   L""
#define PATH_RECYCLE   L"::RecycleBin"

enum ColumnId { COL_NAME, COL_MODIFIED, COL_CREATED, COL_TYPE, COL_SIZE, COL_FREE, COL_TOTAL, COL_PERCENT, COL_FS,
                COL_ORIGLOC, COL_DELETED, COL_STATUS, COL_COUNT };

struct Row {
    const FileEntry *e = nullptr;
    const std::wstring *dir = nullptr;  // folder containing e
    int depth = 0;
    int group = -1;
    bool header = false;
};

struct GroupInfo {
    std::wstring label;
    int count = 0;
    u64 bytes = 0;
    bool collapsed = false;
};

struct RecursiveSearch {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<u64> scanned{0};   // entries looked at so far (progress)
    std::atomic<bool> everything{false}; // answered by Everything's index
    std::wstring pattern;
};

// file list view modes (status bar slider: XL/L/M icons, list, tree, details)
enum ListViewMode { LV_DETAILS = 0, LV_TREE, LV_LIST, LV_ICONS, LV_COUNT };

// item rectangles for the grid views (list / icons), rebuilt when rows, width or cell size change
struct GridLayout {
    u64 rowsGen = ~0ull;
    int mode = -1;
    f32 width = -1, cellW = 0, cellH = 0, iconPx = 0;
    int cols = 1;
    std::vector<RectF> items;   // content coordinates (y grows down from 0)
    f32 contentH = 0;
};

struct Tab {
    int id = 0;
    std::wstring path;                // PATH_THIS_PC, PATH_RECYCLE or a folder
    ViewKind kind = VIEW_FOLDER;
    std::vector<std::wstring> back, fwd;

    std::vector<FileEntry> entries;
    std::unordered_map<std::wstring, std::vector<FileEntry>> children; // expanded tree folders
    std::set<std::wstring> expanded;
    ListError error = LIST_OK;
    bool loading = false;
    bool cloud = false;               // folder is inside a OneDrive / cloud files sync root
    int viewMode = LV_DETAILS;
    f32 iconSize = 96;                // icon view size in DIPs (48 = M, 96 = L, 256 = XL)
    GridLayout grid;
    u64 rowsGen = 0;                  // bumped whenever rows change (invalidates the grid layout)
    bool ensureCursor = false;        // scroll the cursor into view after the next layout (view mode changed)
    u64 loadGen = 0;
    u64 loadStartTick = 0;

    SortKey sort = SORT_NAME;
    bool desc = false;
    GroupKey group = GROUP_NONE;
    std::set<std::wstring> collapsedGroups;
    bool hideFiles = false, hideDirs = false;

    std::wstring filter;
    bool recursive = false;
    std::shared_ptr<RecursiveSearch> search;
    u64 searchDueTick = 0;            // restart the subfolder search at this tick (typing in the filter)
    std::vector<FileEntry> searchResults;

    std::vector<Row> rows;
    std::vector<GroupInfo> groups;
    bool rowsDirty = true;
    std::wstring cursorKeep;          // cursor row path preserved across DropRows()

    std::unordered_set<std::wstring> selected;  // full paths
    int cursor = 0;
    int anchor = -1;
    f32 scroll = 0, scrollTarget = 0;
    f32 viewH = 0;                    // list viewport height (set while drawing)
    std::wstring pendingSelect;       // name or path to select after load
    bool pendingRename = false;

    std::wstring typeBuf;             // type-to-select buffer
    u64 typeTick = 0;

    std::wstring Title() const;
    Tab();
    ~Tab(); // unregisters from the id registry
    Tab(const Tab &) = delete;
    Tab &operator=(const Tab &) = delete;
};

struct Panel {
    std::vector<std::unique_ptr<Tab>> tabs;
    int active = 0;
    f32 colW[COL_COUNT] = {};
    RectF rect;               // computed each frame
    RectF listRect;
    // folder columns strip above the list (foldercolumns.cpp): This PC, then every folder down to the current one
    bool folderColumns = false;
    std::wstring fcPath = L"\x01"; // tab path the strip was last laid out for (L"\x01" = never)
    f32 fcScrollX = 0;
    std::unordered_map<std::wstring, f32> fcScroll; // vertical scroll per column, keyed by its folder
    std::unordered_set<std::wstring> fcReveal;      // columns still to scroll their highlighted folder into view
    int fcTabId = 0;          // current-folder column: sorted entry indices, rebuilt when the tab's rows change
    u64 fcRowsGen = ~0ull;
    std::vector<int> fcCurrent;
    Tab *Cur() { return tabs.empty() ? nullptr : tabs[std::min<size_t>(active, tabs.size() - 1)].get(); }
};

// subfolders of a folder shown in the folder columns strip (loaded in the background)
struct FolderColumnList {
    std::vector<FileEntry> dirs; // sorted by name
    ListError err = LIST_OK;
    bool loading = false, loaded = false;
};

struct LayoutNode {
    bool leaf = true;
    bool vertical = false;    // children stacked top/bottom when true
    f32 ratio = 0.5f;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<LayoutNode> a, b;
    LayoutNode *parent = nullptr;
    RectF rect;
};

// ---------------------------------------------------------------- popups
enum PopupKind { POPUP_NONE, POPUP_PALETTE, POPUP_GOTO, POPUP_MENU, POPUP_OPTIONS, POPUP_CONFIRM, POPUP_FOLDER_DROPDOWN, POPUP_VIEW };

struct MenuItem {
    std::wstring label;
    std::wstring hotkey;
    std::function<void()> action;
    bool separator = false;
    bool checked = false;
    bool enabled = true;
    ID2D1Bitmap *icon = nullptr;
};

struct PaletteItem {
    std::wstring label;
    std::wstring detail;    // path or hotkey
    std::wstring key;       // command id or path
    int score = 0;
    bool bookmarked = false;
    ID2D1Bitmap *icon = nullptr;
};

struct Popup {
    PopupKind kind = POPUP_NONE;
    TextEdit edit;
    std::vector<PaletteItem> items;
    std::vector<MenuItem> menu;
    int sel = 0;
    f32 scroll = 0;
    f32 x = 0, y = 0;          // anchor for menus
    RectF rect;
    std::wstring message;      // confirm dialog
    std::function<void()> onConfirm;
    int optionsTab = 0;
    u64 openTick = 0;
    bool subMenu = false;      // dropdown (popup.menu) shown on top of the options window
    f32 subScroll = 0;
    f32 menuMinW = 0;          // dropdowns match the width of the field that opened them
    std::wstring typeAhead;    // type-to-find inside a dropdown (초성 OK)
    u64 typeAheadTick = 0;
    bool menuEnsureSel = false; // scroll the dropdown so `sel` is visible on the next draw
    int viewTabId = 0;         // view mode slider: the tab it changes
    bool viewDrag = false;
    std::wstring captureCmd;   // options > Shortcuts: command waiting for a key press
    u64 resetArmedTick = 0;    // options > Shortcuts: "reset all" asks for a second click
};

struct ToastMsg {
    std::wstring text;
    u64 tick = 0;
};

// focus target for keyboard / IME
enum FocusKind { FOCUS_LIST, FOCUS_FILTER, FOCUS_PATH, FOCUS_RENAME, FOCUS_POPUP, FOCUS_SETUP };

// ---------------------------------------------------------------- setup screen (setup.cpp)
struct SetupState {
    bool active = false;
    bool installerLaunch = false;  // started as the installer (Setup exe, --setup or the first start)
    bool resizedWindow = false;    // window was shrunk to installer size; restore the session size on close
    InstallInfo info;
    InstallOptions opts;
    TextEdit pathEdit;
    std::wstring status;
    bool statusError = false;
    ID2D1Bitmap *logo = nullptr;
};

// ---------------------------------------------------------------- inspector
struct InspectorState {
    std::wstring path;              // currently previewed
    bool isDir = false;
    bool loading = false;
    u64 gen = 0;
    // text preview
    bool isText = false;
    bool binary = false;
    bool tooLarge = false;
    std::vector<std::wstring> lines;
    // image preview
    ID2D1Bitmap *image = nullptr;
    u32 imgW = 0, imgH = 0;
    f32 scroll = 0;
    bool wordWrap = false;
    bool lineNumbers = true;
    bool showWhitespace = false;
    std::wstring info;              // extra details
};

// ---------------------------------------------------------------- commands
struct Command {
    const wchar_t *id;
    const wchar_t *nameEn;
    const wchar_t *nameKo;
    const wchar_t *aliases;   // search keywords (en,ko)
    std::function<void()> run;
    std::function<bool()> checked;  // optional toggle state
};

struct Hotkey {
    u32 vk;
    bool ctrl, shift, alt;
    std::wstring command;
};

// ---------------------------------------------------------------- app
struct App {
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;
    Renderer r;
    IconCache icons;
    ThumbCache thumbs;
    SetupState setup;
    FolderSizeCalc sizes;
    CloudStatusCache cloud;
    DirWatcher watcher;
    DataPaths paths;
    Settings settings;
    UserData user;
    SessionData session;
    Theme theme;
    f32 dpi = 96;
    f32 S = 1;                 // DPI scale

    Input in;
    ImeState ime;
    FocusKind focus = FOCUS_LIST;
    TextEdit filterEdit;       // active panel filter / path edit share UI
    TextEdit pathEdit;
    TextEdit renameEdit;
    std::wstring renamePath;
    bool pathEditing = false;
    bool filterEditing = false;

    std::unique_ptr<LayoutNode> root;
    Panel *active = nullptr;
    int nextTabId = 1;

    // sidebar
    bool sidebarOpen = true;
    f32 sidebarW = 220;
    f32 sidebarScroll = 0;
    // inspector
    bool inspectorOpen = false;
    f32 inspectorSize = 360;
    InspectorState insp;

    Popup popup;
    std::vector<ToastMsg> toasts;
    std::vector<Command> commands;
    std::vector<Hotkey> hotkeys;

    // interaction state
    u64 hotId = 0, activeId = 0;
    std::wstring tooltip;
    f32 tooltipX = 0, tooltipY = 0;
    u64 hoverStartTick = 0;
    u64 lastHoverId = 0;
    int dragSplitter = 0;          // 1 = sidebar, 2 = inspector, 3 = layout node
    LayoutNode *dragNode = nullptr;
    int dragColumn = -1;
    f32 dragColumnStart = 0, dragColumnW = 0;
    bool dragPending = false;      // file drag candidate
    std::wstring dropHighlight;    // folder path under a drop
    Tab *dropTab = nullptr;
    int dropRow = -1;
    bool needRedraw = true;
    bool quit = false;
    u64 frame = 0;
    std::wstring clipboardCutDir;
    std::unordered_set<std::wstring> cutPaths;
    std::vector<std::pair<std::wstring, std::wstring>> closedTabs; // for reopen
    std::unordered_map<std::wstring, u64> pendingReloads;           // debounced change notifications
    u64 lastSessionSave = 0;
    bool sessionDirty = false;
    bool ready = false;             // set when Init completes
    // custom title bar (tabs in the caption, own window buttons)
    f32 titleH = 0;
    std::vector<RectF> captionHits; // interactive rects inside the title bar (everything else drags the window)
    RectF maxButtonRect;            // reported as HTMAXBUTTON for Windows 11 snap layouts
    bool maxButtonPressed = false;
    bool ncHoverMax = false;
    bool mouseBlocked = false;      // true while drawing UI underneath an open popup
    int cursorKind = 0;             // 0 arrow, 1 size-we, 2 size-ns, 3 ibeam, 4 hand
    std::unordered_map<std::wstring, FolderColumnList> folderLists; // folder columns strip, by folder
    std::vector<FileEntry> sidebarDrives;
    u64 drivesTick = 0;
    bool drivesLoading = false;
    struct Place { StrId name; std::wstring path; const wchar_t *glyph; };
    std::vector<Place> places;
    f32 textDragActive = 0;
    TextEdit *mouseEdit = nullptr;  // text edit being mouse-selected

    // ---- lifecycle (app.cpp)
    bool Init(HINSTANCE inst, const std::wstring &cmdPath, bool setupRequested = false);
    void Shutdown();
    void LoadAll();
    void SaveSession();
    void SaveConfig();
    void SaveUserData();
    void ApplySettings();
    void ApplyBackdrop();
    void ApplyScreenshotScene();
    std::wstring shotSelect;
    void ApplyDpi(f32 newDpi);
    void Frame();               // process input + draw
    void Invalidate() { needRedraw = true; }

    // ---- layout / panels (app.cpp)
    Panel *NewPanelWith(const std::wstring &path);
    void ForEachPanel(const std::function<void(Panel *)> &fn);
    void SplitActive(bool vertical);
    void ClosePanel(Panel *p);
    void FocusPanel(Panel *p);
    void CycleFocus(int dir);
    void FocusNeighbor(int dx, int dy);
    void EvenPanels(bool siblingsOnly);
    LayoutNode *FindNode(Panel *p, LayoutNode *n = nullptr);
    Panel *NextPanel(Panel *p, int dir);

    // ---- tabs & navigation (app.cpp)
    Tab *CurTab() { return active ? active->Cur() : nullptr; }
    Tab *NewTab(Panel *p, const std::wstring &path, bool activate = true);
    void CloseTab(Panel *p, int index);
    void Navigate(Tab *t, const std::wstring &path, bool pushHistory = true, const std::wstring &selectName = L"");
    void GoBack(Tab *t);
    void GoForward(Tab *t);
    void GoUp(Tab *t);
    void Reload(Tab *t, bool keepSelection = true);
    void OnLoaded(int tabId, u64 gen, std::vector<FileEntry> &&entries, ListError err);
    Tab *FindTab(int id);
    void UpdateWatches();
    void OnDirChanged(const std::wstring &dir);
    std::wstring HomePath();
    std::wstring NewTabPath();
    void StartRecursiveSearch(Tab *t);

    // ---- rows / selection (app.cpp)
    void RebuildRows(Tab *t);
    void DropRows(Tab *t);
    std::wstring RowPath(const Row &r) const;
    std::wstring EntryPath(const FileEntry &e, const std::wstring &dir) const;
    std::vector<std::wstring> SelectedPaths(Tab *t, bool fallbackToCursor = true);
    void SelectOnly(Tab *t, int row);
    void SetCursor(Tab *t, int row, bool extend, bool keepSel = false);
    void EnsureVisible(Tab *t, int row);
    void ToggleExpand(Tab *t, int row, int mode); // mode: -1 collapse, 0 toggle, 1 expand
    bool Visible(const FileEntry &e) const;
    std::wstring DisplayName(const FileEntry &e) const;
    int RowAt(Tab *t, const std::wstring &path);
    f32 RowHeight() const;
    // list geometry shared by all view modes (content coordinates, i.e. before scrolling)
    bool IsGridView(const Tab *t) const { return t->viewMode == LV_LIST || t->viewMode == LV_ICONS; }
    void UpdateGrid(Tab *t, f32 width);
    RectF ItemRect(Tab *t, int row);
    int ItemAtPoint(Tab *t, f32 x, f32 y);     // -1 when no item
    f32 ListContentHeight(Tab *t);
    int PageItems(Tab *t);
    int GridNeighbor(Tab *t, int row, int dx, int dy);
    void SetViewMode(Tab *t, int mode, f32 iconSize = 0);
    void ZoomView(Tab *t, f32 notches);
    std::wstring ViewModeName(const Tab *t) const;
    const wchar_t *ViewModeGlyph(const Tab *t) const;

    // ---- actions (actions.cpp)
    void OpenRow(Tab *t, int row, int where); // 0 = here, 1 = new tab, 2 = new panel
    void OpenSelection(int where);
    void CopySelection(bool cut);
    void Paste();
    void DeleteSelection(bool permanent);
    void BeginRename();
    void CommitRename();
    void CancelRename();
    void CreateFolder();
    void CreateFileHere();
    void Duplicate();
    void CopyPathToClipboard(bool smartQuotes);
    void CopyToNeighbor(int dir, bool move);
    void ToggleBookmark(const std::wstring &path);
    bool IsBookmarked(const std::wstring &path) const;
    void ShowContextMenu(Tab *t, f32 x, f32 y, bool background);
    void ShowShellMenu(Tab *t, bool background, POINT screen);
    void RestoreRecycled();
    void EmptyRecycleBin();
    void Toast(const std::wstring &s);
    void Confirm(const std::wstring &msg, std::function<void()> onYes);
    void ShowFolderDropdown(Tab *t, const std::wstring &dir, f32 x, f32 y);
    void InsertDate(int which);

    // ---- commands & hotkeys (commands.cpp)
    void RegisterCommands();
    void BuildHotkeys();
    const Command *FindCommand(const std::wstring &id) const;
    bool RunCommand(const std::wstring &id);
    std::wstring HotkeyText(const std::wstring &cmdId) const;
    std::vector<std::wstring> CommandHotkeys(const std::wstring &cmd) const;
    std::vector<std::wstring> DefaultHotkeys(const std::wstring &cmd) const;
    bool HotkeysChanged(const std::wstring &cmd) const;
    void SetCommandHotkeys(const std::wstring &cmd, const std::vector<std::wstring> &keys);
    void ResetAllHotkeys();
    std::wstring KeyEventHotkey(const KeyEvent &k) const;
    std::wstring AssignHotkey(const std::wstring &cmd, const std::wstring &keys);
    std::wstring ResetCommandHotkeys(const std::wstring &cmd);
    std::wstring TakeHotkey(const std::wstring &cmd, const std::wstring &keys);
    bool HandleHotkey(const KeyEvent &k);

    // ---- input (input.cpp)
    void OnKey(const KeyEvent &k);
    void OnText(const std::wstring &s);
    void OnImeComposition(const std::wstring &comp, int cursor);
    void OnImeEnd();
    bool TextFocused() const { return focus != FOCUS_LIST || popup.kind == POPUP_PALETTE || popup.kind == POPUP_GOTO; }
    TextEdit *FocusedEdit();
    void UpdateImePosition(f32 x, f32 y, f32 h);
    void ListKey(Tab *t, const KeyEvent &k);
    void TypeSelect(Tab *t, const std::wstring &s);
    void BeginFilter(const std::wstring &initial);
    void EndFilter(bool clear);
    void BeginPathEdit();
    void EndPathEdit(bool commit);
    void ApplyFilter();

    // ---- drawing (ui.cpp)
    void Draw();
    void DrawSidebar(RectF r);
    void DrawCloudState(int state, RectF cell, u64 tipId);
    ID2D1Bitmap *HomeIcon();
    void DrawLayout(LayoutNode *n, RectF r);
    void DrawPanel(Panel *p, RectF r);
    f32 DrawTabs(Panel *p, RectF r); // returns the right edge of the "+" button
    void DrawTitleBar(RectF r);
    void ShowAppMenu(f32 x, f32 y);
    int  NonClientHitTest(int x, int y); // client coords -> HT* code
    void DrawHeader(Panel *p, Tab *t, RectF r);
    void DrawColumns(Panel *p, Tab *t, RectF r);
    void DrawList(Panel *p, Tab *t, RectF r);
    f32 FolderColumnsHeight() const;
    void DrawFolderColumns(Panel *p, Tab *t, RectF r);
    void LoadFolderList(const std::wstring &dir);
    void DrawGrid(Panel *p, Tab *t, RectF r);
    void OpenSetup(bool installerLaunch);
    void CloseSetup();
    void DrawSetup(RectF r);
    void SetupKey(const KeyEvent &k);
    bool RowPress(Tab *t, int i, const std::wstring &path, bool sel, bool toggleSel); // true = rows may have changed
    void ListMouseTail(Tab *t, RectF rc, bool hovList, int hoverRow, f32 sbW);
    void ListScroll(Tab *t, RectF rc, f32 contentH, f32 step);
    void OpenViewPopup(Tab *t, RectF anchor);
    void DrawViewPopup();
    void DrawStatus(Panel *p, Tab *t, RectF r);
    void DrawInspector(RectF r);
    void DrawPopup();
    void DrawToasts();
    void DrawTooltip();
    void DrawTextEdit(TextEdit &e, RectF r, FontId f, const Color &c, const std::wstring &placeholder, bool focused);
    void DrawScrollbar(RectF track, f32 contentH, f32 viewH, f32 &scroll, u64 id);
    std::vector<ColumnId> Columns(Tab *t) const;
    const wchar_t *ColumnName(ColumnId c) const;
    std::wstring CellText(Tab *t, const Row &row, ColumnId c);

    // immediate-mode helpers (ui.cpp)
    bool Hover(const RectF &r) const { return !mouseBlocked && r.Contains(in.mx, in.my); }
    bool Clicked(const RectF &r, int button = 0) const { return in.pressed[button] && Hover(r); }
    bool IconButton(u64 id, RectF r, const wchar_t *glyph, const std::wstring &tip, bool enabled = true, bool toggled = false,
                    FontId font = FONT_ICON_SMALL);
    void SetTooltip(u64 id, const std::wstring &tip);

    // ---- popups (popups.cpp)
    void OpenPalette(const std::wstring &initial = L"");
    void OpenGoTo(const std::wstring &initial = L"");
    void OpenMenu(std::vector<MenuItem> items, f32 x, f32 y);
    void OpenOptions();
    void ClosePopup();
    void RefreshPaletteItems();
    void PopupKey(const KeyEvent &k);
    void ActivatePaletteItem(int idx, bool alt);
    void DrawOptions(RectF r);
    void DrawShortcutOptions(RectF r);
    static const int kOptionsTabs = 5;  // general, appearance, behavior, system, shortcuts
    static const int kShortcutsTab = 4;
    // the options window's Shortcuts search box takes typed text (not while a key is being recorded)
    bool ShortcutSearchFocused() const {
        return popup.kind == POPUP_OPTIONS && popup.optionsTab == kShortcutsTab && popup.captureCmd.empty() && !popup.subMenu;
    }
    // draws popup.menu at (x, y); returns activated item index, -2 when a click landed outside, -1 otherwise
    int  DrawMenuBox(f32 x, f32 y, f32 &scroll, RectF *outRect);
    void OpenOptionsDropdown(std::vector<MenuItem> items, f32 x, f32 y, f32 minW);
    void DropdownTypeAhead(const std::wstring &s);

    // ---- inspector (inspector.cpp)
    void UpdateInspector();
    void ClearInspector();

    // ---- sizes
    std::shared_ptr<FolderStats> FolderSize(const std::wstring &path);
    bool CalcSizesFor(const std::wstring &path) const;
};

extern App *g_app;

// ids for immediate-mode widgets
inline u64 HashId(const void *p, u64 salt = 0) { return ((u64)(uintptr_t)p * 0x9E3779B97F4A7C15ull) ^ (salt * 0xC2B2AE3D27D4EB4Full); }
inline u64 HashStr(const std::wstring &s, u64 salt = 0) { return std::hash<std::wstring>()(s) ^ (salt * 0x9E3779B97F4A7C15ull); }

// icon glyphs (Segoe Fluent Icons / MDL2)
#define GLYPH_BACK      L""
#define GLYPH_FORWARD   L""
#define GLYPH_UP        L""
#define GLYPH_REFRESH   L""
#define GLYPH_SEARCH    L""
#define GLYPH_FILTER    L""
#define GLYPH_ADD       L""
#define GLYPH_CLOSE     L""
#define GLYPH_CHEVRON_R L""
#define GLYPH_CHEVRON_D L""
#define GLYPH_STAR      L""
#define GLYPH_STAR_FILL L""
#define GLYPH_SETTINGS  L""
#define GLYPH_SPLIT_R   L""
#define GLYPH_SPLIT_B   L""
#define GLYPH_PREVIEW   L""
#define GLYPH_SIDEBAR   L""
#define GLYPH_HOME      L""
#define GLYPH_PC        L""
#define GLYPH_DELETE    L""
#define GLYPH_CHECK     L""
#define GLYPH_MORE      L""
#define GLYPH_HISTORY   L""
#define GLYPH_SUBFOLDER L""
#define GLYPH_COMMAND   L""
#define GLYPH_SORT_UP   L""
#define GLYPH_SORT_DOWN L""
#define GLYPH_DRIVE     L""
#define GLYPH_FOLDER    L""
#define GLYPH_COLUMNS   L"\xE80A"
