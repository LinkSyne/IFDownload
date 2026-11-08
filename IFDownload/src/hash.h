#pragma once
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/sha.h>
#endif

struct Sha256 {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hsh;
#else
    SHA256_CTX ctx;
#endif
    bool ok;

    void init();
    void feed(const void* data, size_t len);
    std::string finish();
    void close();
};

#ifdef _WIN32
std::string computeFileSha256(const std::wstring& filePath);
#endif
std::string computeFileSha256(const std::string& filePath);
