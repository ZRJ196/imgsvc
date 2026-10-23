#include "hash.h"

#include <openssl/evp.h>

#include <cstdio>

std::string sha256_hex(const void* data, size_t len)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  dlen = 0;

    // EVP_Digest 一步完成摘要计算（数据已经在内存里，不需要流式）
    if (EVP_Digest(data, len, digest, &dlen, EVP_sha256(), NULL) != 1) {
        return std::string();
    }

    char out[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < dlen; ++i) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[dlen * 2] = '\0';

    return std::string(out);
}
