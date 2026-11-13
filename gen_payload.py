#!/usr/bin/env python3
"""把 assets 打包成容器并追加到可执行文件尾部。"""
import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(ROOT, "assets")

# (源文件绝对路径, 容器内名字)
SINGLE = [
    (os.path.join(ASSETS, "sc", "bin", "dinput8.dll"), "dinput8.dll"),
    (os.path.join(ASSETS, "sc", "bin", "ScriptHookRDR2.dll"), "ScriptHookRDR2.dll"),
    (os.path.join(ASSETS, "sc", "bin", "NativeTrainer.asi"), "NativeTrainer.asi"),
    (os.path.join(ASSETS, "rampage", "Rampage_1.0.1491.50", "Rampage.asi"), "Rampage.asi"),
]
TREES = [
    (os.path.join(ASSETS, "rampage", "Rampage_1.0.1491.50", "RampageFiles"), "RampageFiles"),
]


def collect():
    items = []
    for src, name in SINGLE:
        if not os.path.isfile(src):
            sys.exit("missing: " + src)
        items.append((src, name))
    for src, prefix in TREES:
        if not os.path.isdir(src):
            sys.exit("missing dir: " + src)
        for dirpath, _dirnames, filenames in os.walk(src):
            for fn in sorted(filenames):
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, os.path.dirname(src))
                rel = rel.replace(os.sep, "/")
                items.append((full, rel))
    return items


def build_payload(items):
    out = bytearray()
    out += struct.pack("<I", len(items))
    total = 0
    for path, name in items:
        data = open(path, "rb").read()
        bname = name.encode("utf-8")
        if len(bname) > 65535:
            sys.exit("name too long: " + name)
        out += struct.pack("<H", len(bname)) + bname
        out += struct.pack("<Q", len(data))
        out += data
        total += len(data)
        print("  %-40s %9d  %s" % (name, len(data), hashlib.sha256(data).hexdigest()[:16]))
    print("  %d 个文件，%d 字节" % (len(items), total))
    return bytes(out)


def append_to(binary_path, payload, dest):
    exe = open(binary_path, "rb").read()
    off = len(exe)
    trailer = struct.pack("<QQ", len(payload), off) + b"RDR2PKG1"
    with open(dest, "wb") as f:
        f.write(exe)
        f.write(payload)
        f.write(trailer)
    os.chmod(dest, 0o755)
    print("  -> %s  (%d + %d + 24 = %d 字节)" % (dest, len(exe), len(payload), os.path.getsize(dest)))


def main():
    payload_path = os.path.join(ROOT, "build", "payload.bin")
    items = collect()
    payload = build_payload(items)
    os.makedirs(os.path.dirname(payload_path), exist_ok=True)
    open(payload_path, "wb").write(payload)
    print("payload: %d 字节 -> %s" % (len(payload), payload_path))
    if len(sys.argv) >= 3:
        append_to(sys.argv[1], payload, sys.argv[2])


if __name__ == "__main__":
    main()
