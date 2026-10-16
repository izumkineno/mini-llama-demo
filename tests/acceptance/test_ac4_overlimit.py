# test_ac4_overlimit.py —— AC4 超限口径验收（Step 4 产出）
#
# 对应计划条款：
#   - ralplan AC4：恰 4000 放行、4001 拒绝 + 留循环；口径=归一化后 scalar，
#     ZWJ 按 scalar 计；五组管道输入 ASCII/CJK/emoji/BOM/\r\n。
#   - ralplan §Step 4：getline 去 \n 后再去一尾 \r；行首 BOM 剥离后计数。
#   - ralplan §默认文案（P2 待转正）：超限行
#     "输入超限（>4000字符），已忽略，请重新输入。"→stderr。
#   - ralplan §3 carve-out：exit 2（B2 快照漂移）不计入通过判据，本脚本遇 2 则
#     转漂移断言（stderr 含 TEMPLATE_DRIFT: 行）并 skip 单独报告为 DRIFT。
#
# 运行前置：exe 已构建 + GGUF 可用（exe 启动即加载模型，无模型无法验证留循环）。
#   exe 发现顺序：$LAI_EXE > dist/lai-translate.exe > build/…（与 conftest 同序）
#   gguf 发现顺序：$LAI_GGUF > tests/assets/*.gguf（缺失则全文件 skip）。
import glob
import os
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]  # tests/acceptance/xxx.py → 仓库根
OVERLIMIT_LINE = "输入超限（>4000字符），已忽略，请重新输入。"
TIMEOUT_S = 600  # 模型加载 + 4000 scalar prefill（CUDA 真机）


def find_exe() -> Path:
    if os.environ.get("LAI_EXE"):
        return Path(os.environ["LAI_EXE"])
    for cand in ("dist/lai-translate.exe", "build/lai-translate.exe",
                 "build/Release/lai-translate.exe", "build/Debug/lai-translate.exe",
                 "lai-translate.exe"):
        p = REPO / cand
        if p.is_file():
            return p
    pytest.skip("未找到 lai-translate.exe（先构建；或设 $LAI_EXE）")


def find_gguf() -> Path:
    if os.environ.get("LAI_GGUF"):
        return Path(os.environ["LAI_GGUF"])
    hits = sorted(glob.glob(str(REPO / "tests/assets/*.gguf")))
    hits = [h for h in hits if "corrupt" not in os.path.basename(h)]  # 排除 AC1 损坏探针
    if hits:
        return Path(hits[0])
    default = Path(r"E:\tools\smodeltrans\models\downloads\hy-mt2-1.8b-q4\Hy-MT2-1.8B-Q4_K_M.gguf")
    if default.is_file():
        return default
    pytest.skip("未找到 GGUF（设 $LAI_GGUF 或放入 tests/assets/；exe 启动即需加载模型）")


def run_with_input(exe: Path, gguf: Path, payload: str) -> subprocess.CompletedProcess:
    # 管道喂输入：<payload 行> + /quit，验证"拒绝后留循环"（能继续执行 /quit → exit 0）。
    data = payload + "\n/quit\n"
    return subprocess.run([str(exe), str(gguf)], input=data, capture_output=True,
                          text=True, encoding="utf-8", errors="strict",
                          cwd=str(REPO), timeout=TIMEOUT_S)


def check_no_drift(proc: subprocess.CompletedProcess):
    # exit-2 carve-out：先断 exit∈{0,2}；若 2 则转漂移断言并 skip（DRIFT 单独报告）。
    assert proc.returncode in (0, 2), f"非预期退出码: {proc.returncode}\nstderr:\n{proc.stderr}"
    if proc.returncode == 2:
        assert "TEMPLATE_DRIFT:" in proc.stderr, f"exit 2 但无漂移行:\n{proc.stderr}"
        pytest.skip("DRIFT：B2 快照回归告警通道，不计入 AC4 通过判据")


# 五组 payload 构造器（参数 n=目标 scalar 数；BOM/\r\n 按 Step 4 口径折算后恰为 n）。
GROUPS = {
    "ascii": lambda n: "a" * n,
    "cjk": lambda n: "中" * n,          # 每字 1 scalar
    "emoji": lambda n: "😀" * n,        # U+1F600，每符 1 scalar（4 字节 UTF-8）
    "bom": lambda n: "\ufeff" + "b" * n,  # 行首 BOM 剥离后计 n
    "crlf": lambda n: "c" * n + "\r",   # 尾 \r 剥离后计 n（communicate 再补 \n）
}


@pytest.mark.parametrize("group", sorted(GROUPS))
def test_ac4_reject_4001(group: str):
    """4001 scalar → 拒绝 + 留循环：stderr 含超限行，且后续 /quit 正常 exit 0。"""
    exe, gguf = find_exe(), find_gguf()
    proc = run_with_input(exe, gguf, GROUPS[group](4001))
    check_no_drift(proc)
    assert proc.returncode == 0, f"拒绝后未留循环或退出异常:\n{proc.stderr}"
    assert OVERLIMIT_LINE in proc.stderr, f"4001 未报超限（{group}）:\n{proc.stderr}"


@pytest.mark.parametrize("group", sorted(GROUPS))
def test_ac4_accept_4000(group: str):
    """恰 4000 scalar → 放行：stderr 不含超限行，exit 0。
    注意：放行≠翻译必成功——高 fertility 输入（如 4000 emoji）可能触发 U6 上下文
    窗口留循环（"超出上下文窗口"行），那不属于 AC4 超限文案，不断言 stdout。"""
    exe, gguf = find_exe(), find_gguf()
    proc = run_with_input(exe, gguf, GROUPS[group](4000))
    check_no_drift(proc)
    assert proc.returncode == 0, f"4000 放行路径退出异常:\n{proc.stderr}"
    assert OVERLIMIT_LINE not in proc.stderr, f"4000 误报超限（{group}）:\n{proc.stderr}"


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-v"]))
