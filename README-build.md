# README-build.md — 构建说明（Step 1 工具链 pin + 运行机要求）

## 工具链 pin（E1）

| 项 | pin 值 | 本机实测 |
|---|---|---|
| OS | Windows x64 | ✅ |
| VS | 2022 Community + MSVC 14.44.35207 + Windows SDK（cl/dumpbin/lib.exe） | ✅ `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| CMake | ≥3.26 | ✅ 4.1.1 |
| Ninja | VS 自带（`VC\Auxiliary\Build` 同级 `Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`）或 PATH | ⚠️ 本机 Ninja generator 在 Detecting ABI 阶段 hang，实际用 NMake（见下） |
| C++ 标准 | C++17 | ✅ |
| Python（验收脚本） | 3.x + pytest + pytest-timeout | ✅ 3.12.11（`.pylibs/`，见"验收"） |

## clean checkout 第 0 步：还原第三方二进制（不入仓）

```powershell
powershell -ExecutionPolicy Bypass -File third_party\llama\fetch_assets.ps1
```

脚本按 `third_party\llama\VERSION.pin`（tag b11669 + 双包 URL/sha256）下载校验、
解包 `bin/`、取头文件闭包、由 `llama.def` 生成 `lib\llama.lib`。约 660MB，
已有且 hash 命中则秒跳过。

## 构建（两步，一次通过，AC7）

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

> 生成器偏差（E1 原指定 Ninja）：Ninja generator 在本机 `Detecting CXX compiler
> ABI info` 阶段 hang（两次复现，无 cl 子进程；直接 cl 探针正常），降为 NMake
> Makefiles（同 cl 同 flags，非 E2 否决的 MSBuild）。configure 5.6s 一次通过。

产物：`build\lai-translate.exe` + 同目录 DLL（POST_BUILD 自动拷贝；22 项装载关键清单见 `tests\acceptance\RUNTIME_DLLS.txt`）。

## 运行机要求（C3 矩阵查表 + Release note）

- GPU：NVIDIA 30 系（sm_86）/ 40 系（sm_89），CUDA 全 offload。
- Toolkit minor：12.4（见 `VERSION.pin`）→ 查 C3 矩阵 12.4 行 → **驱动 R550+**。
- 本机：RTX 4070 SUPER + 驱动 R610（610.62）→ ✅ 满足。
- CUDA 运行时随包自带（cudart/cublas/cublasLt 在 `bin/`），运行机无需另装 Toolkit。
- VC++ Redist：exe 自身 /MT 静态链接（零依赖）；但 upstream DLL（llama/ggml/cudart…）
  均为 /MD 构建（dumpbin 实测），运行机仍需安装 **VC++ Redist 2015+**（x64）。要连
  DLL 一起静态化只能从源码以 /MT 重编 upstream，超出预编译路线范围。

## 运行

```bat
build\lai-translate.exe E:\tools\smodeltrans\models\downloads\hy-mt2-1.8b-q4\Hy-MT2-1.8B-Q4_K_M.gguf
```

- 模板：B1（`prompts\hy-mt2-v20261016.txt` + `.sha256`，启动哈希校验）。
- 退出码：0 正常 / 1 通用失败 / 2 模板漂移（B1 路径恒不触发）。
- 升级路径：换 `VERSION.pin` tag → 重下包 → 重新生成 `llama.lib`（`dumpbin /exports` + `lib /def`）→ 重跑 AC1–AC7。

## 验收

```powershell
$env:PYTHONPATH='E:\Code\LAI\.pylibs'
python -m pytest tests\acceptance -q --timeout=600 -p no:cacheprovider
```

- `.pylibs/` = pytest 9.1.1 + pytest-timeout 及依赖（沙箱禁写用户 site，
  由 wheel 直解而来；删了重跑需按 `.omd/plans/open-questions.md` 执行补记④重建）。
- 期望：26 passed（AC1–AC6）；AC7 见 `acceptance\AC7.log`（clean 构建 + offload）。
- exe 可在任意目录启动：模板目录按 CWD/prompts → exe 同级 prompts/ →
  exe 上级 prompts/ 顺序解析（`build/` 内启动自动找到仓库 `prompts/`）。

## 目录结构

```
├── src/                C++ 源码（engine/prompt_spec/loop/i18n_count/main）
├── tests/acceptance/   pytest 验收（conftest + test_ac1–ac6 + golden/ + RUNTIME_DLLS.txt）
├── prompts/            B1 模板 + .sha256 + META 取证 + sampling.pin.json + 上游 README 存档
├── third_party/llama/  预编译包：bin/（33 DLL）include/ lib/llama.lib
│                       pkg.zip/pkg-main.zip（原包留存，可复现）VERSION.pin
├── build/              构建产物（exe + 33 DLL，可删后重建）
├── acceptance/         证据归档（pytest 总日志/AC7.log/offload 日志/B1 探针）
├── docs/               01–06 调研文档
├── .omd/               访谈 spec / 冻结计划 v4 / 问题决议
├── .pylibs/            pytest 运行时（wheel 直解，见"验收"）
├── CMakeLists.txt      NMake 构建，C++17，隐式链接 llama.lib
└── README-build.md     本文件
```
