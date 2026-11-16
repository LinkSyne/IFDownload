#pragma once
#include <string>
#include <cstddef>

enum class HashAlgo {
    Sha256,
    Sha1
};

class Hasher {
public:
    explicit Hasher(HashAlgo algo);
    ~Hasher();
    Hasher(const Hasher&) = delete;
    Hasher& operator=(const Hasher&) = delete;
    bool valid() const { return ready; }
    void feed(const void* data, size_t len);
    std::string finishHex();
    void close();

private:
    HashAlgo algoId;
    bool ready = false;
    void* provider = nullptr;
    void* state = nullptr;
};

std::string computeFileHash(const std::string& path, HashAlgo algo);
bool feedFile(const std::string& path, Hasher* hashA, Hasher* hashB, unsigned long long maxBytes);
bool hashEqual(const std::string& expected, const std::string& actual);
