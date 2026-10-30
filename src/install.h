// Per-user install / modify / uninstall (install.cpp)
#pragma once
#include "common.h"

struct InstallOptions {
    std::wstring dir;
    bool desktopShortcut = false;
    bool setDefault = false;     // folders, drives and Win+E open in TwinDeck instead of File Explorer
    bool contextMenu = true;     // "Open in TwinDeck" / "TwinDeck here" in the right-click menus
    bool addToPath = true;       // install folder on the user PATH (run "twindeck" from a terminal)
    bool winEHotkey = false;     // a keyboard hook (TwinDeckOpen.exe --winkey, from sign-in) opens TwinDeck for Win+E
};

struct InstallInfo {
    bool installed = false;
    std::wstring version;
    InstallOptions opts;         // current options, or the defaults when not installed
};

std::wstring SelfPath();
std::wstring DefaultInstallDir();
bool IsInstalled();
InstallInfo QueryInstall();
bool IsRunningInstalledCopy();
bool PerformInstall(const InstallOptions &o, std::wstring *err);
bool PerformUninstall(bool *selfWasInstalled);
void ScheduleSelfDelete();
void CleanupAfterUpdate();      // removes "TwinDeck.exe.old" left by an update over a running copy
bool LaunchInstalled();
