#pragma once
#include <string>
#include <functional>
#include <cstdint>

struct HttpResult {
    long status = 0;
    std::string body;
};

struct DownloadResult {
    bool ok = false;
    unsigned long long resumedBytes = 0;
    unsigned long long received = 0;
    unsigned long long total = 0;
    long status = 0;
};

using ChunkCb = std::function<void(const void*, size_t, unsigned long long, unsigned long long)>;

void setGlobalProxy(const std::string& proxy);
std::string getGlobalProxy();

HttpResult httpGet(const std::string& url);
DownloadResult httpDownload(const std::string& url, const std::string& destPath,
                            unsigned long long expectedTotal, const ChunkCb& onChunk);
