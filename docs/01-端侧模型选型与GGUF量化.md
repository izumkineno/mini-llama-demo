# 01 端侧本地部署模型选型与 GGUF 量化

> 适用：手机 / PC / 边缘盒子等端侧设备，用 llama.cpp 做本地推理。
> 调研时间：2026-10-16；上游：ggml-org/llama.cpp master。

## 1. 端侧部署的约束

- **内存是第一瓶颈**：模型文件要能装进 RAM（CPU 推理）或 RAM+VRAM（GPU offload）。经验公式：
  `显存/内存占用 ≈ 参数量 × 每参数字节数 + 上下文 KV Cache（随 n_ctx 增大而增大）`
- **llama.cpp 的定位**：为本地运行、边缘计算和极致硬件兼容打造的"万能引擎"，轻量、C/C++ 无依赖，
  成熟的 GGUF 量化生态是它在端侧的最大优势（对比 vLLM 面向高并发 A100/H100 生产服务）。
  来源：[AWS 中国区 llama.cpp 部署 Qwen3.6 实测](https://aws.amazon.com/cn/blogs/china/mtp-inference-best-practices-using-llama-cpp/)、
  [llama.cpp 本地部署完全指南](https://segmentfault.com/a/1190000048152423)
- **选型三要素**：参数规模 → 量化档位 → 上下文长度，三者共同决定内存和速度。

## 2. GGUF 与量化档位

- **GGUF**（GPT-Generated Unified Format）是 llama.cpp 专用的单文件二进制格式，
  特点是快速加载/保存、单文件分发、tokenizer 与元数据内嵌。
  来源：[GGUF 格式及模型量化参数介绍](https://zhuanlan.zhihu.com/p/28063135048)、
  [llama.cpp 官方 build 文档](https://github.com/ggml-org/llama.cpp/blob/HEAD/docs/build.md)
- **llama.cpp 支持的量化类型**（见 `include/llama.h` 的 `llama_ftype` 枚举，
  完整原文：[llama.h](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/include/llama.h)）：
  - 1.5~3 bit：IQ1_S/M、IQ2_XXS/XS/S/M、Q2_K(_S)、IQ3_XXS/XS/S/M、Q3_K_S/M/L —— 极限压缩，质量损失明显
  - 4 bit：Q4_0、Q4_1、Q4_K_S/M —— **端侧最常用**
  - 5 bit：Q5_0、Q5_1、Q5_K_S/M
  - 6~8 bit：Q6_K、Q8_0 —— 高质量，文件大
  - 16/32 bit：F16、F32、BF16 —— 微调/母版，不建议端侧直接跑
- **推荐档位**：
  - 默认选 **Q4_K_M**：质量/体积最佳平衡点，Qwen 官方文档的 llama.cpp 示例默认即 Q4_K_M。
    来源：[Qwen 官方 llama.cpp 文档](https://qwen.readthedocs.io/zh-cn/latest/run_locally/llama.cpp.html)
  - 内存极紧（2GB 级手机）：Q3_K_M / IQ3_S / Q2_K，并把 `n_ctx` 降到 2048~4096。
    参考：在 Termux + 2GB 内存设备上跑 Qwen2.5-0.5B-Instruct（约 300MB）。
    来源：[llama.cpp 部署 Qwen2.5-7B 跨平台实践](https://bbs.csdn.net/weixin_33212799/article/details/100158925)
  - 质量优先（PC 16GB+）：Q5_K_M / Q6_K / Q8_0。

## 3. 模型选型推荐（llama.cpp + GGUF）

| 场景 | 参数规模 | 代表模型（找其 GGUF 版） | Q4_K_M 体积量级 | 备注 |
|---|---|---|---|---|
| 手机/低端边缘盒（2~4GB） | 0.5B~1.5B | Qwen3-0.6B/1.7B、Qwen2.5-0.5B/1.5B、MiniCPM | 0.3~1GB | 中文优先 Qwen/MiniCPM |
| 中端手机/笔记本核显（6~8GB） | 3B~4B | Qwen3-4B、Llama-3.2-3B | 2~2.5GB | 通用对话/摘要 |
| PC/边缘工作站（16GB+） | 7B~8B | Qwen3-8B、Qwen2.5-7B、Llama-3.1-8B | 4.5~5.5GB | `Qwen/Qwen3-8B-GGUF:Q8_0` 为官方示例 |
| 高端 PC/单卡工作站（24GB+） | 14B~32B | Qwen3-14B/32B、QwQ-32B、DeepSeek-R1-Distill-Qwen-32B | 8~20GB | Q4 量化 27B 约 16GB，可进单张 24GB 卡 |

- **中文场景优先 Qwen 系**：llama.cpp 对 Qwen2.5/Qwen3 支持完善（含 `--jinja` 聊天模板、YaRN 长上下文、工具调用解析）。
  示例命令（官方文档）：
  `llama-cli -hf Qwen/Qwen3-8B-GGUF:Q8_0 --jinja --color -ngl 99 -c 40960`
- **多模态**：Qwen3-VL 有 GGUF 教程（Jupyter 全流程），但注意 llama.cpp 对视觉编码器量化的支持仍在演进，
  端侧多模态优先验证内存。
  来源：[llamacpp+qwen3vl+gguf 教程](https://github.com/modelscope/modelscope-classroom/blob/main/LLM-tutorial/notebook/llamacpp+qwen3vl+gguf.ipynb)、
  [Jina 多模态向量模型 in llama.cpp](https://jina.ai/zh-CN/news/multimodal-embeddings-in-llama-cpp-and-gguf/)

## 4. 模型获取渠道

1. **Hugging Face GGUF 专区**：如 `Qwen/Qwen3-8B-GGUF`，llama.cpp 可直接 `-hf <repo>:<quant>` 拉取。
2. **ModelScope**（国内镜像/课堂教程多）：搜索模型名 + GGUF。
3. **自行转换+量化**：`convert_hf_to_gguf.py` 转 FP16 GGUF → `llama-quantize` 压到目标档位。
   详见 Qwen 官方文档的"创建和量化 GGUF"指南。
4. **Ollama 模型库**：套壳 llama.cpp，要裸 GGUF 时去 HF/ModelScope 更直接。
   来源：[llama.cpp 与 Ollama 使用指北](https://www.cnblogs.com/luohenyueji/p/20067008)

## 5. 上下文与采样参数速查

- `-c` 上下文长度（默认 4096，0=跟模型）；`-n` 最大生成长度；`--keep` 上下文轮转时保留的前缀 token 数。
- 采样：`--temp 0.6 --top-k 20 --top-p 0.95 --min-p 0`（Qwen3 卡片推荐起点）；重复/死循环时加 `--presence-penalty`（最大 2.0）。
- 长上下文：YaRN 示例 `-c 131072 --rope-scaling yarn --rope-scale 4 --yarn-orig-ctx 32768`。
- 来源：[Qwen 官方 llama.cpp 文档](https://qwen.readthedocs.io/zh-cn/latest/run_locally/llama.cpp.html)

## 6. 下一步

- 构建动态库 → 看 `02-llama.cpp动态库构建指南.md`
- 写代码调用动态库 → 看 `03-llama.cpp动态库C-API调用与集成示例.md`
