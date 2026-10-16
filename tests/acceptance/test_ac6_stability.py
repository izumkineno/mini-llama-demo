# test_ac6_stability.py —— AC6 稳定 10 轮（Step 5 产出，executor 未交付，主会话补）
#
# 连续 10 轮（含空行 + /oops 未知命令）无崩溃；判据合取：
# exit 0 且 非 0xC0000xxx 且 无新增 .dmp 且 stderr 无崩溃指纹；
# carve-out：exit 2 转 DRIFT（run_case 内分流）；pytest-timeout 300s 看门狗。
import glob
import os
import sys
from pathlib import Path

import pytest

from conftest import run_case

REPO = Path(__file__).resolve().parents[2]
GOLDEN = REPO / "tests" / "acceptance" / "golden"

CRASH_FINGERPRINTS = ("Stack trace", "ACCESS_VIOLATION", "0xC000",
                      "abort()", "CUDA error", "ggml_cuda_error")

pytestmark = pytest.mark.timeout(300)  # 需 pytest-timeout；缺失则该 marker 被忽略


def test_ac6_ten_rounds(exe_path, gguf_path):
    zh = (GOLDEN / "zh2en_1.txt").read_text(encoding="utf-8").strip()
    rounds = [zh, "", zh, "/oops", zh, "", zh, zh, "/OOPS", zh]  # 6 译 + 2 空行 + 2 未知命令
    assert len(rounds) == 10
    dmps_before = set(glob.glob(str(REPO / "*.dmp")))
    script = "\n".join(rounds) + "\n/quit\n"
    proc = run_case(exe_path, gguf_path, script)
    assert proc.returncode == 0
    assert proc.returncode >= 0, f"负退出码（信号崩溃）: {proc.returncode}"
    for fp in CRASH_FINGERPRINTS:
        assert fp not in proc.stderr, f"stderr 含崩溃指纹 {fp!r}"
    assert "未知命令" in proc.stderr, "两轮 /oops 应各报一次未知命令"
    dmps_after = set(glob.glob(str(REPO / "*.dmp")))
    assert dmps_after == dmps_before, f"新增崩溃转储: {dmps_after - dmps_before}"
    outs = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    assert len(outs) == 6, f"期望 6 行译文（10 轮−2 空行−2 未知命令），得 {len(outs)}:\n{proc.stdout}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
