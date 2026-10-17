# -*- coding: utf-8 -*-
"""
把 PlatformIO 产出的 bootloader / 分区表 / app 合并成单文件并刷入小喵掌机。

用法:
    python tools/flash.py            # 默认 COM3
    python tools/flash.py COM5
    python tools/flash.py --merge    # 只合并，不刷
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "firmware-merged.bin")


def find_build_dir():
    """自动定位含有 firmware.bin 的构建目录（兼容 .pio/build / .pio/build2）。"""
    base = os.path.join(ROOT, ".pio")
    candidates = []
    if os.path.isdir(base):
        for name in sorted(os.listdir(base)):
            if not name.startswith("build"):
                continue
            env = os.path.join(base, name, "xiaomiao")
            fw = os.path.join(env, "firmware.bin")
            if os.path.exists(fw):
                candidates.append((os.path.getmtime(fw), env))
    if not candidates:
        raise SystemExit("找不到 firmware.bin，请先运行: pio run")
    candidates.sort()
    return candidates[-1][1]


BUILD = find_build_dir()

BOOT_OFF = "0x1000"
PART_OFF = "0x8000"


def factory_offset():
    with open(os.path.join(ROOT, "partitions.csv"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) >= 5 and cols[1] == "app":
                return cols[3]
    return "0x10000"


def main():
    args = [a for a in sys.argv[1:]]
    merge_only = "--merge" in args
    args = [a for a in args if not a.startswith("--")]
    port = args[0] if args else "COM3"

    files = {
        BOOT_OFF: os.path.join(BUILD, "bootloader.bin"),
        PART_OFF: os.path.join(BUILD, "partitions.bin"),
        factory_offset(): os.path.join(BUILD, "firmware.bin"),
    }
    for off, p in files.items():
        if not os.path.exists(p):
            print("缺少 %s，请先运行: pio run" % p)
            return 1

    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin",
           "-o", OUT, "--flash_size", "4MB", "--flash_mode", "dio", "--flash_freq", "40m"]
    for off, p in files.items():
        cmd += [off, p]
    print(" ".join(cmd))
    if subprocess.call(cmd) != 0:
        return 1
    print("合并完成: %s (%d bytes)" % (OUT, os.path.getsize(OUT)))

    if merge_only:
        return 0

    w = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port,
         "--baud", "460800", "write_flash", "-z", "0x0", OUT]
    print(" ".join(w))
    return subprocess.call(w)


if __name__ == "__main__":
    sys.exit(main())
