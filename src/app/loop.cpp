// loop.cpp —— ConsoleLoop REPL 实现（Step 4）
// 对应计划条款同 loop.h。
#include "app/loop.h"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

#include "core/engine.h"
#include "app/i18n_count.h"
#include "prompt/prompt_spec.h"

namespace lai {
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
    std::ios::sync_with_stdio(false);  // stdout/stderr 分流：翻译结果→cout，提示/报错→cerr

    // 采样链：进程内建一次，每轮翻译前 sampler_reset（防跨轮惩罚串扰）。
    // （采样链构造见 prompt/prompt_spec.h CreateSamplerChain）
    llama_sampler* sampler =
        CreateSamplerChain(llama_vocab_n_tokens(engine.Vocab()));
    if (!sampler) {
        std::fprintf(stderr, "采样链创建失败\n");
        return 1;
    }

    Direction dir = Direction::Zh2En;  // D1：默认中→英
    std::string line;
    for (;;) {
        std::fprintf(stderr, "> ");  // 提示符→stderr（默认文案，P2 待转正）
        std::fflush(stderr);
        if (!std::getline(std::cin, line)) {
            llama_sampler_free(sampler);
            return 0;  // EOF（含首行/中段）：退出 0
        }
        StripTrailingCR(line);  // \r\n 计 0：去 \n 后再去一尾 \r
        StripLeadingBOM(line);  // 行首 UTF-8 BOM 剥离后计数
        if (line.empty()) continue;

        if (line[0] == '/') {
            if (EqualsIgnoreCaseAscii(line, "/quit") || EqualsIgnoreCaseAscii(line, "/exit")) {
                llama_sampler_free(sampler);
                return 0;
            }
            if (EqualsIgnoreCaseAscii(line, "/en")) {
                dir = Direction::Zh2En;
                continue;
            }
            if (EqualsIgnoreCaseAscii(line, "/zh")) {
                dir = Direction::En2Zh;
                continue;
            }
            std::fprintf(stderr, "未知命令：%s（可用：/en /zh /quit /exit）\n", line.c_str());
            continue;  // 未知命令：留循环
        }

        // 编码归一化：原始字节先归一化为 UTF-8，失败→stderr + 留循环。
        std::string norm;
        if (!NormalizeToUTF8(line, norm)) {
            std::fprintf(stderr, "输入编码无法识别，已忽略，请重新输入。\n");
            continue;
        }
        // 归一化后按 scalar 计数；超限→stderr 超限行 + 留循环（恰 4000 放行）。
        if (CountScalars(norm) > kMaxInputScalars) {
            std::fprintf(stderr, "输入超限（>4000字符），已忽略，请重新输入。\n");
            continue;
        }

        PromptArtifacts pa;
        if (!prompts.BuildPrompt(dir, norm, pa)) {
            std::fprintf(stderr, "prompt 构造失败\n");
            llama_sampler_free(sampler);
            return 1;  // P4：翻译 infra 失败 fail-fast
        }
        // U6 前置守卫：prompt 余量不足以容纳一轮预测 → 视为输入过长，留循环。
        if (pa.prompt_tokens.size() + static_cast<std::size_t>(kMaxPredictTokens) >
            static_cast<std::size_t>(engine.ContextSize())) {
            std::fprintf(stderr, "输入过长，超出上下文窗口，已忽略，请重新输入。\n");
            continue;
        }

        llama_sampler_reset(sampler);
        std::string out;
        if (!engine.Generate(pa.prompt_tokens, sampler, out)) {
            std::fprintf(stderr, "生成失败\n");
            llama_sampler_free(sampler);
            return 1;  // P4：翻译 infra 失败 fail-fast
        }
        std::cout << out << "\n";  // 翻译结果→stdout（唯一 stdout 载荷）
        std::cout.flush();
    }
}

}  // namespace lai
