# fetch_headers.py —— 取同 tag 头文件闭包（Step 1 provenance 工具，可重复执行）
# 用法：python fetch_headers.py [tag] [out_dir]
#   tag     上游 tag，默认 b11669（与 VERSION.pin 同步，换 tag 时同步改）
#   out_dir 输出目录，默认脚本同级 include/
# 逻辑：按 #include "..." 引号闭包迭代补齐（最多 8 轮），已有文件跳过。
import os
import re
import sys

TAG = sys.argv[1] if len(sys.argv) > 1 else "b11669"
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "include")
os.makedirs(OUT, exist_ok=True)

import urllib.request

BASE = f"https://raw.githubusercontent.com/ggml-org/llama.cpp/{TAG}/"
have = set(os.listdir(OUT))


def fetch(name):
    if name in have:
        return True
    for prefix in ["include/", "ggml/include/", "common/"]:
        try:
            req = urllib.request.Request(BASE + prefix + name, headers={"User-Agent": "Mozilla/5.0"})
            d = urllib.request.urlopen(req, timeout=60).read()
            with open(os.path.join(OUT, name), "wb") as f:
                f.write(d)
            have.add(name)
            print("OK", prefix + name, len(d), flush=True)
            return True
        except Exception:
            pass
    print("FAIL", name, flush=True)
    return False


# 种子：llama.h 入口（其余由闭包带出；llama-cpp.h 为 C++ 封装，按需种子）
for seed in ("llama.h", "llama-cpp.h"):
    fetch(seed)

pat = re.compile(r'#include\s+"([A-Za-z0-9_.-]+\.h)"')
for _ in range(8):
    need = set()
    for f in list(have):
        try:
            with open(os.path.join(OUT, f), encoding="utf-8", errors="replace") as fh:
                t = fh.read()
        except Exception:
            continue
        need.update(pat.findall(t))
    missing = [n for n in need if n not in have]
    if not missing:
        print("CLOSURE COMPLETE")
        break
    print("missing:", missing, flush=True)
    for n in missing:
        fetch(n)
