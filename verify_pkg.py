#!/usr/bin/env python3
"""独立校验：解析追加在二进制尾部的负载，与 assets 逐字节比对。"""
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def parse(path):
    data = open(path, "rb").read()
    plen, poff = struct.unpack("<QQ", data[-24:-8])
    magic = data[-8:]
    assert magic == b"RDR2PKG1", magic
    payload = data[poff:poff + plen]
    assert len(payload) == plen, (len(payload), plen)
    n = struct.unpack("<I", payload[:4])[0]
    pos = 4
    out = {}
    for _ in range(n):
        nl = struct.unpack("<H", payload[pos:pos + 2])[0]
        pos += 2
        name = payload[pos:pos + nl].decode()
        pos += nl
        sz = struct.unpack("<Q", payload[pos:pos + 8])[0]
        pos += 8
        out[name] = payload[pos:pos + sz]
        pos += sz
    assert pos == len(payload), (pos, len(payload))
    return data[:poff], out


def assets():
    a = os.path.join(ROOT, "assets")
    base = os.path.join(a, "rampage", "Rampage_1.0.1491.50")
    m = {
        "dinput8.dll": os.path.join(a, "sc", "bin", "dinput8.dll"),
        "ScriptHookRDR2.dll": os.path.join(a, "sc", "bin", "ScriptHookRDR2.dll"),
        "NativeTrainer.asi": os.path.join(a, "sc", "bin", "NativeTrainer.asi"),
        "Rampage.asi": os.path.join(base, "Rampage.asi"),
    }
    for dp, _d, fs in os.walk(os.path.join(base, "RampageFiles")):
        for f in fs:
            full = os.path.join(dp, f)
            m[os.path.relpath(full, base).replace(os.sep, "/")] = full
    return m


def main():
    exp = assets()
    rc = 0
    for path in sys.argv[1:]:
        head, entries = parse(path)
        ok = set(entries) == set(exp)
        bad = [k for k in exp if k not in entries or entries[k] != open(exp[k], "rb").read()]
        print("%s: 前缀 %d 字节, 负载 %d 个文件, 全部匹配=%s" % (os.path.basename(path), len(head), len(entries), ok and not bad))
        if bad:
            rc = 1
            print("  不一致:", bad[:5])
    return rc


if __name__ == "__main__":
    sys.exit(main())
