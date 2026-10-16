# test_ac3_switch.py —— AC3 方向切换即时生效（Step 4 补：executor 未交付）
#
# scripted stdin：/en→译→/zh→译→/en→译；方向正确性由 golden 子串断言：
# 第 1/3 个译文含英文期望串，第 2 个含中文期望串，连续 3 次无串向。
# stdout 按翻译行顺序出现（每轮译文一行 + 尾换行），依次切分断言。
import sys
from pathlib import Path

import pytest

from conftest import run_case

REPO = Path(__file__).resolve().parents[2]
GOLDEN = REPO / "tests" / "acceptance" / "golden"


def g(name: str) -> str:
    return (GOLDEN / name).read_text(encoding="utf-8").strip()


def test_ac3_switch_roundtrip(exe_path, gguf_path):
    zh = g("zh2en_1.txt")  # 你好，世界。 → 望 Hello
    en = g("en2zh_1.txt")  # Hello, world. → 望 你好
    script = f"/en\n{zh}\n/zh\n{en}\n/en\n{zh}\n/quit\n"
    proc = run_case(exe_path, gguf_path, script)
    assert proc.returncode == 0
    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    assert len(lines) == 3, f"期望 3 行译文，得 {len(lines)} 行:\n{proc.stdout}"
    assert "hello" in lines[0].lower(), f"第 1 轮串向:\n{proc.stdout}"
    assert "你好" in lines[1], f"第 2 轮串向:\n{proc.stdout}"
    assert "hello" in lines[2].lower(), f"第 3 轮串向:\n{proc.stdout}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
