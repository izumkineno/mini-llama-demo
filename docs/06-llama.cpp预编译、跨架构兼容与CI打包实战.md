# 06-llama.cpp 预编译、跨架构兼容与 CI 打包实战

> 定位：回答"要不要自己编译、编出的包能不能跨代跑、怎么用 CI 打包"三个问题，
> 并收录可直接抄的官方 workflow 与例子仓库。配套 02 篇（构建指南）使用。

## 1. 预编译包 vs 自己编译

### 1.1 官方预编译包覆盖什么

llama.cpp 每个 GitHub Release 都提供多平台包（如 `llama-xxx-bin-win-x64.zip`），
内含可执行文件**和**动态库（`llama.dll/.so/.dylib` + 各后端库）。
Windows x64 还按后端拆成 CPU / CUDA / Vulkan 等多个包，开箱即用。

关键设计：**后端是独立动态库**（`ggml-cuda.dll`、`ggml-vulkan.dll`…），
主库运行时按需加载。所以 GPU 架构差异主要靠"多带几个后端库"解决，
不需要为每张卡编一个主库（对应构建开关 `GGML_BACKEND_DL=ON`，见 §3.2）。

### 1.2 什么时候必须自己编译

| 场景 | 结论 | 原因 |
|---|---|---|
| Win/Linux x64，主流 N 卡 | 直接下载 | 官方 CPU/CUDA/Vulkan 包覆盖 |
| 50 系新卡 | 先看包的 CUDA 版本 | 需 Release 注明 CUDA ≥ 12.8，否则自编（见 05 篇 §2.2） |
| Android arm64 / iOS / 嵌入式 | 自己编译 | 官方包不覆盖，需 NDK/Xcode 交叉编译 |
| 老 CPU（无 AVX2）/ 特殊指令集 | 自己编译 | 官方 x64 包按基线编译，本机编（`-march=native`）更快 |
| 要静态库 / 裁剪体积 / 定制开关 | 自己编译 | 如 `BUILD_SHARED_LIBS`、`GGML_CUDA_F16` 等选项 |

补充：

- ggml 对 x86/arm 的 SIMD kernel 有一定**运行时 CPU 特性检测**，官方包在老机器上能跑，
  只是比 `-march=native` 本机编译慢一截——追求性能才需要针对目标 CPU 编译。
- **最省心的"一个包通吃"方案**：主库 + Vulkan 后端库，不经过 CUDA，
  40/50 系乃至 A 卡都能跑，适合对外分发；CUDA 包作为性能选项另供。

## 2. 跨架构兼容：编出的包能不能跨代跑

### 2.1 结论

在 40 系机上 `native` 编出的 CUDA 程序，**默认不能**在 30 系上跑 GPU，
会报 `no kernel image is available for execution on the device`。
反过来（30 系编的包到 40 系）可以通过 PTX JIT 运行。

### 2.2 原理：CUDA 二进制兼容是单向的

- `native` 编译只生成 sm_89 的 SASS + `compute_89` 的 PTX；
- PTX 只能 **JIT 向上兼容**（低算力 PTX 在新卡上跑），**不能向下**——
  `compute_89` 的 PTX 在 sm_86 的 30 系上无法编译，因为它可能用了 89 独有指令；
- CMake 里 `89` 这个写法会同时生成 SASS（`sm_89`）和 PTX（`compute_89`），
  `-real`/`-virtual` 后缀可分别只生成其一。

| 构建来源 | 在 30 系上 | 在 40 系上 |
|---|---|---|
| 40 系 native（sm_89） | ❌ 失败 | ✅ 原生 |
| 30 系 native（sm_86） | ✅ 原生 | ⚠️ PTX JIT 可跑，稍慢 |
| 多架构 `86;89` | ✅ 原生 | ✅ 原生 |

### 2.3 解法

1. **编译时一次编入多代架构**（推荐）：`-DCMAKE_CUDA_ARCHITECTURES="86;89"`，
   或 `all-major` 全覆盖。体积稍大，但一个包通吃 30/40 系。
2. **用 Vulkan 后端分发**：不经过 CUDA，按显卡驱动走，天然跨代，还能顺带支持 A 卡。
3. **注意 CPU 部分是另一回事**：`-march=native` 针对的是编译机 CPU（如编入 AVX512），
   与显卡代际无关。跨机器分发时 CPU 也要用基线架构编译（官方 CI 用 `GGML_NATIVE=OFF`），
   否则会非法指令崩溃。
4. **运行机驱动要达标**：如用 CUDA 12.x 构建的包，运行机驱动需 R570+；
   用 CUDA 13 构建则要更新的驱动（见 05 篇 §2.2 版本对照表）。

一句话：**GPU 跨代靠"多架构编译"，与在哪代卡的机子上编译无关**——关键看编译时写了哪些 sm。

## 3. GitHub Actions 打包实战

### 3.1 官方 CI 现状（已核验，2026-10）

llama.cpp 官方把 CI 按目标拆成了 ~40 个 workflow，不再是单个 `build.yml`。
与打包最相关的（`ggml-org/llama.cpp` → `.github/workflows/`）：

| workflow | 用途 |
|---|---|
| `release.yml`（66KB，主力） | 发版矩阵构建：全平台 × 全后端的包都从这里出 |
| `make-release.yml` | 发版编排：手动触发，默认 dry-run，先过 API/ABI 兼容检查再打 tag、建 Release、给 Docker 镜像重打 tag（cuda/cuda13/vulkan/rocm/intel/musa/openvino × full/light/server） |
| `release-publish.yml` | Release 产物发布 |
| `build-cuda-windows.yml` | Windows CUDA 构建（见 §3.2 详解） |
| `build-cuda-ubuntu.yml` | Linux CUDA 构建 |
| `build-vulkan.yml` / `build-android.yml` / `build-apple.yml` / `build-cross.yml` / `build-cpu.yml` | 各后端/交叉编译构建 |
| `docker.yml` | Docker 镜像矩阵 |
| `build-cache.yml` + `ggml-org/ccache-action` | ccache 预热：先手跑构建把缓存填满，再跑重的 release 构建 |

要点：CUDA 这类重构建在官方是**手动触发**（注释原话：对 CI 太重），且与 release 共用 ccache key，
手跑一次即为发版预热缓存——这个模式自己搭 CI 时可直接照搬。

### 3.2 官方 Windows CUDA 构建详解（`build-cuda-windows.yml` 实录）

- **矩阵**：CUDA `12.4` / `13.4` × `x64` / `arm64`（arm64 走 `cmake/arm64-windows-msvc-cuda.cmake` 工具链）。
- **装 Toolkit**：用仓库自带 action `./.github/actions/windows-setup-cuda`
  （自建 CI 没有这个，改用社区 `Jimver/cuda-toolkit`，见 §3.4）。
- **加速**：`ggml-org/ccache-action`（自建 CI 可用上游 `hendrikmuhs/ccache-action` 代替）。
- **核心构建命令**（Ninja Multi-Config，只编 `ggml-cuda` 这一个 target）：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build -G "Ninja Multi-Config" ^
  -DGGML_BACKEND_DL=ON ^
  -DGGML_NATIVE=OFF ^
  -DGGML_CPU=OFF ^
  -DGGML_CUDA=ON ^
  -DLLAMA_BUILD_BORINGSSL=ON
cmake --build build --config Release -j %NINJA_JOBS% --target ggml-cuda
```

开关含义：`GGML_BACKEND_DL=ON` = 后端编成可动态加载的独立库（§1.1 的基础）；
`GGML_NATIVE=OFF` = 关本机指令集优化，保证包可跨机器分发；
`GGML_CPU=OFF` = 这个 job 只编 CUDA 后端（CPU 部分由别的 job 出）。

- 同文件还有一个 **Windows HIP/ROCm job**（TheRock 发行版，多 `gfxXXXX` target），
  A 卡 Windows 包可参考。

### 3.3 第三方打包范例：`llama-cpp-python`（已核验）

`abetlen/llama-cpp-python` 的 `.github/workflows/build-and-release.yaml` 是"一次 CI 打全平台包"的典范：

- 工具：`pypa/cibuildwheel`，矩阵 `ubuntu-22.04 / windows-2022 / macos-14 / macos-15`，
  另有 `ubuntu-24.04-arm`（arm64）、QEMU  riscv64、Pyodide wasm 四个扩展 job；
- 可移植基线：全平台统一 `CMAKE_ARGS="-DGGML_NATIVE=off"`，注释明说"不要继承 runner 的 native 旗标"；
- 发版：只有打 tag 才进 `release` job，用 `softprops/action-gh-release` 把各 job 的 wheel 汇总发出去。
  日常 push 只构建不发版——推荐照抄这个节奏。

### 3.4 自建 CI 打包：能力矩阵与三关键点

| 目标 | 可行性 | 说明 |
|---|---|---|
| Win/Linux/macOS x64 CPU 版 | ✅ 免费 runner 直接打 | 最简单 |
| macOS arm64 | ✅ 有官方 arm64 runner | M 系原生包 |
| Linux arm64 | ✅ 有官方 arm64 runner | 树莓派/服务器 ARM 包 |
| Android arm64 | ✅ 装 NDK 交叉编译 | runner 上装 NDK 即可，无需真机 |
| CUDA 版（多架构） | ✅ 能编译、不能测 | 装 CUDA Toolkit + 显式指定架构列表；runner 无 GPU，只能编不能跑 |
| Vulkan 版 | ✅ | runner 上装 Vulkan SDK 后编译 |
| iOS | ⚠️ 需 macOS runner + 证书 | 编译可行，分发要签名 |

三关键点：

1. **CUDA 架构必须手写，不能 `native`**：CI runner 没有 GPU，`native` 检测不到架构。
   改显式列表，一次覆盖 30/40/50 系：`-DCMAKE_CUDA_ARCHITECTURES="86;89;120"`
   （Toolkit 必须 ≥ 12.8 才认 120）。
2. **用现成 Action 装工具链**：CUDA 用 `Jimver/cuda-toolkit`，加速用 `hendrikmuhs/ccache-action`
   （CUDA 全量构建 20–60 分钟，ccache 必备），产物用 `actions/upload-artifact` 收集、
   多 job 跑完后统一发 Release。
3. **GPU 包只能"编完发"，冒烟靠自己**：CI 上跑不了 CUDA，只能跑 CPU 单测。
   建议发版前在本地 30 系 + 40/50 系真机各跑一遍推理冒烟。

### 3.5 可直接用的 workflow（含注释，按需删 job）

```yaml
# .github/workflows/llama-package.yml
# 手动触发 + 打 tag 自动发版；日常 push 不跑（CUDA 构建太重）
name: llama-package
on:
  workflow_dispatch:
  push:
    tags: ['pkg-v*']

env:
  LLAMA_REF: master  # 或 pin 到某个 release tag，如 b1234

jobs:
  # ---- Job 1: Windows CUDA（30/40/50 系通吃）----
  win-cuda:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          repository: ggml-org/llama.cpp
          ref: ${{ env.LLAMA_REF }}
          submodules: recursive
      - uses: hendrikmuhs/ccache-action@v2
        with: { key: win-cuda }
      - uses: Jimver/cuda-toolkit@v0.2.19
        with: { cuda: '12.8.0', method: 'network' }
      - run: choco install ninja -y
      - shell: cmd
        run: |
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
          cmake -S . -B build -G "Ninja Multi-Config" ^
            -DGGML_BACKEND_DL=ON -DGGML_NATIVE=OFF ^
            -DGGML_CUDA=ON -DBUILD_SHARED_LIBS=ON ^
            -DCMAKE_CUDA_ARCHITECTURES="86;89;120"
          cmake --build build --config Release -j
      - uses: actions/upload-artifact@v4
        with: { name: win64-cuda, path: build/bin/Release/* }

  # ---- Job 2: Windows Vulkan（跨代/A卡通吃）----
  win-vulkan:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          repository: ggml-org/llama.cpp
          ref: ${{ env.LLAMA_REF }}
          submodules: recursive
      - uses: hendrikmuhs/ccache-action@v2
        with: { key: win-vulkan }
      - uses: humbletim/install-vulkan-sdk@v1.2  # 社区 Vulkan SDK action
        with: { version: latest, cache: true }
      - run: choco install ninja -y
      - shell: cmd
        run: |
          call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
          cmake -S . -B build -G "Ninja Multi-Config" ^
            -DGGML_BACKEND_DL=ON -DGGML_NATIVE=OFF ^
            -DGGML_VULKAN=ON -DBUILD_SHARED_LIBS=ON
          cmake --build build --config Release -j
      - uses: actions/upload-artifact@v4
        with: { name: win64-vulkan, path: build/bin/Release/* }

  # ---- Job 3: Linux CPU（基线 x64）----
  linux-cpu:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
        with:
          repository: ggml-org/llama.cpp
          ref: ${{ env.LLAMA_REF }}
          submodules: recursive
      - uses: hendrikmuhs/ccache-action@v2
        with: { key: linux-cpu }
      - run: |
          cmake -S . -B build -G Ninja \
            -DGGML_NATIVE=OFF -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_BUILD_TYPE=Release
          cmake --build build -j
      - uses: actions/upload-artifact@v4
        with: { name: linux-cpu, path: build/bin/* }

  # ---- Job 4: 汇总发版（仅 tag 触发）----
  release:
    needs: [win-cuda, win-vulkan, linux-cpu]
    if: startsWith(github.ref, 'refs/tags/')
    runs-on: ubuntu-latest
    permissions: { contents: write }
    steps:
      - uses: actions/download-artifact@v4
        with: { merge-multiple: true, path: dist }
      - uses: softprops/action-gh-release@v2
        with: { files: dist/* }
```

注意：

- `LLAMA_REF` 建议 pin 到已验证的 release tag，不要跟 `master` 裸奔；
- 私有仓库注意 runner 分钟数（CUDA 构建吃时长），公有仓库免费；
- macOS 对外分发还需接签名/公证步骤，本骨架未含；
- 发版前务必在 30 系 + 40/50 系真机各跑一遍推理冒烟（CI 跑不了 GPU）。

## 4. 现成例子仓库（已核验）

### 4.1 官方 `examples/`（`ggml-org/llama.cpp/examples/`，API 实录核验）

| 目录 | 内容 | 先看这个如果… |
|---|---|---|
| `simple/` | 最小 C API 调用：backend_init → load → tokenize → decode → sample | 第一次调 `llama.h`（03 篇即以此为骨） |
| `simple-chat/` | 多轮对话封装 | 要做聊天循环 |
| `simple-cmake-pkg/` | 把编好的 llama.cpp 当 CMake 包引入（`find_package` 式） | 自己的 CMake 工程要链接动态库 |
| `llama.android/` | Android 完整 App 例子（JNI + NDK 构建） | 做安卓端 |
| `llama.swiftui/` | iOS/macOS SwiftUI 例子 | 做苹果端 |
| `batched/`、`parallel/` | 批处理 / 并行解码 | 要吞吐 |
| `embedding/`、`retrieval/` | embedding 与检索 | 做 RAG 的向量侧 |
| `speculative/`、`lookahead/`、`lookup/` | 投机采样等加速解码 | 要压延迟 |
| `model-conversion/` | 模型转换脚本 | 要自己转 GGUF |
| `server`（已移出 examples，见 `tools/server/`） | OpenAI 兼容 HTTP 服务 | 要进程外集成（03 篇路线 C） |

### 4.2 发版产物即例子：官方 Release 包

官方每个 Release 的 zip 本身就是"打包好的动态库长什么样"的标准答案：
主库 + 后端库 + 可执行文件 + 模型转换工具。自己 CI 打包时对照它的文件清单查漏即可。

## 5. 社区项目（线索，未逐一抓取核验）

以下为知名度较高的 llama.cpp 打包/集成项目，本轮只确认了仓库存在级别的认知，
**用前请先打开其 Actions / Releases 页核对现状**：

| 仓库 | 看点 |
|---|---|
| `ollama/ollama` | Go 封装 + 自维护 llama.cpp 分支，大规模多平台 CI 打包的标杆 |
| `abetlen/llama-cpp-python` | Python wheel 打包（§3.3 已核验其 workflow） |
| `withcatai/node-llama-cpp` | Node.js 绑定，npm 预编译二进制分发 |
| `a-ghorbani/pocketpal-ai` | React Native 安卓/iOS App 集成例子 |
| `Mobile-Artificial-Intelligence/maid` | Flutter App 集成例子 |

## 6. 推荐起步路径

1. 先下载官方 Release 包验证模型与性能（§1.2）；
2. 不满足（新卡/移动端/嵌入式/极致性能）再自编，先本地编通，再搬上 CI（§3.5）；
3. 集成时先抄 `examples/simple` 跑通最小链路，再按 03 篇三路线选型；
4. 对外分发用"主库 + Vulkan 后端"保底，CUDA 包作为性能选项另供（§1.2、§2.3）。

## 来源

- [llama.cpp workflows 目录（API 实录）](https://github.com/ggml-org/llama.cpp/tree/master/.github/workflows)
- [build-cuda-windows.yml 原文](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/.github/workflows/build-cuda-windows.yml)
- [make-release.yml 原文](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/.github/workflows/make-release.yml)
- [llama-cpp-python build-and-release.yaml 原文](https://raw.githubusercontent.com/abetlen/llama-cpp-python/main/.github/workflows/build-and-release.yaml)
- [llama.cpp examples/ 目录（API 实录）](https://github.com/ggml-org/llama.cpp/tree/master/examples)
