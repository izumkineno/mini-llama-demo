// i18n_count.cpp —— stdin 编码归一化 + Unicode scalar 计数实现（Step 4）
// 对应计划条款同 i18n_count.h。
#include "app/i18n_count.h"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lai {

bool IsValidUtf8(const std::string& s) {
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            // 过长编码拒绝：C0/C1 永不合法（本可 1 字节表示）。
            if (c < 0xC2) return false;
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            // >U+10FFFF 拒绝：F5 及以上首字节永不合法。
            if (c > 0xF4) return false;
            len = 4;
            cp = c & 0x07;
        } else {
            return false;  // 续字节裸奔或非法首字节
        }
        if (i + len > n) return false;  // 残缺序列
        for (std::size_t k = 1; k < len; ++k) {
            const auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        // 过长编码/代理区/超界二次确认（首字节历史检查 + 重组后复核）。
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000)) {
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        i += len;
    }
    return true;
}

bool NormalizeToUTF8(const std::string& raw, std::string& out_utf8) {
    if (IsValidUtf8(raw)) {
        out_utf8 = raw;  // 合法 UTF-8 直用
        return true;
    }
#ifdef _WIN32
    // 非 UTF-8 → 按系统 ANSI 码页经 UTF-16 转 UTF-8（数据层转码，不动控制台码页）。
    if (raw.empty()) {
        out_utf8.clear();
        return true;
    }
    const int wlen =
        MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(),
                            static_cast<int>(raw.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, raw.data(),
                            static_cast<int>(raw.size()), wide.data(), wlen) != wlen) {
        return false;
    }
    const int ulen = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0,
                                         nullptr, nullptr);
    if (ulen <= 0) return false;
    out_utf8.assign(static_cast<std::size_t>(ulen), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out_utf8.data(), ulen,
                            nullptr, nullptr) != ulen) {
        return false;
    }
    return IsValidUtf8(out_utf8);  // 归一化后复核：只放行合法 UTF-8
#else
    (void)out_utf8;
    return false;  // 非 Windows 无 ANSI 码页概念：直接失败，留循环
#endif
}

std::size_t CountScalars(const std::string& utf8) {
    // scalar 数 = 非续字节数（10xxxxxx 为续字节）。调用方保证合法 UTF-8。
    std::size_t count = 0;
    for (char ch : utf8) {
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++count;
    }
    return count;
}

void StripTrailingCR(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

void StripLeadingBOM(std::string& line) {
    // UTF-8 BOM：EF BB BF。只剥行首一个。
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }
}

}  // namespace lai
