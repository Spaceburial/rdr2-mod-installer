#!/bin/bash
# 构建两个平台的单文件安装器
set -e
ZIG="D:/tools/zigpkg/ziglang/zig.exe"
cd "$(dirname "$0")"

CFLAGS="-O2 -std=c11 -Wall -Wextra"
mkdir -p build

"$ZIG" cc $CFLAGS -target x86_64-windows-gnu src/rdr2mod.c -o build/rdr2mod.exe
"$ZIG" cc $CFLAGS -static -target x86_64-linux-musl src/rdr2mod.c -o build/rdr2mod

python gen_payload.py build/rdr2mod.exe build/rdr2mod-win.exe | tail -3
python gen_payload.py build/rdr2mod build/rdr2mod-linux | tail -3
ls -l build/rdr2mod-win.exe build/rdr2mod-linux
