// Inspector (preview pane): file details, text preview (UTF-8/UTF-16/CP949 자동 판별), image preview, folder stats
#include "app.h"
#include "jobs.h"

static bool IsValidUtf8(const u8 *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        u8 c = s[i];
        if (c < 0x80) { i++; continue; }
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!len) return false;
        if (i + len > n) return true; // truncated at buffer end
        for (int k = 1; k < len; k++) if ((s[i + k] & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

static std::wstring DecodeText(const std::vector<u8> &b, bool *binary) {
    *binary = false;
    size_t n = b.size();
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) return std::wstring((const wchar_t *)(b.data() + 2), (n - 2) / 2);
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        std::wstring w((n - 2) / 2, L'\0');
        for (size_t i = 0; i < w.size(); i++) w[i] = (wchar_t)((b[2 + i * 2] << 8) | b[3 + i * 2]);
        return w;
    }
    size_t off = (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    size_t probe = std::min<size_t>(n, 8192);
    int zeros = 0;
    for (size_t i = off; i < probe; i++) if (b[i] == 0) zeros++;
    if (zeros > 0) {
        // UTF-16LE without BOM looks like ASCII interleaved with zeros
        bool utf16 = probe > 2 && zeros > (int)probe / 3;
        if (utf16) return std::wstring((const wchar_t *)b.data(), n / 2);
        *binary = true;
        return L"";
    }
    UINT cp = IsValidUtf8(b.data() + off, n - off) ? CP_UTF8 : 949; // Korean ANSI (EUC-KR/CP949) fallback
    int len = MultiByteToWideChar(cp, 0, (const char *)b.data() + off, (int)(n - off), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(cp, 0, (const char *)b.data() + off, (int)(n - off), &w[0], len);
    return w;
}

void App::ClearInspector() {
    SafeRelease(insp.image);
    insp.path.clear();
    insp.lines.clear();
    insp.isText = insp.binary = insp.tooLarge = false;
    insp.imgW = insp.imgH = 0;
    insp.scroll = 0;
    insp.info.clear();
    insp.loading = false;
    insp.gen++;
}

void App::UpdateInspector() {
    if (!inspectorOpen) return;
    Tab *t = CurTab();
    if (!t) return;
    if (t->rowsDirty) RebuildRows(t);
    std::wstring path;
    const FileEntry *fe = nullptr;
    auto sel = SelectedPaths(t, true);
    if (!sel.empty()) {
        int row = RowAt(t, sel.back());
        if (row >= 0) { path = sel.back(); fe = t->rows[row].e; }
    }
    if (path.empty()) {
        if (insp.path != t->path) { ClearInspector(); insp.path = t->path; insp.isDir = true; }
        return;
    }
    if (path == insp.path) return;
    ClearInspector();
    insp.path = path;
    insp.isDir = fe && (fe->isDir || fe->isDrive);
    if (!fe || fe->isRecycled) return;
    if (insp.isDir) {
        if (!fe->isDrive) sizes.Get(path, true);
        return;
    }
    u64 gen = insp.gen;
    std::wstring ext = PathExtension(path);
    u64 size = fe->size;
    insp.loading = true;
    // OneDrive "online-only" placeholders: reading the content would download the whole file, so only ask the
    // shell for the thumbnail the cloud provider already has
    bool cloudOnly = (fe->attrs & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE)) != 0;
    if (IsImageExtension(ext) || cloudOnly) {
        JobSubmit([path, gen, cloudOnly] {
            auto img = std::make_shared<DecodedImage>();
            if (cloudOnly || !DecodeImageFile(path, 2048, *img)) {
                ShellThumbnail(path, 512, *img);
                img->srcW = img->srcH = 0; // a thumbnail says nothing about the real resolution
            }
            MainPost([img, gen] {
                App *a = g_app;
                if (!a || a->insp.gen != gen) return;
                a->insp.loading = false;
                if (img->ok) {
                    a->insp.image = a->r.CreateBitmapBGRA(img->pixels.data(), img->w, img->h, img->w * 4);
                    a->insp.imgW = img->srcW;
                    a->insp.imgH = img->srcH;
                }
                a->Invalidate();
            });
        });
        return;
    }
    bool thumbs = settings.loadThumbnails;
    JobSubmit([path, gen, size, thumbs] {
        auto lines = std::make_shared<std::vector<std::wstring>>();
        auto img = std::make_shared<DecodedImage>();
        bool binary = false, tooLarge = false, isText = false;
        const size_t limit = 1024 * 1024;
        HANDLE f = CreateFileW(LongPathPrefix(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            std::vector<u8> buf((size_t)std::min<u64>(size, limit));
            DWORD rd = 0;
            if (!buf.empty()) ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, nullptr);
            CloseHandle(f);
            buf.resize(rd);
            std::wstring text = DecodeText(buf, &binary);
            if (!binary) {
                isText = true;
                tooLarge = size > limit;
                size_t start = 0;
                while (start <= text.size() && lines->size() < 50000) {
                    size_t nl = text.find(L'\n', start);
                    std::wstring line = text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
                    if (!line.empty() && line.back() == L'\r') line.pop_back();
                    lines->push_back(std::move(line));
                    if (nl == std::wstring::npos) break;
                    start = nl + 1;
                }
            }
        }
        if (binary && thumbs) ShellThumbnail(path, 512, *img);
        MainPost([lines, img, gen, binary, tooLarge, isText] {
            App *a = g_app;
            if (!a || a->insp.gen != gen) return;
            a->insp.loading = false;
            a->insp.binary = binary && !img->ok;
            a->insp.tooLarge = tooLarge;
            a->insp.isText = isText;
            a->insp.lines = std::move(*lines);
            if (img->ok) {
                a->insp.image = a->r.CreateBitmapBGRA(img->pixels.data(), img->w, img->h, img->w * 4);
                a->insp.imgW = img->srcW;
                a->insp.imgH = img->srcH;
            }
            a->Invalidate();
        });
    });
}

static std::wstring AttrString(u32 a) {
    std::wstring s;
    if (a & FILE_ATTRIBUTE_READONLY) s += L"R";
    if (a & FILE_ATTRIBUTE_HIDDEN) s += L"H";
    if (a & FILE_ATTRIBUTE_SYSTEM) s += L"S";
    if (a & FILE_ATTRIBUTE_ARCHIVE) s += L"A";
    if (a & FILE_ATTRIBUTE_COMPRESSED) s += L"C";
    if (a & FILE_ATTRIBUTE_ENCRYPTED) s += L"E";
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) s += L"L";
    if (a & FILE_ATTRIBUTE_OFFLINE) s += L"O";
    return s.empty() ? L"-" : s;
}

void App::DrawInspector(RectF rc) {
    r.FillSurface(rc, theme.panelBg);
    if (insp.path.empty() || insp.gen == 0) UpdateInspector();
    Tab *t = CurTab();
    f32 lh = r.Metrics(FONT_UI).lineH;
    RectF head = rc;
    head = head.CutTop(std::floor(lh + 14 * S));
    r.FillSurface(head, theme.headerBg);
    r.Line(head.x, head.Bottom() - 0.5f, head.Right(), head.Bottom() - 0.5f, theme.border);
    r.Text(FONT_UI_BOLD, T(S_Inspector), RectF(head.x + 12 * S, head.y, head.w - 120 * S, head.h), theme.textDim);
    RectF bclose(head.Right() - head.h, head.y, head.h, head.h);
    RectF blayout(bclose.x - head.h, head.y, head.h, head.h);
    if (IconButton(HashId(&insp, 1), bclose.Shrink(4 * S), GLYPH_CLOSE, TL(L"Close inspector", L"미리 보기 닫기"))) { RunCommand(L"InspectorToggle"); return; }
    if (IconButton(HashId(&insp, 2), blayout.Shrink(4 * S), settings.inspectorLayout ? GLYPH_SPLIT_R : GLYPH_SPLIT_B,
                   TL(L"Toggle inspector layout", L"미리 보기 위치 전환")))
        RunCommand(L"InspectorChangeLayout");

    RectF body = rc.Shrink(12 * S);
    body.y = head.Bottom() + 10 * S;
    body.h = rc.Bottom() - body.y - 10 * S;
    if (!t) return;
    const FileEntry *fe = nullptr;
    int row = insp.path.empty() ? -1 : RowAt(t, insp.path);
    if (row >= 0) fe = t->rows[row].e;
    // ----- title
    ID2D1Bitmap *icon = nullptr;
    std::wstring title;
    if (fe) { icon = icons.Get(insp.path, *fe); title = DisplayName(*fe); }
    else if (t->kind == VIEW_FOLDER) { FileEntry d; d.isDir = true; d.name = PathFileName(t->path); icon = icons.Get(t->path, d); title = t->Title(); }
    else title = t->Title();
    f32 isz = std::floor(lh * 1.6f);
    RectF tr = body.CutTop(isz + 6 * S);
    if (icon) r.DrawBitmap(icon, RectF(tr.x, tr.y, isz, isz));
    r.Text(FONT_TITLE, title, RectF(tr.x + isz + 10 * S, tr.y, tr.w - isz - 10 * S, isz), theme.text);

    // ----- details
    std::vector<std::pair<std::wstring, std::wstring>> kv;
    if (fe) {
        if (fe->isDrive) {
            kv.push_back({T(S_ColType), CellText(t, t->rows[row], COL_TYPE)});
            kv.push_back({T(S_ColTotal), FormatSize(fe->totalBytes)});
            kv.push_back({T(S_ColFree), FormatSize(fe->freeBytes)});
            kv.push_back({T(S_ColFileSystem), fe->fsName});
        } else {
            kv.push_back({T(S_ColType), FileTypeName(fe->name, fe->isDir)});
            if (fe->isDir) {
                auto st = sizes.Get(insp.path, true);
                if (st) {
                    std::wstring s = Format(T(S_FolderStats), st->files.load(), st->dirs.load(), FormatSize(st->size).c_str());
                    if (!st->done) { s += L"  " + std::wstring(T(S_Calculating)); Invalidate(); }
                    kv.push_back({T(S_ColSize), s});
                }
            } else {
                kv.push_back({T(S_ColSize), Format(L"%s (%llu bytes)", FormatSize(fe->size).c_str(), fe->size)});
            }
            if (fe->isRecycled) {
                kv.push_back({T(S_ColOrigLocation), fe->origLocation});
                kv.push_back({T(S_ColDeleted), FormatFileTime(fe->deleted)});
            } else {
                kv.push_back({T(S_ColModified), FormatFileTime(fe->modified)});
                kv.push_back({T(S_ColCreated), FormatFileTime(fe->created)});
                kv.push_back({T(S_ColAccessed), FormatFileTime(fe->accessed)});
                kv.push_back({T(S_Attributes), AttrString(fe->attrs)});
            }
            if (insp.imgW) kv.push_back({T(S_Dimensions), Format(L"%u × %u", insp.imgW, insp.imgH)});
            kv.push_back({T(S_Path), insp.path});
        }
    } else if (t->kind == VIEW_FOLDER) {
        auto st = sizes.Get(t->path, true);
        if (st) {
            std::wstring s = Format(T(S_FolderStats), st->files.load(), st->dirs.load(), FormatSize(st->size).c_str());
            if (!st->done) { s += L"  " + std::wstring(T(S_Calculating)); Invalidate(); }
            kv.push_back({T(S_ColSize), s});
        }
        kv.push_back({T(S_Path), t->path});
    } else {
        kv.push_back({L"", T(S_NoSelection)});
    }
    f32 keyW = 0;
    for (auto &p : kv) keyW = std::max(keyW, r.Measure(FONT_SMALL, p.first));
    keyW = std::min(keyW + 12 * S, body.w * 0.4f);
    body.CutTop(6 * S);
    for (auto &p : kv) {
        RectF rr = body.CutTop(std::floor(r.Metrics(FONT_SMALL).lineH + 6 * S));
        r.Text(FONT_SMALL, p.first, RectF(rr.x, rr.y, keyW, rr.h), theme.textFaint);
        RectF vr(rr.x + keyW, rr.y, rr.w - keyW, rr.h);
        r.Text(FONT_SMALL, p.second, vr, theme.text);
        if (Hover(vr) && r.Measure(FONT_SMALL, p.second) > vr.w) SetTooltip(HashStr(p.second, 77), p.second);
    }
    body.CutTop(10 * S);
    r.Line(body.x, body.y, body.Right(), body.y, theme.border);
    body.CutTop(8 * S);

    // ----- preview
    if (insp.image) {
        D2D1_SIZE_F sz = insp.image->GetSize();
        f32 sc = std::min(body.w / sz.width, body.h / sz.height);
        sc = std::min(sc, 4.f);
        f32 w = sz.width * sc, h = sz.height * sc;
        RectF ir(body.x + (body.w - w) / 2, body.y + 4 * S, w, h);
        // checkerboard behind transparent images
        r.PushClip(ir);
        f32 cs = 8 * S;
        for (f32 yy = ir.y; yy < ir.Bottom(); yy += cs)
            for (f32 xx = ir.x; xx < ir.Right(); xx += cs)
                if (((int)((xx - ir.x) / cs) + (int)((yy - ir.y) / cs)) & 1) r.FillRect(RectF(xx, yy, cs, cs), theme.rowAlt);
        r.PopClip();
        r.DrawBitmap(insp.image, ir);
        return;
    }
    if (insp.loading) {
        r.Text(FONT_SMALL, T(S_Loading), RectF(body.x, body.y, body.w, lh), theme.textFaint);
        Invalidate();
        return;
    }
    if (insp.binary) {
        r.Text(FONT_SMALL, T(S_PreviewBinary), RectF(body.x, body.y, body.w, lh), theme.textFaint);
        return;
    }
    if (!insp.isText) return;
    // text preview, scroll measured in lines
    f32 mlh = r.Metrics(FONT_MONO).lineH;
    int nLines = (int)insp.lines.size();
    if (Hover(body) && in.wheel != 0) insp.scroll -= in.wheel * 3;
    insp.scroll = Clamp(insp.scroll, 0.f, (f32)std::max(0, nLines - 1));
    f32 gutter = 0;
    if (insp.lineNumbers) gutter = r.Measure(FONT_MONO, std::to_wstring(nLines)) + 14 * S;
    r.PushClip(body);
    f32 y = body.y;
    for (int i = (int)insp.scroll; i < nLines && y < body.Bottom(); i++) {
        std::wstring line = insp.lines[i];
        // tabs as 4 spaces
        std::wstring shown;
        for (wchar_t c : line) {
            if (c == L'\t') shown += insp.showWhitespace ? L"→   " : L"    ";
            else if (c == L' ' && insp.showWhitespace) shown += L'·';
            else shown += c;
        }
        f32 h = mlh;
        RectF lr(body.x + gutter, y, body.w - gutter, mlh);
        if (insp.wordWrap && !shown.empty()) {
            h = r.MeasureWrapped(FONT_MONO, shown, lr.w);
            lr.h = h;
            TextOpts o; o.wrap = true; o.vcenter = false;
            r.Text(FONT_MONO, shown, lr, theme.text, o);
        } else {
            TextOpts o; o.ellipsis = false;
            r.Text(FONT_MONO, shown, lr, theme.text, o);
        }
        if (insp.lineNumbers) {
            TextOpts no; no.align = ALIGN_RIGHT;
            r.Text(FONT_MONO, std::to_wstring(i + 1), RectF(body.x, y, gutter - 10 * S, mlh), theme.textFaint, no);
        }
        y += h;
    }
    r.PopClip();
    if (insp.tooLarge) {
        RectF nr(body.x, body.Bottom() - mlh, body.w, mlh);
        r.FillSurface(nr, theme.panelBg);
        r.Text(FONT_SMALL, T(S_PreviewTooLarge), nr, theme.textFaint);
    }
}
