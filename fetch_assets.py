#!/usr/bin/env python3
"""下载构建所需的第三方素材到 assets/。

素材不上传到仓库（都是别人的分发物），仓库里只有源码和构建脚本。
用法：python fetch_assets.py
"""
import io
import os
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(ROOT, "assets")

SCRIPT_HOOK_URL = "https://www.dev-c.com/files/ScriptHookRDR2_1.0.1491.17.zip"
# dev-c.com 挡直链，必须带 Referer
SCRIPT_HOOK_HEADERS = {
    "Referer": "https://www.dev-c.com/rdr2/scripthookrdr2/",
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
}
RAMPAGE_URL = "https://github.com/rampage-trainer/rdr2/releases/download/v.1.6.8/Rampage.zip"


def fetch(url, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    print("  下载 %s" % url)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001
            print("    第 %d 次失败：%s" % (attempt + 1, e))
    sys.exit("下载失败：" + url)


def unzip_to(data, dest):
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(dest)
    print("  解包到 %s" % dest)


def main():
    os.makedirs(ASSETS, exist_ok=True)
    have_sc = os.path.isfile(os.path.join(ASSETS, "sc", "bin", "ScriptHookRDR2.dll"))
    have_rp = os.path.isdir(os.path.join(ASSETS, "rampage", "Rampage_1.0.1491.50"))
    if not have_sc:
        unzip_to(fetch(SCRIPT_HOOK_URL, SCRIPT_HOOK_HEADERS), os.path.join(ASSETS, "sc"))
    else:
        print("  已存在 ScriptHookRDR2")
    if not have_rp:
        unzip_to(fetch(RAMPAGE_URL), os.path.join(ASSETS, "rampage"))
    else:
        print("  已存在 Rampage")
    print("\n素材就绪，现在可以跑 build.sh（需要 zig，见 README）")


if __name__ == "__main__":
    main()
