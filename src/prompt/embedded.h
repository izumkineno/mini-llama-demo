// embedded.h —— 编译时内嵌模板（B1，Hy-MT2）
//
// 本文件是模板的唯一运行时来源：构建时直接编入二进制，运行时零文件依赖。
// prompts/ 目录仅作取证存档（META/jinja/README/pin），删改不影响 exe 行为。
//
// 来源：prompts/hy-mt2-v20261016.txt（75 字节 = pre 47 + {{USER}} 8 + post 20，
//   UTF-8 无 BOM 无尾换行）+ hy-mt2-v20261016.txt.sha256。
// 渲染决策见 prompts/hy-mt2-META.md（单轮单 user、无 system，add_generation_prompt=True）。
//
// 人读形态（与下面字节串逐字节等价）：
//   pre  = <｜hy_begin▁of▁sentence｜><｜hy_User｜>
//   post = <｜hy_Assistant｜>
// 其中 ｜=U+FF5C（EF BD 9C），▁=U+2581（E2 96 81）。
// 非 ASCII 字节一律用 \x 转义，保证与源文件编码设置无关的字节精确；
// 每个 \x 转义后紧跟的字符均非 hex digit，不存在 MSVC 贪婪 \x 解析风险。
//
// 更新流程（换模板版本）：同步改 kTemplateVersion/kTemplateSha256/kTemplatePre/
// kTemplatePost + 下面三处 static_assert 字节数 → 重新构建 → 启动自检会复核哈希。
#pragma once

namespace lai {
namespace embedded {

// 文件名主干（原 prompts/hy-mt2-vX.txt 的版本标识，日志/grep 用）。
inline constexpr char kTemplateVersion[] = "hy-mt2-v20261016";

// 内嵌模板整体 sha256（pre + "{{USER}}" + post 拼装后的哈希，== 原 .sha256 首字段）。
inline constexpr char kTemplateSha256[] =
    "a06006c7c6b1cfff2f024588eb09b01d5d4c04190ddc3923e1e41c928cef277d";

// {{USER}} 占位符（拼装/哈希自检用；模板切分已在源码侧完成，此处仅作常量）。
inline constexpr char kSlot[] = "{{USER}}";

// kTemplatePre：{{USER}} 之前全部字节（含 BOS 控制串），47 字节。
// tokenize flags：add_special=true, parse_special=true（携带 BOS + 解析模板控制 token）。
inline constexpr char kTemplatePre[] =
    "<\xEF\xBD\x9Chy_begin\xE2\x96\x81of\xE2\x96\x81sentence\xEF\xBD\x9C>"
    "<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";

// kTemplatePost：{{USER}} 之后全部字节（assistant 起始控制符），20 字节。
// tokenize flags：add_special=false, parse_special=true（防重复 BOS + 模板控制 token 生效）。
inline constexpr char kTemplatePost[] = "<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

}  // namespace embedded
}  // namespace lai

static_assert(sizeof(lai::embedded::kTemplatePre) == 48u, "kTemplatePre 须为 47 字节 + NUL");
static_assert(sizeof(lai::embedded::kTemplatePost) == 21u, "kTemplatePost 须为 20 字节 + NUL");
static_assert(sizeof(lai::embedded::kSlot) == 9u, "kSlot 须为 {{USER}} 8 字节 + NUL");
