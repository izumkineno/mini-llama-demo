# 05-llama.cpp 与 ONNX 综合能力及架构兼容对比

> 搜集日期 2026-10-16。llama.cpp 侧依据本目录 01–03 篇；ONNX 侧依据 04 篇；N 卡架构表来自 NVIDIA 官方 Compute Capability 页。

## 1. 综合能力对比

| 维度 | llama.cpp（+ GGUF） | ONNX（ORT + GenAI） |
|---|---|---|
| 核心定位 | **LLM 专用**推理引擎（C/C++，零依赖口号） | **通用**模型中间表示 + 推理引擎，LLM 靠 GenAI 扩展 |
| 模型覆盖 | Llama/Qwen/Mistral/Gemma/Phi/DeepSeek 等 LLM/Multimodal，社区 GGUF 转换跟进快 | CV/NLP/传统ML/LLM 全覆盖；LLM 需专用导出（Optimum/Olive/model-builder） |
| 量化 | GGUF K-quant（Q4_K_M 等）+ i-quant + KV-cache 量化，档位细、开箱即用 | int8/int4（RTN/AWQ/GPTQ）、FP16/混合精度；需走导出量化流水线 |
| LLM 生成能力 | 采样器/grammar/推测解码/连续批处理（server）成熟 | GenAI：Multi-LoRA、连续解码、约束解码；推测解码仍在路线图 |
| CPU 推理 | 极强（ARM/x86 SIMD 手写优化，树莓派可跑） | CPU EP + XNNPACK/oneDNN，通用但 LLM 单测通常弱于 llama.cpp |
| GPU 后端 | CUDA、Vulkan、Metal、ROCm、SYCL、OpenCL 等（ggml 后端，见 02 篇） | EP 插件：CUDA、TensorRT、DirectML、CoreML、QNN、OpenVINO、WebGPU… |
| NPU/手机 SoC | 基本无（靠 CPU/Vulkan） | **强项**：QNN/NNAPI/CoreML/OpenVINO/DirectML，手机与 AI PC 的正统入口 |
| 集成形态 | 动态库 C API（`llama.h`，见 03 篇）/ server / CLI | 动态库 C/C++/C#/Python/Java，多语言一等公民 |
| 生态 | Ollama、LM Studio、Jan、各类端侧 App | 微软系（Windows ML、Foundry Local、Office/Azure）+ 全行业 CV/NLP 存量 |
| 上手成本 | 拉 GGUF 即跑，一个二进制/库搞定 | 先导出 ONNX（opset/算子支持常踩坑），再选 EP 调优 |

一句话选型：**纯 LLM 端侧、要快要省** → llama.cpp；**多模型形态、手机 NPU/Windows 生态、企业级多语言** → ONNX。

## 2. 固件与架构依赖：N 卡 40 系 vs 50 系

### 2.1 官方算力对照表（developer.nvidia.com/cuda/gpus）

| 架构 | Compute Capability | 代表卡 |
|---|---|---|
| Ada Lovelace | **sm_89** | RTX 40 全系（4050–4090）、L40/L4 |
| Blackwell（消费级） | **sm_120（CC 12.0）** | RTX 50 全系（5050–5090） |
| Blackwell（数据中心） | sm_100 / sm_103 | B200/GB200/GB300 |
| Hopper | sm_90 | H100/H200/GH200 |
| Ampere | sm_86 / sm_80 | RTX 30 系 / A100 |

关键差异：50 系 Blackwell 引入 FP4 Tensor Core、更大显存带宽与新指令，**编译器与驱动都有最低版本门槛**。

### 2.2 CUDA 工具链/驱动门槛

- 40 系（sm_89）：CUDA 11.8+ 即可完整支持，生态最成熟。
- 50 系（sm_120）：需要 **CUDA 12.8+ 工具链编译**（12.6 最高只到 sm_90），驱动需 R570+（Windows 572.16+，Linux 570+）。用旧 CUDA 编出的二进制**不含 sm_120 SASS**，在 50 系上要么 JIT 回退（慢、且缺新特性），要么直接失败。
- 兼容铁律（NVIDIA 小版本兼容机制）：
  - 二进制要求：运行环境 CUDA **≥** 构建用 CUDA（12/13 大版本下严格成立）。
  - PTX 是安全绳：编译时附带 `compute_XX` PTX 可在更新 GPU 上 JIT 运行，但性能与新指令支持打折；生产发布应直接编出目标 sm 的 SASS。

### 2.3 两个框架各自的依赖面

**llama.cpp（ggml CUDA 后端）**

- 依赖：CUDA Toolkit（编译期）+ 驱动（运行期）；**不依赖 cuDNN**（自研 kernel + cuBLAS），依赖面比 ORT 窄。
- 多架构发布：靠 `CMAKE_CUDA_ARCHITECTURES`（`native`/`all-major`/显式列表）一次编出多 sm SASS；官方预编译包覆盖主流架构，新卡（50 系）需确认 release note 中的 CUDA 版本 ≥12.8。
- 兜底路线：**Vulkan 后端**不经过 CUDA，按显卡驱动走，天然跨 40/50 系甚至 A 卡/I 卡，是"一个二进制兼容所有 N 卡"的最省心选项（性能约为 CUDA 后端的 70–90%，按模型与量化而定）。

**ONNX Runtime（CUDA EP）**

- 依赖：CUDA Toolkit + **cuDNN**（8 与 9 互不兼容）+ 驱动，三件套版本必须与预编译包的构建版本对齐（对照表见 04 篇 §5）。
- ORT 1.27+ 默认 GPU 包已是 CUDA 13.0 构建：目标机 CUDA 必须 ≥13.0；若目标机是 12.x 环境，必须换装 CUDA12 分支包（12.8 构建，需环境 ≥12.8）。
- 50 系支持：CUDA EP 本身跟随 CUDA 版本走，12.8+/13.0 包天然支持 sm_120；另有 **TensorRT-RTX EP** 面向 RTX 卡做进一步优化。
- 兜底路线：Windows 上 **DirectML EP**（经 DirectX，不绑定 CUDA 版本，40/50 通吃，适合消费级分发）；Python 包可用 `preload_dlls()` 复用 PyTorch 自带 CUDA/cuDNN DLL，免去目标机装 Toolkit。

### 2.4 40/50 共存发布策略（建议）

1. **开发机**：CUDA 12.8+（或 13.x）+ R570+ 驱动，保证能编出 sm_89 + sm_120 双架构。
2. **llama.cpp 路线**：发布 CUDA 版时显式编入两代 SASS；或主推 Vulkan 版做"通用包"，CUDA 版做"性能包"。
3. **ONNX 路线**：按目标机 CUDA 大版本准备两个 GPU 包（cuda13/cuda12），cuDNN 大版本随包声明；Windows 消费级可再备一个 DirectML 包。
4. **验机清单**：`nvidia-smi`（驱动版本）→ `nvcc --version`（如有）→ 实际在 40 系与 50 系各跑一遍冒烟推理，确认无 JIT 回退告警、无 EP fallback 到 CPU。

## 3. 结论

- 能力上两者互补而非替代：llama.cpp 是 LLM 端侧的"短平快"，ONNX 是跨模型形态、跨芯片（尤其 NPU）的"通用底座"。
- 兼容上真正的变量是 **CUDA 大版本 + cuDNN 大版本 + 驱动**，与框架选择正交：50 系把最低水位抬到了 CUDA 12.8 / R570，无论走哪条路线都要按 §2.4 准备多架构/多版本包，或用 Vulkan/DirectML 这类与 CUDA 解耦的后端做通用分发。

## 4. 来源

- <https://developer.nvidia.com/cuda/gpus>（官方 CC 对照表：40 系 8.9、50 系 12.0）
- <https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html>（ORT CUDA/cuDNN 版本要求）
- <https://onnxruntime.ai/docs/execution-providers/>（EP 总表，含 TensorRT-RTX/DirectML 定位）
- <https://github.com/microsoft/onnxruntime-genai>（GenAI 支持矩阵）
- 本目录 01–04 篇（llama.cpp 构建/调用/ONNX 部署细节）
