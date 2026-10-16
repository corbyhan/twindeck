@echo off
setlocal
set VSLANG=1033
rem TwinDeck build script (MSVC). Usage: build.bat [debug|release]
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=release

where cl >nul 2>nul
if errorlevel 1 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
)

cd /d "%~dp0"
if not exist build mkdir build

set CFLAGS=/nologo /std:c++17 /EHsc /W3 /utf-8 /MP /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fdbuild\
if "%CONFIG%"=="debug" (
    set CRT=/Zi /Od /MTd /D_DEBUG
) else (
    set CRT=/Zi /O2 /MT /DNDEBUG
)
set CFLAGS=%CFLAGS% %CRT%

set LIBS=user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib shlwapi.lib comctl32.lib ^
 d3d11.lib dxgi.lib d2d1.lib dwrite.lib windowscodecs.lib imm32.lib dwmapi.lib uuid.lib propsys.lib ^
 netapi32.lib mpr.lib dbghelp.lib uxtheme.lib

rem TwinDeckOpen.exe (folder "open" verb, blank icon) is built first: TwinDeck.exe embeds it and writes it out on install
if not exist build\opener mkdir build\opener
rc /nologo /fo build\opener\opener.res opener\opener.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Fobuild\opener\ /Fdbuild\opener\ ^
 opener\opener.cpp src\explorernav.cpp build\opener\opener.res /Febuild\TwinDeckOpen.exe ^
 /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF user32.lib shell32.lib ole32.lib oleaut32.lib shlwapi.lib propsys.lib uuid.lib
if errorlevel 1 exit /b 1

rc /nologo /fo build\app.res res\app.rc || exit /b 1

rem Everything SDK client (C, MIT): compiled in instead of shipping Everything64.dll; see third_party\everything
cl /nologo /c /W1 %CRT% /DUNICODE /D_UNICODE /DEVERYTHINGUSERAPI= /Fobuild\everything_sdk.obj /Fdbuild\ ^
 third_party\everything\src\Everything.c
if errorlevel 1 exit /b 1

cl %CFLAGS% src\*.cpp build\everything_sdk.obj build\app.res /Febuild\TwinDeck.exe /link /SUBSYSTEM:WINDOWS /DEBUG /MAP:build\TwinDeck.map /OPT:REF /OPT:ICF %LIBS%
if errorlevel 1 exit /b 1
rem the same program doubles as the installer: a file name containing "Setup" opens the setup screen
copy /Y build\TwinDeck.exe build\TwinDeck-Setup.exe >nul
echo Build OK: build\TwinDeck.exe
