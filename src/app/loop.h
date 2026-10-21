// loop.h —— ConsoleLoop REPL（Step 4）
//
// 对应计划条款：
//   - ralplan §Step 4 命令：/en、/zh 大小写不敏感；/quit、/exit 退出 0；空行忽略；
//     EOF 退出 0（含中段）；未知命令 stderr 留循环；翻译→stdout、提示/报错→stderr。
//   - ralplan §Step 4 超限：>4000 scalar → stderr 超限行 + 留循环（恰 4000 放行）。
//   - ralplan §默认文案（P2 待转正）：提示符 "> "→stderr；超限
//     "输入超限（>4000字符），已忽略，请重新输入。"→stderr；未知命令
//     "未知命令：%s（可用：/en /zh /quit /exit）"→stderr。
//   - ralplan O-D（D1）：默认中→英；文案待用户转正。
#pragma once

namespace lai {

class EngineRuntime;
class PromptManager;

// 运行 REPL，返回进程退出码：
//   0=正常（/quit、/exit、首行/中段 EOF）；1=翻译 infra 失败（fail-fast，P4）。
// 超限/未知命令/编码失败均为"留循环"（不返回）。
int RunLoop(EngineRuntime& engine, PromptManager& prompts);

}  // namespace lai
