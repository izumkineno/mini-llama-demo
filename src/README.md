# src/ —— lai-translate 源码布局

单二进制翻译 CLI（C++17，MSVC-only）。`main.cpp` 为薄装配入口，各模块按
ralplan Step 分层，目录即架构：

| 目录 | 模块 | 来源 |
|---|---|---|
| `core/` | `engine.*`：llama 模型加载/上下文/解码封装 | Step 2 |
| `prompt/` | `prompt_spec.*`：PromptManager + 采样链；`embedded.h`：编译时内嵌模板 | Step 3 |
| `app/` | `loop.*`：REPL；`i18n_count.*`：stdin 归一化/计数 | Step 4 |
| `util/` | `sha256.*`：通用 SHA-256（启动模板自检用） | — |

依赖方向（无环）：`main → app → {prompt, core} → util`；
`prompt → core` 仅用 tokenize 透传原语。`llama.h` 是唯一的 DLL 边界（纯 C API）。

约定：自有符号全部在 `namespace lai` 内；include 以 `src/` 为根加前缀，
如 `#include "core/engine.h"`；模块名与冻结 spec 的 Step 词汇对齐，不随意改名。
