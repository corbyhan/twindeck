#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

std::wstring ToLower(const std::wstring &s) {
    std::wstring r = s;
    if (!r.empty()) CharLowerBuffW(&r[0], (DWORD)r.size());
    return r;
}

bool ContainsNoCase(const std::wstring &hay, const std::wstring &needle) {
    if (needle.empty()) return true;
    return ToLower(hay).find(ToLower(needle)) != std::wstring::npos;
}

std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring Format(const wchar_t *fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)wcslen(buf);
    return std::wstring(buf, n);
}

std::wstring FormatSize(u64 bytes) {
    const wchar_t *units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    f64 v = (f64)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 5) { v /= 1024.0; u++; }
    if (u == 0) return Format(L"%llu B", bytes);
    return Format(v < 10 ? L"%.2f %s" : (v < 100 ? L"%.1f %s" : L"%.0f %s"), v, units[u]);
}

std::wstring FormatFileTime(u64 ft) {
    if (!ft) return L"";
    FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME utc, loc;
    FileTimeToSystemTime(&f, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc);
    return Format(L"%04d-%02d-%02d %02d:%02d", loc.wYear, loc.wMonth, loc.wDay, loc.wHour, loc.wMinute);
}

std::wstring PathJoin(const std::wstring &a, const std::wstring &b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

bool PathIsRootDrive(const std::wstring &p) {
    return (p.size() == 2 && p[1] == L':') || (p.size() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/'));
}

std::wstring PathParent(const std::wstring &p) {
    if (p.empty() || PathIsRootDrive(p)) return L"";
    std::wstring s = p;
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    size_t i = s.find_last_of(L"\\/");
    if (i == std::wstring::npos) return L"";
    std::wstring r = s.substr(0, i);
    if (r.size() == 2 && r[1] == L':') r += L"\\";
    // UNC share root: \\server\share has no browsable parent
    if (r == L"\\" || r == L"\\\\" ) return L"";
    if (r.size() > 2 && r[0] == L'\\' && r[1] == L'\\' && r.find(L'\\', 2) == std::wstring::npos) return L"";
    return r;
}

std::wstring PathFileName(const std::wstring &p) {
    std::wstring s = p;
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    if (PathIsRootDrive(s)) return s.substr(0, 2);
    size_t i = s.find_last_of(L"\\/");
    return i == std::wstring::npos ? s : s.substr(i + 1);
}

std::wstring PathExtension(const std::wstring &p) {
    std::wstring n = PathFileName(p);
    size_t i = n.find_last_of(L'.');
    if (i == std::wstring::npos || i == 0) return L"";
    return ToLower(n.substr(i + 1));
}

// Natural ("file2" < "file10") case-insensitive compare, locale aware for Hangul
int NaturalCompare(const std::wstring &a, const std::wstring &b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        wchar_t ca = a[i], cb = b[j];
        if (iswdigit(ca) && iswdigit(cb)) {
            size_t si = i, sj = j;
            while (si < a.size() && a[si] == L'0') si++;
            while (sj < b.size() && b[sj] == L'0') sj++;
            size_t ei = si, ej = sj;
            while (ei < a.size() && iswdigit(a[ei])) ei++;
            while (ej < b.size() && iswdigit(b[ej])) ej++;
            size_t la = ei - si, lb = ej - sj;
            if (la != lb) return la < lb ? -1 : 1;
            for (size_t k = 0; k < la; k++)
                if (a[si + k] != b[sj + k]) return a[si + k] < b[sj + k] ? -1 : 1;
            i = ei; j = ej;
            continue;
        }
        wchar_t la = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)ca);
        wchar_t lb = (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)cb);
        if (la != lb) return la < lb ? -1 : 1;
        i++; j++;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// ---- Hangul ----
// Syllables U+AC00..U+D7A3 = ((L * 21) + V) * 28 + T
static const wchar_t kChoseong[19] = {
    0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
    0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};

static inline bool IsHangulSyllable(wchar_t c) { return c >= 0xAC00 && c <= 0xD7A3; }
static inline bool IsCompatJamoConsonant(wchar_t c) { return c >= 0x3131 && c <= 0x314E; }

std::wstring HangulChoseong(const std::wstring &s) {
    std::wstring r = s;
    for (auto &c : r)
        if (IsHangulSyllable(c)) c = kChoseong[(c - 0xAC00) / (21 * 28)];
    return r;
}

static bool CharEq(wchar_t t, wchar_t p) {
    if (t == p) return true;
    // pattern consonant jamo matches syllable with that initial
    if (IsCompatJamoConsonant(p) && IsHangulSyllable(t)) return kChoseong[(t - 0xAC00) / (21 * 28)] == p;
    return false;
}

// Subsequence fuzzy match with scoring. Contiguous / word-start matches score higher.
std::wstring Trim(const std::wstring &s) {
    size_t a = s.find_first_not_of(L' '), b = s.find_last_not_of(L' ');
    return a == std::wstring::npos ? L"" : s.substr(a, b - a + 1);
}

// '*' and '?' against the whole text, ignoring case (*.md, report_??.txt)
static bool WildcardMatch(const wchar_t *s, const wchar_t *p) {
    const wchar_t *star = nullptr, *resume = nullptr;
    while (*s) {
        if (*p == L'?' || (*p != L'*' && towlower(*p) == towlower(*s))) { s++; p++; }
        else if (*p == L'*') { star = p++; resume = s; }
        else if (star) { p = star + 1; s = ++resume; }
        else return false;
    }
    while (*p == L'*') p++;
    return *p == 0;
}

bool FuzzyMatch(const std::wstring &textIn, const std::wstring &patIn, int *score) {
    if (patIn.empty()) { if (score) *score = 0; return true; }
    if (patIn.find(L';') != std::wstring::npos) { // alternatives: *.md;*.txt
        size_t start = 0;
        while (start <= patIn.size()) {
            size_t end = patIn.find(L';', start);
            std::wstring part = Trim(patIn.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
            if (!part.empty() && FuzzyMatch(textIn, part, score)) return true;
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return false;
    }
    if (patIn.find_first_of(L"*?") != std::wstring::npos) {
        if (score) *score = 0;
        return WildcardMatch(textIn.c_str(), Trim(patIn).c_str());
    }
    std::wstring text = ToLower(textIn), pat = ToLower(patIn);
    // fast path: substring
    size_t pos = text.find(pat);
    if (pos != std::wstring::npos) {
        if (score) *score = 1000 - (int)pos * 2 - (int)(text.size() - pat.size()) + (pos == 0 ? 300 : 0);
        return true;
    }
    int sc = 0, run = 0;
    size_t ti = 0;
    for (size_t pi = 0; pi < pat.size(); pi++) {
        if (pat[pi] == L' ') continue;
        bool found = false;
        while (ti < text.size()) {
            if (CharEq(text[ti], pat[pi])) {
                bool wordStart = ti == 0 || !iswalnum(text[ti - 1]);
                sc += 10 + run * 5 + (wordStart ? 20 : 0);
                run++; ti++; found = true;
                break;
            }
            run = 0; ti++;
        }
        if (!found) return false;
    }
    if (score) *score = sc - (int)text.size();
    return true;
}

u64 NowTicks() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (u64)c.QuadPart;
}

f64 TicksToSeconds(u64 t) {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    return (f64)t / (f64)f.QuadPart;
}
