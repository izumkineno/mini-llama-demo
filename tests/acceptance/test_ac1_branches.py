# test_ac1_branches.py —— AC1 引擎加载三分支（Step 5 补：executor 未交付）
#
# - 正常：进提示符，stderr 含 `llama_version` 行，exit 0。
# - 缺失：argv 给不存在路径 → exit≠0 且 stderr 含该路径。
# - 损坏：tests/assets/corrupt.gguf（1KB 非法魔数，仓库内生成，不复制大文件）
#   → exit≠0 且 stderr 含该路径。
import sys
from pathlib import Path

import pytest

from conftest import run_case

REPO = Path(__file__).resolve().parents[2]


def test_ac1_normal_prompt(exe_path, gguf_path):
    proc = run_case(exe_path, gguf_path, "/quit\n")
    assert proc.returncode == 0
    assert "llama_version" in proc.stderr, f"stderr 缺 llama_version 行:\n{proc.stderr}"


def test_ac1_missing_gguf(exe_path):
    bogus = REPO / "tests" / "assets" / "does-not-exist-xyz.gguf"
    proc = run_case(exe_path, bogus, "/quit\n", timeout=120, allowed=(1,))
    assert proc.returncode != 0, "缺失 GGUF 应 exit≠0"
    assert str(bogus) in proc.stderr, f"stderr 应含缺失路径:\n{proc.stderr}"


def test_ac1_corrupt_gguf(exe_path):
    assets = REPO / "tests" / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    # 后缀 .gguf.bad：loader 只认魔数不认后缀；避免被他组 *.gguf 自发现误拾。
    bad = assets / "corrupt.gguf.bad"
    if not bad.is_file():  # 非法 GGUF 魔数，loader 应快速拒绝
        bad.write_bytes(b"BAD!" + b"\x00" * 1020)
    proc = run_case(exe_path, bad, "/quit\n", timeout=120, allowed=(1,))
    assert proc.returncode != 0, "损坏 GGUF 应 exit≠0"
    assert str(bad) in proc.stderr, f"stderr 应含损坏路径:\n{proc.stderr}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
