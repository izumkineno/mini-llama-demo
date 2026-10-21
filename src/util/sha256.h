// sha256.h —— 紧凑 SHA-256（公共域算法，零外部依赖）
// 用途：内嵌模板启动哈希自检。无状态纯函数，可独立单测。
#pragma once

#include <cstddef>
#include <string>

namespace lai {
namespace util {

// 对 [data, data+len) 做 SHA-256，返回 64 位小写 hex。
std::string Sha256Hex(const void* data, std::size_t len);

}  // namespace util
}  // namespace lai
