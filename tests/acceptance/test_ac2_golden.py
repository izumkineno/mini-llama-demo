# test_ac2_golden.py —— AC2 golden 翻译断言 + 确定性复验（Step 3/5 补：executor 未交付）
#
# - manifest 驱动：tests/acceptance/golden/manifest.json（proposed_by 独立于执行人；
#   本轮 proposed_by=architect-golden，执行=主会话接管，M2 合规）。
# - 逐对单进程：en2zh 对前置 `/zh` 行（默认方向=中→英），输入+`/quit`；断言 exit 0
#   且 stdout 归一化（strip + 大小写折叠）后含期望子串。
# - 确定性：zh2en_1 跑两次，stdout 逐字节一致（Step 3 验收）。
# - 归一化口径说明：大小写折叠仅为容忍句首大写（Hello vs hello）；期望子串本身
#   不得改（M2 调参不得改期望）。
import json
import sys
from pathlib import Path

import pytest

from conftest import run_case

REPO = Path(__file__).resolve().parents[2]
GOLDEN = REPO / "tests" / "acceptance" / "golden"


def load_manifest():
    m = json.loads((GOLDEN / "manifest.json").read_text(encoding="utf-8"))
    assert m.get("proposed_by"), "manifest 缺 proposed_by（M2）"
    assert m["proposed_by"] != "step234-executor", "M2 违规：命题人与执行人同一"
    return m


def norm(s: str) -> str:
    return s.strip().lower()


@pytest.mark.parametrize("pair_id", [p["id"] for p in load_manifest()["pairs"]])
def test_ac2_golden_pair(exe_path, gguf_path, pair_id: str):
    m = load_manifest()
    pair = next(p for p in m["pairs"] if p["id"] == pair_id)
    src = (GOLDEN / pair["input_file"]).read_text(encoding="utf-8").strip()
    assert src, f"{pair_id} 输入为空"
    prefix = "/zh\n" if pair["direction"] == "en2zh" else ""
    proc = run_case(exe_path, gguf_path, prefix + src + "\n/quit\n")
    assert proc.returncode == 0
    assert norm(pair["expected_substring"]) in norm(proc.stdout), (
        f"{pair_id} 未含期望子串 {pair['expected_substring']!r}:\n--- stdout ---\n{proc.stdout}")


def test_ac2_deterministic_rerun(exe_path, gguf_path):
    """同输入两次输出逐字节一致（确定性护栏链验证）。"""
    src = (GOLDEN / "zh2en_1.txt").read_text(encoding="utf-8").strip()
    a = run_case(exe_path, gguf_path, src + "\n/quit\n").stdout
    b = run_case(exe_path, gguf_path, src + "\n/quit\n").stdout
    assert a == b, f"两次输出不一致:\n--- run1 ---\n{a}\n--- run2 ---\n{b}"
    assert a.strip(), "输出为空"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
