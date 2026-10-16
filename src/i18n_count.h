// i18n_count.h —— stdin 编码归一化 + Unicode scalar 计数（Step 4）
//
// 对应计划条款：
//   - ralplan §Step 4：NormalizeToUTF8（严格 UTF-8 校验 → 直用；失败 →
//     MultiByteToWideChar(CP_ACP) → WideCharToMultiByte(CP_UTF8)；仍失败 →
//     stderr + 留循环）；否决 SetConsoleCP(CP_UTF8)。
//   - ralplan §Step 4 scalar 计数点：getline 去 \n 后再去一尾 \r；行首 UTF-8
//     BOM（EF BB BF）剥离后计数；归一化后按 Unicode scalar 计；ZWJ 按 scalar 计。
//   - Guardrails：stdin 原始字节先归一化为 UTF-8，归一化后计数；上限 4000。
#pragma once

#include <cstddef>
#include <string>

// AC4 口径：单行输入上限（Unicode scalar，归一化后计数）。恰 4000 放行，4001 拒绝。
inline constexpr std::size_t kMaxInputScalars = 4000;

// 严格 UTF-8 校验：拒绝过长编码、代理区、>U+10FFFF、残缺序列。
bool IsValidUtf8(const std::string& s);

// stdin 归一化：合法 UTF-8 直用；否则按系统 ANSI 码页转码。
// 成功返回 true（out_utf8 为合法 UTF-8）；仍失败返回 false，调用方打 stderr + 留循环。
// 注意：禁止 SetConsoleCP 方案（见计划 Step 4），此处只做数据层转码，不动控制台码页。
bool NormalizeToUTF8(const std::string& raw, std::string& out_utf8);

// 按 Unicode scalar 计数（= 非续字节数；要求输入已是合法 UTF-8）。
// ZWJ（U+200D）是一个独立 scalar，自然计为 1，符合 AC4 口径。
std::size_t CountScalars(const std::string& utf8);

// getline 去 \n 后再去一尾 \r（\r\n 计 0，符合 Step 4）。
void StripTrailingCR(std::string& line);

// 剥行首一个 UTF-8 BOM（EF BB BF），剥离后计数。
void StripLeadingBOM(std::string& line);
