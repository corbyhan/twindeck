// Minimal JSON DOM: parse / serialize (UTF-8 on disk, wide strings in memory)
#pragma once
#include "common.h"
#include <map>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    f64 num = 0;
    std::wstring str;
    std::vector<Json> arr;
    std::vector<std::pair<std::wstring, Json>> obj; // keeps insertion order

    Json() {}
    static Json MakeObject() { Json j; j.type = Object; return j; }
    static Json MakeArray() { Json j; j.type = Array; return j; }
    Json(bool v) : type(Bool), b(v) {}
    Json(int v) : type(Number), num(v) {}
    Json(i64 v) : type(Number), num((f64)v) {}
    Json(u32 v) : type(Number), num(v) {}
    Json(f64 v) : type(Number), num(v) {}
    Json(f32 v) : type(Number), num(v) {}
    Json(const wchar_t *s) : type(String), str(s) {}
    Json(const std::wstring &s) : type(String), str(s) {}

    bool IsObject() const { return type == Object; }
    bool IsArray() const { return type == Array; }

    const Json *Find(const wchar_t *key) const;
    Json &operator[](const wchar_t *key); // creates on object
    void Push(const Json &v) { type = Array; arr.push_back(v); }

    bool GetBool(const wchar_t *key, bool def) const;
    f64 GetNum(const wchar_t *key, f64 def) const;
    int GetInt(const wchar_t *key, int def) const { return (int)GetNum(key, def); }
    std::wstring GetStr(const wchar_t *key, const std::wstring &def) const;

    std::string Serialize(bool pretty = true) const;
    static bool Parse(const std::string &utf8, Json &out, std::wstring *err = nullptr);
};

bool JsonLoadFile(const std::wstring &path, Json &out);
bool JsonSaveFile(const std::wstring &path, const Json &j); // atomic (temp + MoveFileEx)
