// Subfolder search through Everything (voidtools) when it is running.
#pragma once
#include "fs.h"

// Searches root and its subfolders for pattern (words, or wildcards like *.md; ';' separates alternatives).
// False when Everything cannot answer for this folder or pattern (not running, index not loaded, not a local NTFS
// drive, Hangul initials): walk the folders instead. On success relDir is set relative to root.
bool EverythingSearch(const std::wstring &root, const std::wstring &pattern, bool showHidden, u32 maxResults,
                      std::vector<FileEntry> &out);
