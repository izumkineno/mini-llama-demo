// main.cpp —— 装配入口：engine + prompt + loop（Step 2–4 装配）
//
// 对应计划条款：
//   - ralplan §3 进程边界契约：退出码 0 正常 / 1 通用失败 / 2 模板漂移 fail-fast；
//     0xC0000xxx 为 Windows fatal（crash 通道，非 main 返回值）。
//   - ralplan AC1：stderr 含 llama_version 行；GGUF 缺失/损坏 → exit≠0（engine 内实现）。
//   - Guardrails：头/DLL 同版本，发版日志打印 llama_version()。
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "engine.h"
#include "llama.h"
#include "loop.h"
#include "prompt_spec.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    // 仅输出码页 → UTF-8，修控制台中文乱码。不碰输入码页（计划否决
    // SetConsoleCP；stdin 仍走 NormalizeToUTF8 归一化）。重定向到管道/文件时
    // 本调用无影响（pytest 捕获的字节流不变）。
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 2) {
        std::fprintf(stderr, "用法：lai-translate.exe <gguf>\n");
        return 1;
    }
    const std::string gguf = argv[1];

    // AC1 / Guardrails：版本行 → stderr（含 "llama_version" 字面，供 pytest grep）。
    std::fprintf(stderr, "llama_version: %s\n", llama_version());

    EngineRuntime engine;
    if (!engine.Init(gguf)) return 1;            // GGUF 缺失/损坏 → stderr 含路径 + exit 1
    if (!engine.EnsureCudaAvailable()) return 1;  // C3：CUDA 不可用 → stderr + exit 1

    PromptManager prompts;
    int prompt_exit = 0;
    if (!prompts.Init(engine, prompt_exit)) {
        // B1 哈希 mismatch/模板不可用 → 1；B2 快照漂移 → 2（TEMPLATE_DRIFT 行已打）。
        return (prompt_exit == 2) ? 2 : 1;
    }

    return RunLoop(engine, prompts);  // 0 正常；1 翻译 infra 失败
}
