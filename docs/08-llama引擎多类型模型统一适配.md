# 08-以 llama.cpp 为引擎的多类型模型统一适配

> 搜集日期 2026-10-17。上游文档为本机抓取原文（`docs/multimodal.md`、`tools/mtmd/README.md`、
> `tools/server/README.md`）；C API 行号为本仓库 `third_party/llama/include/llama.h` 实测。
> 与 07 篇的关系：07 篇解决"多个 txt2txt 模型怎么换"（描述层）；本篇解决"多种**类型**
> 的模型（文本/视觉/音频/向量/重排…）怎么做统一适配"（执行层）。两篇字母编号各自独立，
> 引用 07 篇方案时会显式写"07-B"字样。

## 1. llama.cpp 作为引擎：能力矩阵（不止 txt2txt）

| # | 能力 | 输入→输出 | 引擎机制 | 模型文件形态 |
|---|---|---|---|---|
| 1 | 文本生成 decoder-only | text→text | `llama_decode` + sampler chain（现状 `EngineRuntime::Generate` 已覆盖） | 单 GGUF |
| 2 | Encoder-decoder（T5/BART 系） | text→text，异形循环 | `llama_encode` + `llama_decode`，`llama_model_decoder_start_token` 取解码起始符 | 单 GGUF |
| 3 | Embeddings | text→vector | 建 context 时定 `pooling_type`（MEAN/CLS/LAST）+ `llama_set_causal_attn(false)`；`--embeddings`；`/v1/embeddings` | 单 GGUF（EmbeddingGemma 等专用模型） |
| 4 | Rerank | query+docs→scores | `pooling_type=RANK`（分类头进图，`llama.h:183`）；`--rerank`；rerank 端点（PR #9510） | 单 GGUF（reranker 专用） |
| 5 | 视觉理解 | image+text→text | **libmtmd**（独立库，不在 `llama.h` 内）+ mmproj；server 经 OpenAI 兼容 `image_url` part | **双 GGUF**：text 模型 + mmproj |
| 6 | 音频理解 | audio→text | 同上（libmtmd）；Ultravox / Voxtral / Qwen3-ASR | 双 GGUF |
| 7 | 视频输入 | video→text | 同上 + `video-fps`/`video-ffmpeg-dir`；如 SmolVLM2-Video | 双 GGUF |
| 8 | Omni 混合 | audio+vision+text→text | 同上；Qwen2.5/3-Omni、Gemma 4 | 双 GGUF（多 projector） |
| 9 | Diffusion LM（LLaDA/Dream 系） | text→text，异形循环 | mask 迭代去噪，非自回归逐 token；`llama_model_is_diffusion`（`llama.h:677`） | 单 GGUF |
| 10 | 决策模型 | state→choice/score/noul | `/v1/systemone` 端点（openjev/clef 等原生决策模型） | 单 GGUF |
| 11 | 约束生成 | text→JSON/规则文本 | `llama_sampler_init_grammar` / JSON-schema；server 有 schema 约束参数 | 任意生成模型 |
| 12 | Function calling | text+tools→tool calls | server `--jinja` + `docs/function-calling.md`（"~any model" tool use） | 任意 chat 模型 |

运行时能力探测（适配器的"眼睛"，`llama.h` 实测存在）：

- `llama_model_has_encoder/has_decoder`（:661/:664）→ 编码器-解码器分流；
- `llama_model_is_recurrent/is_hybrid/is_diffusion`（:671/:674/:677）→ 异形架构分流；
- `llama_model_chat_template`（:655）+ `llama_model_meta_*`（:630-642）→ 模板与采样元数据；
- server `/v1/models` 广播 `architecture: {input_modalities, output_modalities}`（`text/image/audio/video/decisions`）→ 网关侧的能力发现。

本仓库 vendored 现状缺口（关键结论）：`third_party/llama/` 只有 `llama.h` 系头文件 + 预编译 `bin/lib`，
**没有 mtmd**。而上游把多模态明确做成"子项目"：libmtmd 与 libllama 分离开发，
README 原话是 under **very heavy development**，**breaking changes are expected**。
含义：vision/audio 不是"调个 API"的事，要么 vendor libmtmd（钉版本+隔离封装），要么走 server 进程。

上游预量化多模态模型（`docs/multimodal.md`，`ggml-org/*-GGUF`，`-hf` 即跑）：
视觉 Gemma 3 / SmolVLM(2) / Pixtral / Qwen2-VL / Qwen2.5-VL / Mistral Small 3.1 / InternVL2.5·3 / Llama 4 Scout / Moondream2 / Gemma 4；
音频 Ultravox 0.5 / Voxtral / Qwen3-ASR；混合 Qwen2.5-Omni / Qwen3-Omni / Gemma 4。
另：OCR 系（PaddleOCR-VL/GLM-OCR/DeepSeek-OCR/Dots.OCR/HunyuanOCR）与**专用 prompt 结构强绑定**（各有 PR 讨论），
sidecar 必须能表达"专用 prompt"，不能只存通用模板。

## 2. 多类型统一适配的主流方案（4 个，可叠加）

### 方案 A：能力路由 + 模态 Processor（HF / libmtmd 式）

- **原理**：`Processor = Tokenizer + FeatureExtractor + ChatTemplate`，按 `input_modality` 分流预处理，
  统一喂引擎。libmtmd 的 README 明示其 API 设计 inspired by HF `transformers` 的 `Processor` 类——
  这就是上游官方的"多类型统一"答案。
- **落到本项目**：`EngineRuntime` 之上加 Capability 层：
  `Capability = {modalities, needs_mmproj, pooling, encode/decode 路径, sampler/约束}`；
  加载时探测（§1 的 has_encoder/is_diffusion/meta KV/mmproj 存在性）→ 选择执行路径。
- **优点**：单进程、C API 直调、延迟最低；**缺点**：mtmd 须 vendor 且 API 不稳定，每种模态预处理自己接。

### 方案 B：协议面统一（OpenAI 兼容网关式）

- **原理**：`/v1/chat/completions` 的 `content` parts（`text`/`image_url`/`input_audio`/`input_video`）
  天然就是多模态统一协议，embeddings/rerank/responses 各有端点，`/v1/models` 广播 modalities。
  本项目退化为协议客户端，多类型承载全部由 server/router 解决（含 07-C 的多模型路由）。
- **优点**：多类型 + 多模型 + 并发一次全解，与生态工具互通；
  **缺点**：双进程（07 篇 §3-C 行已分析），端侧零依赖优势丢失。

### 方案 C：模型包 sidecar 扩展版（07-B 的多类型推广）

- **原理**：07-B 的 adapter schema 增加 capability 段：
  `modalities` / `mmproj 文件名` / `pooling` / `encoder_decoder` / `diffusion` / `ocr_prompt` 等。
  07 篇的 TEMPLATE+PARAMETER 只覆盖 txt2txt，本篇把它补全到全类型。
- **定位澄清**：C 是"描述层"，A 是"执行层"，两者正交——sidecar **声明** capability，能力路由**消费**它。
- **优点**：增量演进，07 篇工作不浪费；**缺点**：schema 随模态膨胀，必须版本化 + 严格校验。

### 方案 D：按能力拆运行时（llama.cpp 上游自身的做法）

- **原理**：上游自己就是按能力拆的：`llama-cli`（文本）、`llama-mtmd-cli`（多模态）、
  server 按端点拆（chat/embeddings/rerank/systemone/messages）；embeddings 必须 `--embeddings` 单独启动——
  因为 pooling/attention 参数在 context 创建时锁定，**同一实例不可兼做生成与向量**。
- **含义**：不要幻想一个 `EngineRuntime` 实例包打天下；"统一"应发生在路由/协议层，执行层按能力拆实例。
- **优点**：与上游对齐，升级阻力最小；**缺点**：多实例 = 多份显存，需 LRU/预算（07-D）。

### 对比

| 维度 | A 能力路由 | B 协议网关 | C sidecar 扩展 | D 按能力拆实例 |
|---|---|---|---|---|
| 统一发生在哪层 | 进程内路由层 | HTTP 协议层 | 描述文件层 | 不统一，路由层拼装 |
| 进程形态 | 单 exe | 双进程 | 单 exe（+模型目录） | 单 exe，多实例 |
| vision/audio 来源 | vendor libmtmd | server 内置 | 声明 mmproj（执行靠 A/D） | mtmd 实例独立 |
| embeddings 兼做 | 独立 pooling 实例 | 独立端点/实例 | 声明 pooling | 独立实例（强制） |
| 与上游对齐度 | 高（学 libmtmd） | 最高（直接用 server） | 自研（对齐 Modelfile 思想） | 最高（照抄上游拆法） |
| 工程量 | 中～大 | 中 | 小～中 | 中（实例池+LRU） |
| 适用阶段 | §3 阶段 1/3 | §3 阶段 4 | §3 阶段 2（先行） | §3 全程原则 |

一句话选型：**描述用 C，执行按 D 拆实例，路由学 A，对外服务时再 B**——四者是层不是单选题，叠加使用。

## 3. 给本项目的落地路线（从 demo 到引擎平台）

- **阶段 0（现状）**：`EngineRuntime` = txt2txt decoder-only 单路径（07 篇 §1 的 8 个写死点）。
- **阶段 1：能力探测 + 执行分流（纯文本域内先练手，不碰 mtmd）**：
  加载后调 `has_encoder/has_decoder/is_diffusion` + meta KV → 填 `Capability` 结构体；
  encoder-decoder 走 encode+decode 路径，diffusion 走 mask 循环（或明确拒绝并报错，不要静默错跑）；
  embeddings 起独立实例（pooling MEAN + `causal_attn=false`），与生成实例分离（D 原则先行）。
  最小验收：Qwen3（生成）+ T5 系（编解码）+ EmbeddingGemma（向量）+ 一个 reranker。
- **阶段 2：sidecar capability 段（07-B 推广）**：`modalities/mmproj/pooling/ocr_prompt` 入 schema 并版本化；
  加载顺序：sidecar 显式值 > GGUF 元数据（§1 探测 API）> 编译时默认。
- **阶段 3：多模态（二选一）**：(a) vendor libmtmd——必须钉住版本并包一层自己的 `ModalityProcessor`，
  mtmd 符号只出现在一个 `.cpp` 内（对冲 breaking changes）；(b) 短期混合部署：文本直调 C API，
  vision/audio 走本地 `llama-server` 子进程（单机回环，延迟可接受）。
  最小验收：Gemma 3-4B（视觉）+ Voxtral（音频）。
- **阶段 4：对外网关化（B）**：内部仍是 A+C+D，只在边界披一层 OpenAI 兼容协议面给第三方用。

## 4. 风险清单

1. mtmd API 不稳定：vendor 必须钉版本 + 隔离封装（§3-3a），跟踪上游 `tools/mtmd/README.md` 的 breaking 公告。
2. pooling/attention/context 参数在实例创建时锁定：生成与向量不可同实例（D 不是建议，是铁律）。
3. OCR 系模型 prompt 强相关：sidecar 必须能表达专用 prompt 结构，不能只存通用模板。
4. diffusion/decision 系是异形执行循环：独立路径，不要硬塞进 `Generate()`。
5. 多实例显存预算：沿用 07-D（lazy load + LRU + 空闲 sleep，上游 `--sleep-idle-seconds` 是参照语义）。
6. `--jinja`/tool-use 与 grammar 约束是"生成实例内"的正交能力，不单独占实例，但占 sampler/模板复杂度。

## 5. 来源

- 多模态总览（模态/预量化表/OCR 警告）：https://github.com/ggml-org/llama.cpp/blob/master/docs/multimodal.md
- libmtmd（子项目定位、mmproj 双文件、HF Processor 对标、breaking 警告）：https://github.com/ggml-org/llama.cpp/blob/master/tools/mtmd/README.md
- server（端点矩阵、router、`input/output_modalities` 广播、`/v1/systemone`）：https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md
- rerank 端点：https://github.com/ggml-org/llama.cpp/pull/9510（server README Features 引用，未展开读 PR 正文）
- function calling：`docs/function-calling.md`（仅知存在，未细读；server 以 `--jinja` 开启）
- 本仓库实测：`third_party/llama/include/llama.h:177-184`（pooling，RANK=4）、`:655`（chat_template）、
  `:660-677`（has_encoder/has_decoder/decoder_start_token/is_recurrent/is_hybrid/is_diffusion）、
  `:990`（`llama_encode`）、`:1114`（`llama_set_causal_attn`）；`third_party/llama/{bin,include,lib}` 无 mtmd。
