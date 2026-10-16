// Localized UI strings (Korean / English)
#pragma once
#include "common.h"

#define STRING_TABLE(X) \
    X(S_AppTitle,          L"TwinDeck",                     L"TwinDeck") \
    X(S_ThisPC,            L"This PC",                         L"내 PC") \
    X(S_RecycleBin,        L"Recycle Bin",                     L"휴지통") \
    X(S_Home,              L"Home",                            L"홈") \
    X(S_Desktop,           L"Desktop",                         L"바탕 화면") \
    X(S_Downloads,         L"Downloads",                       L"다운로드") \
    X(S_Documents,         L"Documents",                       L"문서") \
    X(S_Pictures,          L"Pictures",                        L"사진") \
    X(S_Music,             L"Music",                           L"음악") \
    X(S_Videos,            L"Videos",                          L"동영상") \
    X(S_Bookmarks,         L"Bookmarks",                       L"즐겨찾기") \
    X(S_Recents,           L"Recents",                         L"최근 폴더") \
    X(S_Drives,            L"Drives",                          L"드라이브") \
    X(S_Places,            L"Places",                          L"위치") \
    X(S_ColName,           L"Name",                            L"이름") \
    X(S_ColModified,       L"Date Modified",                   L"수정한 날짜") \
    X(S_ColCreated,        L"Date Created",                    L"만든 날짜") \
    X(S_ColAccessed,       L"Date Accessed",                   L"액세스한 날짜") \
    X(S_ColType,           L"Type",                            L"유형") \
    X(S_ColSize,           L"Size",                            L"크기") \
    X(S_ColItems,          L"Items",                           L"항목 수") \
    X(S_ColFree,           L"Free Space",                      L"남은 공간") \
    X(S_ColTotal,          L"Total Size",                      L"전체 크기") \
    X(S_ColPercent,        L"% Full",                          L"사용률") \
    X(S_ColFileSystem,     L"File System",                     L"파일 시스템") \
    X(S_ColOrigLocation,   L"Original Location",               L"원래 위치") \
    X(S_ColDeleted,        L"Date Deleted",                    L"삭제한 날짜") \
    X(S_Folder,            L"Folder",                          L"폴더") \
    X(S_File,              L"File",                            L"파일") \
    X(S_FileOfType,        L"%s File",                         L"%s 파일") \
    X(S_Items,             L"%d items",                        L"항목 %d개") \
    X(S_ItemsSelected,     L"%d selected",                     L"%d개 선택됨") \
    X(S_FreeOf,            L"%s free of %s",                   L"%s 사용 가능 / 전체 %s") \
    X(S_Loading,           L"Loading…",                        L"불러오는 중…") \
    X(S_Empty,             L"This folder is empty.",           L"이 폴더는 비어 있습니다.") \
    X(S_NoMatch,           L"No items match the filter.",      L"필터와 일치하는 항목이 없습니다.") \
    X(S_AccessDenied,      L"Access denied.",                  L"액세스가 거부되었습니다.") \
    X(S_PathNotFound,      L"Path not found.",                 L"경로를 찾을 수 없습니다.") \
    X(S_Filter,            L"Filter",                          L"필터") \
    X(S_FilterHint,        L"Type to filter (초성 OK)",         L"입력하여 필터 (초성 검색 지원)") \
    X(S_SearchSubfolders,  L"Searching subfolders…",           L"하위 폴더 검색 중…") \
    X(S_CommandHint,       L"Type a command…",                 L"명령 입력…") \
    X(S_GoToHint,          L"Go to folder or path…",           L"이동할 폴더 또는 경로…") \
    X(S_NewFolder,         L"New Folder",                      L"새 폴더") \
    X(S_NewFile,           L"New File.txt",                    L"새 텍스트 문서.txt") \
    X(S_Rename,            L"Rename",                          L"이름 바꾸기") \
    X(S_Options,           L"Options",                         L"옵션") \
    X(S_Inspector,         L"Inspector",                       L"미리 보기") \
    X(S_Details,           L"Details",                         L"자세히") \
    X(S_Path,              L"Path",                            L"경로") \
    X(S_Attributes,        L"Attributes",                      L"특성") \
    X(S_Dimensions,        L"Dimensions",                      L"해상도") \
    X(S_Contents,          L"Contents",                        L"내용") \
    X(S_FolderStats,       L"%s in %llu files, %llu folders",  L"파일 %llu개, 폴더 %llu개, %s") \
    X(S_Calculating,       L"Calculating…",                    L"계산 중…") \
    X(S_PreviewTooLarge,   L"File too large to preview.",      L"파일이 너무 커서 미리 볼 수 없습니다.") \
    X(S_PreviewBinary,     L"Binary file",                     L"바이너리 파일") \
    X(S_NoSelection,       L"No selection",                    L"선택된 항목 없음") \
    X(S_ConfirmDelete,     L"Permanently delete %d item(s)?",  L"%d개 항목을 영구적으로 삭제할까요?") \
    X(S_ConfirmEmptyBin,   L"Permanently delete all items in the Recycle Bin?", L"휴지통의 모든 항목을 영구적으로 삭제할까요?") \
    X(S_Yes,               L"Yes",                             L"예") \
    X(S_No,                L"No",                              L"아니요") \
    X(S_Ok,                L"OK",                              L"확인") \
    X(S_Cancel,            L"Cancel",                          L"취소") \
    X(S_Copied,            L"Copied %d item(s)",               L"%d개 항목 복사됨") \
    X(S_Cut,               L"Cut %d item(s)",                  L"%d개 항목 잘라냄") \
    X(S_PathCopied,        L"Path copied",                     L"경로가 복사되었습니다") \
    X(S_BookmarkAdded,     L"Bookmark added",                  L"즐겨찾기에 추가됨") \
    X(S_BookmarkRemoved,   L"Bookmark removed",                L"즐겨찾기에서 제거됨") \
    X(S_Installed,         L"Installed. Shell integration registered.", L"설치 완료. 탐색기 메뉴가 등록되었습니다.") \
    X(S_Uninstalled,       L"Uninstalled.",                    L"제거되었습니다.") \
    X(S_MoreOptions,       L"More options (Windows menu)",     L"추가 옵션 (Windows 메뉴)") \
    X(S_OpenInApp,   L"Open in TwinDeck",              L"TwinDeck에서 열기") \
    X(S_AppHere,     L"TwinDeck here",                 L"여기서 TwinDeck 열기") \
    X(S_On,                L"On",                              L"켜짐") \
    X(S_Off,               L"Off",                             L"꺼짐") \
    X(S_Hotkey,            L"Hotkey",                          L"단축키") \
    X(S_Group,             L"Group",                           L"그룹") \
    X(S_Today,             L"Today",                           L"오늘") \
    X(S_Yesterday,         L"Yesterday",                       L"어제") \
    X(S_ThisWeek,          L"Earlier this week",               L"이번 주") \
    X(S_LastWeek,          L"Last week",                       L"지난주") \
    X(S_ThisMonth,         L"Earlier this month",              L"이번 달") \
    X(S_LastMonth,         L"Last month",                      L"지난달") \
    X(S_ThisYear,          L"Earlier this year",               L"올해") \
    X(S_LongAgo,           L"A long time ago",                 L"오래 전") \
    X(S_SizeEmpty,         L"Empty (0 KB)",                    L"비어 있음 (0KB)") \
    X(S_SizeTiny,          L"Tiny (0 - 16 KB)",                L"아주 작음 (0 - 16KB)") \
    X(S_SizeSmall,         L"Small (16 KB - 1 MB)",            L"작음 (16KB - 1MB)") \
    X(S_SizeMedium,        L"Medium (1 - 128 MB)",             L"보통 (1 - 128MB)") \
    X(S_SizeLarge,         L"Large (128 MB - 1 GB)",           L"큼 (128MB - 1GB)") \
    X(S_SizeHuge,          L"Huge (1 - 4 GB)",                 L"매우 큼 (1 - 4GB)") \
    X(S_SizeGigantic,      L"Gigantic (> 4 GB)",               L"초대형 (> 4GB)") \
    X(S_Folders,           L"Folders",                         L"폴더") \
    X(S_Others,            L"Others",                          L"기타") \
    X(S_SetGeneral,        L"General",                         L"일반") \
    X(S_SetAppearance,     L"Appearance",                      L"모양") \
    X(S_SetBehavior,       L"Behavior",                        L"동작") \
    X(S_SetSystem,         L"System",                          L"시스템") \
    X(S_Language,          L"Language",                        L"언어") \
    X(S_Auto,              L"Auto",                            L"자동") \
    X(S_Dark,              L"Dark",                            L"어둡게") \
    X(S_Light,             L"Light",                           L"밝게") \
    X(S_Hor,               L"Horizontal",                      L"가로") \
    X(S_Vert,              L"Vertical",                        L"세로") \
    X(S_StartHome,         L"Home",                            L"홈") \
    X(S_StartLastSession,  L"Last Session",                    L"마지막 세션") \
    X(S_StartThisPC,       L"This PC",                         L"내 PC") \
    X(S_CharSelect,        L"Select typed item",               L"입력한 항목 선택") \
    X(S_CharFilter,        L"Type in filter",                  L"필터에 입력") \
    X(S_Restart,           L"Restart required",                L"다시 시작 필요") \
    X(S_ErrorOp,           L"Operation failed: %s",            L"작업 실패: %s") \
    X(S_Volume,            L"Local Disk",                      L"로컬 디스크") \
    X(S_Removable,         L"Removable Disk",                  L"이동식 디스크") \
    X(S_Network,           L"Network Drive",                   L"네트워크 드라이브") \
    X(S_CDROM,             L"CD Drive",                        L"CD 드라이브") \
    X(S_Back,              L"Back",                            L"뒤로") \
    X(S_Forward,           L"Forward",                         L"앞으로") \
    X(S_Up,                L"Up",                              L"위로") \
    X(S_NewTab,            L"New Tab",                         L"새 탭") \
    X(S_Composition,       L"Composing",                       L"조합 중")

enum StrId {
#define X(id, en, ko) id,
    STRING_TABLE(X)
#undef X
    S_COUNT
};

enum Lang { LANG_EN = 0, LANG_KO = 1 };

void        I18nSetLanguage(Lang l);
Lang        I18nGetLanguage();
Lang        I18nDetectSystemLanguage();
const wchar_t *T(StrId id);
// Pick between en/ko literal pair (used for command/settings tables)
inline const wchar_t *TL(const wchar_t *en, const wchar_t *ko) { return I18nGetLanguage() == LANG_KO ? ko : en; }
