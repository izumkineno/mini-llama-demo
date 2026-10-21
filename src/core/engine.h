// engine.h —— EngineRuntime：模型加载 + 上下文 + 解码（Step 2）
//
// 对应计划条款：
//   - ralplan §Step 2：backend_init → load_from_file(n_gpu_layers=99) →
//     init_from_model(n_ctx=8192)；llama_log_set；CUDA 不可用非零退出；
//     n_threads=硬件并发，n_batch=512。
//   - ralplan §Step 2 n_batch=512 prefill chunking：prompt tokens 按 512 分块顺序
//     llama_process，前 k-1 块 set_output_logits 全 false，末块仅最后一个 token 置
//     true；decode batch=1。
//   - ralplan §Step 2 tokenize 职责切分：本类仅提供 Tokenize 透传原语 + 冒烟验证；
//     正式 prompt token 化 flags 策略归 PromptSpec（Step 3）所有。
//   - ralplan §Step 2 AC1 锚点：GGUF 缺失/损坏两分支各"stderr 含路径 + exit≠0"。
//   - Guardrails：只经 llama.h C 接口，不跨 DLL 边界传 C++ 对象；同一 context 的
//     decode 循环不并发；CUDA 不可用报错退出（非零），不做 CPU 静默兜底。
#pragma once

#include <string>
#include <vector>

#include "llama.h"  // C 接口唯一来源；过 DLL 边界的只有 C 类型/llama.h 结构体
                    // （本侧的 string/vector 不出本 translation unit 边界，docs/03 §6.1）

namespace lai {

// Step 2 pin：n_ctx=8192（C4），n_batch=512，n_gpu_layers=99（C3 全 offload）。
inline constexpr int kEngineNCtx = 8192;
inline constexpr int kEngineNBatch = 512;
inline constexpr int kEngineNGpuLayers = 99;
// MVP 每轮解码上限（防无 EOG 长尾；U6 容量余量待 Step 2 实测复核，此处取保守值）。
inline constexpr int kMaxPredictTokens = 1024;

class EngineRuntime {
  public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    // 初始化：backend_init → load_from_file(99) → init_from_model(8192)。
    // 失败时已向 stderr 打印（含 gguf 路径，AC1 锚点），调用方直接 exit 1。
    bool Init(const std::string& gguf_path);

    // C3：CUDA 可用性检查。不可用 → stderr + 返回 false（main exit 1），
    // 禁止静默回落 CPU。
    bool EnsureCudaAvailable();

    // Step 2 透传原语：原样透传 llama_tokenize，不做任何 flags 策略。
    // 正式 flags 策略归 PromptSpec（内嵌模板两段式）所有。
    bool Tokenize(const std::string& text, bool add_special, bool parse_special,
                  std::vector<llama_token>& out_tokens);

    // 单轮生成：每轮先重建 context（清空 KV，见实现注释）→ prefill chunking →
    // decode batch=1 → 每次采样后 llama_vocab_is_eog 判停。
    // sampler 由调用方经 prompt/prompt_spec.h CreateSamplerChain 创建并持有。
    // 成功返回 true；失败已打 stderr，调用方按 P4 fail-fast（exit 1）。
    bool Generate(const std::vector<llama_token>& prompt_tokens, llama_sampler* sampler, std::string& out_text);

    const llama_vocab* Vocab() const { return vocab_; }
    int ContextSize() const { return kEngineNCtx; }

  private:
    // 每轮翻译前重建 context，等价于清空 KV（MVP 简化，见 .cpp 注释）。
    bool RecreateContext();

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::string gguf_path_;  // 留存用于重建 context 与报错（含路径要求）
};

}  // namespace lai
