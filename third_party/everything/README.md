# Everything SDK (client)

From the Everything SDK by voidtools: https://www.voidtools.com/Everything-SDK.zip
(downloaded 2026-10-16). MIT license, see the header of `src/Everything.c`.

TwinDeck compiles `src/Everything.c` into the program (no `Everything64.dll`) to search subfolders through a
running Everything instead of walking the folders (`src/everything.cpp`).

Change from the original: in `src/Everything.c`, `EVERYTHINGUSERAPI` is only defined when not already defined,
so that the build can define it empty and the functions are not exported from TwinDeck.exe.
