// i18n_count.cpp —— 编码归一化与 scalar 计数实现（Step 4）
// 对应计划条款同 i18n_count.h（Step 4 stdin 编码归一化 + scalar 计数点）。
#include "i18n_count.h"

#include <windows.h>

#include <cstdint>

bool IsValidUtf8(const std::string& s) {
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7F) {  // ASCII
            ++i;
            continue;
        }
        std::size_t extra = 0;  // 续字节数
        std::uint32_t cp = 0;   // 解出的码点
        std::uint32_t lo = 0;   // 该长度允许的最小码点（防过长编码）
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1F;
            lo = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0F;
            lo = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07;
            lo = 0x10000;
        } else {
            return false;  // 0x80-0xBF 裸续字节或 0xF8+ 非法首字节
        }
        if (i + extra >= n) return false;  // 残缺序列
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (cp < lo) return false;                          // 过长编码
        if (cp > 0x10FFFF) return false;                    // 超出 Unicode 范围
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;      // 代理区
        i += extra + 1;
    }
    return true;
}

bool NormalizeToUTF8(const std::string& raw, std::string& out_utf8) {
    if (IsValidUtf8(raw)) {
        out_utf8 = raw;  // 严格校验通过则直用
        return true;
    }
    // 非 UTF-8：按系统 ANSI 码页（CP_ACP）解码再转 UTF-8。
    // 注意：此处禁止 SetConsoleCP(CP_UTF8)（计划 Step 4 已否决），只做数据层转码。
    if (raw.empty()) {
        out_utf8.clear();
        return true;
    }
    const int wlen =
        MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(), static_cast<int>(raw.size()), wide.data(),
                            wlen) <= 0) {
        return false;
    }
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return false;
    out_utf8.assign(static_cast<std::size_t>(ulen), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out_utf8.data(), ulen, nullptr, nullptr) <= 0) {
        return false;
    }
    return true;
}

std::size_t CountScalars(const std::string& utf8) {
    // 合法 UTF-8 下，scalar 数 = 首字节数 = 非续字节数。
    std::size_t count = 0;
    for (unsigned char ch : utf8) {
        if ((ch & 0xC0) != 0x80) ++count;
    }
    return count;
}

void StripTrailingCR(std::string& line) {
    // getline 已去 \n；此处再去一尾 \r（Windows \r\n 输入计 0）。
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

void StripLeadingBOM(std::string& line) {
    // 行首 UTF-8 BOM（EF BB BF）剥一个，剥离后计数。
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
}
