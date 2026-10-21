// prompt_spec.cpp —— PromptSpec 双产物 + 采样链实现（Step 3，编译时内嵌模板）
// 对应计划条款同 prompt_spec.h。
#include "prompt/prompt_spec.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "core/engine.h"
#include "prompt/embedded.h"
#include "util/sha256.h"

namespace lai {
namespace {

// —— 小工具 ——

std::string ToLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool PromptManager::Init(EngineRuntime& engine) {
    engine_ = &engine;
    template_pre_ = embedded::kTemplatePre;
    template_post_ = embedded::kTemplatePost;

    // 启动哈希自检：内嵌拼装体整体 sha256 须等于 pin 值（原 .sha256 首字段）。
    // 通过 → stderr 模板就绪行（含 B1/哈希校验通过锚点词，日志断言依赖）。
    const std::string full = template_pre_ + embedded::kSlot + template_post_;
    const std::string actual = util::Sha256Hex(full.data(), full.size());
    if (ToLowerAscii(actual) != ToLowerAscii(embedded::kTemplateSha256)) {
        std::fprintf(stderr, "内嵌模板哈希不一致（常量可能被误改）：%s\n",
                     embedded::kTemplateVersion);
        return false;
    }
    std::fprintf(stderr, "模板：%s（B1 内嵌，哈希校验通过）\n", embedded::kTemplateVersion);
    return true;
}

bool PromptManager::BuildPrompt(Direction dir, const std::string& user_utf8, PromptArtifacts& out) const
{
    out.applied_text.clear();
    out.prompt_tokens.clear();
    if (!engine_ || user_utf8.empty()) return false;

    // 方向指令后缀（B1 两段式：方向判定的唯一信息载体；模板本体无方向字段）。
    const char* zh2en_suffix =
        "\nTranslate the above Chinese text into English. Output only the English translation, without any explanations or extra text.";
    const char* en2zh_suffix =
        "\nTranslate the above English text into Chinese. Output only the Chinese translation, without any explanations or extra text.";
    const char* suffix = nullptr;
    switch (dir) {
        case Direction::Zh2En: suffix = zh2en_suffix; break;
        case Direction::En2Zh: suffix = en2zh_suffix; break;
        default:
            std::fprintf(stderr, "内部错误：非法翻译方向\n");
            return false;
    }

    // 双产物拼装：applied_text（日志/排障）与 prompt_tokens（喂 prefill）来自同一纯拼装体。
    const std::string full = template_pre_ + std::string("User input:\n") + user_utf8 + suffix + template_post_;
    out.applied_text = full;

    // 两段式 tokenize：Tok(pre,T,T) + Tok(user,F,F) + Tok(post,F,T) 直接拼接。
    // 方向指令后缀并入用户段：纯拼装（无模板转义）+ F,F（防把指令词切成特殊 token）。
    const std::string seg_user = std::string("User input:\n") + user_utf8 + suffix;
    std::vector<llama_token> tok_pre, tok_user, tok_post;
    if (!engine_->Tokenize(template_pre_, true, true, tok_pre) ||
        !engine_->Tokenize(seg_user, false, false, tok_user) ||
        !engine_->Tokenize(template_post_, false, true, tok_post)) {
        return false;
    }
    out.prompt_tokens.reserve(tok_pre.size() + tok_user.size() + tok_post.size());
    out.prompt_tokens.insert(out.prompt_tokens.end(), tok_pre.begin(), tok_pre.end());
    out.prompt_tokens.insert(out.prompt_tokens.end(), tok_user.begin(), tok_user.end());
    out.prompt_tokens.insert(out.prompt_tokens.end(), tok_post.begin(), tok_post.end());
    return !out.prompt_tokens.empty();
}

llama_sampler* CreateSamplerChain(int32_t n_vocab) {
    // llama_sampler_chain_init(params) —— 构造顺序锁死（编译时 pin）：
    // penalties(64, 1.10, 0.0, 0.0) → min_p(0.10, min_keep=1) → temp(0.0) → greedy。
    llama_sampler_chain_params cparams = llama_sampler_chain_default_params();
    llama_sampler* chain = llama_sampler_chain_init(cparams);
    if (!chain) return nullptr;

    llama_sampler* pen = llama_sampler_init_penalties(n_vocab, 64, 1.10f, 0.0f, 0.0f);
    llama_sampler* mp = llama_sampler_init_min_p(0.10f, 1);
    llama_sampler* temp = llama_sampler_init_temp(0.0f);
    llama_sampler* greedy = llama_sampler_init_greedy();
    if (!pen || !mp || !temp || !greedy) {
        if (pen) llama_sampler_free(pen);
        if (mp) llama_sampler_free(mp);
        if (temp) llama_sampler_free(temp);
        if (greedy) llama_sampler_free(greedy);
        llama_sampler_free(chain);
        return nullptr;
    }
    // 所有权移交链：此后只 free(chain)，链内 sampler 由链释放，勿单独 free。
    llama_sampler_chain_add(chain, pen);
    llama_sampler_chain_add(chain, mp);
    llama_sampler_chain_add(chain, temp);
    llama_sampler_chain_add(chain, greedy);
    return chain;
}

}  // namespace lai
