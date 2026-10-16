# Hy-MT2-1.8B 模板取证 META（B1）

- 模板文件：`prompts/hy-mt2-v20261016.txt`（75 字节，UTF-8 无 BOM，无尾换行）
- 模板 sha256：`a06006c7c6b1cfff2f024588eb09b01d5d4c04190ddc3923e1e41c928cef277d`
- 版本号：`v20261016`（取抓取日期加 v 前缀，对齐计划 `hy-mt2-vX.txt` 命名；上游仓 revision=master，无 release tag；2026-10-16 由主会话改名，原名 `hy-mt2-20261016.txt`，内容字节不变）
- 抓取日期：2026-10-16
- 判定结论：**走 B1**（找到可复制的独立模板 exact text；O-B "B2证实标准"第②条"清单内无可复制的独立模板 exact text"不成立，B2 不启用）

## 来源 URL

1. 官方 Jinja 源文件（主来源，654 字节）：
   `https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B/resolve/master/chat_template.jinja`
   原样存档：`prompts/hy-mt2-v20261016.jinja.source`（sha256 见下）
2. ModelScope 模型 API（README 全文 + 元数据）：
   `https://modelscope.cn/api/v1/models/Tencent-Hunyuan/Hy-MT2-1.8B`
3. ModelScope GGUF 发布仓 API（含 `ModelInfos.gguf.chat_template` + `gguf_file_list` + sha256）：
   `https://modelscope.cn/api/v1/models/Tencent-Hunyuan/Hy-MT2-1.8B-GGUF`
4. 基座仓 tokenizer（特殊 token ↔ token ID）：
   `https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B/resolve/master/tokenizer_config.json`
5. llama.cpp 上游 hunyuan-dense 模板支持（`src/llama-chat.cpp:76` arch 映射 + `:226-227` 模板内容嗅探）：
   `https://grep.app/api/search?q=LLM_CHAT_TEMPLATE_HUNYUAN_DENSE`
6. GGUF 发布仓 README 内 llama.cpp 用法（`--jinja` + STQ kernel PR #22836 说明）：
   见 URL-3 返回的 `ReadMeContent`（llama_cpp 节）；PR 页 `https://github.com/ggml-org/llama.cpp/pull/22836`
   （标题：ggml-cpu STQ1_0 ternary quantization，为量化 kernel PR，非模板文档）
7. HuggingFace：`https://huggingface.co/tencent/Hy-MT2-1.8B`（官方 org 为小写 `tencent`，见 README Model Links 表）
   及 `https://huggingface.co/Tencent-Hunyuan/Hy-MT2-1.8B`：本环境均 HTTP 401 不可达（页面 + API 均试过），
   hf-mirror 对非 CN IP 跳回原站。HF 侧未取到独立证据；ModelScope 侧 README 即官方模型卡镜像。

## 三源一致性（字节级已验证）

| 源 | 内容 | 长度 |
|---|---|---|
| chat_template.jinja（URL-1 实下字节） | 官方 Jinja 源码 | 654 B |
| 本地 GGUF `tokenizer.chat_template`（只读解析头） | 内嵌模板 | 654 B，sha256 `b7491ec0e9c869dfce20f2176758099bf248d979dd05530ede99deb21698acee` |
| ModelScope API `ModelInfos.gguf.chat_template`（URL-3） | 解析字段 | 596 chars，UTF-8 编码后与上两者**逐字节相等** |

本地模型文件 `Hy-MT2-1.8B-Q4_K_M.gguf`（1133080448 B）整文件 sha256
`dc5f44fcf1fa496ee7ad725982c0c8c553a4de00259b53af84c4b89fb0c06699`
与 GGUF 发布仓公布值**完全一致** → 本地文件即官方发布，发布方=`Tencent-Hunyuan/Hy-MT2-1.8B-GGUF`。
GGUF 头：arch=`hunyuan-dense`，`tokenizer.ggml.model`=`gpt2`，`tokenizer.ggml.pre`=`hunyuan-dense`。

## 官方 Jinja 原文（654 B，与 .jinja.source 字节一致，此处仅供人阅读）

```jinja
{% if messages[0]['role'] == 'system' %}{% set loop_messages = messages[1:] %}{% set system_message = messages[0]['content'] %}<｜hy_begin▁of▁sentence｜>{{ system_message }}<｜hy_place▁holder▁no▁3｜>{% else %}{% set loop_messages = messages %}<｜hy_begin▁of▁sentence｜>{% endif %}{% for message in loop_messages %}{% if message['role'] == 'user' %}<｜hy_User｜>{{ message['content'] }}{% elif message['role'] == 'assistant' %}<｜hy_Assistant｜>{{ message['content'] }}<｜hy_place▁holder▁no▁2｜>{% endif %}{% endfor %}{% if add_generation_prompt %}<｜hy_Assistant｜>{% else %}<｜hy_place▁holder▁no▁8｜>{% endif %}
```

标签字符说明：`｜`=U+FF5C 全角竖线，`▁`=U+2581（空格替代符），`<>`=ASCII。
token ID（见 tokenizer_config.json）：BOS `<｜hy_begin▁of▁sentence｜>`=120000，
`<｜hy_User｜>`=120006，`<｜hy_Assistant｜>`=120007，均为 special token；
`eos_token`=`<｜hy_place▁holder▁no▁2｜>`；`tokenizer_config.json` 内无 `chat_template` 键（已 grep 确认）。

## 渲染决策（Jinja → B1 纯拼装串）

MVP 输入形状固定：单轮单 user 消息、无 system、无历史（见计划 O-B msgs 规格）。
Jinja 在此形状下退化为确定性 pre/post 静态串，无数据依赖分支。生成形态取
`add_generation_prompt=True`（与官方生成用法一致：README transformers 示例
`apply_chat_template(..., add_generation_prompt=True)`；llama.cpp 示例用 `--jinja`）：

- pre = `<｜hy_begin▁of▁sentence｜><｜hy_User｜>`
- `{{USER}}` = 用户待译句位置（对应 Jinja user 分支 `{{ message['content'] }}`）
- post = `<｜hy_Assistant｜>`（generation prompt，assistant 起始控制符，不含回复内容）

`hy-mt2-v20261016.txt` = pre + `{{USER}}` + post，共 75 字节，`{{USER}}` 恰 1 个
（构造脚本已断言 `count({{USER}})==1` 且无杂散 `{{`/`}}`；标签字节由 ASCII 锚点
从已验证 Jinja 字节中机械提取，零 CJK 手工转录）。
备选形态（`add_generation_prompt=False` → post=`<｜hy_place▁holder▁no▁8｜>`）未采用，
因官方生成示例均为 True 形态；若 Step 3 golden 异常，可凭本 META 回溯复核。

## URL 清单逐项结论（一句话/项）

1. ModelScope 基座仓模型页+文件：README 无独立模板文本（含翻译指令示例与 `apply_chat_template` 用法，
   声明无默认 system_prompt）；文件 `tokenizer_config.json` 有特殊 token 无 `chat_template` 键；
   文件 `chat_template.jinja`（654 B）即独立模板 exact text。→ **命中**
2. HuggingFace 同名仓模型卡+文件：本环境 401 不可达，未获证据（官方 org 为 `tencent` 小写）。→ **未取到（环境限制）**
3. GGUF 发布仓 README/Release note：README 无模板文本但给出 llama.cpp `--jinja` 用法；
   仓 API `chat_template` 字段与本地 GGUF 内嵌模板逐字节一致，且本地文件 sha256 与官方一致。→ **命中（经 API 字段）**
4. llama.cpp 上游模板支持：`src/llama-chat.cpp` 有 `hunyuan-dense → LLM_CHAT_TEMPLATE_HUNYUAN_DENSE`
   映射及模板内容嗅探（含本模板特征串），属架构级支持，无 Hy-MT2 独立模板文档页。→ **支持存在，无独立文档**
5. 本地 GGUF 只读扫描：`tokenizer.chat_template` 存在，654 B，与官方 Jinja 逐字节一致。→ **命中**

## 遗留与交接

- 本取证仅覆盖"URL 清单 + 内嵌模板扫描"；B1 无需 probe 快照/签字（B2 通道未启用）。
- Step 3 PromptSpec 按本 META 切分：`kTemplatePre`=BOS+User 标签，`kTemplatePost`=Assistant 标签；
  两段式 tokenize flags 见计划（模板段 `parse_special=true`，用户段 `false`）。
- 若主会话对 `add_generation_prompt=True` 渲染有异议，备选串与完整 Jinja 均在本文件，可重构。
