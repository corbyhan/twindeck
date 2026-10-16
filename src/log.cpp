#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <dbghelp.h>

static HANDLE g_logFile = INVALID_HANDLE_VALUE;
static std::mutex g_logMutex;
static std::wstring g_dumpDir;

void LogInit(const std::wstring &dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME t; GetLocalTime(&t);
    std::wstring path = PathJoin(dir, Format(L"TwinDeck-%04d-%02d-%02d-%02d-%02d-%02d.log",
                                             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond));
    g_logFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_logFile != INVALID_HANDLE_VALUE) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD w; WriteFile(g_logFile, bom, 3, &w, nullptr);
    }
    // keep only the newest 10 logs
    WIN32_FIND_DATAW fd;
    std::vector<std::wstring> logs;
    HANDLE h = FindFirstFileW(PathJoin(dir, L"TwinDeck-*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do logs.push_back(fd.cFileName); while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i + 10 < logs.size(); i++) DeleteFileW(PathJoin(dir, logs[i]).c_str());
}

void Log(const wchar_t *fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME t; GetLocalTime(&t);
    std::wstring line = Format(L"[%02d:%02d:%02d.%03d] %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, buf);
    OutputDebugStringW(line.c_str());
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logFile != INVALID_HANDLE_VALUE) {
        std::string u = WideToUtf8(line);
        DWORD w; WriteFile(g_logFile, u.data(), (DWORD)u.size(), &w, nullptr);
    }
}

typedef BOOL(WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep) {
    HMODULE dbg = LoadLibraryW(L"dbghelp.dll");
    if (dbg) {
        auto fn = (MiniDumpWriteDumpFn)GetProcAddress(dbg, "MiniDumpWriteDump");
        if (fn) {
            CreateDirectoryW(g_dumpDir.c_str(), nullptr);
            SYSTEMTIME t; GetLocalTime(&t);
            std::wstring path = PathJoin(g_dumpDir, Format(L"TwinDeck_%04d-%02d-%02d-%02d-%02d.dmp",
                                                           t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute));
            HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), ep, FALSE};
                fn(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(f);
                Log(L"Crash dump written: %s", path.c_str());
            }
        }
    }
    MessageBoxW(nullptr, L"TwinDeck에서 오류가 발생하여 종료합니다.\n덤프 파일이 로그 폴더에 저장되었습니다.",
                APP_NAME, MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler(const std::wstring &dumpDir) {
    g_dumpDir = dumpDir;
    SetUnhandledExceptionFilter(CrashFilter);
}
