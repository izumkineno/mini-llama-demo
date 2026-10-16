# 04-ONNX 模型与 ONNX Runtime 端侧部署

> 搜集日期 2026-10-16；来源以 onnxruntime.ai 官方文档与 microsoft/onnxruntime-genai 仓库 README 为主，链接见文末。

## 1. 一句话定位

- **ONNX**：与框架/平台无关的模型中间表示（计算图 + 标准算子集 opset），解决"PyTorch 训练 → 各处部署"的互操作问题。2017 年由微软与 Facebook 联合推出，现由 Linux 基金会 LF AI 托管。
- **ONNX Runtime (ORT)**：微软开源的高性能推理引擎，负责把 ONNX 图切分并调度到不同硬件后端上执行。
- **ONNX Runtime GenAI**：ORT 之上的生成式 AI 扩展，实现 LLM 自回归生成循环（预处理/采样/KV-cache/流式输出/grammar 约束），是"ONNX 路线跑大模型"的对口组件。

## 2. ONNX 模型格式要点

- 文件保存的是**计算图**（节点=算子，边=张量，有向无环图），不是某一框架的权重改名。
- 两个版本概念要分清：`IR version`（文件结构版本）与 `Opset version`（算子语义版本，同名算子在不同 opset 下行为可能不同）。
- 两套内置算子集：`ai.onnx`（神经网络）与 `ai.onnx.ml`（传统机器学习）。
- 格式转换链路（常见方向）：
  - PyTorch → `torch.onnx.export` / torch.dynamo 导出；TensorFlow → tf2onnx；sklearn → skl2onnx。
  - HuggingFace Transformer → Optimum 导出 + Transformers 优化器（算子融合）。
  - SafeTensors → 先问"计算图在哪里"：SafeTensors 只存权重张量，必须配合模型结构代码重新导出 ONNX。
  - **GGUF → ONNX 通常不是直接转换问题**：GGUF 是 llama.cpp 生态的推理格式（含 K-quant 等 llama 专有量化），没有通用转换器；要走 ONNX 路线应从原始权重（SafeTensors/HF）重新导出。

## 3. ONNX Runtime 架构：Execution Provider（EP）

ORT 的核心设计是**可插拔 EP**：同一套 API，运行时按优先级把子图分配给各硬件后端执行（`GetCapability()` 协商），不支持的节点自动回退 CPU。

官方 EP 一览（docs/execution-providers）：

| 类别 | EP |
|---|---|
| CPU | 默认 CPU、Intel oneDNN、XNNPACK、TVM（preview） |
| GPU | NVIDIA CUDA、NVIDIA TensorRT、TensorRT-RTX、DirectML（Windows）、AMD MIGraphX、AMD ROCm（deprecated）、Intel OpenVINO、WebGPU |
| 端侧/移动 | CoreML（Apple）、NNAPI（Android）、高通 QNN、OpenVINO、XNNPACK |
| NPU/其他 | Rockchip RKNPU、华为 CANN、Xilinx Vitis-AI、Arm ACL/ArmNN（均为社区维护 preview）、Azure |

调用时按优先级指定即可（Python 示例，官方文档原样）：

```python
import onnxruntime as rt
sess = rt.InferenceSession("model.onnx",
    providers=['CUDAExecutionProvider', 'CPUExecutionProvider'])
```

多 EP 打进同一个包时，运行环境必须备齐各 EP 的依赖库；也可以用 Plugin EP 机制按需加载。

## 4. 跑 LLM：ONNX Runtime GenAI

仓库 README 给出的支持矩阵（Status: Preview，但已支撑 Foundry Local、Windows ML、VS Code AI Toolkit）：

- **模型结构**：Llama、Qwen（含视觉）、Phi（含视觉）、Mistral、Gemma、DeepSeek、ChatGLM、InternLM2、Whisper 等；Stable Diffusion 在研。
- **API**：Python、C#、C/C++、Java（需源码编译）；Objective-C 在路线图。
- **系统/架构**：Linux、Windows、Mac、Android；x86/x64/arm64；iOS 在路线图。
- **硬件加速**：CPU、CUDA、DirectML、NvTensorRtRtx、OpenVINO、QNN、WebGPU；AMD GPU 在路线图。
- **特性**：Multi-LoRA、连续解码（continuous decoding）、约束解码（constrained decoding，工具调用 grammar）；推测解码（speculative decoding）在路线图。
- **量化**：官方示例即 int4 模型（如 `cpu-int4-rtn-block-32`），另有 AWQ/GPTQ/RTN 等量化导出链路（Olive/Optimum/model-builder 脚本）。

最小 Python 流程（README 原例压缩）：

```python
import onnxruntime_genai as og
model = og.Model('cpu_and_mobile/cpu-int4-rtn-block-32')
tokenizer = og.Tokenizer(model)
params = og.GeneratorParams(model)
params.set_search_options(max_length=2048)
generator = og.Generator(model, params)
generator.append_tokens(tokenizer.encode(prompt))
while not generator.is_done():
    generator.generate_next_token()
```

模型目录 = ONNX 图文件 + `genai_config.json` + tokenizer 文件，C/C++ 侧同样按 Model/Tokenizer/Generator 三件套调用（见官方 C API 文档）。

## 5. CUDA EP 的版本依赖（与 05 篇衔接）

官方 CUDA-ExecutionProvider 页面的硬性要求（推理包）：

| ORT 版本 | 构建用 CUDA | cuDNN | 说明 |
|---|---|---|---|
| 1.27.x–1.30.x（默认 GPU 包） | 13.0 | 9.x | 需运行环境 CUDA ≥ 13.0 |
| 1.27.x–1.30.x（CUDA12 分支包） | 12.8 | 9.x | 需运行环境 CUDA ≥ 12.8 |
| 1.21.x–1.26.x | 12.8 | 9.x | 此前默认 GPU 包 |
| ≤1.18.x（CUDA11 包） | 11.8 | 8.x | 与任何 11.x 兼容 |

- **小版本兼容规则**（NVIDIA 官方机制）：CUDA 11 按"构建版本 ≤ 运行版本"兼容；CUDA 12/13 要求运行环境**不低于**构建版本（用 12.8 构建 → 目标机 CUDA 必须 ≥12.8）。
- **cuDNN 8 与 9 互不兼容**，选包时必须与运行环境（及同机的 PyTorch 版本：2.3 用 8.x，≥2.4 用 9.x）对齐。
- `onnxruntime-gpu` 提供 `preload_dlls()` 预加载 CUDA/cuDNN/MSVC 动态库，可复用 PyTorch 自带的 DLL，缓解"目标机没装 CUDA Toolkit"问题。

## 6. 端侧部署形态小结

- **桌面/工作站（Win/Linux/Mac）**：ORT 官方预编译包 + EP（CUDA/DirectML/CoreML/OpenVINO），C/C++/C#/Python 多语言 API。
- **手机**：ORT Mobile（裁剪算子/体积）+ NNAPI/QNN/CoreML EP。
- **IoT/嵌入式**：树莓派等 ARM 板（官方有教程）+ XNNPACK/ACL/RKNPU EP。
- **浏览器**：ORT Web（WebGPU/WebNN EP）。
- **NPU**：QNN（高通）、OpenVINO（Intel）、DirectML（Windows 统一抽象）是端侧 NPU 的主要入口。

## 7. 来源

- <https://onnxruntime.ai/docs/execution-providers/>（EP 总表与机制）
- <https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html>（CUDA EP 版本要求表）
- <https://github.com/microsoft/onnxruntime-genai>（GenAI README：支持矩阵/安装/示例）
- <https://onnx.ai/>、<https://github.com/onnx/onnx>（ONNX 开放标准）
- <https://vortezwohl.github.io/ai/2026/09/02/深入理解ONNX模型格式与部署.html>（格式转换与 GPU/端侧部署中文详解，SafeTensors/GGUF 转 ONNX 的坑）
