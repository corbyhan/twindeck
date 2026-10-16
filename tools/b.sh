#!/bin/sh
# build and print decoded errors (MSVC messages may be in the system code page)
cd "$(dirname "$0")/.."
mkdir -p build
# stop only the copies running from this build folder, never an installed TwinDeck that is in use
powershell -NoProfile -Command "\$b = [IO.Path]::GetFullPath('build'); Get-Process TwinDeck, TwinDeck-Setup -ErrorAction SilentlyContinue | Where-Object { \$_.Path -and \$_.Path.StartsWith(\$b, 'OrdinalIgnoreCase') } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
cmd //c "$(cygpath -w "$PWD")\build.bat $1" > build/log.txt 2>&1
iconv -f cp949 -t utf-8 build/log.txt > build/log8.txt 2>/dev/null || cp build/log.txt build/log8.txt
grep -a -E "error C|error LNK|fatal error" build/log8.txt | head -${2:-40}
grep -a "Build OK" build/log8.txt
