# -*- coding: utf-8 -*-
"""
把下载好的 zip 解压成 PlatformIO 可识别的包：
  dl_toolchain.zip -> ~/.platformio/packages/toolchain-xtensa-esp32
  dl_arduino.zip   -> ~/.platformio/packages/framework-arduinoespressif32
"""
import json
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PKGS = os.path.join(os.path.expanduser("~"), ".platformio", "packages")

JOBS = [
    {
        "zip": os.path.join(ROOT, "dl_toolchain.zip"),
        "dest": os.path.join(PKGS, "toolchain-xtensa-esp32"),
        "name": "toolchain-xtensa-esp32",
        "version": "8.4.0+2021r2-patch5",
        "probe": "bin/xtensa-esp32-elf-gcc.exe",
    },
    {
        "zip": os.path.join(ROOT, "dl_arduino.zip"),
        "dest": os.path.join(PKGS, "framework-arduinoespressif32"),
        "name": "framework-arduinoespressif32",
        "version": "4.20017.0",
        "probe": "tools/platformio-build.py",
    },
]


def strip_root(names):
    tops = {n.replace("\\", "/").split("/")[0] for n in names if "/" in n.replace("\\", "/")}
    if len(tops) == 1:
        return tops.pop()
    return None


def install(job):
    zpath, dest = job["zip"], job["dest"]
    if not os.path.exists(zpath):
        print("缺少", zpath)
        return False
    probe = os.path.join(dest, *job["probe"].split("/"))
    if os.path.exists(probe):
        print("已安装:", dest)
        return True

    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(dest, exist_ok=True)

    print("解压", zpath, "->", dest, flush=True)
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        root = strip_root(names)
        print("  文件数", len(names), "根目录", root, flush=True)
        z.extractall(dest)

    if root:
        inner = os.path.join(dest, root)
        if os.path.isdir(inner):
            for item in os.listdir(inner):
                shutil.move(os.path.join(inner, item), os.path.join(dest, item))
            os.rmdir(inner)

    if not os.path.exists(probe):
        print("警告: 解压后未找到", job["probe"])
        return False

    with open(os.path.join(dest, "package.json"), "w", encoding="utf-8") as f:
        json.dump({"name": job["name"], "version": job["version"], "system": ["*"],
                   "description": "manually installed by xiaomiao-vocab"}, f)
    print("完成:", dest)
    return True


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    ok = True
    for job in JOBS:
        if only and job["name"] != only:
            continue
        ok = install(job) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
