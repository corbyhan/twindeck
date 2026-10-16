#pragma once
#include <windows.h>
#include <string>

// Moves the File Explorer window in front (its active tab) to path. False when the foreground window is not
// File Explorer or the navigation fails.
bool NavigateForegroundExplorer(const std::wstring &path);
