// engine.cpp —— EngineRuntime 实现（Step 2）
// 对应计划条款同 engine.h；API 链路对照 docs/03 §2 最小示例。
#include "engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "llama.h"

// llama 日志回调：经 llama_log_set 接管（Step 2 要求），默认只放行敞口级信息。
// 规则：WARN/ERROR 全放行；INFO 仅放行 "llama_version" 锚点行（AC1 断言依赖）；
// CONT 仅在上一放行行"未以换行结尾"时跟随（真续行），否则视为新消息丢弃——
// 这恰好滤掉 INFO 刷屏续片与 load 进度点阵（前一行以 \n 结束），不误伤跨行报错。
// 调试时置 LAI_VERBOSE_LOG=1 恢复全量透出（AC7 归档即用此模式采集）。
static bool g_last_log_open = false;  // 上一放行行是否未闭合（无尾随 \n）
static void LlamaLogCallback(ggml_log_level level, const char* text, void* /*user_data*/) {
    if (!text) return;
    if (std::getenv("LAI_VERBOSE_LOG")) {
        std::fputs(text, stderr);
        g_last_log_open = false;
        return;
    }
    bool fwd = false;
    if (level >= GGML_LOG_LEVEL_WARN) {
        fwd = true;
    } else if (level == GGML_LOG_LEVEL_INFO && std::strncmp(text, "llama_version", 13) == 0) {
        fwd = true;
    } else if (level == GGML_LOG_LEVEL_CONT && g_last_log_open) {
        fwd = true;
    }
    if (fwd) {
        std::fputs(text, stderr);
        const size_t n = std::strlen(text);
        g_last_log_open = (n > 0 && text[n - 1] != '\n');
    } else if (level != GGML_LOG_LEVEL_CONT) {
        g_last_log_open = false;  // 被丢弃的整行消息不续接后续 CONT
    }
}

EngineRuntime::EngineRuntime() = default;

EngineRuntime::~EngineRuntime() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
    // backend 与进程同寿：free 留给进程退出（llama_backend_init 进程起止各一次，
    // 此处不调 backend_free，避免与采样器/批处理析构顺序纠缠，MVP 从简）。
}

bool EngineRuntime::Init(const std::string& gguf_path) {
    gguf_path_ = gguf_path;

    // AC1 锚点分支一：GGUF 缺失 → stderr 含路径 + 非零退出（由 main 执行 exit）。
    {
        FILE* probe = nullptr;
        if (::fopen_s(&probe, gguf_path.c_str(), "rb") != 0 || probe == nullptr) {
            std::fprintf(stderr, "模型文件缺失，无法加载：%s\n", gguf_path.c_str());
            return false;
        }
        std::fclose(probe);
    }

    llama_log_set(LlamaLogCallback, nullptr);  // Step 2：日志接管
    llama_backend_init();                      // Step 2：backend_init（进程一次）

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = kEngineNGpuLayers;  // C3：99 = 尽量全 offload
    // load 期 stderr 重定向：llama 内部有直接 stderr 写（EOG 进度点阵 72 点，
    // 经 LAI_LOG_DEBUG 探针证实不走 log 回调），此处 fd 级静默。VERBOSE 下跳过，
    // 保留原生诊断；加载失败的对外报错走下面自有 fprintf，不受影响。
#ifdef _WIN32
    int saved_stderr = -1;
    if (!std::getenv("LAI_VERBOSE_LOG")) {
        std::fflush(stderr);
        saved_stderr = _dup(_fileno(stderr));
        if (saved_stderr != -1) {
            int nul = _open("NUL", _O_WRONLY);
            if (nul != -1) {
                _dup2(nul, _fileno(stderr));
                _close(nul);
            } else {
                _close(saved_stderr);
                saved_stderr = -1;
            }
        }
    }
#endif
    model_ = llama_model_load_from_file(gguf_path.c_str(), mparams);
#ifdef _WIN32
    if (saved_stderr != -1) {
        std::fflush(stderr);
        _dup2(saved_stderr, _fileno(stderr));
        _close(saved_stderr);
    }
#endif
    // AC1 锚点分支二：GGUF 损坏/加载失败 → stderr 含路径 + 非零退出。
    if (!model_) {
        std::fprintf(stderr, "模型加载失败（文件可能损坏）：%s\n", gguf_path.c_str());
        return false;
    }
    vocab_ = llama_model_get_vocab(model_);

    if (!RecreateContext()) return false;

    // Step 2 冒烟验证：透传原语自检（只验链路通，不定 flags 策略）。
    std::vector<llama_token> smoke;
    if (!Tokenize("ping", true, true, smoke)) {
        std::fprintf(stderr, "tokenize 冒烟验证失败：%s\n", gguf_path.c_str());
        return false;
    }
    return true;
}

bool EngineRuntime::EnsureCudaAvailable() {
    // C3：预编译包无 CUDA 后端（或驱动不满足 R570+ 基线）时返回 false。
    // P4：显式报错 + 非零退出，禁止静默回落 CPU。
    if (!llama_supports_gpu_offload()) {
        std::fprintf(stderr, "CUDA 不可用：当前 llama 构建不支持 GPU offload（需官方 win-cuda 包 + R570+ 驱动），已退出。\n");
        return false;
    }
    return true;
}

bool EngineRuntime::Tokenize(const std::string& text, bool add_special, bool parse_special,
                             std::vector<llama_token>& out_tokens) {
    if (!vocab_) return false;
    // 两遍式：先 NULL/0 取长度（返回负数），再分配再调一次（docs/03 §3.2）。
    const int need = -llama_tokenize(vocab_, text.c_str(), static_cast<int32_t>(text.size()), nullptr, 0,
                                     add_special, parse_special);
    if (need <= 0) {
        out_tokens.clear();
        return need == 0;  // 空串允许（0 token），负值异常
    }
    out_tokens.assign(static_cast<std::size_t>(need), 0);
    const int got = llama_tokenize(vocab_, text.c_str(), static_cast<int32_t>(text.size()),
                                   out_tokens.data(), need, add_special, parse_special);
    if (got < 0) {
        out_tokens.clear();
        return false;
    }
    out_tokens.resize(static_cast<std::size_t>(got));
    return true;
}

bool EngineRuntime::RecreateContext() {
    // MVP 简化：每轮翻译重建 context，等价于清空 KV。
    // 为何不用 KV 清空 API：llama_kv_cache_clear / llama_memory_clear 在不同 Release
    // 间存在新旧两套形态，属版本敏感 API；重建 context 只用 init/from_model 基础原语，
    // 与 pin 版本无关，零假设可写（D2 最小闭环）。post-MVP 可换内存清空 API 提速。
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = kEngineNCtx;      // C4：8192
    cparams.n_batch = kEngineNBatch;  // Step 2：512
    cparams.n_ubatch = kEngineNBatch;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;  // 取不到拓扑时的保守回落
    cparams.n_threads = static_cast<int32_t>(hw);         // Step 2：硬件并发
    cparams.n_threads_batch = static_cast<int32_t>(hw);
    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        std::fprintf(stderr, "创建 llama 上下文失败（n_ctx=%d）：%s\n", kEngineNCtx, gguf_path_.c_str());
        return false;
    }
    return true;
}

bool EngineRuntime::Generate(const std::vector<llama_token>& prompt_tokens, llama_sampler* sampler,
                             std::string& out_text) {
    out_text.clear();
    if (!ctx_ || !vocab_ || !sampler) return false;
    if (prompt_tokens.empty()) return false;

    // U6 联合 fit：prompt + 预测上限不得超过 n_ctx（留 20% 余量待实测，此处先硬截断防溢出）。
    if (prompt_tokens.size() + static_cast<std::size_t>(kMaxPredictTokens) >
        static_cast<std::size_t>(kEngineNCtx)) {
        std::fprintf(stderr, "输入过长，超出上下文窗口（prompt tokens=%zu，n_ctx=%d），已忽略，请重新输入。\n",
                     prompt_tokens.size(), kEngineNCtx);
        return false;
    }

    if (!RecreateContext()) return false;  // 新一轮：清空 KV

    llama_batch_ext* batch = llama_batch_ext_init(ctx_);
    if (!batch) {
        std::fprintf(stderr, "创建 batch 失败\n");
        return false;
    }

    // —— prefill chunking（Step 2）：按 n_batch=512 分块顺序 llama_process ——
    const std::size_t n_prompt = prompt_tokens.size();
    const std::size_t n_chunks = (n_prompt + kEngineNBatch - 1) / kEngineNBatch;
    for (std::size_t c = 0; c < n_chunks; ++c) {
        const std::size_t begin = c * kEngineNBatch;
        const std::size_t len =
            (begin + kEngineNBatch <= n_prompt) ? kEngineNBatch : (n_prompt - begin);
        const bool is_last = (c + 1 == n_chunks);
        llama_batch_ext_clear(batch);
        for (std::size_t i = 0; i < len; ++i) {
            const int32_t idx =
                llama_batch_ext_add_token(batch, 0, prompt_tokens[begin + i]);
            const llama_pos pos = static_cast<llama_pos>(begin + i);
            llama_batch_ext_set_pos(batch, idx, &pos);
        }
        // 前 k-1 块 set_output_logits 全 false（默认即 false，不置位）；
        // 末块仅最后一个 token 置 true（只取末位置 logits 供首次采样）。
        if (is_last) llama_batch_ext_set_output_logits(batch, static_cast<int32_t>(len - 1), true);
        if (llama_process(ctx_, LLAMA_PROCESS_TYPE_DECODE, batch) != 0) {
            std::fprintf(stderr, "prefill 失败（chunk %zu/%zu）\n", c + 1, n_chunks);
            llama_batch_ext_free(batch);
            return false;
        }
    }

    // —— decode（batch=1，同一 context 不并发，Guardrails）——
    llama_pos n_past = static_cast<llama_pos>(n_prompt);
    for (int step = 0; step < kMaxPredictTokens; ++step) {
        const llama_token id = llama_sampler_sample(sampler, ctx_, -1);
        if (llama_vocab_is_eog(vocab_, id)) break;  // Guardrails：每次采样后判停
        char piece[256];
        const int n = llama_token_to_piece(vocab_, id, piece, sizeof(piece), 0, true);
        if (n > 0) out_text.append(piece, static_cast<std::size_t>(n));

        llama_batch_ext_clear(batch);
        const int32_t idx = llama_batch_ext_add_token(batch, 0, id);
        llama_batch_ext_set_pos(batch, idx, &n_past);
        llama_batch_ext_set_output_logits(batch, 0, true);
        if (llama_process(ctx_, LLAMA_PROCESS_TYPE_DECODE, batch) != 0) {
            std::fprintf(stderr, "decode 失败（step %d）\n", step);
            llama_batch_ext_free(batch);
            return false;
        }
        ++n_past;
    }

    llama_batch_ext_free(batch);
    return true;
}
