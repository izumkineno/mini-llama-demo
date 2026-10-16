// loop.cpp —— ConsoleLoop REPL 实现（Step 4）
// 对应计划条款同 loop.h。
#include "loop.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

#include "engine.h"
#include "i18n_count.h"
#include "prompt_spec.h"

namespace {

// ASCII 大小写不敏感比较（命令词均为 ASCII）。
bool EqualsIgnoreCaseAscii(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            static_cast<unsigned char>(b[i])) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

}  // namespace

int RunLoop(EngineRuntime& engine, PromptManager& prompts) {
    // 采样器：loop 生命周期内复用，每轮翻译前 llama_sampler_reset 清 penalties 历史，
    // 防跨轮惩罚串扰（B1/B2 通用链，构造见 prompt_spec CreateSamplerChain）。
    llama_sampler* sampler = CreateSamplerChain(llama_vocab_n_tokens(engine.Vocab()));
    if (!sampler) {
        std::fprintf(stderr, "采样链创建失败\n");
        return 1;
    }

    Direction dir = Direction::Zh2En;  // C6/D1：默认中→英
    std::string raw;
    for (;;) {
        std::fprintf(stderr, "> ");  // P2 默认文案：提示符 → stderr
        std::fflush(stderr);

        if (!std::getline(std::cin, raw)) {
            // EOF（含循环中段 EOF）→ 优雅退出 0（D1）。
            llama_sampler_free(sampler);
            return 0;
        }
        StripTrailingCR(raw);   // 去一尾 \r（\r\n 计 0）
        StripLeadingBOM(raw);   // 剥行首 BOM 后计数

        // stdin 编码归一化（Step 4）：失败 → stderr + 留循环。
        // 文案为实现侧补齐（计划未 pin 此行）：
        std::string user;
        if (!NormalizeToUTF8(raw, user)) {
            std::fprintf(stderr, "输入编码无法识别，已忽略，请重新输入。\n");
            continue;
        }

        if (user.empty()) continue;  // 空行忽略（D1）

        // 命令（大小写不敏感；P1 待转正双别名）。
        if (EqualsIgnoreCaseAscii(user, "/en")) {
            dir = Direction::Zh2En;
            continue;
        }
        if (EqualsIgnoreCaseAscii(user, "/zh")) {
            dir = Direction::En2Zh;
            continue;
        }
        if (EqualsIgnoreCaseAscii(user, "/quit") || EqualsIgnoreCaseAscii(user, "/exit")) {
            llama_sampler_free(sampler);
            return 0;
        }
        if (!user.empty() && user[0] == '/') {
            // P2 默认文案：未知命令 → stderr + 留循环。
            std::fprintf(stderr, "未知命令：%s（可用：/en /zh /quit /exit）\n", user.c_str());
            continue;
        }

        // 超限检查：归一化后 scalar 计数；恰 4000 放行，4001 拒绝 + 留循环。
        if (CountScalars(user) > kMaxInputScalars) {
            // P2 默认文案 → stderr。
            std::fprintf(stderr, "输入超限（>4000字符），已忽略，请重新输入。\n");
            continue;
        }

        PromptArtifacts artifacts;
        if (!prompts.BuildPrompt(dir, user, artifacts)) {
            std::fprintf(stderr, "prompt 构造失败\n");
            llama_sampler_free(sampler);
            return 1;  // infra 失败 fail-fast（P4），非留循环
        }

        // U6 联合 fit 前置检查（输入相关失败 → 留循环，非 exit 1）：
        // prompt tokens + 预测上限不得超过 n_ctx。4000 emoji 等高 fertility 输入可能
        // 触发此分支——注意它与 AC4 超限文案不同（4000 scalar 已放行，此处是 token 窗口）。
        // 文案为实现侧补齐（计划未 pin 此行）；Generate 内保留二次兜底。
        if (artifacts.prompt_tokens.size() + static_cast<std::size_t>(kMaxPredictTokens) >
            static_cast<std::size_t>(engine.ContextSize())) {
            std::fprintf(stderr, "输入过长，超出上下文窗口，已忽略，请重新输入。\n");
            continue;
        }

        llama_sampler_reset(sampler);  // 新一轮：清采样器历史
        std::string out;
        if (!engine.Generate(artifacts.prompt_tokens, sampler, out)) {
            std::fprintf(stderr, "生成失败\n");
            llama_sampler_free(sampler);
            return 1;  // prefill/decode infra 失败 fail-fast（P4）
        }
        // 翻译输出 → stdout（通道分离：提示/报错→stderr）。
        std::fwrite(out.c_str(), 1, out.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
}
