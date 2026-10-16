# 02 llama.cpp 动态库构建指南

> 目标产物：`llama` 共享库（Windows `llama.dll` / Linux `libllama.so` / macOS `libllama.dylib`），
> C 接口头文件 `include/llama.h`，以及 `llama-cli` / `llama-server` 等示例工具。
> 上游原文：[docs/build.md](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/docs/build.md)、
> [README](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/README.md)。
> 调研时间：2026-10-16，上游 master。

## 1. 一句话结论

- llama.cpp 的**主产物就是 `llama` 库**（原文："The main product of this project is the `llama` library"），
  其 C 风格接口在 `include/llama.h`。
- **默认即构建共享库**：只有显式加 `-DBUILD_SHARED_LIBS=OFF` 才是静态构建。
  ```bash
  git clone https://github.com/ggml-org/llama.cpp
  cd llama.cpp
  cmake -B build
  cmake --build build --config Release -j 8
  # 产物：build/bin/*（可执行文件），build/src/*.so / *.dll（动态库，依平台而定）
  cmake --install build --prefix ./install --config Release
  # 安装后：install/bin、install/lib、install/include
  ```
- 静态构建（不需要动态库时）：
  ```bash
  cmake -B build -DBUILD_SHARED_LIBS=OFF
  cmake --build build --config Release
  ```

## 2. Windows 构建（x86/x64/arm64）

前置：Visual Studio 2022（工作负荷选"使用 C++ 的桌面开发"，组件勾选 C++ CMake Tools、Git、Clang、MSBuild 对 LLVM 支持），
务必用 **VS2022 开发者命令行/PowerShell** 执行。

```powershell
git clone https://github.com/ggml-org/llama.cpp
cd llama.cpp
cmake -B build
cmake --build build --config Release -j 8
```

- WoA（arm64）推荐预设（自动处理 OpenMP）：
  ```bash
  cmake --preset arm64-windows-llvm-release -D GGML_OPENMP_FETCH=ON
  cmake --build build-arm64-windows-llvm-release
  ```
  `GGML_OPENMP_FETCH=ON` 会在配置时下载 LLVM OpenMP 运行时（需 Clang + 7-Zip + 联网），
  产物 `libomp.dll` 会自动拷贝到运行时目录并随安装发布；不用 OpenMP 则 `-D GGML_OPENMP=OFF`。
- x64 + ninja + clang 预设：
  ```bash
  cmake --preset x64-windows-llvm-release
  cmake --build build-x64-windows-llvm-release
  ```
- HTTPS/TLS 可选：装 OpenSSL 开发库则 server 等工具带 SSL 支持，不装也能编过、运行时无 SSL。
- 产物：`build/bin/Release/*.dll`（`llama.dll`、`ggml*.dll` 等）+ `*.exe`；调用方发布时 **DLL 要与 EXE 同目录**。

## 3. Linux / macOS 构建

```bash
# Linux 通用 CPU 版
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8

# macOS：Metal 默认开启（GPU 加速），关闭用 -DGGML_METAL=OFF
cmake -B build -DGGML_METAL=OFF   # 仅在需要纯 CPU 时加
```

- BLAS 加速（prompt 处理 batch>32 时有效，不影响逐 token 生成速度）：
  ```bash
  # OpenBLAS 示例
  cmake -B build -DGGML_BLAS=ON -DGGML_BLAS_VENDOR=OpenBLAS
  ```
- Debug 构建：
  - 单配置生成器（Unix Makefiles）：`cmake -B build -DCMAKE_BUILD_TYPE=Debug`
  - 多配置生成器（VS/Xcode）：`cmake --build build --config Debug`

## 4. GPU 后端（按需二选一或多选）

llama.cpp 可同时编多个后端，运行时用 `--device` / `--list-devices` 选择；也可用 `GGML_BACKEND_DL=ON`
把后端编成运行时动态加载的插件库，同一个二进制在不同 GPU 机器上跑。

| 后端 | 目标 | 关键 CMake 开关 |
|---|---|---|
| CUDA | NVIDIA GPU | `-DGGML_CUDA=ON`（全卡兼容再加 `-DGGML_NATIVE=OFF`；指定架构 `-DCMAKE_CUDA_ARCHITECTURES="86;89"`） |
| Vulkan | 跨厂商 GPU | `-DGGML_VULKAN=1`（需 Vulkan SDK；macOS 再加 `-DGGML_METAL=OFF`） |
| HIP | AMD GPU | `-DGGML_HIP=ON [-DGPU_TARGETS=gfx1100]` |
| Metal | Apple Silicon | 默认 ON，`-DGGML_METAL=OFF` 关闭 |
| SYCL | Intel GPU | 见 `docs/backend/SYCL.md` |
| OpenCL | Adreno 等 | `-DGGML_OPENCL=ON`（Android/Win-ARM 流程见官方 build.md） |
| CANN / MUSA / OpenVINO / Hexagon | Ascend / 摩尔线程 / Intel / 骁龙 | `-DGGML_CANN=on` / `-DGGML_MUSA=ON` / 见各 `docs/backend/*.md` |
| Arm KleidiAI | Arm CPU 微内核加速 | `-DGGML_CPU_KLEIDIAI=ON`（仅 AArch64/arm64，Android 见下） |

运行时关闭 GPU：`-ngl 0` 大多有效，彻底禁用用 `--device none`。

## 5. Android 构建（端侧重点）

### 5.1 Termux 直接编（无需 root，最快验证）

```bash
apt update && apt upgrade -y
apt install git cmake libandroid-spawn
git clone https://github.com/ggml-org/llama.cpp && cd llama.cpp
cmake -B build && cmake --build build --config Release -j 4
curl -L {model-url} -o ~/{model}.gguf
./build/bin/llama-cli -m ~/{model}.gguf -c 4096 -p "{prompt}"
```

注意：`context-size` 先设 4096，设太大内存爆了会杀终端。
来源：[docs/android.md](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/docs/android.md)

### 5.2 NDK 交叉编译（发版用，产物为 .so）

```bash
export ANDROID_NDK=/path/to/ndk   # 先装好 Android SDK/NDK
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DGGML_NATIVE=OFF \
  -DGGML_OPENMP=OFF \
  -DGGML_LLAMAFILE=OFF \
  -DLLAMA_OPENSSL=OFF
cmake --build build-android --config Release -j
cmake --install build-android --prefix ./install-android --config Release
```

- `GGML_NATIVE=OFF` 交叉编译必填；`GGML_CPU_KLEIDIAI=ON` 可选开 Arm 加速（仅 `arm64-v8a`）。
- 推送到设备运行：
  ```bash
  adb shell "mkdir -p /data/local/tmp/llama.cpp"
  adb push ./install-android /data/local/tmp/llama.cpp/
  adb push {model}.gguf /data/local/tmp/llama.cpp/
  adb shell
  cd /data/local/tmp/llama.cpp
  LD_LIBRARY_PATH=lib ./bin/llama-cli -m {model}.gguf -c 4096 -p "{prompt}"
  ```
  Android 不会自动找 `lib` 目录，必须设 `LD_LIBRARY_PATH`（新 API Level 才支持 RPATH）。
- App 集成：把 `examples/llama.android` 目录导入 Android Studio 做 Gradle 构建，
  或参考该目录的 JNI 绑定自己封装。生产级示例可看 Arm AI Chat（Play 商店）。

## 6. 产物清单与发布注意事项

- `cmake --install` 后标准布局：`bin/`（llama-cli、llama-server、llama-quantize…）、
  `lib/`（`llama` + `ggml` 系列动态库/后端插件）、`include/`（`llama.h` 等头文件）。
- C++ ABI/编译器一致性：调用方与动态库尽量同编译器、同 CRT（Windows 同 /MD、同 VS 版本）编译，
  否则跨 DLL 传 STL 对象会炸——**只经 C 接口（llama.h）交互，不跨边界传 C++ 对象**。
- Windows 发布带上全部依赖 DLL（含 `libomp.dll`、`ggml-*.dll`、后端插件 DLL）。
- 版本查询：`llama_version()`； CGC 兼容性以 `llama.h` 为准，不要依赖内部 `ggml-*` 头。
