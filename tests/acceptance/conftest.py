# conftest.py —— Step 5 harness-side 共享 fixture（计划 §Step 5）
#
# - `dll_preflight`：读 tests/acceptance/RUNTIME_DLLS.txt（清单=隐式依赖闭包 ∪
#   后端白名单，v4 M4），逐项断言与 exe 同目录存在；缺失=前置失败，不拉 exe。
# - `suppress_win_popups`：pytest 父进程经 ctypes 置 SetErrorMode
#   (SEM_FAILCRITICALERRORS)，teardown 恢复；exe 内不得依赖此解决 loader 弹窗。
# - `exe_path` / `gguf_path`：$LAI_EXE/$LAI_GGUF 优先，其次仓库约定位置。
# - `run_case`：子进程执行 + exit∈{0,2} 分流；exit 2 转漂移断言并 skip（DRIFT）。
import ctypes
import glob
import os
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
DEFAULT_GGUF = Path(r"E:\tools\smodeltrans\models\downloads\hy-mt2-1.8b-q4\Hy-MT2-1.8B-Q4_K_M.gguf")
TIMEOUT_S = 600  # 模型加载 + 推理（CUDA 真机）


@pytest.fixture(scope="session")
def exe_path() -> Path:
    if os.environ.get("LAI_EXE"):
        p = Path(os.environ["LAI_EXE"])
        assert p.is_file(), f"$LAI_EXE 不存在: {p}"
        return p
    for cand in ("build/lai-translate.exe", "build/Release/lai-translate.exe",
                 "build/Debug/lai-translate.exe", "lai-translate.exe"):
        p = REPO / cand
        if p.is_file():
            return p
    pytest.skip("未找到 lai-translate.exe（先构建；或设 $LAI_EXE）")


@pytest.fixture(scope="session")
def gguf_path() -> Path:
    if os.environ.get("LAI_GGUF"):
        p = Path(os.environ["LAI_GGUF"])
        assert p.is_file(), f"$LAI_GGUF 不存在: {p}"
        return p
    hits = sorted(glob.glob(str(REPO / "tests/assets/*.gguf")))
    if hits:
        return Path(hits[0])
    if DEFAULT_GGUF.is_file():
        return DEFAULT_GGUF
    pytest.skip("未找到 GGUF（设 $LAI_GGUF 或放入 tests/assets/）")


@pytest.fixture(scope="session", autouse=True)
def suppress_win_popups():
    SEM_FAILCRITICALERRORS = 0x0001
    kernel32 = ctypes.windll.kernel32
    prev = kernel32.SetErrorMode(SEM_FAILCRITICALERRORS)
    yield
    kernel32.SetErrorMode(prev)


@pytest.fixture(scope="session", autouse=True)
def dll_preflight(exe_path: Path):
    """RUNTIME_DLLS.txt 清单 vs exe 同目录：缺失=前置失败（v4 M4）；
    包内存在但非清单 DLL 缺失不判失败（记 warning 由 AC7.log 归档差异处置）。"""
    manifest = Path(__file__).resolve().parent / "RUNTIME_DLLS.txt"
    assert manifest.is_file(), f"缺少 {manifest}（Step 5 产出）"
    wanted = [ln.strip() for ln in manifest.read_text(encoding="utf-8").splitlines()
              if ln.strip() and not ln.strip().startswith("#")]
    assert wanted, "RUNTIME_DLLS.txt 为空"
    missing = [d for d in wanted if not (exe_path.parent / d).is_file()]
    assert not missing, f"dll_preflight 前置失败，exe 同目录缺失: {missing}"
    return wanted


def run_case(exe: Path, gguf: Path, stdin_text: str,
             timeout: int = TIMEOUT_S,
             allowed: tuple = (0,)) -> subprocess.CompletedProcess:
    proc = subprocess.run([str(exe), str(gguf)], input=stdin_text,
                          capture_output=True, text=True, encoding="utf-8",
                          errors="strict", cwd=str(REPO), timeout=timeout)
    expect = tuple(allowed) + ((2,) if 2 not in allowed else ())
    assert proc.returncode in expect, (
        f"非预期退出码: {proc.returncode}\n--- stderr ---\n{proc.stderr}")
    if proc.returncode == 2:
        assert "TEMPLATE_DRIFT:" in proc.stderr, f"exit 2 但无漂移行:\n{proc.stderr}"
        pytest.skip("DRIFT：B2 快照回归告警通道，不计入通过判据")
    return proc
