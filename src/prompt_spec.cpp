// prompt_spec.cpp —— PromptSpec + 采样链实现（Step 3）
// 对应计划条款同 prompt_spec.h。
//
// —— B1 模板段表（MVP N=1，{{USER}} 占位符 exactly 一个，见 Step 1 落盘校验）——
//   段 0 kTemplatePre ：模板文件 {{USER}} 之前全部字节（含 BOS 控制串，如有）。
//                     tokenize flags：add_special=true, parse_special=true
//                     （携带 BOS + 解析模板控制 token）。
//   段 1 user_utf8   ：用户输入（已归一化 UTF-8，≤4000 scalar）。
//                     tokenize flags：add_special=false, parse_special=false
//                     （防重复 BOS + 用户特殊串不解析 = 防注入）。
//   段 2 kTemplatePost：模板文件 {{USER}} 之后全部字节。
//                     tokenize flags：add_special=false, parse_special=true
//                     （防重复 BOS + 模板控制 token 生效）。
// applied_text = pre + user + post 纯拼装（不转义）；prompt_tokens = 三段按序拼接。
// 双断言：模板控制 token 生效 + 用户特殊串不解析。
#include "prompt_spec.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "engine.h"

namespace {

// prompts/ 目录解析（post-MVP 修：不再只认 CWD，exe 任意目录启动可用）。
// 顺序：① CWD/prompts（仓库根启动 + pytest，最高优先，可覆盖）
//       ② exe 同级 prompts/（便携布局）③ exe 上级 prompts/（build/ 内启动）。
// 返回首个"存在且内有 hy-mt2-v*.txt 或 B2 快照"的目录，否则回退 "prompts"（保住旧报错文案）。
std::string ResolvePromptsDir() {
    namespace fs = std::filesystem;
    std::vector<std::string> roots;
    roots.emplace_back("prompts");
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, wbuf, static_cast<DWORD>(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n > 0) {
        std::error_code ec;
        fs::path exe(wbuf);
        if (exe.has_parent_path()) {
            fs::path dir = exe.parent_path();
            roots.push_back((dir / "prompts").string());
            if (dir.has_parent_path()) {
                roots.push_back((dir.parent_path() / "prompts").string());
            }
        }
        (void)ec;
    }
#endif
    std::error_code ec;
    for (const auto& r : roots) {
        if (!fs::is_directory(r, ec)) continue;
        bool usable = fs::exists(fs::path(r) / "applied_probe.txt", ec) &&
                      fs::exists(fs::path(r) / "applied_probe.tokens.json", ec);
        if (!usable) {
            for (const auto& e : fs::directory_iterator(r, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string name = e.path().filename().string();
                if (name.size() > 12 && name.compare(0, 8, "hy-mt2-v") == 0 &&
                    name.compare(name.size() - 4, 4, ".txt") == 0) {
                    usable = true;
                    break;
                }
            }
        }
        if (usable) return r;
    }
    return roots.front();  // 都不可用：回退 CWD/prompts，走原有"模板不可用"报错
}

// —— 小工具 ——

bool ReadFileBytes(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string ToLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// —— 紧凑 SHA256（公共域算法，零外部依赖）——
// 用途：B1 模板哈希校验；B2 快照回归 L1/L2 的 sha8 展示。
struct Sha256Ctx {
    std::uint32_t h[8];
    std::uint64_t total = 0;
    unsigned char buf[64] = {};
    std::size_t buflen = 0;
};

constexpr std::uint32_t kShaK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static std::uint32_t RoR(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Sha256Block(Sha256Ctx& c, const unsigned char* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
               (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = RoR(w[i - 15], 7) ^ RoR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = RoR(w[i - 2], 17) ^ RoR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t v[8];
    for (int i = 0; i < 8; ++i) v[i] = c.h[i];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = RoR(v[4], 6) ^ RoR(v[4], 11) ^ RoR(v[4], 25);
        const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + ch + kShaK[i] + w[i];
        const std::uint32_t s0 = RoR(v[0], 2) ^ RoR(v[0], 13) ^ RoR(v[0], 22);
        const std::uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = s0 + mj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) c.h[i] += v[i];
}

void Sha256Init(Sha256Ctx& c) {
    c.h[0] = 0x6a09e667;
    c.h[1] = 0xbb67ae85;
    c.h[2] = 0x3c6ef372;
    c.h[3] = 0xa54ff53a;
    c.h[4] = 0x510e527f;
    c.h[5] = 0x9b05688c;
    c.h[6] = 0x1f83d9ab;
    c.h[7] = 0x5be0cd19;
    c.total = 0;
    c.buflen = 0;
}

void Sha256Update(Sha256Ctx& c, const void* data, std::size_t len) {
    const auto* p = static_cast<const unsigned char*>(data);
    c.total += len;
    while (len > 0) {
        const std::size_t take = (std::min<std::size_t>)(64 - c.buflen, len);
        for (std::size_t i = 0; i < take; ++i) c.buf[c.buflen++] = *p++;
        len -= take;
        if (c.buflen == 64) {
            Sha256Block(c, c.buf);
            c.buflen = 0;
        }
    }
}

void Sha256Final(Sha256Ctx& c, unsigned char digest[32]) {
    const std::uint64_t bitlen = c.total * 8;
    unsigned char pad = 0x80;
    Sha256Update(c, &pad, 1);
    pad = 0x00;
    while (c.buflen != 56) Sha256Update(c, &pad, 1);
    unsigned char lenbytes[8];
    for (int i = 0; i < 8; ++i) lenbytes[i] = static_cast<unsigned char>(bitlen >> (56 - 8 * i));
    // 直接压剩余块（此时 buflen==56，追加 8 字节恰满一块）
    for (int i = 0; i < 8; ++i) c.buf[c.buflen++] = lenbytes[i];
    Sha256Block(c, c.buf);
    for (int i = 0; i < 8; ++i) {
        digest[4 * i] = static_cast<unsigned char>(c.h[i] >> 24);
        digest[4 * i + 1] = static_cast<unsigned char>(c.h[i] >> 16);
        digest[4 * i + 2] = static_cast<unsigned char>(c.h[i] >> 8);
        digest[4 * i + 3] = static_cast<unsigned char>(c.h[i]);
    }
}

std::string Sha256Hex(const void* data, std::size_t len) {
    Sha256Ctx c;
    Sha256Init(c);
    Sha256Update(c, data, len);
    unsigned char d[32];
    Sha256Final(c, d);
    char hex[65];
    for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return std::string(hex, 64);
}

std::string Sha8(const void* data, std::size_t len) { return Sha256Hex(data, len).substr(0, 8); }

// L2 展示用：token-ID 序列小端字节流的 sha8（v4 重定义 b1/b2 口径）。
std::string TokensSha8(const std::vector<llama_token>& toks) {
    std::string bytes;
    bytes.reserve(toks.size() * 4);
    for (llama_token t : toks) {
        const std::uint32_t u = static_cast<std::uint32_t>(t);
        bytes.push_back(static_cast<char>(u & 0xFF));
        bytes.push_back(static_cast<char>((u >> 8) & 0xFF));
        bytes.push_back(static_cast<char>((u >> 16) & 0xFF));
        bytes.push_back(static_cast<char>((u >> 24) & 0xFF));
    }
    return Sha8(bytes.data(), bytes.size());
}

// —— B2 快照基线格式 ——
//
// prompts/applied_probe.txt（L1 基线，Step 1 落盘须遵循本格式）：
//   行1：ZH2EN_LEN:<n>            （十进制字节数）
//   接着恰好 n 字节 zh2en applied 原文（逐字节，不增删）
//   一个 \n 分隔
//   行：EN2ZH_LEN:<m>
//   接着恰好 m 字节 en2zh applied 原文（文件至此结束，无 trailing 换行）
// 长度前缀保证 L1 逐字节比对无歧义（applied 含换行/分隔符也不怕），且仍可人工目检。
//
// prompts/applied_probe.tokens.json（L2 基线）：
//   {"zh2en": [<int>, ...], "en2zh": [<int>, ...]}
// 下面的解析器只认这两个键的整数数组，其余容忍（空白/键序/额外键均可）。
bool ParseAppliedProbeTxt(const std::string& content, std::string& zh2en, std::string& en2zh) {
    // 行1：ZH2EN_LEN:<n>\n
    if (content.compare(0, 11, "ZH2EN_LEN:") != 0) return false;
    const std::size_t eol1 = content.find('\n', 0);
    if (eol1 == std::string::npos) return false;
    long n = std::strtol(content.substr(11, eol1 - 11).c_str(), nullptr, 10);
    if (n < 0) return false;
    std::size_t pos = eol1 + 1;
    if (pos + static_cast<std::size_t>(n) > content.size()) return false;
    zh2en = content.substr(pos, static_cast<std::size_t>(n));
    pos += static_cast<std::size_t>(n);
    if (pos >= content.size() || content[pos] != '\n') return false;
    ++pos;
    // 行：EN2ZH_LEN:<m>\n
    if (content.compare(pos, 11, "EN2ZH_LEN:") != 0) return false;
    const std::size_t eol2 = content.find('\n', pos);
    if (eol2 == std::string::npos) return false;
    long m = std::strtol(content.substr(pos + 11, eol2 - pos - 11).c_str(), nullptr, 10);
    if (m < 0) return false;
    pos = eol2 + 1;
    if (pos + static_cast<std::size_t>(m) != content.size()) return false;  // 至此恰结束
    en2zh = content.substr(pos, static_cast<std::size_t>(m));
    return true;
}

bool ParseIntArray(const std::string& json, const std::string& key, std::vector<llama_token>& out) {
    out.clear();
    const std::size_t k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    const std::size_t open = json.find('[', k);
    if (open == std::string::npos) return false;
    const std::size_t close = json.find(']', open);
    if (close == std::string::npos) return false;
    const std::string body = json.substr(open + 1, close - open - 1);
    std::size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() &&
               (std::isspace(static_cast<unsigned char>(body[i])) || body[i] == ',')) {
            ++i;
        }
        if (i >= body.size()) break;
        char* end = nullptr;
        const long v = std::strtol(body.c_str() + i, &end, 10);
        if (end == body.c_str() + i) return false;
        out.push_back(static_cast<llama_token>(v));
        i = static_cast<std::size_t>(end - body.c_str());
    }
    return true;
}

// B2 双探针（msgs 规格见 O-B：单轮单 user 消息，无 system/历史）：
//   zh2en: content="你好，世界", add_ass=false
//   en2zh: content="Hello, world", add_ass=false
// add_ass=false 含义：不追加 assistant 起始符，快照仅覆盖用户轮渲染结果，
// 与 B1 语义（纯拼装，不预置 assistant 回复槽）等价（等价性论证见 O-B）。
constexpr char kProbeZh2En[] = "你好，世界";
constexpr char kProbeEn2Zh[] = "Hello, world";

// B2 内嵌模板渲染：llama_chat_apply_template(tmpl=NULL) 官方路径（docs/03 §3.5）。
bool ApplyBuiltinTemplate(const char* content, std::string& out) {
    const llama_chat_message msg{/*role=*/"user", /*content=*/content};
    const int need = llama_chat_apply_template(nullptr, &msg, 1, /*add_ass=*/false, nullptr, 0);
    if (need <= 0) return false;
    std::string buf(static_cast<std::size_t>(need) + 1, '\0');
    const int got =
        llama_chat_apply_template(nullptr, &msg, 1, /*add_ass=*/false, buf.data(), need + 1);
    if (got < 0) return false;
    out.assign(buf.data(), static_cast<std::size_t>(got > 0 ? got : need));
    return !out.empty();  // 证实标准②要求非空 applied 文本
}

}  // namespace

bool PromptManager::Init(EngineRuntime& engine, int& exit_code) {
    engine_ = &engine;
    exit_code = 0;

    // —— B1/B2 按文件存在性分流（Step 3；vX 为版本号占位符，Step 1 落盘时实例化，
    // 如 prompts/hy-mt2-v1.txt；glob 取字典序最大=最新版本）——
    const std::string kPrompts = ResolvePromptsDir();
    std::vector<std::string> candidates;
    std::error_code ec;
    if (std::filesystem::is_directory(kPrompts, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(kPrompts, ec)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.size() > 12 && name.compare(0, 8, "hy-mt2-v") == 0 &&
                name.compare(name.size() - 4, 4, ".txt") == 0) {
                candidates.push_back(entry.path().string());
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());

    const bool has_snapshot =
        std::filesystem::exists(std::filesystem::path(kPrompts) / "applied_probe.txt", ec) &&
        std::filesystem::exists(std::filesystem::path(kPrompts) / "applied_probe.tokens.json", ec);
    if (!candidates.empty()) {
        // —— B1 分支：三件套文件 pin ——
        mode_ = TemplateMode::B1_File;
        template_path_ = candidates.back();
        if (candidates.size() > 1) {
            std::fprintf(stderr, "发现多个模板文件，选用最新版本：%s\n", template_path_.c_str());
        }
        std::string tmpl;
        if (!ReadFileBytes(template_path_, tmpl)) {
            std::fprintf(stderr, "模板文件读取失败：%s\n", template_path_.c_str());
            exit_code = 1;
            return false;
        }
        // 启动哈希校验（B1 专用）：mismatch → stderr + exit 1（本通道恒不 exit 2）。
        std::string sha_file;
        const std::string sha_path = template_path_ + ".sha256";
        if (!ReadFileBytes(sha_path, sha_file)) {
            std::fprintf(stderr, "模板哈希文件缺失：%s\n", sha_path.c_str());
            exit_code = 1;
            return false;
        }
        // .sha256 容忍 "hash  filename" 与裸 hash 两种写法，取前 64 hex 比对。
        std::string expect;
        for (char c : sha_file) {
            if (std::isxdigit(static_cast<unsigned char>(c))) expect.push_back(c);
            if (expect.size() == 64) break;
        }
        const std::string actual = Sha256Hex(tmpl.data(), tmpl.size());
        if (ToLowerAscii(expect) != actual) {
            std::fprintf(stderr, "模板哈希不一致（可能被篡改或版本错配）：%s\n", template_path_.c_str());
            exit_code = 1;
            return false;
        }
        // {{USER}} 个数运行时复核（须 exactly 一个；Step 1 落盘时已校验，此处防漂移）。
        constexpr char kSlot[] = "{{USER}}";
        std::size_t first = tmpl.find(kSlot);
        std::size_t second =
            (first == std::string::npos) ? std::string::npos : tmpl.find(kSlot, first + sizeof(kSlot) - 1);
        if (first == std::string::npos || second != std::string::npos) {
            std::fprintf(stderr, "模板占位符 {{USER}} 个数须为 1：%s\n", template_path_.c_str());
            exit_code = 1;
            return false;
        }
        template_pre_ = tmpl.substr(0, first);
        template_post_ = tmpl.substr(first + sizeof(kSlot) - 1);
        std::fprintf(stderr, "模板：%s（B1，哈希校验通过）\n", template_path_.c_str());
        return true;
    }

    if (has_snapshot) {
        // —— B2 分支：模型内嵌模板官方路径 + 启动一次性快照回归 ——
        mode_ = TemplateMode::B2_Builtin;
        std::string txt, js;
        const std::string kProbeTxt = (std::filesystem::path(kPrompts) / "applied_probe.txt").string();
        const std::string kProbeJs =
            (std::filesystem::path(kPrompts) / "applied_probe.tokens.json").string();
        if (!ReadFileBytes(kProbeTxt, txt) || !ReadFileBytes(kProbeJs, js)) {
            std::fprintf(stderr, "B2 快照基线读取失败（%s/applied_probe.*）\n", kPrompts.c_str());
            exit_code = 1;
            return false;
        }
        std::string base_zh, base_en;
        std::vector<llama_token> base_zh_toks, base_en_toks;
        if (!ParseAppliedProbeTxt(txt, base_zh, base_en) || !ParseIntArray(js, "zh2en", base_zh_toks) ||
            !ParseIntArray(js, "en2zh", base_en_toks)) {
            std::fprintf(stderr, "B2 快照基线损坏，无法解析（%s/applied_probe.*）\n",
                             kPrompts.c_str());
            exit_code = 1;
            return false;
        }
        // 启动一次性快照回归：固定双探针各跑 apply_template(NULL) + 整串 tokenize，
        // L1(applied 逐字节)+L2(token-ID 全等)双层合取；首个失败即退，仅一行告警。
        // 禁止逐轮翻译触发（本函数只在 Init 调用一次）。
        struct Probe {
            const char* name;
            const char* content;
            const std::string* base_text;
            const std::vector<llama_token>* base_toks;
        };
        const Probe probes[2] = {{"zh2en", kProbeZh2En, &base_zh, &base_zh_toks},
                                 {"en2zh", kProbeEn2Zh, &base_en, &base_en_toks}};
        for (const Probe& p : probes) {
            std::string cur_text;
            if (!ApplyBuiltinTemplate(p.content, cur_text)) {
                std::fprintf(stderr, "B2 内嵌模板渲染失败（probe=%s）\n", p.name);
                exit_code = 1;
                return false;
            }
            // L1：applied 逐字节。
            if (cur_text != *p.base_text) {
                std::fprintf(stderr, "TEMPLATE_DRIFT: level=L1 probe=%s b1=%s b2=%s\n", p.name,
                             Sha8(p.base_text->data(), p.base_text->size()).c_str(),
                             Sha8(cur_text.data(), cur_text.size()).c_str());
                exit_code = 2;
                return false;
            }
            // L2：整串 Tok(applied, add_special=true, parse_special=true) 后全等。
            // m3 探针文本不变式：探针 content 不得含模板控制串（{{...}}、<|...|>、
            // BOS/EOS 字面、\n 外控制符转义），否则整串 parse 不稳定、目检无法判定，
            // 且未来若恢复 B1 对照则整串 vs 分段差异恒误报 exit 2。Step 1 落盘探针时
            // 做控制串扫描，命中即换探针文本并重做快照——本处双探针已确认干净。
            std::vector<llama_token> cur_toks;
            if (!engine_->Tokenize(cur_text, true, true, cur_toks)) {
                std::fprintf(stderr, "B2 快照 tokenize 失败（probe=%s）\n", p.name);
                exit_code = 1;
                return false;
            }
            if (cur_toks != *p.base_toks) {
                std::fprintf(stderr, "TEMPLATE_DRIFT: level=L2 probe=%s b1=%s b2=%s\n", p.name,
                             TokensSha8(*p.base_toks).c_str(), TokensSha8(cur_toks).c_str());
                exit_code = 2;
                return false;
            }
        }
        std::fprintf(stderr, "模板：GGUF 内嵌模板（B2，快照回归通过）\n");
        return true;
    }

    // 两者都不满足 → exit 1 报错（B1 无模板文件 且 B2 无快照基线）。
    // 注：文案中的 "prompts/..." 指解析目录（CWD 优先，见 ResolvePromptsDir）。
    std::fprintf(stderr, "模板不可用：%s/hy-mt2-v*.txt 与快照基线均不存在\n", kPrompts.c_str());
    exit_code = 1;
    return false;
}

bool PromptManager::BuildPrompt(Direction dir, const std::string& user_utf8, PromptArtifacts& out) {
    out = PromptArtifacts{};
    // 方向指令包装（Step 1 P4 补充取证：官方 README "Default Translation" 行，见
    // prompts/hy-mt2-META.md 附录；tencent/Hy-MT2-1.8B README.md:120，抓取 2026-10-16）。
    // 官方用法：指令在 user 轮内（无 system，模型无默认 system_prompt），target_lang 用
    // 全名（中文指令用中文名、英文指令用英文名）。模板文件本身不分方向（仍单 {{USER}}），
    // 方向仅决定此前缀——属 Step 3 prompt 装配，非 executor 发现⑧的 system 修订（无需 P0）。
    std::string instructed;
    switch (dir) {
        case Direction::Zh2En:
            instructed = "将以下文本翻译为英文，注意**只需要输出翻译后的结果，不要额外解释**：\n\n" + user_utf8;
            break;
        case Direction::En2Zh:
            instructed = "Translate the following text into Chinese. Note that you should **only output the translated result without any additional explanation**:\n\n" + user_utf8;
            break;
        default:
            std::fprintf(stderr, "内部错误：非法翻译方向\n");
            return false;
    }
    if (!engine_) return false;

    if (mode_ == TemplateMode::B1_File) {
        // —— B1：纯拼装 + 两段式 tokenize（段表见本文件头注释）——
        // 注意：指令前缀随用户段走 Tok(·,F,F)（防注入：指令中的 **/： 等按普通文本解析）。
        out.applied_text = template_pre_ + instructed + template_post_;
        std::vector<llama_token> t0, t1, t2;
        if (!engine_->Tokenize(template_pre_, true, true, t0)) return false;
        if (!engine_->Tokenize(instructed, false, false, t1)) return false;
        if (!engine_->Tokenize(template_post_, false, true, t2)) return false;
        out.prompt_tokens.reserve(t0.size() + t1.size() + t2.size());
        out.prompt_tokens.insert(out.prompt_tokens.end(), t0.begin(), t0.end());
        out.prompt_tokens.insert(out.prompt_tokens.end(), t1.begin(), t1.end());
        out.prompt_tokens.insert(out.prompt_tokens.end(), t2.begin(), t2.end());
        return !out.prompt_tokens.empty();
    }

    // —— B2：apply_template(NULL) + 整串 tokenize（两段式不执行：无模板文件时切分无输入）——
    // msgs 构造与探针同形：单轮单 user 消息，add_ass=false。
    if (!ApplyBuiltinTemplate(user_utf8.c_str(), out.applied_text)) {
        std::fprintf(stderr, "内嵌模板渲染失败\n");
        return false;
    }
    // 整串 flags 与快照回归一致：add_special=true, parse_special=true。
    if (!engine_->Tokenize(out.applied_text, true, true, out.prompt_tokens)) {
        std::fprintf(stderr, "prompt tokenize 失败\n");
        return false;
    }
    return !out.prompt_tokens.empty();
}

llama_sampler* CreateSamplerChain(int32_t n_vocab) {
    // Step 3 采样链（构造顺序锁死，与 prompts/sampling.pin.json 一致）：
    //   ① penalties(64, 1.10, 0.0, 0.0) — 0.10/1.10/64 无上游出处，标"待调初值"；
    //      freq/present=0.0 即禁用。
    //   ② min_p(0.10, min_keep=1) — min_keep=1 锁死。
    //   ③ temp(0.0) — 确定性占位；MVP 内非零 temperature 不存在。
    //   ④ greedy — 收尾选择器。
    // MVP 内不存在 top-k/top-p/dist/seed。
    // temp(0.0)+greedy 为有意冗余（双保险），禁止"优化"掉其一（移除须 P0 特批）。
    llama_sampler* chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!chain) return nullptr;
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(n_vocab, 64, 1.10f, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_min_p(0.10f, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_greedy());
    return chain;
}
