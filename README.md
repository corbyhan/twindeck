# TwinDeck

한글을 제대로 지원하는 빠른 Windows 파일 관리자입니다. C++ / Win32 / Direct3D 11 + Direct2D로 작성했고, 외부 라이브러리 없이 실행 파일 하나로 동작합니다.

## 한글 지원
| 항목 | 내용 |
|---|---|
| 글꼴 렌더링 | Direct2D/DirectWrite 레이아웃 + 시스템 대체 글꼴 → 한글·한자·이모지 정상 표시 |
| IME | 조합 중인 글자를 입력란 안에 직접 표시하고 후보 창 위치를 지정. 목록에서 한글로 바로 입력하면 필터로 연결 |
| 검색 | **초성 검색** (`ㅎㄱ` → `한글문서.txt`), 초성으로 바로 항목 선택 |
| 그룹화 | 이름 그룹화 시 **초성(ㄱ~ㅎ)별 그룹** |
| UI 언어 | 한국어 / 영어 (자동 감지, 옵션에서 전환) |
| 텍스트 미리 보기 | UTF-8 / UTF-16 / **CP949(EUC-KR)** 자동 판별 |
| 단축키 | 한글 입력 모드에서도 Ctrl 단축키 동작 |

## 주요 기능
- `NtQueryDirectoryFile`(FileIdExtdDirectoryInformation)을 사용한 빠른 폴더 열거. 이 API를 쓸 수 없으면 `FindFirstFileEx`로 대체
- 폴더 크기 병렬 계산: 하위 폴더를 16개씩 묶어 스레드풀에 분배하고, 심볼릭 링크·정션은 따라가지 않음. SSD/HDD/네트워크별로 켜고 끌 수 있음
- 탭, 상하·좌우 분할 패널, 트리 펼치기, 정렬과 그룹화(이름/유형/크기/날짜), 실시간 필터, 하위 폴더 재귀 검색
- 보기 모드: 자세히 / 트리 / 목록 / 아이콘(보통·큰·아주 큰, 48~256px 사이 자유 조절). 상태 표시줄 오른쪽 버튼의 슬라이더나 `Ctrl+휠`로 바꾸고, 아이콘 보기에서는 사진·동영상·문서 썸네일 표시. 탭마다 따로 저장
- 미리 보기 창: 텍스트(줄 번호, 자동 줄 바꿈, 공백 표시), 이미지(WIC), 셸 썸네일, 폴더 통계
- 명령 팔레트(`Ctrl+Shift+P`), 폴더 이동(`Ctrl+P`), 주소 표시줄 편집과 Tab 자동 완성
- 파일 작업: Windows의 `IFileOperation`(진행 표시와 실행 취소 지원) — 복사/잘라내기/붙여넣기, 복제, 이름 바꾸기, 휴지통, 영구 삭제
- 원드라이브 등 클라우드 동기화 폴더에서는 탐색기처럼 "상태" 열 표시 (온라인 전용 ☁ / 이 디바이스에서 사용 가능 ✓ / 항상 유지 / 동기화 중 / 오류)
- 휴지통 보기·복원·비우기, 내 PC 보기(드라이브 용량 막대 표시), Windows 셸 우클릭 메뉴, 드래그 앤 드롭
- 즐겨찾기, 최근 폴더(사용 빈도와 최근성을 함께 반영하는 frecency 점수), 세션(창 위치·분할 레이아웃·탭) 복원
- `ReadDirectoryChangesW`로 폴더 변경을 감시해 자동으로 새로 고침
- 배경 효과: 없음 / 투명 / 미카 / 아크릴 / 탭(미카 대체) 중 선택하고 배경 불투명도(0~100%)를 조절. 글자와 아이콘은 그대로 선명하게 두고 배경만 비침 (옵션 → 모양, Windows 11 22H2 이상. Windows 10은 아크릴 흐림으로 대체)
- 탐색기 통합: "TwinDeck에서 열기" 메뉴 등록, 사용자 단위 설치/제거(HKCU, 관리자 권한 불필요)
- 크래시 덤프(`MiniDumpWriteDump`)와 로그 기록

## 빌드
요구 사항: Visual Studio 2022 Build Tools (C++ 데스크톱), Windows 10 SDK 이상

```bat
build.bat            :: release → build\TwinDeck.exe, build\TwinDeck-Setup.exe
build.bat debug
```

아이콘을 다시 만들려면 Pillow가 설치된 Python으로 `python tools\make_icon.py`를 실행하세요.

## 설치
`build\TwinDeck-Setup.exe`(빌드 시 함께 생성)를 실행하면 설치 화면이 열립니다. 프로그램 하나가 설치 프로그램을 겸하며, 파일 이름에 `Setup`이 들어 있거나 `--setup`으로 실행하거나 설치하지 않은 채 처음 실행할 때 이 화면이 나옵니다. 설치한 뒤에는 옵션 → 시스템 → "설치 설정…" 또는 Windows 설정 → 앱의 "수정"에서 다시 열 수 있습니다.

| 항목 | 내용 |
|---|---|
| 설치 경로 | 기본값 `%LOCALAPPDATA%\Programs\TwinDeck`. 바꾸면 이전 위치는 정리합니다 |
| 바탕 화면 바로 가기 | 바탕 화면에 아이콘 생성 (시작 메뉴 바로 가기는 항상 생성) |
| 기본 파일 관리자 | 폴더·드라이브 열기와 `Win+E`를 TwinDeck으로 연결 |
| 우클릭 메뉴 | 탐색기 폴더·빈 곳 메뉴에 "TwinDeck에서 열기" 추가 |
| PATH에 추가 | 터미널에서 `twindeck`으로 실행 |

관리자 권한 없이 현재 사용자(HKCU)에만 설치합니다. 제거하면 위 항목을 모두 되돌리고 설정과 즐겨찾기(`%LOCALAPPDATA%\TwinDeck`)는 남깁니다.

무인 설치: `TwinDeck.exe --install [--dir=경로] [--desktop] [--default] [--no-menu] [--no-path] [--quiet]`, 제거: `TwinDeck.exe --uninstall [--quiet]`

## 데이터 위치
`%LOCALAPPDATA%\TwinDeck\` 아래에 다음 파일과 폴더가 저장됩니다.
- `TwinDeck-Config.json`, `TwinDeck-UserData.json`, `TwinDeck-Session.json`
- `Logs\`

exe 파일 옆에 `TwinDeck-Config.json`이 있으면 포터블 모드로 동작해 그 폴더에 저장합니다.

## 주요 단축키
| 키 | 동작 |
|---|---|
| `Ctrl+Shift+P` / `F1` | 명령 팔레트 |
| `Ctrl+P` | 폴더로 이동 |
| `Ctrl+L`, `Alt+D` | 주소 표시줄 편집 |
| `Ctrl+F` / 바로 입력 | 필터 (초성 지원) |
| `Ctrl+Shift+F` | 하위 폴더에서 검색 |
| `Ctrl+T` / `Ctrl+W` / `Ctrl+Shift+T` | 새 탭 / 탭 닫기 / 닫은 탭 다시 열기 |
| `Ctrl+\` / `Ctrl+Shift+\` | 오른쪽 / 아래쪽으로 분할 |
| `Space`, `Ctrl+I` | 미리 보기 창 |
| `F2`, `Delete`, `Shift+Delete` | 이름 바꾸기, 휴지통, 영구 삭제 |
| `Ctrl+Shift+N` | 새 폴더 |
| `Alt+←/→/↑`, `Backspace` | 뒤로 / 앞으로 / 위로 |
| `→` / `←` | 폴더 펼치기 / 접기 |
| `Ctrl+Shift+5/6/7/8`, `Ctrl+휠` | 자세히 / 트리 / 목록 / 큰 아이콘, 아이콘 크기 조절 |
| `Ctrl+,` | 옵션 |

단축키는 `TwinDeck-UserData.json`의 `Hotkeys` 배열(`{"Keys": "Ctrl+K", "Command": "NewTab"}`)에서 바꿀 수 있습니다.

## 구조
| 파일 | 역할 |
|---|---|
| `src/main.cpp` | WinMain, WndProc, IME 메시지, 렌더 루프 |
| `src/app.*` | 상태 모델: 패널 트리, 탭, 행 생성, 탐색 |
| `src/ui.cpp`, `popups.cpp`, `inspector.cpp` | 즉시 모드 UI |
| `src/input.cpp` | 키보드, 텍스트 편집, IME, 필터 |
| `src/fs.*` | NT 디렉터리 열거, 폴더 크기 계산, 변경 감시 |
| `src/shell.*` | 아이콘, 파일 작업, 클립보드, 셸 메뉴, 휴지통, 드래그 앤 드롭 |
| `src/render.*` | D3D11 스왑체인과 Direct2D/DirectWrite |
| `src/commands.cpp` | 명령과 단축키 정의 |
| `src/settings.*`, `json.*` | 설정 저장 |
| `src/i18n.*` | 한국어/영어 문자열 |
| `src/install.cpp` | 설치와 탐색기 통합 |
| `tools/` | 개발용 스크립트: 아이콘 생성, 스크린샷, 입력 자동화, 크래시 덤프 분석 |

## 라이선스
[MIT](LICENSE)
