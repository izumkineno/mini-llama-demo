// prompt_spec.h —— PromptSpec 双产物 + 采样链（Step 3，编译时内嵌模板）
//
// 对应计划条款（内嵌修订版）：
//   - 模板来源：编译时内嵌（src/prompt/embedded.h），运行时零文件依赖；
//     prompts/ 目录仅作取证存档（META/jinja/README/pin），删改不影响 exe。
//   - 模板切分：kTemplatePre（47 B）+ {{USER}} + kTemplatePost（20 B）；
//     切分在源码侧完成，启动时仅做整体哈希自检。
//   - 两段式 tokenize：Tok(pre,T,T) + Tok(user,F,F) + Tok(post,F,T)，
//     首段 carry BOS，后续防重复。
//   - 启动哈希自检：拼装后整体 sha256 须等于 pin 值，mismatch → stderr + 返回 false。
//   - 采样链：penalties(64,1.10,0.0,0.0) → min_p(0.10,min_keep=1)
//     → temp(0.0) → greedy；top-k/top-p/dist/seed 不存在；非零 temperature 不存在；
//     temp0/greedy 有意冗余（禁止"优化"掉其一）。
//   - 进程边界契约：退出码 0/1；exit 2（B2 漂移）通道已随文件模板退役，永不触发。
//   - 退役说明：B1-文件/B2-快照双通道（文件存在性分流、TEMPLATE_DRIFT、
//     ResolvePromptsDir 多目录解析）已整体移除，见 git 历史。
#pragma once

#include <string>
#include <vector>

#include "llama.h"

namespace lai {

class EngineRuntime;

// 翻译方向：/zh=译成中文(en2zh)，/en=译成英文(zh2en)，默认 zh2en（C6）。
enum class Direction {
    Zh2En,  // 中→英
    En2Zh,  // 英→中
};

// Step 3 双产物：applied_text 供日志/排障；prompt_tokens 直接喂 prefill。
struct PromptArtifacts {
    std::string applied_text;
    std::vector<llama_token> prompt_tokens;
};

class PromptManager {
  public:
    PromptManager() = default;

    // 启动初始化（进 loop 前一次性）：装载内嵌模板 + 整体哈希自检。
    // 自检失败（内嵌常量被误改）→ stderr + 返回 false，main 直接 exit 1。
    // 成功返回 true。本函数不触碰文件系统。
    bool Init(EngineRuntime& engine);

    // 正式 prompt 构造（输入已归一化、已计数、≤4000 scalar）：
    // 内嵌模板纯拼装 + 两段式 tokenize。
    bool BuildPrompt(Direction dir, const std::string& user_utf8, PromptArtifacts& out) const;

  private:
    EngineRuntime* engine_ = nullptr;
    std::string template_pre_;   // kTemplatePre 拷贝（{{USER}} 之前，47 B）
    std::string template_post_;  // kTemplatePost 拷贝（{{USER}} 之后，20 B）
};

// 采样链工厂（构造顺序锁死，编译时 pin；值与 prompts/sampling.pin.json 存档一致，
// status="待调初值"）：
//   penalties(64, 1.10, 0.0, 0.0) → min_p(0.10, min_keep=1) → temp(0.0) → greedy。
// 返回链式采样器（所有权归调用方，用 llama_sampler_free 释放；链内 sampler 所有权
// 归链，勿单独 free）。失败返回 nullptr。
// 注意：top-k/top-p/dist/seed 在 MVP 内不存在；temp(0.0)+greedy 为有意冗余，
// 禁止"优化"掉其一（移除须 P0 特批）。
llama_sampler* CreateSamplerChain(int32_t n_vocab);  // n_vocab=词表大小（b11669 起 penalties 首参）；取自 llama_vocab_n_tokens(engine.Vocab())

}  // namespace lai
