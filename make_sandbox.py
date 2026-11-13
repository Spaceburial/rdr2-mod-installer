#!/usr/bin/env python3
"""搭一个假的 Steam 目录树，用来真机验证安装器。"""
import os
import shutil
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test")


def w(path, text, crlf=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.replace("\n", "\r\n") if crlf else text
    open(path, "wb").write(data.encode("utf-8"))


def build():
    if os.path.isdir(ROOT):
        shutil.rmtree(ROOT)
    steam = os.path.join(ROOT, "Steam")
    sdlib = os.path.join(ROOT, "sdlib")
    home = os.path.join(ROOT, "home")

    # 主库：不放游戏，只有一个空 common
    os.makedirs(os.path.join(steam, "steamapps", "common"), exist_ok=True)

    # 第二库（模拟 SD 卡）：游戏在这里
    game = os.path.join(sdlib, "steamapps", "common", "Red Dead Redemption 2")
    os.makedirs(game, exist_ok=True)
    open(os.path.join(game, "RDR2.exe"), "wb").write(b"MZ" + b"\0" * 200)
    open(os.path.join(game, "PlayRDR2.exe"), "wb").write(b"MZ" + b"\0" * 120)

    # 存档（Proton 前缀里）
    save = os.path.join(sdlib, "steamapps", "compatdata", "1174180", "pfx", "drive_c", "users", "steamuser",
                        "Documents", "Rockstar Games", "RDR2", "ABCD1234")
    os.makedirs(save, exist_ok=True)
    open(os.path.join(save, "SRDR30000"), "wb").write(b"fake save data" * 10)
    w(os.path.join(save, "cfg.dat"), "profile=1\n", crlf=False)

    # libraryfolders.vdf
    libs = (
        '"libraryfolders"\n'
        "{\n"
        '\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"label"\t\t""\n\t}\n'
        '\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"label"\t\t"SD"\n\t}\n'
        "}\n" % (steam.replace("\\", "\\\\"), sdlib.replace("\\", "\\\\"))
    )
    w(os.path.join(steam, "steamapps", "libraryfolders.vdf"), libs)

    # localconfig.vdf
    lc = (
        '"UserLocalConfigStore"\n'
        "{\n"
        '\t"Broadcast"\n\t{\n\t\t"Permissions"\t\t"2"\n\t}\n'
        '\t"Software"\n\t{\n'
        '\t\t"Valve"\n\t\t{\n'
        '\t\t\t"Steam"\n\t\t\t{\n'
        '\t\t\t\t"AutoLoginUser"\t\t"deck"\n'
        '\t\t\t\t"apps"\n\t\t\t\t{\n'
        '\t\t\t\t\t"1174180"\n\t\t\t\t\t{\n'
        '\t\t\t\t\t\t"LastPlayed"\t\t"1731000000"\n'
        '\t\t\t\t\t\t"Playtime"\t\t"120"\n'
        "\t\t\t\t\t}\n"
        '\t\t\t\t\t"228980"\n\t\t\t\t\t{\n'
        '\t\t\t\t\t\t"LastPlayed"\t\t"1700000000"\n'
        "\t\t\t\t\t}\n"
        "\t\t\t\t}\n"
        "\t\t\t}\n"
        "\t\t}\n"
        "\t}\n"
        "}\n"
    )
    w(os.path.join(steam, "userdata", "12345678", "config", "localconfig.vdf"), lc)

    os.makedirs(home, exist_ok=True)
    print("sandbox: %s" % ROOT)
    print("  steam : %s" % steam)
    print("  sdlib : %s" % sdlib)
    print("  home  : %s" % home)


if __name__ == "__main__":
    build()
    sys.exit(0)
