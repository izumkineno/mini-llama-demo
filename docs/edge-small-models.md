# 端侧小模型调研：应用场景与 8GB 可跑模型选型（llama.cpp 优先）

> 调研时间：2026-10-17 ｜ 结论口径：能在 **8GB 显存（独显）或 8GB 内存/统一内存**的消费级设备上本地运行的开源小模型，优先列出 **llama.cpp（GGUF）** 支持的型号。

---

## 1. 端侧小模型是什么、为什么用

端侧小模型（SLM，Small Language Model，一般指参数 < 10B、可在消费级硬件运行的模型）与云端大模型的差异不在"智商上限"，而在**部署位置**：直接跑在手机、AI PC、Mac、单卡工作站甚至嵌入式设备上。

核心收益（Gartner 预测到 2027 年企业对任务型 SLM 的使用将达到通用 LLM 的 3 倍）：

- **隐私**：数据不出域，适合医疗、办公文档、代码等敏感场景。
- **延迟**：本地推理可做到亚 100ms 级响应，无网络往返。
- **成本**：百万次对话量级下，本地 SLM 成本约为云端 LLM 的 1%~5%。
- **可用性**：弱网/离线可用，边际调用成本几乎为零。
- **代价**：硬件异构、量化质量损失、KV Cache 与并发管理、模型升级与安全补丁都回到自己手里。"能下载"不等于"能稳定生产运行"。

---

## 2. 端侧小模型主要用于哪些方面

| # | 应用方向 | 做什么 | 为什么适合端侧 |
|---|---------|--------|---------------|
| 1 | 本地对话助手 / AI 助手 | 日常问答、改写、摘要、头脑风暴 | 通用高频调用，本地跑省钱、低延迟、隐私好 |
| 2 | 本地知识库问答（RAG） | 企业文档、个人笔记、代码库检索增强问答 | 文档不出域是刚需；小模型做生成器足够，检索在本地向量库完成 |
| 3 | 代码补全与解释 | 行级/函数级补全、报错解释、单测生成、重构 | 延迟敏感；7B 级代码模型本地即可达到可用水平 |
| 4 | 推理 / 数学 / Agent 任务 | 深度思考解题、工具调用（function calling）、多步 Agent | 蒸馏推理模型（如 DeepSeek-R1-Distill）在 8B 档已有强推理能力；小模型做 Agent 的单步执行器性价比高 |
| 5 | 多模态理解（图/文档/视频抽帧） | 截图解读、图表解释、PDF/OCR 文档理解、视频抽帧摘要 | 截图、证件、内部文档不宜上传云端；4B~9B 多模态模型本地可用 |
| 6 | 离线翻译与多语种处理 | 离线翻译、跨语言摘要 | 出境、弱网场景刚需；Gemma/Qwen 的多语种覆盖达 140+ 语言 |
| 7 | 语音交互（ASR/TTS/语音助手） | 本地语音识别、语音合成、全双工语音 Agent | 实时性要求高，云端往返延迟不可接受；可与文本模型组成流水线 |
| 8 | 轻量分类 / 结构化抽取 / 路由 | 意图分类、情感分析、信息抽取、大小模型路由、JSON 结构化输出 | 1B~4B 模型即可胜任，常作为流水线中的"小节点"或网关前置 |
| 9 | 端侧安全护栏 | 输入/输出内容审核、多模态风险评分 | 审核逻辑本身不宜经过第三方；0.8B~4B 护栏模型可跑在网关侧 |
| 10 | 嵌入式与 IoT | 智能家居指令、穿戴健康异常检测、工业质检、车机助手 | 算力极弱、无稳定网络，只能跑 100M~4B 档模型 |

典型工程形态是**混合路由**：95% 的简单/领域内请求走本地 SLM，5% 的复杂请求升级到云端 LLM，兼顾质量与成本。

---

## 3. 8GB 设备能跑多大的模型（换算规则）

经验法则（Q4 量化、GGUF 格式）：

- **8GB 独立显存**：可跑 **7B~9B** 模型（Q4_K_M 权重约 4.5~6.5GB），留 1.5~2GB 给 KV Cache 与运行时开销。追求全量 GPU offload，不要碰 12B+（被迫 CPU offload 后速度暴跌，不如小模型全驻显存）。
- **8GB 内存 / 统一内存**（核显本、Mac、手机）：选 **3B~4B** 模型（Q4 约 2~3.5GB），因为操作系统和推理运行时共用同一内存池。7B 虽能加载，但留给上下文的余量极小。
- **量化档位**：8GB 档首选 **Q4_K_M**（体积约为 FP16 的 1/4，质量损失低至中等）；7B 模型若上下文需求小可上 Q5_K_M；显存极紧张时才考虑 Q3/IQ 系列。
- **KV Cache 是隐形杀手**：权重是固定开销，KV Cache 随上下文长度线性增长。长上下文（32K+）在 8GB 卡上基本不现实，日常把上下文控制在 4K~16K。模型标称的 128K/262K 是架构上限，不是 8GB 设备的可用配置。
- **文件大小 ≠ 运行时占用**：GGUF 文件只是权重，还要加上 KV Cache、计算 buffer、多模态编码器（视觉/音频）开销。实测内存占用通常比文件大 10%~40%。

---

## 4. 典型模型推荐（8GB 可跑 + llama.cpp 支持）

> 说明：下表"llama.cpp"列指该架构是否在 llama.cpp 官方支持列表中（含 GGUF 转换/量化链路）；微调版（Instruct/Chat/推理版）一般随基座自动支持。Hugging Face 上搜 `GGUF` 即可找到对应量化权重。

### 4.1 有 8GB 独立显存：首选 7B~9B

| 模型 | 参数/版本 | Q4 大小 | 特点 | 适用场景 | llama.cpp |
|------|----------|---------|------|---------|-----------|
| **Qwen3.5-9B** ⭐综合首选 | 9B，混合 Gated DeltaNet+Attention，原生 262K 上下文 | ~5.7GB (Q4_K_M) | 推理/代码/指令跟随/OCR 俱强，原生图文输入，支持思考/非思考双模式，Apache 2.0 | 日常主力：问答、代码、文档理解、轻量 Agent | ✅ Qwen 全系支持 |
| **DeepSeek-R1-0528-Qwen3-8B** | 8B（R1 蒸馏到 Qwen3-8B） | ~5.2GB | 同档数学/逻辑推理最强（AIME 等榜单大幅领先同级），MIT 协议；缺点是思考链很长、日常闲聊偏慢 | 数学、难题 debugging、逻辑推理 | ✅（Qwen 架构） |
| **Qwen2.5-Coder-7B-Instruct** | 7B，代码专用 | ~4.7GB | 40+ 编程语言，代码生成/修复/解释/补全；体积小，给长代码上下文留的余量最大；Apache 2.0 | 纯代码场景（搭配通用模型组成双模型路由） | ✅ |
| **GLM-4.6V-Flash** | 多模态（BF16 约 20GB，需社区 GGUF） | ~6.2GB (Q4_K_M) | 图像/文档/视频理解与视觉推理强，MIT 协议；缺点是权重偏大，长上下文易溢出到内存 | 以图/文档/视频理解为核心的工作流 | ✅ ChatGLM/GLM 系支持（用社区 GGUF） |
| **Llama-3.1-8B-Instruct** | 8B | ~4.9GB | 生态最大（微调/工具链最全），英文与工具调用稳；中文相对 Qwen 弱 | 英文任务、Agent 工具调用、需要丰富微调生态时 | ✅ 第一优先级支持 |
| **Mistral-7B-Instruct** | 7B，32K 上下文 | ~4.1GB | 老牌标杆，欧洲语言好；能力已被 Qwen3.5 等新模型超越，作兼容基线 | 存量项目兼容、欧洲小语种 | ✅ |
| **Qwen3-8B** | 8B，双模式（思考/非思考） | ~5.2GB | 中文稳、119 语言；注意默认输出可能走 thinking 通道，接脚本时要显式关（`think:false`）或解析字段 | 中文日常主力（新装机优先 Qwen3.5-9B） | ✅ |

### 4.2 只有 8GB 内存/统一内存（核显本、Mac、手机）：选 3B~4B

| 模型 | 参数/版本 | Q4 大小 | 特点 | 适用场景 | llama.cpp |
|------|----------|---------|------|---------|-----------|
| **Qwen3.5-4B** ⭐综合首选 | 4B，多模态，256K 上下文 | ~3.4GB | 小体积但支持图文输入、思考模式、工具调用；MMLU-Pro 79.1，接近 9B 版，Apache 2.0 | 8GB 内存设备的日常主力 | ✅ |
| **Phi-4-mini-instruct** (微软) | 3.8B，128K 上下文 | ~2.5GB | 推理/数学/代码性价比之王（HumanEval 74.4%），纯文本无视觉开销，Q5 也放得下，MIT 协议 | CPU 推理、数学逻辑、结构化问答、代码解释 | ✅ Phi 系支持 |
| **Gemma-3-4B-IT** (Google) | 4B，多模态 | ~3.3GB | 140+ 语言，图文理解稳，有官方 QAT 量化版；协议为 Gemma 使用条款（比 Apache/MIT 严格） | 多语种、截图/文档理解 | ✅ Gemma 系支持 |
| **Gemma-4-E4B** (Google) | 有效参数 ~4.5B，多模态 | Ollama 默认包 ~9.6GB（含高精度量化），4-bit 约 5GB | 2026-04 新品，原生函数调用与结构化 JSON 输出，Apache 2.0；边缘部署新标杆 | 需要 function calling 的端侧 Agent、图文任务 | ✅（Gemma 架构；GGUF 多为社区版，先核验） |
| **SmolLM3-3B** (Hugging Face) | 3B，64K（YaRN 可扩 128K） | ~2GB | 全开放（权重+训练配方+数据配比），支持思考/直答双模式，Apache 2.0；能力上限低于 Qwen3.5-4B | 极度省内存的写作、摘要、日常闲聊 | ✅ SmolLM 系支持 |
| **Llama-3.2-3B-Instruct** (Meta) | 3B，128K 上下文 | ~2.2GB | 同级工具调用最强（BFCL V2 67%），速度最快一档（40~60 tok/s） | 3B 档 Agent 执行器、结构化输出、分类路由 | ✅ |
| **Ministral-3-3B** (Mistral) | 3B，多模态，256K 上下文 | ~3.0GB | 图文输入+超长上下文，欧洲语言强 | 欧洲多语种、长文档 | ✅（Mistral 架构） |
| **Qwen3-4B** | 4B，双模式 | ~2.5GB | 轻快省资源，适合做流水线小节点（分类/标题/改写）；复杂推理交给 8B | 流水线小节点、轻任务 | ✅ |

### 4.3 更小的档位（手机、嵌入式、浏览器）

| 模型 | 参数 | 说明 | llama.cpp |
|------|------|------|-----------|
| Gemma-4-E2B | 有效 ~2.3B | 手机/树莓派档，4-bit 约 5GB 内存可跑 | ✅ |
| Llama-3.2-1B | 1B | Q4 约 1GB，做路由、自动补全、分类 | ✅ |
| SmolLM2-1.7B / 135M | 1.7B / 135M | 全开放，135M 可跑浏览器（WebLLM/WebGPU） | ✅ |
| MiniCPM-V-4.6 | ~1.3B（视觉语言） | GGUF 版约 2~3GB 起，单图/多图/视频/OCR+工具调用 | ✅ MiniCPM 系支持 |
| Qwen3.5-0.8B/2B | 0.8B / 2B | 嵌入式实验、窄任务微调基座 | ✅ |
| Falcon-H1-Tiny | 90M/100M | 185MB 级，嵌入式分类/抽取/微调基座 | ✅（有官方 GGUF） |
| LFM2.5-2.6B (Liquid AI) | 2.6B | 混合架构，ONNX Q4 约 1.5GB，厂商称 <2.5GB 可跑 | ✅（官方提供 GGUF） |

### 4.4 实测参考（RTX 4060 Laptop 8GB，Ollama，同机实测非实验室榜单）

| 模型 | 热运行显存 | 生成速度 | 体感结论 |
|------|-----------|---------|---------|
| qwen3:4b | ~2.7GB | ~40 tok/s | 很快，适合轻任务/流水线节点 |
| qwen3:8b | ~4.9GB | ~24 tok/s | 中文日常主力，速度与质量最平衡 |
| gemma4:e4b | ~3.1GB | ~55 tok/s | 多模态性价比最高，截图/图表/抽帧视频摘要可用 |
| gemma4:12b | ~5.8GB | ~8 tok/s | 描述更细但太慢，只适合按需二次复核 |

> 规律：文件大小与显存占用非线性；冷启动十几秒 vs 热启动零点几秒是两种体验；12B 在 8GB 卡上"能跑但不好用"，印证了 7B~9B 全驻显存优先的原则。

---

## 5. llama.cpp 支持情况总览

llama.cpp 要求模型为 **GGUF 格式**（PyTorch/SafeTensors 需先转换），官方支持的主流架构包括：

- **文本**：LLaMA 全系（1/2/3）、Mistral/Mixtral(MoE)、Gemma、Qwen 全系、Phi 全系、DeepSeek 全系、ChatGLM3/4、Baichuan、Yi、Falcon、MPT、Bloom、GPT-NeoX/Pythia、SmolLM、OpenELM、GLM-Edge、Granite、StarCoder/Refact/CodeShell（代码）、DBRX/Jamba/OLMoE（MoE）、Mamba/FalconMamba/RWKV（状态空间架构）等，完整清单见 llama.cpp README。
- **多模态**：LLaVA 1.5/1.6、Qwen2-VL、MiniCPM、Yi-VL、Moondream、Bunny、MobileVLM、GLM-EDGE、LFM2-VL 等（llama.cpp-multimodal / llama-server 多模态文档）。
- **微调版自动跟随**：绝大多数 `-Instruct/-Chat`、领域微调、LoRA 合并版随基座架构自动支持。
- **找权重**：Hugging Face 按 `library:gguf` 筛选，或搜 `<模型名> GGUF`（官方 Qwen/Gemma/LFM 等提供官方 GGUF，其余多为社区量化版，上生产前核验哈希与实测质量）。

推理后端：CPU / CUDA / Metal / Vulkan 全覆盖；Apple Silicon 上 MLX 通常比 llama.cpp 快 20%~50%，可按平台二选一；Ollama/LM Studio 是开箱即用的封装（底层多为 llama.cpp），`ollama pull qwen3.5:4b` 一条命令即跑。

---

## 6. 选型速查（默认路由建议）

| 你的设备 | 默认主力 | 轻任务/小节点 | 看图/读文档 | 搞推理/数学 | 写代码 |
|---------|---------|--------------|------------|------------|-------|
| 8GB 独显（3060Ti/3070/4060 等） | Qwen3.5-9B (Q4_K_M) | Qwen3-4B | GLM-4.6V-Flash / Gemma-4-E4B | DeepSeek-R1-0528-Qwen3-8B | Qwen2.5-Coder-7B |
| 8GB 内存/统一内存（核显本/Mac） | Qwen3.5-4B | SmolLM3-3B / Llama-3.2-1B | Gemma-3-4B | Phi-4-mini | Phi-4-mini |
| 手机/嵌入式 | Gemma-4-E2B / LFM2.5 | Llama-3.2-1B | MiniCPM-V / SmolVLM2 | —— | —— |

三条硬约束：① 先保证**全量驻留 fast memory**（独显看显存、核显/Mac 看统一内存余量），不要为了大参数被迫 offload；② 上下文按 **4K~16K** 规划；③ 多模态与长上下文都要**实测峰值内存**，不能只看权重文件大小。

---

## 7. 参考来源

- Atomic Chat《Best Local LLM for 8GB RAM or VRAM in 2026》(2026-08)：8GB 两档完整选型与 GGUF 实测数据 — https://atomic.chat/blog/guides/best-local-llm-8gb
- Local AI Master《Best Small Language Models 2026》(2026-03，更新至 2026-08)：SLM 横评、榜单与硬件对照表 — https://localaimaster.com/blog/small-language-models-guide-2026
- 字与码《本地四个开源模型跑了一遍：Qwen3、Gemma 4 在 8GB 显存上的真实表现》(2026-06)：RTX 4060 Laptop 实测 — https://zicode.com/blog/local-open-model-benchmark-qwen-gemma/
- ClawSphere《端侧模型与本地部署研究站》（核验于 2026-10-05）：51 家族/156 型号设备分级目录 — https://clawsphere.io/edge-models/
- llama.cpp 官方《Supported Models》：架构支持清单与 GGUF 要求 — https://ggml-org-llama-cpp.mintlify.app/models/supported-models
- Microsoft Azure《什么是小语言模型(SLM)?》/ Red Hat《LLM 与 SLM 的对比》：SLM 定义与用例 — https://azure.microsoft.com/zh-cn/resources/cloud-computing-dictionary/what-are-small-language-models ／ https://www.redhat.com/zh-cn/topics/ai/llm-vs-slm
