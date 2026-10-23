#pragma once

#include <cstddef>
#include <string>

// 计算 SHA-256，返回 64 位十六进制小写字符串（失败返回空串）
//
// 用途：给上传的图片算"内容指纹"，既能去重，又不暴露原始内容。
// 实现：OpenSSL 的 EVP_Digest（OpenSSL 3.0 推荐接口，SHA256_Init 那套已废弃）
std::string sha256_hex(const void* data, size_t len);
