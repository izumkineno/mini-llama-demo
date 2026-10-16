# 03 llama.cpp 动态库 C-API 调用与集成示例

> 头文件：`include/llama.h`（C 接口，`LLAMA_API` 导出宏处理了 `__declspec(dllexport/dllimport)`，
> 全文见 [llama.h](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/include/llama.h)）。
> 最小官方示例：[examples/simple/simple.cpp](https://raw.githubusercontent.com/ggml-org/llama.cpp/master/examples/simple/simple.cpp)。
> 调研时间：2026-10-16。

## 1. 推理主流程（背下来）

```
llama_backend_init()
  → llama_model_default_params() + llama_model_load_from_file(model.gguf)
  → llama_context_default_params() + llama_init_from_model()
  → llama_sampler_chain_init() + 采样器链（top-k/top-p/temp/dist…）
  → llama_tokenize() 把 prompt 转 token
  → 循环：llama_process(ctx, DECODE, batch) → llama_sampler_sample() → llama_token_to_piece()
  → llama_batch_ext_free / llama_sampler_free / llama_free / llama_model_free
```

与官方 `simple.cpp` 完全对应，下节是其精简可编译版。

## 2. 最小 C++ 示例（链接动态库版）

```cpp
// min_llama.cpp —— 链接 llama 动态库
#include "llama.h"
#include <cstdio>
#include <string>
#include <vector>

static void batch_set_tokens(llama_batch_ext * batch, const llama_token * tokens,
                             int32_t n_tokens, llama_pos pos_0) {
    llama_batch_ext_clear(batch);
    for (int32_t i = 0; i < n_tokens; ++i) {
        const int32_t idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
        const llama_pos pos = pos_0 + i;
        llama_batch_ext_set_pos(batch, idx, &pos);
    }
    llama_batch_ext_set_output_logits(batch, n_tokens - 1, true);
}

int main(int argc, char ** argv) {
    const char * model_path = argc > 1 ? argv[1] : "model.gguf";
    std::string prompt = argc > 2 ? argv[2] : "Hello my name is";
    const int n_predict = 32, ngl = 99;

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = ngl;                    // 0=纯CPU，99=尽量offload
    llama_model * model = llama_model_load_from_file(model_path, mparams);
    if (!model) { fprintf(stderr, "load model failed\n"); return 1; }
    const llama_vocab * vocab = llama_model_get_vocab(model);

    // tokenize
    const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(),
                                         NULL, 0, true, true);
    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab, prompt.c_str(), (int32_t)prompt.size(),
                       prompt_tokens.data(), (int32_t)prompt_tokens.size(), true, true) < 0) {
        fprintf(stderr, "tokenize failed\n"); return 1;
    }

    // context
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = n_prompt + n_predict - 1;
    cparams.n_batch = n_prompt;
    llama_context * ctx = llama_init_from_model(model, cparams);
    if (!ctx) { fprintf(stderr, "create context failed\n"); return 1; }

    // sampler：greedy 最简；生产用 top-k + top-p + temp + dist 链
    llama_sampler * smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    // 首批：整个 prompt 一次喂入
    llama_batch_ext * batch = llama_batch_ext_init(ctx);
    int n_pos = 0;
    batch_set_tokens(batch, prompt_tokens.data(), n_prompt, 0);
    int n_tokens = n_prompt;

    for (; n_pos + n_tokens < n_prompt + n_predict; ) {
        if (llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch)) {
            fprintf(stderr, "decode failed\n"); return 1;
        }
        n_pos += n_tokens;
        llama_token id = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, id)) break;
        char buf[128];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        printf("%.*s", n, buf); fflush(stdout);
        batch_set_tokens(batch, &id, 1, n_pos);
        n_tokens = 1;
    }
    printf("\n");

    llama_batch_ext_free(batch);
    llama_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);
    return 0;
}
```

编译（假设 `install/` 为 `cmake --install` 产物）：

```bash
# Linux
g++ min_llama.cpp -o min_llama -I./install/include -L./install/lib -lllama -Wl,-rpath,'$ORIGIN/lib'
# Windows (MSVC，开发者命令行)
cl min_llama.cpp /I install\include install\lib\llama.lib /Fe:min_llama.exe
:: 运行目录需放 llama.dll + ggml*.dll
```

生产级采样链（替代 greedy）：

```cpp
llama_sampler_chain_add(smpl, llama_sampler_init_top_k(50));
llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9f, 1));
llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed)); // 或 greedy/miostat 收尾
```

## 3. 核心 API 速查（均在 llama.h）

### 3.1 生命周期

| 函数 | 说明 |
|---|---|
| `llama_backend_init()` / `llama_backend_free()` | 进程起止各调一次，初始化 ggml 后端 |
| `llama_model_default_params()` / `llama_model_load_from_file(path, params)` | 载模型；`n_gpu_layers` 控 GPU 层数；`devices`/`tensor_split` 控多卡 |
| `llama_init_from_model(model, ctx_params)` / `llama_free(ctx)` / `llama_model_free(model)` | 建/销 context 与 model（旧名 `llama_new_context_with_model` 已废弃） |
| `llama_version()` / `llama_print_system_info()` | 版本与系统信息 |
| `llama_log_set(cb, ud)` | 接管日志（默认打 stderr），GUI 程序必接 |

### 3.2 分词

```c
int32_t llama_tokenize(vocab, text, text_len, tokens, n_max, add_special, parse_special);
// 传 NULL/0 先取长度（返回负数），再分配再调一次；add_special 一般 true
int32_t llama_token_to_piece(vocab, token, buf, len, lstrip, special);
int32_t llama_detokenize(vocab, tokens, n, text, max, remove_special, unparse_special);
bool llama_vocab_is_eog(vocab, token); // 是否结束符，采样循环终止条件
```

### 3.3 推理（batch + process）

- `llama_batch_ext_init(ctx)` / `llama_batch_ext_free()` 管理 batch；
  `llama_batch_ext_add_token/set_pos/set_output_logits/clear` 组 batch。
- `llama_process(ctx, LLAMA_PROCESS_TYPE_DECODE, batch)` 新入口（encode 用于编解码模型）；
  编解码模型先 `ENCODE` 再喂 decoder 起始符，见 simple.cpp 的 `llama_model_has_encoder` 分支。
- `llama_context_params` 关键字段：`n_ctx`、`n_batch`、`n_ubatch`、`n_threads`/`n_threads_batch`、
  `rope_scaling_type`/`rope_freq_base`、KV Cache 类型 `type_k/type_v`、MTP 的 `ctx_type`。

### 3.4 采样器

- 链式：`chain_init → chain_add(...) → sampler_sample(smpl, ctx, idx) → sampler_free`。
- 常用：`top_k / top_p / min_p / typical / temp / temp_ext / xtc / top_n_sigma / penalties / dry / mirostat(v2) / grammar / logit_bias / infill / dist / greedy`。
- 注意：加进链的 sampler 所有权归链，不要单独 free；`sampler_sample` 等价于"取 logits→apply→选 token→accept"。

### 3.5 聊天模板

```c
int32_t llama_chat_apply_template(tmpl, chat, n_msg, add_ass, buf, len);
int32_t llama_chat_builtin_templates(output, len);
```

`tmpl` 传 NULL 用模型内嵌模板；`chat` 为 `{role, content}` 数组。Qwen 等模型走 `--jinja` 即此路。
注意官方注明此函数**不是完整 jinja 解析器**，只支持预定义模板子集。

### 3.6 量化/ misc

- `llama_model_quantize_default_params()` + `llama_model_quantize_file()`：代码内做 GGUF 量化。
- `llama_split_path/prefix`：处理 `-00002-of-00004.gguf` 分片模型。
- 性能：`llama_perf_context_print/sample_print`（示例用，第三方 App 建议自己打点计时）。

## 4. 运行时动态加载（插件式集成，不链接 .lib）

只依赖 `llama.h` 的函数指针，适合"模型引擎可选安装"的 App。

```cpp
// Windows
#include <windows.h>
HMODULE h = LoadLibraryA("llama.dll");
auto p_backend_init = (void(*)())GetProcAddress(h, "llama_backend_init");
auto p_load = (llama_model*(*)(const char*, llama_model_params))GetProcAddress(h, "llama_model_load_from_file");
// …其余同理，用 llama.h 的声明逐个取地址
```

```cpp
// Linux
#include <dlfcn.h>
void* h = dlopen("./lib/libllama.so", RTLD_NOW);
auto p_backend_init = (void(*)())dlsym(h, "llama_backend_init");
// …
```

要点：

- C 接口无名字改编（`extern "C"`），`dlsym/GetProcAddress` 名即 `llama.h` 函数名。
- 仍要保证 `llama.dll` 的依赖（`ggml-*.dll`、后端插件、`libomp.dll`）在搜索路径内。
- 结构体按值传参（如 `llama_model_default_params()` 返回结构体），动态加载时**必须用同一版头文件**编译调用方，
  否则内存布局不一致会静默错乱——升级 DLL 必须同步升级头文件重新编译。

## 5. 三种集成路线对比

| 路线 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| 直接链接动态库（本章主线） | 链接 `llama.lib/.so`，调 C API | 性能最好、控制最细（batch/采样/KV 全控） | 要自己处理线程、模板、流式输出 |
| 起 `llama-server` 子进程 + HTTP | 调 OpenAI 兼容 REST API + 内置 WebUI | 隔离性好、多语言零绑定 | 多进程开销、部署多一个二进制 |
| 高层绑定 | Python `llama-cpp-python`、Node `node-llama-cpp`、Android `examples/llama.android` 的 JNI | 开发快 | 版本滞后上游、可控性弱 |

- `llama-server` 一键起服务：`llama-server -hf Qwen/Qwen3-8B-GGUF:Q8_0 --jinja -ngl 99 -c 40960`，
  自带 OpenAI 兼容接口与 Web 前端，适合"先跑通、再下沉 C API"。
- Android App：直接用 `examples/llama.android` 的 `AiChat`/`InferenceEngine`/`GgufMetadataReader` 封装，
  或照其 JNI 层自己包（见 `02` 文档 5.2 节）。

## 6. 踩坑清单

1. **C++ 对象不过 DLL 边界**：只传 C 类型/`llama.h` 结构体；调用方与 DLL 统一编译器与 CRT。
2. **头/DLL 同版本**：`llama.h` 布局随版本变，混版=未定义行为；发版时把 `llama_version()` 打进日志。
3. **`n_ctx` 按设备设**：手机先 2048/4096，PC 再往上加；KV Cache 是大头。
4. **日志接管**：GUI/服务程序用 `llama_log_set` 接走 stderr 输出。
5. **多线程**：`llama_tokenize` 等标注线程安全，但同一 `llama_context` 的 decode 循环不要并发调；
   要并发=多 context。
6. **GPU 层数**：`n_gpu_layers=0` 纯 CPU；`99` 全 offload；中间值混合 offload 大模型。
7. **结束判断**：每次采样后 `llama_vocab_is_eog()`，否则停不下来。
