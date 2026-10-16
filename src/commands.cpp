// Command registry (command palette) and hotkeys
#include "app.h"
#include "jobs.h"


static void SetSort(App *a, SortKey k) {
    Tab *t = a->CurTab();
    if (!t) return;
    if (t->sort == k) t->desc = !t->desc;
    else {
        t->sort = k;
        t->desc = (k == SORT_MODIFIED || k == SORT_CREATED || k == SORT_ACCESSED || k == SORT_SIZE || k == SORT_DELETED ||
                   k == SORT_PERCENT || k == SORT_TOTAL);
    }
    t->rowsDirty = true;
    a->sessionDirty = true;
    a->Invalidate();
}

static void SetGroup(App *a, GroupKey g) {
    Tab *t = a->CurTab();
    if (!t) return;
    t->group = t->group == g ? GROUP_NONE : g;
    t->collapsedGroups.clear();
    t->rowsDirty = true;
    a->sessionDirty = true;
    a->Invalidate();
}

static void ToggleSetting(App *a, bool &v) {
    v = !v;
    a->SaveConfig();
    a->ForEachPanel([](Panel *p) { for (auto &t : p->tabs) t->rowsDirty = true; });
    a->Invalidate();
}

void App::RegisterCommands() {
    commands.clear();
    auto C = [&](const wchar_t *id, const wchar_t *en, const wchar_t *ko, const wchar_t *aliases, std::function<void()> fn,
                 std::function<bool()> checked = nullptr) {
        commands.push_back({id, en, ko, aliases, std::move(fn), std::move(checked)});
    };
    App *a = this;
#define TAB() Tab *t = a->CurTab(); if (!t) return

    // --- navigation
    C(L"Open", L"Open", L"열기", L"enter,실행", [a] { a->OpenSelection(0); });
    C(L"OpenInTab", L"Open In Tab", L"새 탭에서 열기", L"tab,탭", [a] { a->OpenSelection(1); });
    C(L"OpenInRightSplit", L"Open In Right Split", L"오른쪽 분할 창에서 열기", L"split,분할", [a] { a->OpenSelection(2); });
    C(L"OpenInBottomSplit", L"Open In Bottom Split", L"아래쪽 분할 창에서 열기", L"split,분할", [a] {
        Tab *t = a->CurTab(); if (!t) return;
        auto sel = a->SelectedPaths(t);
        std::wstring p = sel.size() == 1 && IsDirectory(sel[0]) ? sel[0] : t->path;
        a->SplitActive(true);
        a->Navigate(a->CurTab(), p, false);
    });
    C(L"MoveFolderBack", L"Back To Folder", L"뒤로", L"back,history,이전", [a] { a->GoBack(a->CurTab()); });
    C(L"MoveFolderForward", L"Forward To Folder", L"앞으로", L"forward,history,다음", [a] { a->GoForward(a->CurTab()); });
    C(L"MoveFolderUp", L"Up To Folder", L"상위 폴더로", L"parent,up,위로", [a] { a->GoUp(a->CurTab()); });
    C(L"GoHome", L"Go Home", L"홈으로", L"home,user,홈", [a] { a->Navigate(a->CurTab(), a->HomePath()); });
    C(L"GoThisPC", L"Go To This PC", L"내 PC로", L"drives,computer,드라이브", [a] { a->Navigate(a->CurTab(), PATH_THIS_PC); });
    C(L"GoRecycleBin", L"Go To Recycle Bin", L"휴지통으로", L"trash,bin,휴지통", [a] { a->Navigate(a->CurTab(), PATH_RECYCLE); });
    C(L"GoToOpen", L"Go To…", L"이동…", L"goto,jump,path,경로,이동", [a] { a->OpenGoTo(); });
    C(L"EditPath", L"Open Address Bar", L"주소 표시줄 편집", L"path,address,주소", [a] { a->BeginPathEdit(); });
    C(L"CommandsOpen", L"Command Palette", L"명령 팔레트", L"commands,palette,명령", [a] { a->OpenPalette(); });
    C(L"Refresh", L"Refresh", L"새로 고침", L"reload,f5,새로", [a] { TAB(); a->sizes.Invalidate(t->path); a->Reload(t); });
    C(L"OpenContainingPath", L"Open Containing Path", L"포함된 폴더 열기", L"parent,location,위치", [a] {
        TAB();
        if (t->cursor >= (int)t->rows.size() || t->rows[t->cursor].header) return;
        const Row &r = t->rows[t->cursor];
        std::wstring p = a->RowPath(r);
        if (r.e->isRecycled) p = PathJoin(r.e->origLocation, r.e->name);
        a->Navigate(t, PathParent(p), true, PathFileName(p));
    });

    // --- tabs & panels
    C(L"NewTab", L"New Tab", L"새 탭", L"tab,open,탭", [a] { a->NewTab(a->active, a->NewTabPath()); });
    C(L"DuplicateTab", L"Duplicate Tab", L"탭 복제", L"tab,copy,탭", [a] { TAB(); a->NewTab(a->active, t->path); });
    C(L"CloseTab", L"Close Tab", L"탭 닫기", L"exit,탭", [a] { if (a->active) a->CloseTab(a->active, a->active->active); });
    C(L"CloseOtherTabs", L"Close Other Tabs", L"다른 탭 닫기", L"exit,탭", [a] {
        Panel *p = a->active; if (!p) return;
        auto keep = std::move(p->tabs[p->active]);
        p->tabs.clear(); p->tabs.push_back(std::move(keep)); p->active = 0;
        a->UpdateWatches(); a->Invalidate();
    });
    C(L"CloseAllTabs", L"Close All Tabs", L"모든 탭 닫기", L"exit,탭", [a] {
        Panel *p = a->active; if (!p) return;
        auto keep = std::move(p->tabs[p->active]);
        p->tabs.clear(); p->tabs.push_back(std::move(keep)); p->active = 0;
        a->Navigate(p->Cur(), a->HomePath());
    });
    C(L"ReopenClosedTab", L"Reopen Closed Tab", L"닫은 탭 다시 열기", L"undo,restore,탭", [a] {
        if (a->closedTabs.empty()) return;
        auto p = a->closedTabs.back(); a->closedTabs.pop_back();
        a->NewTab(a->active, p.first);
    });
    C(L"NextTab", L"Next Tab", L"다음 탭", L"tab,탭", [a] {
        Panel *p = a->active; if (!p || p->tabs.empty()) return;
        p->active = (p->active + 1) % (int)p->tabs.size(); a->ClearInspector(); a->UpdateInspector(); a->Invalidate();
    });
    C(L"PrevTab", L"Previous Tab", L"이전 탭", L"tab,탭", [a] {
        Panel *p = a->active; if (!p || p->tabs.empty()) return;
        p->active = (p->active + (int)p->tabs.size() - 1) % (int)p->tabs.size(); a->ClearInspector(); a->UpdateInspector(); a->Invalidate();
    });
    C(L"PanelSplitRight", L"Split Right", L"오른쪽으로 분할", L"vertical,window,분할", [a] { a->SplitActive(false); });
    C(L"PanelSplitBottom", L"Split Bottom", L"아래로 분할", L"horizontal,window,분할", [a] { a->SplitActive(true); });
    C(L"ClosePanel", L"Close Panel", L"패널 닫기", L"window,패널", [a] { if (a->active) a->ClosePanel(a->active); });
    C(L"PanelSelectNext", L"Select Next Panel", L"다음 패널 선택", L"window,패널", [a] { a->CycleFocus(1); });
    C(L"PanelSelectPrev", L"Select Previous Panel", L"이전 패널 선택", L"window,패널", [a] { a->CycleFocus(-1); });
    C(L"PanelSelectLeft", L"Select Left Panel", L"왼쪽 패널 선택", L"window,패널", [a] { a->FocusNeighbor(-1, 0); });
    C(L"PanelSelectRight", L"Select Right Panel", L"오른쪽 패널 선택", L"window,패널", [a] { a->FocusNeighbor(1, 0); });
    C(L"PanelSelectUp", L"Select Panel Above", L"위쪽 패널 선택", L"window,up,패널", [a] { a->FocusNeighbor(0, -1); });
    C(L"PanelSelectDown", L"Select Panel Below", L"아래쪽 패널 선택", L"window,down,패널", [a] { a->FocusNeighbor(0, 1); });
    C(L"PanelEvenAll", L"Even All Panels", L"모든 패널 크기 같게", L"equal,window,균등", [a] { a->EvenPanels(false); });
    C(L"PanelEvenSiblings", L"Even Panel Siblings", L"인접 패널 크기 같게", L"equal,window,균등", [a] { a->EvenPanels(true); });

    // --- files
    C(L"Copy", L"Copy", L"복사", L"clipboard,복사", [a] { a->CopySelection(false); });
    C(L"Cut", L"Cut", L"잘라내기", L"clipboard,move,이동", [a] { a->CopySelection(true); });
    C(L"Paste", L"Paste", L"붙여넣기", L"clipboard,붙여", [a] { a->Paste(); });
    C(L"CopyPath", L"Copy Path", L"경로 복사", L"path,clipboard,경로", [a] { a->CopyPathToClipboard(false); });
    C(L"CopyPathSmart", L"Copy As Path (Smart)", L"경로로 복사 (스마트)", L"path,quotes,경로", [a] { a->CopyPathToClipboard(true); });
    C(L"CopyFilesToNextPanel", L"Copy Files To Next Panel", L"다음 패널로 복사", L"copy,panel,복사", [a] { a->CopyToNeighbor(1, false); });
    C(L"CopyFilesToPrevPanel", L"Copy Files To Previous Panel", L"이전 패널로 복사", L"copy,panel,복사", [a] { a->CopyToNeighbor(-1, false); });
    C(L"MoveFilesToNextPanel", L"Move Files To Next Panel", L"다음 패널로 이동", L"move,panel,이동", [a] { a->CopyToNeighbor(1, true); });
    C(L"Duplicate", L"Duplicate", L"복제", L"copy,clone,복사본", [a] { a->Duplicate(); });
    C(L"Rename", L"Rename", L"이름 바꾸기", L"f2,name,이름", [a] { a->BeginRename(); });
    C(L"Trash", L"Trash", L"휴지통으로 이동", L"delete,remove,삭제", [a] { a->DeleteSelection(false); });
    C(L"DeletePermanently", L"Delete", L"영구 삭제", L"remove,permanent,삭제", [a] { a->DeleteSelection(true); });
    C(L"CreateFolder", L"Create Folder", L"새 폴더 만들기", L"new,directory,mkdir,폴더", [a] { a->CreateFolder(); });
    C(L"CreateFile", L"Create File", L"새 파일 만들기", L"new,touch,파일", [a] { a->CreateFileHere(); });
    C(L"Properties", L"Properties", L"속성", L"info,details,속성", [a] { TAB(); auto s = a->SelectedPaths(t); if (s.empty() && t->kind == VIEW_FOLDER) s.push_back(t->path); ShellProperties(a->hwnd, s); });
    C(L"ContextMenuOpen", L"Open Context Menu", L"컨텍스트 메뉴 열기", L"menu,right click,메뉴", [a] {
        TAB();
        RectF ir = a->ItemRect(t, t->cursor);
        a->ShowContextMenu(t, a->active->listRect.x + ir.x + 40 * a->S, a->active->listRect.y + ir.Bottom() - t->scroll, false);
    });
    C(L"ShellMenuOpen", L"Open Windows Context Menu", L"Windows 컨텍스트 메뉴", L"shell,menu,메뉴", [a] {
        TAB(); POINT pt; GetCursorPos(&pt); a->ShowShellMenu(t, t->selected.empty(), pt);
    });
    C(L"RestoreTrashItems", L"Restore Trash Items", L"휴지통 항목 복원", L"undelete,restore,복원", [a] { a->RestoreRecycled(); });
    C(L"EmptyTrashItems", L"Empty Trash Items", L"휴지통 비우기", L"remove,empty,비우기", [a] { a->EmptyRecycleBin(); });

    // --- selection
    C(L"SelectAll", L"Select All", L"모두 선택", L"entries,전체", [a] {
        TAB(); t->selected.clear();
        for (auto &r : t->rows) if (!r.header) t->selected.insert(a->RowPath(r));
        a->UpdateInspector(); a->Invalidate();
    });
    C(L"SelectNone", L"Select None", L"선택 해제", L"clear,해제", [a] { TAB(); t->selected.clear(); a->Invalidate(); });
    C(L"InvertSelection", L"Invert Selection", L"선택 반전", L"invert,반전", [a] {
        TAB(); std::unordered_set<std::wstring> n;
        for (auto &r : t->rows) if (!r.header) { auto p = a->RowPath(r); if (!t->selected.count(p)) n.insert(p); }
        t->selected.swap(n); a->Invalidate();
    });

    // --- filter / search
    C(L"Filter", L"Filter", L"필터", L"search,find,검색,찾기", [a] { a->BeginFilter(L""); });
    C(L"SearchInSubfolders", L"Search In Subfolders", L"하위 폴더에서 검색", L"filter,recursive,flatten,검색", [a] {
        TAB(); if (t->kind != VIEW_FOLDER) return;
        t->recursive = !t->recursive;
        a->BeginFilter(L"");
        a->StartRecursiveSearch(t);
        a->Invalidate();
    }, [a] { Tab *t = a->CurTab(); return t && t->recursive; });

    // --- tree / groups
    C(L"ToggleSubfolders", L"Toggle Subfolders", L"하위 폴더 펼치기/접기", L"unfold,tree,hierarchy,트리", [a] { TAB(); a->ToggleExpand(t, t->cursor, 0); });
    C(L"CollapseAll", L"Collapse All", L"모두 접기", L"tree,close,접기", [a] {
        TAB(); a->DropRows(t); t->expanded.clear(); t->children.clear();
        for (auto &g : t->groups) t->collapsedGroups.insert(g.label);
        t->rowsDirty = true; a->UpdateWatches(); a->Invalidate();
    });
    C(L"ExpandAllGroups", L"Expand All Groups", L"모든 그룹 펼치기", L"open,show,펼치기", [a] { TAB(); t->collapsedGroups.clear(); t->rowsDirty = true; a->Invalidate(); });

    // --- sort & group
    C(L"SortByName", L"Sort By Name", L"이름순 정렬", L"order,정렬", [a] { SetSort(a, SORT_NAME); });
    C(L"SortByDateModified", L"Sort By Date Modified", L"수정한 날짜순 정렬", L"order,date,정렬,날짜", [a] { SetSort(a, SORT_MODIFIED); });
    C(L"SortByDateCreated", L"Sort By Date Created", L"만든 날짜순 정렬", L"order,date,정렬,날짜", [a] { SetSort(a, SORT_CREATED); });
    C(L"SortByDateAccessed", L"Sort By Date Accessed", L"액세스한 날짜순 정렬", L"order,date,정렬", [a] { SetSort(a, SORT_ACCESSED); });
    C(L"SortByType", L"Sort By Type", L"유형순 정렬", L"order,extension,정렬,확장자", [a] { SetSort(a, SORT_TYPE); });
    C(L"SortBySize", L"Sort By Size", L"크기순 정렬", L"order,정렬,용량", [a] { SetSort(a, SORT_SIZE); });
    C(L"SortByFreeSpace", L"Sort By Free Space", L"남은 공간순 정렬", L"order,drive,정렬", [a] { SetSort(a, SORT_FREE); });
    C(L"SortByPercentFull", L"Sort By Percent Full", L"사용률순 정렬", L"order,drive,정렬", [a] { SetSort(a, SORT_PERCENT); });
    C(L"SortByDateDeleted", L"Sort By Date Deleted", L"삭제한 날짜순 정렬", L"order,trash,정렬", [a] { SetSort(a, SORT_DELETED); });
    C(L"SortByOriginalLocation", L"Sort By Original Location", L"원래 위치순 정렬", L"order,trash,정렬", [a] { SetSort(a, SORT_ORIGLOC); });
    C(L"ViewDetails", L"View: Details", L"보기: 자세히", L"view,layout,columns,보기,자세히", [a] { TAB(); a->SetViewMode(t, LV_DETAILS); });
    C(L"ViewTree", L"View: Tree", L"보기: 트리", L"view,layout,보기,트리", [a] { TAB(); a->SetViewMode(t, LV_TREE); });
    C(L"ViewList", L"View: List", L"보기: 목록", L"view,layout,small icons,보기,목록", [a] { TAB(); a->SetViewMode(t, LV_LIST); });
    C(L"ViewIconsMedium", L"View: Medium Icons", L"보기: 보통 아이콘", L"view,thumbnails,보기,아이콘", [a] { TAB(); a->SetViewMode(t, LV_ICONS, 48); });
    C(L"ViewIconsLarge", L"View: Large Icons", L"보기: 큰 아이콘", L"view,thumbnails,보기,아이콘", [a] { TAB(); a->SetViewMode(t, LV_ICONS, 96); });
    C(L"ViewIconsExtraLarge", L"View: Extra Large Icons", L"보기: 아주 큰 아이콘", L"view,thumbnails,보기,아이콘", [a] { TAB(); a->SetViewMode(t, LV_ICONS, 256); });
    C(L"ViewZoomIn", L"View: Bigger Icons", L"보기: 아이콘 크게", L"zoom,view,보기,확대", [a] { TAB(); a->ZoomView(t, 1); });
    C(L"ViewZoomOut", L"View: Smaller Icons", L"보기: 아이콘 작게", L"zoom,view,보기,축소", [a] { TAB(); a->ZoomView(t, -1); });
    C(L"GroupByNone", L"Group By None", L"그룹 없음", L"group,그룹", [a] { TAB(); t->group = GROUP_NONE; t->rowsDirty = true; a->Invalidate(); });
    C(L"GroupByName", L"Group By Name", L"이름으로 그룹화 (초성)", L"group,alphabet,그룹,초성", [a] { SetGroup(a, GROUP_NAME); });
    C(L"GroupByType", L"Group By Type", L"유형으로 그룹화", L"group,extension,그룹", [a] { SetGroup(a, GROUP_TYPE); });
    C(L"GroupBySize", L"Group By Size", L"크기로 그룹화", L"group,그룹", [a] { SetGroup(a, GROUP_SIZE); });
    C(L"GroupByDateModified", L"Group By Date Modified", L"수정한 날짜로 그룹화", L"group,day,month,year,그룹", [a] { SetGroup(a, GROUP_MODIFIED); });
    C(L"GroupByDateCreated", L"Group By Date Created", L"만든 날짜로 그룹화", L"group,day,month,year,그룹", [a] { SetGroup(a, GROUP_CREATED); });
    C(L"GroupByDateAccessed", L"Group By Date Accessed", L"액세스한 날짜로 그룹화", L"group,그룹", [a] { SetGroup(a, GROUP_ACCESSED); });
    C(L"GroupByDateDeleted", L"Group By Date Deleted", L"삭제한 날짜로 그룹화", L"group,trash,그룹", [a] { SetGroup(a, GROUP_DELETED); });
    C(L"GroupByOriginalLocation", L"Group By Original Location", L"원래 위치로 그룹화", L"group,trash,그룹", [a] { SetGroup(a, GROUP_ORIGLOC); });
    C(L"GroupByFileSystem", L"Group By File System", L"파일 시스템으로 그룹화", L"group,drive,그룹", [a] { SetGroup(a, GROUP_FS); });
    C(L"GroupByPercentFull", L"Group By Percent Full", L"사용률로 그룹화", L"group,drive,그룹", [a] { SetGroup(a, GROUP_PERCENT); });

    // --- view toggles
    C(L"ToggleHiddenFiles", L"Toggle Hidden Files", L"숨김 파일 표시", L"show,hide,숨김", [a] { ToggleSetting(a, a->settings.showHiddenFiles); },
      [a] { return a->settings.showHiddenFiles; });
    C(L"ToggleSystemFiles", L"Toggle System Files", L"시스템 파일 표시", L"show,hide,시스템", [a] { ToggleSetting(a, a->settings.showSystemFiles); },
      [a] { return a->settings.showSystemFiles; });
    C(L"ToggleFileExtension", L"Toggle File Extension", L"파일 확장자 표시", L"show,hide,확장자", [a] { ToggleSetting(a, a->settings.showFileExtension); },
      [a] { return a->settings.showFileExtension; });
    C(L"ToggleCheckboxes", L"Toggle Checkboxes", L"확인란 표시", L"checkbox,확인란", [a] { ToggleSetting(a, a->settings.showCheckboxes); },
      [a] { return a->settings.showCheckboxes; });
    C(L"ToggleFiles", L"Toggle Files", L"파일 표시/숨기기", L"show,hide,파일", [a] { TAB(); t->hideFiles = !t->hideFiles; t->rowsDirty = true; a->Invalidate(); },
      [a] { Tab *t = a->CurTab(); return t && !t->hideFiles; });
    C(L"ToggleFolders", L"Toggle Folders", L"폴더 표시/숨기기", L"show,hide,폴더", [a] { TAB(); t->hideDirs = !t->hideDirs; t->rowsDirty = true; a->Invalidate(); },
      [a] { Tab *t = a->CurTab(); return t && !t->hideDirs; });
    C(L"ToggleFoldersFirst", L"Folders First", L"폴더 먼저 정렬", L"sort,폴더", [a] { ToggleSetting(a, a->settings.foldersFirst); },
      [a] { return a->settings.foldersFirst; });
    C(L"Animations", L"Animations", L"애니메이션", L"smooth,scroll,애니메이션", [a] { ToggleSetting(a, a->settings.animations); },
      [a] { return a->settings.animations; });
    C(L"InspectorToggle", L"Toggle Inspector", L"미리 보기 창 표시", L"preview,properties,미리보기", [a] {
        a->inspectorOpen = !a->inspectorOpen; a->sessionDirty = true;
        if (a->inspectorOpen) a->UpdateInspector(); else a->ClearInspector();
        a->Invalidate();
    }, [a] { return a->inspectorOpen; });
    C(L"InspectorChangeLayout", L"Toggle Inspector Layout", L"미리 보기 위치 전환", L"change,switch,flip,미리보기", [a] {
        a->settings.inspectorLayout = 1 - a->settings.inspectorLayout; a->SaveConfig(); a->Invalidate();
    });
    C(L"InspectorToggleWordWrap", L"Toggle Inspector Word Wrap", L"미리 보기 자동 줄 바꿈", L"wrap,줄바꿈", [a] { a->insp.wordWrap = !a->insp.wordWrap; a->Invalidate(); },
      [a] { return a->insp.wordWrap; });
    C(L"InspectorToggleLineNumbers", L"Toggle Inspector Line Numbers", L"미리 보기 줄 번호", L"lines,줄번호", [a] { a->insp.lineNumbers = !a->insp.lineNumbers; a->Invalidate(); },
      [a] { return a->insp.lineNumbers; });
    C(L"InspectorToggleWhitespace", L"Toggle Inspector Whitespace", L"미리 보기 공백 표시", L"whitespace,공백", [a] { a->insp.showWhitespace = !a->insp.showWhitespace; a->Invalidate(); },
      [a] { return a->insp.showWhitespace; });
    C(L"ToggleFolderColumns", L"Toggle Folder Columns", L"폴더 구조 보기", L"columns,tree,structure,miller,폴더 구조,열", [a] {
        if (!a->active) return;
        a->active->folderColumns = !a->active->folderColumns;
        a->sessionDirty = true;
        a->Invalidate();
    }, [a] { return a->active && a->active->folderColumns; });
    C(L"ToggleSidebar", L"Toggle Quick Access", L"빠른 실행 사이드바", L"sidebar,quick access,사이드바", [a] { a->sidebarOpen = !a->sidebarOpen; a->sessionDirty = true; a->Invalidate(); },
      [a] { return a->sidebarOpen; });
    C(L"IncreaseFontSize", L"Increase Font Size", L"글꼴 크게", L"large,big,zoom,확대", [a] {
        a->settings.fontSize = std::min(32.f, a->settings.fontSize + 0.5f); a->SaveConfig(); a->ApplySettings(); });
    C(L"DecreaseFontSize", L"Decrease Font Size", L"글꼴 작게", L"small,zoom,축소", [a] {
        a->settings.fontSize = std::max(6.f, a->settings.fontSize - 0.5f); a->SaveConfig(); a->ApplySettings(); });
    C(L"ResetFontSize", L"Reset Font Size", L"글꼴 크기 초기화", L"zoom,reset,초기화", [a] { a->settings.fontSize = 10; a->SaveConfig(); a->ApplySettings(); });
    C(L"IncreaseSpacing", L"Increase Spacing", L"행 간격 넓게", L"padding,간격", [a] { a->settings.spacing = std::min(12, a->settings.spacing + 1); a->SaveConfig(); a->Invalidate(); });
    C(L"DecreaseSpacing", L"Decrease Spacing", L"행 간격 좁게", L"padding,간격", [a] { a->settings.spacing = std::max(0, a->settings.spacing - 1); a->SaveConfig(); a->Invalidate(); });
    C(L"ToggleTheme", L"Toggle Dark/Light Theme", L"다크/라이트 테마 전환", L"color,scheme,dark,light,테마", [a] {
        a->settings.colorScheme = a->theme.dark ? SCHEME_LIGHT : SCHEME_DARK; a->SaveConfig(); a->ApplySettings(); });
    C(L"ToggleLanguage", L"Toggle Language (한국어/English)", L"언어 전환 (한국어/English)", L"language,korean,english,언어,한국어", [a] {
        a->settings.language = I18nGetLanguage() == LANG_KO ? LANGSET_EN : LANGSET_KO; a->SaveConfig(); a->ApplySettings(); });

    // --- bookmarks / recents
    C(L"BookmarksToggle", L"Toggle Folder Bookmark", L"현재 폴더 즐겨찾기", L"save,directory,bookmark,즐겨찾기", [a] { TAB(); a->ToggleBookmark(t->path); },
      [a] { Tab *t = a->CurTab(); return t && a->IsBookmarked(t->path); });
    C(L"BookmarksClear", L"Clear Bookmarks", L"즐겨찾기 모두 지우기", L"delete,remove,즐겨찾기", [a] {
        a->Confirm(TL(L"Clear all bookmarks?", L"즐겨찾기를 모두 지울까요?"), [a] { a->user.bookmarks.clear(); a->SaveUserData(); });
    });
    C(L"ClearRecents", L"Clear Recents", L"최근 폴더 지우기", L"delete,remove,history,최근", [a] { a->user.recents.clear(); a->SaveUserData(); a->Invalidate(); });

    // --- system
    C(L"OptionsOpen", L"Options", L"옵션", L"settings,preferences,설정", [a] { a->OpenOptions(); });
    C(L"ConfigOpen", L"Open Config File", L"설정 파일 열기", L"json,config,설정", [a] { a->SaveConfig(); ShellOpen(a->hwnd, a->paths.config, L"open"); });
    C(L"OpenConsole", L"Open Console", L"터미널 열기", L"command,prompt,terminal,cmd,터미널", [a] { TAB(); if (t->kind == VIEW_FOLDER) OpenConsoleAt(t->path); });
    C(L"OpenFileExplorer", L"Open File Explorer", L"파일 탐색기 열기", L"explorer,탐색기", [a] {
        TAB(); auto s = a->SelectedPaths(t, false);
        OpenExplorerAt(t->kind == VIEW_FOLDER ? t->path : L"", s.size() == 1 ? s[0] : L"");
    });
    C(L"CalcDirsContents", L"Refresh Folder Stats", L"폴더 크기 다시 계산", L"reload,calculate,size,크기", [a] {
        TAB(); a->sizes.CancelAll();
        for (auto &e : t->entries) if (e.isDir) a->sizes.Get(PathJoin(t->path, e.name), true);
        a->Invalidate();
    });
    C(L"InsertCurrentDate", L"Insert Current Date", L"오늘 날짜 넣기", L"day,month,year,날짜", [a] { a->InsertDate(0); });
    C(L"InsertFileCreateDate", L"Insert File Create Date", L"파일 만든 날짜 넣기", L"date,날짜", [a] { a->InsertDate(1); });
    C(L"InsertFileModifiedDate", L"Insert File Last Modified Date", L"파일 수정한 날짜 넣기", L"date,날짜", [a] { a->InsertDate(2); });
    C(L"NewWindow", L"New Window", L"새 창", L"window,창", [a] {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        Tab *t = a->CurTab();
        std::wstring args = t && t->kind == VIEW_FOLDER ? L"\"" + t->path + L"\"" : L"";
        ShellExecuteW(nullptr, L"open", exe, args.c_str(), nullptr, SW_SHOWNORMAL);
    });
    C(L"CloseWindow", L"Close Window", L"창 닫기", L"exit,quit,종료", [a] { PostMessageW(a->hwnd, WM_CLOSE, 0, 0); });
    C(L"MinimizeWindow", L"Minimize Window", L"창 최소화", L"minimize,최소화", [a] { ShowWindow(a->hwnd, SW_MINIMIZE); });
    C(L"Setup", L"Install / Setup", L"설치 / 설치 설정", L"install,setup,uninstall,default,path,설치,제거", [a] { a->OpenSetup(false); });
#undef TAB
}

const Command *App::FindCommand(const std::wstring &id) const {
    for (auto &c : commands) if (id == c.id) return &c;
    return nullptr;
}

bool App::RunCommand(const std::wstring &id) {
    const Command *c = FindCommand(id);
    if (!c) return false;
    c->run();
    Invalidate();
    return true;
}

// ------------------------------------------------------------------ hotkeys
struct KeyName { const wchar_t *name; u32 vk; };
static const KeyName kKeyNames[] = {
    {L"Enter", VK_RETURN}, {L"Esc", VK_ESCAPE}, {L"Escape", VK_ESCAPE}, {L"Space", VK_SPACE}, {L"Tab", VK_TAB},
    {L"Backspace", VK_BACK}, {L"Delete", VK_DELETE}, {L"Del", VK_DELETE}, {L"Insert", VK_INSERT}, {L"Home", VK_HOME},
    {L"End", VK_END}, {L"PageUp", VK_PRIOR}, {L"PageDown", VK_NEXT}, {L"Up", VK_UP}, {L"Down", VK_DOWN},
    {L"Left", VK_LEFT}, {L"Right", VK_RIGHT}, {L"Plus", VK_OEM_PLUS}, {L"=", VK_OEM_PLUS}, {L"Minus", VK_OEM_MINUS},
    {L"-", VK_OEM_MINUS}, {L"Comma", VK_OEM_COMMA}, {L",", VK_OEM_COMMA}, {L"Period", VK_OEM_PERIOD}, {L".", VK_OEM_PERIOD},
    {L"Slash", VK_OEM_2}, {L"/", VK_OEM_2}, {L"Backtick", VK_OEM_3}, {L"`", VK_OEM_3}, {L"[", VK_OEM_4},
    {L"Backslash", VK_OEM_5}, {L"\\", VK_OEM_5}, {L"]", VK_OEM_6}, {L"Quote", VK_OEM_7}, {L";", VK_OEM_1},
    {L"Apps", VK_APPS},
};

static bool ParseHotkey(const std::wstring &s, Hotkey &h) {
    h = Hotkey();
    size_t start = 0;
    while (start < s.size()) {
        size_t plus = s.find(L'+', start);
        if (plus == start) plus = s.find(L'+', start + 1); // key "+" itself
        std::wstring part = s.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
        start = plus == std::wstring::npos ? s.size() : plus + 1;
        if (_wcsicmp(part.c_str(), L"Ctrl") == 0) h.ctrl = true;
        else if (_wcsicmp(part.c_str(), L"Shift") == 0) h.shift = true;
        else if (_wcsicmp(part.c_str(), L"Alt") == 0) h.alt = true;
        else if (part.size() == 1 && iswalnum(part[0])) h.vk = towupper(part[0]);
        else if (part.size() >= 2 && (part[0] == L'F' || part[0] == L'f') && iswdigit(part[1])) h.vk = VK_F1 + _wtoi(part.c_str() + 1) - 1;
        else {
            bool found = false;
            for (auto &k : kKeyNames) if (_wcsicmp(k.name, part.c_str()) == 0) { h.vk = k.vk; found = true; break; }
            if (!found) return false;
        }
    }
    return h.vk != 0;
}

static std::wstring HotkeyToString(const Hotkey &h) {
    std::wstring s;
    if (h.ctrl) s += L"Ctrl+";
    if (h.shift) s += L"Shift+";
    if (h.alt) s += L"Alt+";
    if ((h.vk >= 'A' && h.vk <= 'Z') || (h.vk >= '0' && h.vk <= '9')) s += (wchar_t)h.vk;
    else if (h.vk >= VK_F1 && h.vk <= VK_F24) s += Format(L"F%d", h.vk - VK_F1 + 1);
    else {
        for (auto &k : kKeyNames) if (k.vk == h.vk) { s += k.name; break; }
    }
    return s;
}

static const wchar_t *kDefaultHotkeys[][2] = {
    {L"Ctrl+Shift+P", L"CommandsOpen"}, {L"Ctrl+P", L"GoToOpen"}, {L"Ctrl+L", L"EditPath"}, {L"Alt+D", L"EditPath"},
    {L"Ctrl+T", L"NewTab"}, {L"Ctrl+W", L"CloseTab"}, {L"Ctrl+Shift+T", L"ReopenClosedTab"}, {L"Ctrl+Tab", L"NextTab"},
    {L"Ctrl+Shift+Tab", L"PrevTab"}, {L"Alt+Left", L"MoveFolderBack"}, {L"Alt+Right", L"MoveFolderForward"},
    {L"Alt+Up", L"MoveFolderUp"}, {L"Ctrl+Up", L"MoveFolderUp"}, {L"F5", L"Refresh"}, {L"Ctrl+R", L"Refresh"},
    {L"Ctrl+C", L"Copy"}, {L"Ctrl+X", L"Cut"}, {L"Ctrl+V", L"Paste"}, {L"Ctrl+Shift+C", L"CopyPath"},
    {L"Ctrl+D", L"Duplicate"}, {L"F2", L"Rename"}, {L"Delete", L"Trash"}, {L"Shift+Delete", L"DeletePermanently"},
    {L"Ctrl+Shift+N", L"CreateFolder"}, {L"Ctrl+Alt+N", L"CreateFile"}, {L"Ctrl+N", L"NewWindow"}, {L"Ctrl+A", L"SelectAll"},
    {L"Ctrl+F", L"Filter"}, {L"Ctrl+Shift+F", L"SearchInSubfolders"}, {L"Ctrl+\\", L"PanelSplitRight"},
    {L"Ctrl+Shift+\\", L"PanelSplitBottom"}, {L"Ctrl+Shift+W", L"ClosePanel"}, {L"F6", L"PanelSelectNext"},
    {L"Shift+F6", L"PanelSelectPrev"}, {L"Ctrl+Alt+Left", L"PanelSelectLeft"}, {L"Ctrl+Alt+Right", L"PanelSelectRight"},
    {L"Ctrl+Alt+Up", L"PanelSelectUp"}, {L"Ctrl+Alt+Down", L"PanelSelectDown"}, {L"Space", L"InspectorToggle"},
    {L"Ctrl+I", L"InspectorToggle"}, {L"Ctrl+B", L"BookmarksToggle"}, {L"Ctrl+H", L"ToggleHiddenFiles"},
    {L"Ctrl+Comma", L"OptionsOpen"}, {L"Ctrl+Plus", L"IncreaseFontSize"}, {L"Ctrl+Minus", L"DecreaseFontSize"},
    {L"Ctrl+0", L"ResetFontSize"}, {L"Ctrl+Backtick", L"OpenConsole"}, {L"Ctrl+E", L"OpenFileExplorer"},
    {L"Ctrl+Shift+E", L"ToggleSubfolders"}, {L"Ctrl+Shift+Right", L"CopyFilesToNextPanel"},
    {L"Ctrl+Shift+Left", L"CopyFilesToPrevPanel"}, {L"Alt+Enter", L"Properties"}, {L"Shift+F10", L"ContextMenuOpen"},
    {L"Apps", L"ContextMenuOpen"}, {L"Ctrl+Shift+F10", L"ShellMenuOpen"}, {L"Ctrl+Alt+1", L"GroupByNone"},
    {L"Ctrl+Alt+2", L"GroupByName"}, {L"Ctrl+Alt+3", L"GroupByType"}, {L"Ctrl+Alt+4", L"GroupByDateModified"},
    {L"Ctrl+Alt+5", L"GroupBySize"}, {L"Ctrl+Shift+1", L"SortByName"}, {L"Ctrl+Shift+2", L"SortByDateModified"},
    {L"Ctrl+Shift+3", L"SortByType"}, {L"Ctrl+Shift+4", L"SortBySize"},
    {L"Ctrl+Shift+5", L"ViewDetails"}, {L"Ctrl+Shift+6", L"ViewTree"}, {L"Ctrl+Shift+7", L"ViewList"},
    {L"Ctrl+Shift+8", L"ViewIconsLarge"}, {L"Ctrl+Shift+B", L"ToggleSidebar"}, {L"Ctrl+Shift+K", L"ToggleFolderColumns"},
    {L"Alt+Home", L"GoHome"}, {L"Ctrl+Shift+D", L"InsertCurrentDate"}, {L"F1", L"CommandsOpen"},
};

void App::BuildHotkeys() {
    hotkeys.clear();
    std::set<std::wstring> overridden;
    for (auto &u : user.hotkeys) overridden.insert(u.command);
    for (auto &d : kDefaultHotkeys) {
        if (overridden.count(d[1])) continue;
        Hotkey h;
        if (ParseHotkey(d[0], h)) { h.command = d[1]; hotkeys.push_back(h); }
    }
    for (auto &u : user.hotkeys) {
        Hotkey h;
        if (!u.keys.empty() && ParseHotkey(u.keys, h)) { h.command = u.command; hotkeys.push_back(h); }
    }
    // Ctrl+1..9 select tab
    for (int i = 1; i <= 9; i++) {
        Hotkey h;
        h.vk = '0' + i;
        h.ctrl = true;
        h.command = Format(L"SelectTab%d", i);
        hotkeys.push_back(h);
    }
}

std::wstring App::HotkeyText(const std::wstring &cmdId) const {
    for (auto &h : hotkeys) if (h.command == cmdId) return HotkeyToString(h);
    return L"";
}

bool App::HandleHotkey(const KeyEvent &k) {
    bool textFocus = focus != FOCUS_LIST;
    for (auto &h : hotkeys) {
        if (h.vk != k.vk || h.ctrl != k.ctrl || h.shift != k.shift || h.alt != k.alt) continue;
        if (textFocus) {
            bool fkey = h.vk >= VK_F1 && h.vk <= VK_F24;
            if (!h.ctrl && !h.alt && !fkey) continue;
            if (h.ctrl && !h.alt && !h.shift &&
                (h.vk == 'A' || h.vk == 'C' || h.vk == 'X' || h.vk == 'V' || h.vk == 'Z' || h.vk == VK_LEFT ||
                 h.vk == VK_RIGHT || h.vk == VK_BACK || h.vk == VK_DELETE || h.vk == VK_HOME || h.vk == VK_END))
                continue;
        }
        if (h.command.rfind(L"SelectTab", 0) == 0) {
            int n = _wtoi(h.command.c_str() + 9);
            if (active && !active->tabs.empty()) {
                active->active = n == 9 ? (int)active->tabs.size() - 1 : std::min(n - 1, (int)active->tabs.size() - 1);
                ClearInspector();
                UpdateInspector();
                Invalidate();
            }
            return true;
        }
        if (RunCommand(h.command)) return true;
    }
    return false;
}
