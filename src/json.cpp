#include "json.h"
#include <stdio.h>

const Json *Json::Find(const wchar_t *key) const {
    if (type != Object) return nullptr;
    for (auto &kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

Json &Json::operator[](const wchar_t *key) {
    type = Object;
    for (auto &kv : obj)
        if (kv.first == key) return kv.second;
    obj.emplace_back(key, Json());
    return obj.back().second;
}

bool Json::GetBool(const wchar_t *key, bool def) const {
    const Json *j = Find(key);
    if (!j) return def;
    if (j->type == Bool) return j->b;
    if (j->type == Number) return j->num != 0;
    return def;
}

f64 Json::GetNum(const wchar_t *key, f64 def) const {
    const Json *j = Find(key);
    return (j && j->type == Number) ? j->num : (j && j->type == Bool ? (j->b ? 1 : 0) : def);
}

std::wstring Json::GetStr(const wchar_t *key, const std::wstring &def) const {
    const Json *j = Find(key);
    return (j && j->type == String) ? j->str : def;
}

// ---------------- serialize ----------------
static void EscapeStr(std::string &o, const std::wstring &w) {
    std::string s = WideToUtf8(w);
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    o += '"';
}

static void Indent(std::string &o, int n) { for (int i = 0; i < n; i++) o += "    "; }

static void Ser(std::string &o, const Json &j, bool pretty, int depth) {
    switch (j.type) {
    case Json::Null: o += "null"; break;
    case Json::Bool: o += j.b ? "true" : "false"; break;
    case Json::Number: {
        char b[64];
        if (j.num == (f64)(i64)j.num && j.num < 9e15 && j.num > -9e15) sprintf_s(b, "%lld", (long long)j.num);
        else sprintf_s(b, "%.17g", j.num);
        o += b;
    } break;
    case Json::String: EscapeStr(o, j.str); break;
    case Json::Array:
        if (j.arr.empty()) { o += "[]"; break; }
        o += '[';
        for (size_t i = 0; i < j.arr.size(); i++) {
            if (pretty) { o += '\n'; Indent(o, depth + 1); }
            Ser(o, j.arr[i], pretty, depth + 1);
            if (i + 1 < j.arr.size()) o += ',';
        }
        if (pretty) { o += '\n'; Indent(o, depth); }
        o += ']';
        break;
    case Json::Object:
        if (j.obj.empty()) { o += "{}"; break; }
        o += '{';
        for (size_t i = 0; i < j.obj.size(); i++) {
            if (pretty) { o += '\n'; Indent(o, depth + 1); }
            EscapeStr(o, j.obj[i].first);
            o += pretty ? ": " : ":";
            Ser(o, j.obj[i].second, pretty, depth + 1);
            if (i + 1 < j.obj.size()) o += ',';
        }
        if (pretty) { o += '\n'; Indent(o, depth); }
        o += '}';
        break;
    }
}

std::string Json::Serialize(bool pretty) const {
    std::string o;
    Ser(o, *this, pretty, 0);
    if (pretty) o += '\n';
    return o;
}

// ---------------- parse ----------------
struct Parser {
    const char *p, *end;
    std::wstring err;
    void Ws() {
        for (;;) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            // allow // comments and trailing commas (config files are hand-edited)
            if (p + 1 < end && p[0] == '/' && p[1] == '/') { while (p < end && *p != '\n') p++; continue; }
            break;
        }
    }
    bool Fail(const wchar_t *m) { if (err.empty()) err = m; return false; }
    static int Hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool Str(std::wstring &out) {
        if (p >= end || *p != '"') return Fail(L"expected string");
        p++;
        std::string s;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) return Fail(L"bad escape");
                char e = *p++;
                switch (e) {
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'u': {
                    if (end - p < 4) return Fail(L"bad \\u");
                    u32 cp = 0;
                    for (int i = 0; i < 4; i++) { int h = Hex(p[i]); if (h < 0) return Fail(L"bad \\u"); cp = cp * 16 + h; }
                    p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        u32 lo = 0;
                        for (int i = 0; i < 4; i++) lo = lo * 16 + Hex(p[2 + i]);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                    }
                    // encode utf8
                    if (cp < 0x80) s += (char)cp;
                    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 63)); }
                    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
                    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 63)); s += (char)(0x80 | ((cp >> 6) & 63)); s += (char)(0x80 | (cp & 63)); }
                } break;
                default: s += e;
                }
            } else s += c;
        }
        if (p >= end) return Fail(L"unterminated string");
        p++;
        out = Utf8ToWide(s);
        return true;
    }
    bool Value(Json &j, int depth) {
        if (depth > 128) return Fail(L"too deep");
        Ws();
        if (p >= end) return Fail(L"unexpected end");
        char c = *p;
        if (c == '{') {
            p++; j = Json::MakeObject();
            Ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                Ws();
                if (p < end && *p == '}') { p++; return true; } // trailing comma
                std::wstring k;
                if (!Str(k)) return false;
                Ws();
                if (p >= end || *p != ':') return Fail(L"expected ':'");
                p++;
                Json v;
                if (!Value(v, depth + 1)) return false;
                j.obj.emplace_back(k, std::move(v));
                Ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return Fail(L"expected ',' or '}'");
            }
        }
        if (c == '[') {
            p++; j = Json::MakeArray();
            Ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                Ws();
                if (p < end && *p == ']') { p++; return true; }
                Json v;
                if (!Value(v, depth + 1)) return false;
                j.arr.push_back(std::move(v));
                Ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return Fail(L"expected ',' or ']'");
            }
        }
        if (c == '"') { j.type = Json::String; return Str(j.str); }
        if (end - p >= 4 && !strncmp(p, "true", 4)) { j = Json(true); p += 4; return true; }
        if (end - p >= 5 && !strncmp(p, "false", 5)) { j = Json(false); p += 5; return true; }
        if (end - p >= 4 && !strncmp(p, "null", 4)) { j = Json(); p += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char *s = p;
            p++;
            while (p < end && (strchr("0123456789.eE+-", *p))) p++;
            std::string num(s, p);
            j.type = Json::Number;
            j.num = strtod(num.c_str(), nullptr);
            return true;
        }
        return Fail(L"unexpected character");
    }
};

bool Json::Parse(const std::string &utf8, Json &out, std::wstring *err) {
    Parser ps;
    ps.p = utf8.data();
    ps.end = utf8.data() + utf8.size();
    if (ps.end - ps.p >= 3 && (u8)ps.p[0] == 0xEF && (u8)ps.p[1] == 0xBB && (u8)ps.p[2] == 0xBF) ps.p += 3;
    bool ok = ps.Value(out, 0);
    if (!ok && err) *err = ps.err;
    return ok;
}

bool JsonLoadFile(const std::wstring &path, Json &out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    if (sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(f); return false; }
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD rd = 0;
    BOOL ok = buf.empty() ? TRUE : ReadFile(f, &buf[0], (DWORD)buf.size(), &rd, nullptr);
    CloseHandle(f);
    if (!ok) return false;
    buf.resize(rd);
    std::wstring err;
    if (!Json::Parse(buf, out, &err)) {
        Log(L"JSON parse error in %s: %s", path.c_str(), err.c_str());
        return false;
    }
    return true;
}

bool JsonSaveFile(const std::wstring &path, const Json &j) {
    std::string s = j.Serialize(true);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(f, s.data(), (DWORD)s.size(), &w, nullptr);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
