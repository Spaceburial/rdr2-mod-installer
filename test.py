#!/usr/bin/env python3
"""安装器真机测试（在 Windows 上用模拟 Steam 目录跑）。"""
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(ROOT, "build", "rdr2mod-win.exe")
TEST = os.path.join(ROOT, "test")
HOME = os.path.join(TEST, "home")
STEAM = os.path.join(TEST, "Steam")
SDLIB = os.path.join(TEST, "sdlib")
GAME = os.path.join(SDLIB, "steamapps", "common", "Red Dead Redemption 2")
VDF = os.path.join(STEAM, "userdata", "12345678", "config", "localconfig.vdf")

results = []


def check(name, cond, extra=""):
    results.append((name, bool(cond), extra))
    print(("  PASS  " if cond else "  FAIL  ") + name + (("   " + extra) if extra and not cond else ""))


def run(*args, stdin=None):
    p = subprocess.run([EXE] + list(args) + ["--home", HOME, "--steam-root", STEAM],
                       capture_output=True, input=stdin, text=True, encoding="utf-8", errors="replace")
    return p.returncode, p.stdout + p.stderr


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def expected_files():
    a = os.path.join(ROOT, "assets")
    base = os.path.join(a, "rampage", "Rampage_1.0.1491.50")
    items = {
        "dinput8.dll": os.path.join(a, "sc", "bin", "dinput8.dll"),
        "ScriptHookRDR2.dll": os.path.join(a, "sc", "bin", "ScriptHookRDR2.dll"),
        "Rampage.asi": os.path.join(base, "Rampage.asi"),
    }
    for dirpath, _d, files in os.walk(os.path.join(base, "RampageFiles")):
        for f in files:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, base).replace(os.sep, "/")
            items[rel] = full
    return items


def braces_balanced(text):
    depth, inq, esc = 0, False, False
    for ch in text:
        if inq:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                inq = False
            continue
        if ch == '"':
            inq = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth < 0:
                return False
    return depth == 0 and not inq


def launch_val():
    t = open(VDF, encoding="utf-8", errors="replace").read()
    i = t.find('"LaunchOptions"')
    if i < 0:
        return None
    j = t.find('"', i + 15)
    if j < 0:
        return None
    k = j + 1
    out = []
    while k < len(t):
        if t[k] == "\\" and k + 1 < len(t):
            out.append(t[k + 1])
            k += 2
            continue
        if t[k] == '"':
            break
        out.append(t[k])
        k += 1
    return "".join(out)


def fresh():
    subprocess.run([sys.executable, os.path.join(ROOT, "make_sandbox.py")], check=True,
                   stdout=subprocess.DEVNULL)


print("== T1 自动发现 + 安装 ==")
fresh()
orig_vdf = open(VDF, "rb").read()
rc, out = run("--install")
check("退出码 0", rc == 0, out[-400:])
check("找到第二库里的游戏", "sdlib" in out, out[:400])
exp = expected_files()
ok = True
for rel, src in exp.items():
    dst = os.path.join(GAME, rel.replace("/", os.sep))
    if not os.path.isfile(dst) or sha(dst) != sha(src):
        ok = False
        print("    不一致:", rel)
check("20 个文件内容逐字节一致", ok, "%d 个" % len(exp))
check("NativeTrainer 默认不装", not os.path.exists(os.path.join(GAME, "NativeTrainer.asi")))
check("存档已备份", os.path.isdir(os.path.join(HOME, "RDR2-mod-backup")) and any(
    os.path.isfile(os.path.join(dp, f)) for dp, _dn, fn in os.walk(os.path.join(HOME, "RDR2-mod-backup")) for f in fn))

print("== T2 启动项写入 ==")
t = open(VDF, encoding="utf-8", errors="replace").read()
check("LaunchOptions 只有一条", t.count('"LaunchOptions"') == 1)
check("值是 WINEDLLOVERRIDES", (launch_val() or "").startswith("WINEDLLOVERRIDES="), str(launch_val()))
check("VDF 括号平衡", braces_balanced(t))
check("其他键没被破坏", '"LastPlayed"' in t and '"Playtime"' in t and '"AutoLoginUser"' in t)
raw = open(VDF, "rb").read()
check("CRLF 保留", b"\r\n" in raw and b"\n" not in raw.replace(b"\r\n", b""))

print("== T3 重复安装幂等 ==")
snap = open(VDF, "rb").read()
rc, out = run("--install")
check("退出码 0", rc == 0)
check("vdf 字节完全相同", open(VDF, "rb").read() == snap)
check("仍只有一条 LaunchOptions", open(VDF, encoding="utf-8", errors="replace").read().count('"LaunchOptions"') == 1)

print("== T4 状态 ==")
rc, out = run("--status")
lines = out.splitlines()
have = {ln.split()[0]: ln for ln in lines if ln.strip().startswith(("dinput8", "ScriptHookRDR2", "Rampage.asi", "RampageFiles", "NativeTrainer"))}
check("状态显示已装", "已装" in have.get("dinput8.dll", "") and "已装" in have.get("Rampage.asi", "")
      and "已装" in have.get("RampageFiles", ""), out[:600])

print("== T5 临时禁用/启用 ==")
rc, out = run("--toggle")
check("禁用后只剩 .off", os.path.exists(os.path.join(GAME, "dinput8.dll.off")) and not os.path.exists(os.path.join(GAME, "dinput8.dll")))
rc, out = run("--toggle")
check("启用后恢复", os.path.exists(os.path.join(GAME, "dinput8.dll")) and not os.path.exists(os.path.join(GAME, "dinput8.dll.off")))

print("== T6 卸载 ==")
rc, out = run("--uninstall")
check("退出码 0", rc == 0, out[-400:])
check("文件已删除", not os.path.exists(os.path.join(GAME, "Rampage.asi")) and not os.path.isdir(os.path.join(GAME, "RampageFiles")))
check("备份还在", os.path.isdir(os.path.join(HOME, "RDR2-mod-backup")))
lv = launch_val()
check("启动项已清空（原来没有）", lv == "", repr(lv))
check("VDF 结构仍平衡", braces_balanced(open(VDF, encoding="utf-8", errors="replace").read()))

print("== T7 游戏从未启动过（没有 appid 块）==")
fresh()
t = open(VDF, encoding="utf-8", errors="replace").read()
a = t.find('\t\t\t\t\t"1174180"')
b = t.find('\t\t\t\t\t"228980"')
open(VDF, "w", encoding="utf-8", newline="").write(t[:a] + t[b:])
rc, out = run("--install")
check("退出码 0", rc == 0, out[-400:])
t2 = open(VDF, encoding="utf-8", errors="replace").read()
check("新建了 appid 块", t2.count('"1174180"') == 1 and t2.count('"LaunchOptions"') == 1, t2[-600:])
check("括号平衡", braces_balanced(t2))
check("值是 WINEDLLOVERRIDES", (launch_val() or "").startswith("WINEDLLOVERRIDES="), str(launch_val()))

print("== T8 --no-vdf ==")
fresh()
before = open(VDF, "rb").read()
rc, out = run("--install", "--no-vdf")
check("vdf 未被改动", open(VDF, "rb").read() == before)
check("文件仍装了", os.path.exists(os.path.join(GAME, "Rampage.asi")))

print("== T9 交互菜单 ==")
fresh()
rc, out = run(stdin="3\n0\n")
check("菜单可用", rc == 0 and "选择：" in out and "游戏目录" in out, out[:400])

print("== T10 --nativetrainer ==")
fresh()
rc, out = run("--install", "--nativetrainer")
check("NativeTrainer 装上", os.path.exists(os.path.join(GAME, "NativeTrainer.asi")))
check("文件数 21", rc == 0 and "21 个" in out, out[-300:])

bad = [r for r in results if not r[1]]
print("\n%d 项检查，%d 失败" % (len(results), len(bad)))
for r in bad:
    print("  FAILED:", r[0], r[2][:300] if r[2] else "")
sys.exit(1 if bad else 0)
