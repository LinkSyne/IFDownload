#include "hash.h"
#include <cstdio>
#include <vector>

void Sha256::init() {
    ok = false;
#ifdef _WIN32
    alg = nullptr;
    hsh = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return;
    }
    if (BCryptCreateHash(alg, &hsh, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        alg = nullptr;
        return;
    }
    ok = true;
#else
    if (SHA256_Init(&ctx) == 1) {
        ok = true;
    }
#endif
}

void Sha256::feed(const void* data, size_t len) {
    if (!ok || len == 0) return;
#ifdef _WIN32
    BCryptHashData(hsh, (PUCHAR)data, (ULONG)len, 0);
#else
    SHA256_Update(&ctx, data, len);
#endif
}

std::string Sha256::finish() {
    if (!ok) return {};
    unsigned char digest[32];
#ifdef _WIN32
    if (BCryptFinishHash(hsh, digest, sizeof(digest), 0) < 0) {
        return {};
    }
#else
    if (SHA256_Final(digest, &ctx) != 1) {
        return {};
    }
#endif
    char hex[65];
    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    hex[64] = '\0';
    return hex;
}

void Sha256::close() {
#ifdef _WIN32
    if (hsh) {
        BCryptDestroyHash(hsh);
        hsh = nullptr;
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
        alg = nullptr;
    }
#endif
    ok = false;
}

#ifdef _WIN32
std::string computeFileSha256(const std::wstring& filePath) {
    FILE* fp = _wfopen(filePath.c_str(), L"rb");
    if (!fp) return {};
    Sha256 ctx;
    ctx.init();
    if (!ctx.ok) {
        fclose(fp);
        return {};
    }
    std::vector<char> buffer(65536);
    size_t rd = 0;
    while ((rd = fread(buffer.data(), 1, buffer.size(), fp)) > 0) {
        ctx.feed(buffer.data(), rd);
    }
    fclose(fp);
    std::string res = ctx.finish();
    ctx.close();
    return res;
}
#endif

std::string computeFileSha256(const std::string& filePath) {
    FILE* fp = fopen(filePath.c_str(), "rb");
    if (!fp) return {};
    Sha256 ctx;
    ctx.init();
    if (!ctx.ok) {
        fclose(fp);
        return {};
    }
    std::vector<char> buffer(65536);
    size_t rd = 0;
    while ((rd = fread(buffer.data(), 1, buffer.size(), fp)) > 0) {
        ctx.feed(buffer.data(), rd);
    }
    fclose(fp);
    std::string res = ctx.finish();
    ctx.close();
    return res;
}
