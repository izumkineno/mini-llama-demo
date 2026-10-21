// main.cpp —— 装配入口：参数解析 → llama_version 锚点 → CUDA 门禁 →
// PromptManager::Init → RunLoop。退出码：0 正常 / 1 失败（fail-fast，P4）。
// 用法：lai-translate.exe <gguf>；无参/mode 文件等直接 exit 1（prompt 构造策略
// 已删繁就简，编译时内嵌唯一模板，无模式分流）。
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/engine.h"
#include "llama.h"
#include "app/loop.h"
#include "prompt/prompt_spec.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    // 控制台 UTF-8 输出：中文 stderr/译文 stdout 不乱码（只影响本进程输出码页，
    // 与被否决的 SetConsoleCP 输入方案无关——输入走 i18n_count 数据层转码）。
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc != 2) {
        std::fprintf(stderr, "用法：lai-translate.exe <gguf>\n");
        return 1;
    }
    const std::string gguf = argv[1];

    // llama_version 锚点行：AC1/AC6 日志断言依赖（经日志回调放行，不受静默影响）。
    const char* ver = llama_version();
    std::fprintf(stderr, "llama_version: %s\n", ver ? ver : "unknown");

    lai::EngineRuntime engine;
    if (!engine.EnsureCudaAvailable()) return 1;  // C3：无 CUDA 直接 exit 1
    if (!engine.Init(gguf)) return 1;             // AC1：缺失/损坏已打 stderr

    lai::PromptManager prompts;
    if (!prompts.Init(engine)) return 1;  // 内嵌模板哈希自检失败 → exit 1

    return lai::RunLoop(engine, prompts);
}
