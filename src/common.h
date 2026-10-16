// TwinDeck - common definitions
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <atomic>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;

#define APP_NAME      L"TwinDeck"
#define APP_VERSION   L"0.1.4"
#define APP_DIR_NAME  L"TwinDeck"

template <typename T>
inline void SafeRelease(T *&p) {
    if (p) { p->Release(); p = nullptr; }
}

template <typename T>
inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct RectF {
    f32 x = 0, y = 0, w = 0, h = 0;
    RectF() {}
    RectF(f32 x_, f32 y_, f32 w_, f32 h_) : x(x_), y(y_), w(w_), h(h_) {}
    f32 Right() const { return x + w; }
    f32 Bottom() const { return y + h; }
    bool Contains(f32 px, f32 py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    RectF Shrink(f32 d) const { return RectF(x + d, y + d, std::max(0.f, w - 2 * d), std::max(0.f, h - 2 * d)); }
    RectF CutTop(f32 d) { RectF r(x, y, w, std::min(d, h)); y += r.h; h -= r.h; return r; }
    RectF CutBottom(f32 d) { f32 dd = std::min(d, h); RectF r(x, y + h - dd, w, dd); h -= dd; return r; }
    RectF CutLeft(f32 d) { RectF r(x, y, std::min(d, w), h); x += r.w; w -= r.w; return r; }
    RectF CutRight(f32 d) { f32 dd = std::min(d, w); RectF r(x + w - dd, y, dd, h); w -= dd; return r; }
};

struct Color {
    f32 r = 0, g = 0, b = 0, a = 1;
    Color() {}
    Color(f32 r_, f32 g_, f32 b_, f32 a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    static Color Hex(u32 rgb, f32 a = 1) {
        return Color(((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, a);
    }
    Color WithAlpha(f32 na) const { return Color(r, g, b, na); }
};

// string helpers (strutil.cpp)
std::wstring ToLower(const std::wstring &s);
bool ContainsNoCase(const std::wstring &hay, const std::wstring &needle);
std::string  WideToUtf8(const std::wstring &w);
std::wstring Utf8ToWide(const std::string &s);
std::wstring Format(const wchar_t *fmt, ...);
std::wstring FormatSize(u64 bytes);
std::wstring FormatFileTime(u64 fileTime);
std::wstring PathJoin(const std::wstring &a, const std::wstring &b);
std::wstring PathParent(const std::wstring &p);
std::wstring PathFileName(const std::wstring &p);
std::wstring PathExtension(const std::wstring &p); // lowercase without dot
bool         PathIsRootDrive(const std::wstring &p);
int          NaturalCompare(const std::wstring &a, const std::wstring &b);
std::wstring Trim(const std::wstring &s); // spaces at both ends
// Hangul-aware fuzzy match: also matches initial consonants (초성 검색), e.g. "ㅍㄹ" matches "파일"
bool         FuzzyMatch(const std::wstring &text, const std::wstring &pattern, int *score = nullptr);
std::wstring HangulChoseong(const std::wstring &s);

// logging (log.cpp)
void LogInit(const std::wstring &dir);
void Log(const wchar_t *fmt, ...);
void InstallCrashHandler(const std::wstring &dumpDir);

// time
u64 NowTicks();          // QPC
f64 TicksToSeconds(u64 t);
