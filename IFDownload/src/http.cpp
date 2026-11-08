#include "http.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static const char* kAgentA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
static std::string gCustomProxyA;

#ifdef _WIN32
#include <ws2tcpip.h>
#include <winhttp.h>

#pragma comment(lib, "ws2_32.lib")

static const wchar_t* kAgentW = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
static std::wstring gCustomProxyW;

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    bool https;
    bool valid;
};

static UrlParts crack(const std::string& url) {
    UrlParts p = {{}, {}, false, false};
    std::wstring w = widen(url);
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t h[256] = {}, pt[4096] = {};
    uc.lpszHostName = h;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pt;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) {
        return p;
    }
    p.host = h;
    p.path = pt;
    p.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    p.valid = true;
    return p;
}

void setGlobalProxyW(const std::wstring& proxy) {
    gCustomProxyW = proxy;
    gCustomProxyA = narrow(proxy);
}

std::wstring getGlobalProxyW() {
    return gCustomProxyW;
}

void setGlobalProxy(const std::string& proxy) {
    gCustomProxyA = proxy;
    gCustomProxyW = widen(proxy);
}

std::string getGlobalProxy() {
    return gCustomProxyA;
}

static bool testPort(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(s, (sockaddr*)&addr, sizeof(addr));

    fd_set writes;
    FD_ZERO(&writes);
    FD_SET(s, &writes);

    timeval tv = {0, 150000};
    int res = select(0, nullptr, &writes, nullptr, &tv);

    closesocket(s);
    WSACleanup();
    return res > 0;
}

std::wstring scanLocalProxyW() {
    int candidatePorts[] = { 7890, 10809, 2080, 8080, 1080, 9050, 8888 };
    for (int p : candidatePorts) {
        if (testPort(p)) {
            wchar_t buf[32];
            swprintf(buf, sizeof(buf) / sizeof(wchar_t), L"127.0.0.1:%d", p);
            return buf;
        }
    }
    return {};
}

std::string scanLocalProxy() {
    return narrow(scanLocalProxyW());
}

static std::wstring detectEnvProxy() {
    const char* envVars[] = { "HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy" };
    for (const char* v : envVars) {
        char* val = getenv(v);
        if (val && *val) {
            std::string s = val;
            size_t slash = s.find("://");
            if (slash != std::string::npos) s = s.substr(slash + 3);
            size_t endSlash = s.find('/');
            if (endSlash != std::string::npos) s = s.substr(0, endSlash);
            return widen(s);
        }
    }
    return {};
}

static HINTERNET openSession() {
    std::wstring proxy = gCustomProxyW;
    if (proxy.empty()) proxy = detectEnvProxy();
    if (proxy.empty()) proxy = scanLocalProxyW();

    HINTERNET ses = nullptr;
    if (!proxy.empty()) {
        ses = WinHttpOpen(kAgentW, WINHTTP_ACCESS_TYPE_NAMED_PROXY, proxy.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) {
        ses = WinHttpOpen(kAgentW, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) return nullptr;

    DWORD protocols = 0x00000800 | 0x00002000;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    DWORD timeout = 25000;
    WinHttpSetOption(ses, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 90000;
    WinHttpSetOption(ses, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    return ses;
}

static void configureRequest(HINTERNET req, bool https) {
    if (https) {
        DWORD secFlags = 0x00000100 | 0x00000200 | 0x00001000 | 0x00002000 | 0x00000080;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }
    WinHttpAddRequestHeaders(req,
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36\r\n"
        L"Accept: */*\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
}

static DWORD getStatusCode(HINTERNET req) {
    DWORD code = 0;
    DWORD sz = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    return code;
}

static std::string getRedirectLocation(HINTERNET req) {
    DWORD locSz = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &locSz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || locSz == 0) return {};
    std::wstring loc(locSz / sizeof(wchar_t), 0);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
        &loc[0], &locSz, WINHTTP_NO_HEADER_INDEX)) return {};
    size_t chars = locSz / sizeof(wchar_t);
    while (chars > 0 && loc[chars - 1] == L'\0') chars--;
    loc.resize(chars);
    return narrow(loc);
}

static std::string resolveUrl(const std::string& currentUrl, const std::string& location) {
    if (location.find("http://") == 0 || location.find("https://") == 0) return location;
    UrlParts p = crack(currentUrl);
    if (!p.valid) return location;
    std::string base = (p.https ? "https://" : "http://") + narrow(p.host);
    if (!location.empty() && location[0] == '/') return base + location;
    return base + "/" + location;
}

Response httpGet(const std::string& url) {
    Response resp = {0, {}};
    std::string cur = url;
    for (int redir = 0; redir < 8; redir++) {
        UrlParts u = crack(cur);
        if (!u.valid) break;
        HINTERNET ses = openSession();
        if (!ses) break;
        HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!con) { WinHttpCloseHandle(ses); break; }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); break; }
        configureRequest(req, u.https);
        if (!WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); break;
        }
        DWORD code = getStatusCode(req);
        if (code >= 301 && code <= 308) {
            std::string loc = getRedirectLocation(req);
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            if (loc.empty()) break;
            cur = resolveUrl(cur, loc);
            continue;
        }
        resp.code = code;
        if (code == 200) {
            std::vector<char> chunk(65536);
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                DWORD toRead = avail > chunk.size() ? (DWORD)chunk.size() : avail;
                DWORD readBytes = 0;
                if (!WinHttpReadData(req, chunk.data(), toRead, &readBytes) || readBytes == 0) break;
                resp.body.append(chunk.data(), readBytes);
            }
        }
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        break;
    }
    return resp;
}

DWORD httpHead(const std::string& url) {
    std::string cur = url;
    for (int redir = 0; redir < 8; redir++) {
        UrlParts u = crack(cur);
        if (!u.valid) return 0;
        HINTERNET ses = openSession();
        if (!ses) return 0;
        HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!con) { WinHttpCloseHandle(ses); return 0; }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return 0; }
        configureRequest(req, u.https);
        if (!WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return 0;
        }
        DWORD code = getStatusCode(req);
        if (code >= 301 && code <= 308) {
            std::string loc = getRedirectLocation(req);
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            if (loc.empty()) return code;
            cur = resolveUrl(cur, loc);
            continue;
        }
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return code;
    }
    return 0;
}

bool httpDownloadW(const std::string& url, const std::wstring& destPath, ChunkCb onChunk) {
    std::string cur = url;
    for (int redir = 0; redir < 10; redir++) {
        UrlParts u = crack(cur);
        if (!u.valid) return false;
        HINTERNET ses = openSession();
        if (!ses) return false;
        HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!con) { WinHttpCloseHandle(ses); return false; }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false; }
        configureRequest(req, u.https);
        if (!WinHttpSendRequest(req, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false;
        }
        DWORD code = getStatusCode(req);
        if (code >= 301 && code <= 308) {
            std::string loc = getRedirectLocation(req);
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            if (loc.empty()) return false;
            cur = resolveUrl(cur, loc);
            continue;
        }
        if (code != 200 && code != 206) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false;
        }
        ULONGLONG totalBytes = 0;
        wchar_t clBuf[32] = {};
        DWORD clSz = sizeof(clBuf);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clSz, WINHTTP_NO_HEADER_INDEX)) {
            totalBytes = (ULONGLONG)_wtoi64(clBuf);
        }
        FILE* fp = _wfopen(destPath.c_str(), L"wb");
        if (!fp) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return false;
        }
        ULONGLONG got = 0;
        std::vector<char> buffer(65536);
        DWORD avail = 0;
        bool ok = true;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            DWORD toRead = avail > buffer.size() ? (DWORD)buffer.size() : avail;
            DWORD readBytes = 0;
            if (!WinHttpReadData(req, buffer.data(), toRead, &readBytes) || readBytes == 0) {
                ok = false;
                break;
            }
            if (fwrite(buffer.data(), 1, readBytes, fp) != readBytes) {
                ok = false;
                break;
            }
            got += readBytes;
            if (onChunk) onChunk(buffer.data(), readBytes, got, totalBytes);
        }
        fclose(fp);
        if (totalBytes > 0 && got != totalBytes) ok = false;
        if (!ok) _wremove(destPath.c_str());
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return ok;
    }
    return false;
}

bool httpDownload(const std::string& url, const std::string& destPath, ChunkCb onChunk) {
    return httpDownloadW(url, widen(destPath), onChunk);
}

#else
#include <curl/curl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

void setGlobalProxy(const std::string& proxy) {
    gCustomProxyA = proxy;
}

std::string getGlobalProxy() {
    return gCustomProxyA;
}

static bool testPort(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    connect(s, (sockaddr*)&addr, sizeof(addr));

    fd_set writes;
    FD_ZERO(&writes);
    FD_SET(s, &writes);

    timeval tv = {0, 150000};
    int res = select(s + 1, nullptr, &writes, nullptr, &tv);
    close(s);
    return res > 0;
}

std::string scanLocalProxy() {
    int candidatePorts[] = { 7890, 10809, 2080, 8080, 1080, 9050, 8888 };
    for (int p : candidatePorts) {
        if (testPort(p)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "127.0.0.1:%d", p);
            return buf;
        }
    }
    return {};
}

static size_t writeStringCb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    std::string* s = (std::string*)userp;
    s->append((char*)contents, total);
    return total;
}

Response httpGet(const std::string& url) {
    Response resp = {0, {}};
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string proxy = gCustomProxyA;
    if (proxy.empty()) proxy = scanLocalProxy();
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgentA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.code = (DWORD)code;
    }
    curl_easy_cleanup(curl);
    return resp;
}

DWORD httpHead(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return 0;

    std::string proxy = gCustomProxyA;
    if (proxy.empty()) proxy = scanLocalProxy();
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgentA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    DWORD ret = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        ret = (DWORD)code;
    }
    curl_easy_cleanup(curl);
    return ret;
}

struct DownloadContext {
    FILE* fp;
    ChunkCb cb;
    ULONGLONG got;
    ULONGLONG total;
    bool ok;
};

static size_t fileWriteCb(void* ptr, size_t size, size_t nmemb, void* stream) {
    DownloadContext* ctx = (DownloadContext*)stream;
    size_t total = size * nmemb;
    size_t written = fwrite(ptr, size, nmemb, ctx->fp);
    if (written != nmemb) {
        ctx->ok = false;
        return 0;
    }
    ctx->got += total;
    if (ctx->cb) {
        ctx->cb(ptr, total, ctx->got, ctx->total);
    }
    return total;
}

static int xferinfoCb(void* clientp, curl_off_t dltotal, curl_off_t, curl_off_t, curl_off_t) {
    DownloadContext* ctx = (DownloadContext*)clientp;
    if (dltotal > 0) ctx->total = (ULONGLONG)dltotal;
    return 0;
}

bool httpDownload(const std::string& url, const std::string& destPath, ChunkCb onChunk) {
    FILE* fp = fopen(destPath.c_str(), "wb");
    if (!fp) return false;

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        remove(destPath.c_str());
        return false;
    }

    DownloadContext ctx = { fp, onChunk, 0, 0, true };

    std::string proxy = gCustomProxyA;
    if (proxy.empty()) proxy = scanLocalProxy();
    if (!proxy.empty()) curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kAgentA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 524288L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfoCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    bool ok = (res == CURLE_OK && (code == 200 || code == 206) && ctx.ok);
    if (!ok) remove(destPath.c_str());
    return ok;
}

#endif
