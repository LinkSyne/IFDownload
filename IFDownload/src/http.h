#pragma once
#include <string>
#include <functional>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
using DWORD = uint32_t;
using ULONGLONG = uint64_t;
#endif

struct Response {
    DWORD code;
    std::string body;
};

using ChunkCb = std::function<void(const void*, size_t, ULONGLONG, ULONGLONG)>;

void        setGlobalProxy(const std::string& proxy);
std::string getGlobalProxy();
std::string scanLocalProxy();

#ifdef _WIN32
void         setGlobalProxyW(const std::wstring& proxy);
std::wstring getGlobalProxyW();
std::wstring scanLocalProxyW();
bool         httpDownloadW(const std::string& url, const std::wstring& destPath, ChunkCb onChunk);
#endif

Response httpGet(const std::string& url);
DWORD    httpHead(const std::string& url);
bool     httpDownload(const std::string& url, const std::string& destPath, ChunkCb onChunk);
