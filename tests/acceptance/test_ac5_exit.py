# test_ac5_exit.py —— AC5 退出语义（Step 4/5 补：executor 未交付）
#
# - /quit → exit 0；/exit → exit 0（双别名，大小写不敏感抽查 /QUIT）。
# - 空 stdin（<NUL 等价）→ 首行 EOF → exit 0。
# - 中段 EOF：一次翻译后直接 EOF（无退出命令）→ exit 0。
# - 适用 exit-2 carve-out（run_case 内分流）。
import sys
from pathlib import Path

import pytest

from conftest import run_case

REPO = Path(__file__).resolve().parents[2]
GOLDEN = REPO / "tests" / "acceptance" / "golden"


def test_ac5_quit(exe_path, gguf_path):
    assert run_case(exe_path, gguf_path, "/quit\n").returncode == 0


def test_ac5_exit_alias(exe_path, gguf_path):
    assert run_case(exe_path, gguf_path, "/QUIT\n").returncode == 0


def test_ac5_immediate_eof(exe_path, gguf_path):
    assert run_case(exe_path, gguf_path, "").returncode == 0


def test_ac5_midloop_eof(exe_path, gguf_path):
    src = (GOLDEN / "zh2en_1.txt").read_text(encoding="utf-8").strip()
    proc = run_case(exe_path, gguf_path, src + "\n")  # 翻译后直接 EOF
    assert proc.returncode == 0
    assert proc.stdout.strip(), "中段 EOF 前应已产出译文"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
