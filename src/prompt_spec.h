// prompt_spec.h —— PromptSpec 双产物 + 采样链（Step 3）
//
// 对应计划条款：
//   - ralplan §Step 3 PromptSpec 输出形态：双产物
//     PromptArtifacts{applied_text, prompt_tokens}，签名
//     BuildPrompt(Direction, user_utf8)（输入已归一化、已计数、≤4000）。
//   - ralplan §Step 3 模板切分（B1 专用）：prompts/hy-mt2-vX.txt 按 {{USER}} 切为
//     kTemplatePre + kTemplatePost（段表见 prompt_spec.cpp 头注释）。
//   - ralplan §Step 3 两段式 tokenize（B1 专用）：
//     Tok(pre,T,T) + Tok(user,F,F) + Tok(post,F,T)，首段 carry BOS，后续防重复。
//   - ralplan §Step 3 启动哈希校验（B1 专用）：mismatch → stderr + exit 1。
//   - ralplan §Step 3 启动一次性快照回归（B2 专用，v4 M1 方案 b）：双探针
//     zh2en"你好，世界" / en2zh"Hello, world"，L1/L2 双层合取，mismatch →
//     TEMPLATE_DRIFT 行 + exit 2；禁止逐轮触发。
//   - ralplan §Step 3 采样链：penalties(64,1.10,0.0,0.0) → min_p(0.10,min_keep=1)
//     → temp(0.0) → greedy；top-k/top-p/dist/seed 不存在；非零 temperature 不存在；
//     temp0/greedy 有意冗余（禁止"优化"掉其一）。
//   - ralplan §3 进程边界契约：退出码 0/1/2；B1 漂移走 exit 1，B2 漂移走 exit 2。
#pragma once

#include <string>
#include <vector>

#include "llama.h"

class EngineRuntime;

// 翻译方向：/zh=译成中文(en2zh)，/en=译成英文(zh2en)，默认 zh2en（C6）。
enum class Direction {
    Zh2En,  // 中→英
    En2Zh,  // 英→中
};

// 模板来源：B1=三件套文件 pin；B2=模型内嵌模板官方路径（apply_template(NULL)）。
enum class TemplateMode {
    B1_File,
    B2_Builtin,
};

// Step 3 双产物：applied_text 供日志/L1 比对/排障；prompt_tokens 直接喂 prefill。
struct PromptArtifacts {
    std::string applied_text;
    std::vector<llama_token> prompt_tokens;
};

class PromptManager {
  public:
    PromptManager() = default;

    // 启动初始化（进 loop 前一次性）：
    //   B1/B2 按文件存在性分流：prompts/hy-mt2-v*.txt 存在走 B1，否则若快照基线
    //   （applied_probe.txt + applied_probe.tokens.json）存在走 B2，两者都不满足
    //   → stderr + exit 1。
    //   B1：模板哈希校验，mismatch → stderr + exit 1（本通道恒不 exit 2）。
    //   B2：启动一次性快照回归（L1/L2 双层合取），mismatch → TEMPLATE_DRIFT 行 +
    //   exit 2；基线缺失/损坏 → stderr + exit 1。
    // 返回 true=就绪；false=失败，exit_code 置 1 或 2，main 直接返回该码。
    bool Init(EngineRuntime& engine, int& exit_code);

    // 正式 prompt 构造（输入已归一化、已计数、≤4000 scalar）。
    // B1：模板切分 + 两段式；B2：apply_template(NULL) + 整串 tokenize。
    bool BuildPrompt(Direction dir, const std::string& user_utf8, PromptArtifacts& out);

    TemplateMode Mode() const { return mode_; }
    const std::string& TemplatePath() const { return template_path_; }

  private:
    TemplateMode mode_ = TemplateMode::B1_File;
    EngineRuntime* engine_ = nullptr;
    std::string template_path_;  // B1：实际命中的模板文件（含版本号的 vX 实例）
    std::string template_pre_;   // B1：kTemplatePre（{{USER}} 之前）
    std::string template_post_;  // B1：kTemplatePost（{{USER}} 之后）
};

// 采样链工厂（B1/B2 通用，与模板来源无关）：
//   penalties(64, 1.10, 0.0, 0.0) → min_p(0.10, min_keep=1) → temp(0.0) → greedy。
// 参数 pin 见 prompts/sampling.pin.json（status="待调初值"）。
// 返回链式采样器（所有权归调用方，用 llama_sampler_free 释放；链内 sampler 所有权
// 归链，勿单独 free）。失败返回 nullptr。
// 注意：top-k/top-p/dist/seed 在 MVP 内不存在；temp(0.0)+greedy 为有意冗余，
// 禁止"优化"掉其一（移除须 P0 特批）。
llama_sampler* CreateSamplerChain(int32_t n_vocab);  // n_vocab=词表大小（b11669 起 penalties 首参）；取自 llama_vocab_n_tokens(engine.Vocab())
