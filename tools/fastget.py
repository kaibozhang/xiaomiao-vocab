# -*- coding: utf-8 -*-
"""
多线程分段下载器：单线程只有几十 KB/s 的源，开 16 段能跑到 ~700KB/s。
用法: python tools/fastget.py <url> <out> [segments]
"""
import os
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

BLOCK = 256 * 1024


def get_size(url):
    req = urllib.request.Request(url, method="HEAD")
    with urllib.request.urlopen(req, timeout=60) as r:
        return int(r.headers.get("Content-Length", 0))


def fetch_range(url, start, end, path, tries=5):
    for t in range(tries):
        try:
            got = os.path.getsize(path) if os.path.exists(path) else 0
            if got >= end - start + 1:
                return
            req = urllib.request.Request(url, headers={"Range": "bytes=%d-%d" % (start + got, end)})
            with urllib.request.urlopen(req, timeout=90) as r, open(path, "ab") as f:
                while True:
                    chunk = r.read(BLOCK)
                    if not chunk:
                        break
                    f.write(chunk)
            if os.path.getsize(path) >= end - start + 1:
                return
        except Exception as e:
            print("  段重试", start, e, flush=True)
            time.sleep(1 + t)
    raise RuntimeError("段下载失败 %d" % start)


def main():
    url, out = sys.argv[1], sys.argv[2]
    nseg = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    total = get_size(url)
    print("下载 %s -> %s (%.1f MB, %d 段)" % (url[:90], out, total / 1e6, nseg), flush=True)
    step = total // nseg + 1
    jobs = []
    for i in range(nseg):
        s = i * step
        e = min(total - 1, s + step - 1)
        if s > e:
            break
        jobs.append((s, e, "%s.p%03d" % (out, i)))
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=nseg) as ex:
        futs = [ex.submit(fetch_range, url, s, e, p) for s, e, p in jobs]
        done_bytes = 0
        while True:
            time.sleep(3)
            done = sum(1 for f in futs if f.done())
            cur = sum(os.path.getsize(p) for _, _, p in jobs if os.path.exists(p))
            if cur - done_bytes > 2 * 1024 * 1024 or done != len(jobs):
                print("  %d/%d 段  %.1f/%.1f MB  %.0f KB/s"
                      % (done, len(jobs), cur / 1e6, total / 1e6, cur / (time.time() - t0) / 1024), flush=True)
                done_bytes = cur
            if done == len(jobs):
                break
    with open(out, "wb") as fo:
        for _, _, p in jobs:
            with open(p, "rb") as fi:
                while True:
                    chunk = fi.read(1 << 20)
                    if not chunk:
                        break
                    fo.write(chunk)
            os.remove(p)
    size = os.path.getsize(out)
    print("完成 %s (%d bytes, %.0f 秒)" % (out, size, time.time() - t0), flush=True)
    if size != total:
        print("警告: 大小与 Content-Length 不一致")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
