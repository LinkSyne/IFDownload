#include "hash.h"
#include "fs.h"
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

static size_t digestLength(HashAlgo algo) {
    return algo == HashAlgo::Sha256 ? 32 : 20;
}

static std::string digestToHex(const unsigned char* digest, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        s += digits[digest[i] >> 4];
        s += digits[digest[i] & 0x0F];
    }
    return s;
}

Hasher::Hasher(HashAlgo algo) : algoId(algo) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    LPCWSTR id = (algo == HashAlgo::Sha256) ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM;
    if (BCryptOpenAlgorithmProvider(&hAlg, id, nullptr, 0) != 0) return;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return;
    }
    provider = hAlg;
    state = hHash;
    ready = true;
#else
    const EVP_MD* md = (algo == HashAlgo::Sha256) ? EVP_sha256() : EVP_sha1();
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return;
    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return;
    }
    state = ctx;
    ready = true;
#endif
}

Hasher::~Hasher() {
    close();
}

void Hasher::feed(const void* data, size_t len) {
    if (!ready || !data || len == 0) return;
#ifdef _WIN32
    BCryptHashData((BCRYPT_HASH_HANDLE)state, (PUCHAR)data, (ULONG)len, 0);
#else
    EVP_DigestUpdate((EVP_MD_CTX*)state, data, len);
#endif
}

std::string Hasher::finishHex() {
    if (!ready) return {};
    unsigned char digest[32];
    size_t len = digestLength(algoId);
#ifdef _WIN32
    if (BCryptFinishHash((BCRYPT_HASH_HANDLE)state, digest, (ULONG)len, 0) != 0) return {};
#else
    unsigned int outLen = 0;
    if (EVP_DigestFinal_ex((EVP_MD_CTX*)state, digest, &outLen) != 1) return {};
    len = outLen;
#endif
    return digestToHex(digest, len);
}

void Hasher::close() {
    if (!ready && !state && !provider) return;
#ifdef _WIN32
    if (state) {
        BCryptDestroyHash((BCRYPT_HASH_HANDLE)state);
        state = nullptr;
    }
    if (provider) {
        BCryptCloseAlgorithmProvider((BCRYPT_ALG_HANDLE)provider, 0);
        provider = nullptr;
    }
#else
    if (state) {
        EVP_MD_CTX_free((EVP_MD_CTX*)state);
        state = nullptr;
    }
#endif
    ready = false;
}

std::string computeFileHash(const std::string& path, HashAlgo algo) {
    FILE* fp = fs::openRead(path);
    if (!fp) return {};
    Hasher h(algo);
    if (!h.valid()) {
        fclose(fp);
        return {};
    }
    std::vector<char> buf(262144);
    size_t rd = 0;
    while ((rd = fread(buf.data(), 1, buf.size(), fp)) > 0) {
        h.feed(buf.data(), rd);
    }
    bool ok = !ferror(fp);
    fclose(fp);
    if (!ok) return {};
    std::string hex = h.finishHex();
    h.close();
    return hex;
}

bool feedFile(const std::string& path, Hasher* hashA, Hasher* hashB, unsigned long long maxBytes) {
    FILE* fp = fs::openRead(path);
    if (!fp) return false;
    std::vector<char> buf(262144);
    unsigned long long left = maxBytes;
    while (left > 0) {
        size_t want = buf.size();
        if ((unsigned long long)want > left) want = (size_t)left;
        size_t rd = fread(buf.data(), 1, want, fp);
        if (rd == 0) break;
        if (hashA && hashA->valid()) hashA->feed(buf.data(), rd);
        if (hashB && hashB->valid()) hashB->feed(buf.data(), rd);
        left -= rd;
    }
    fclose(fp);
    return left == 0;
}

bool hashEqual(const std::string& expected, const std::string& actual) {
    if (expected.size() != actual.size()) return false;
    for (size_t i = 0; i < expected.size(); i++) {
        char a = expected[i];
        char b = actual[i];
        if (a >= 'A' && a <= 'F') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'F') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}
