#include "http.h"
#include "fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const char* kUserAgent = "IFDownload/1.2";
static std::string gProxy;

void setGlobalProxy(const std::string& proxy) {
    gProxy = proxy;
}

std::string getGlobalProxy() {
    return gProxy;
}

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

struct UrlParts {
    std::wstring host;
    std::wstring path;
    bool https = false;
    bool valid = false;
};

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

static UrlParts crackUrl(const std::string& url) {
    UrlParts p;
    std::wstring w = widen(url);
    URL_COMPONENTSW uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[8192] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) return p;
    p.host = host;
    p.path = path;
    p.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    p.valid = true;
    return p;
}

static std::string envProxy() {
    const char* vars[] = {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy"};
    for (const char* v : vars) {
        const char* val = getenv(v);
        if (!val || !*val) continue;
        std::string s = val;
        size_t scheme = s.find("://");
        if (scheme != std::string::npos) s = s.substr(scheme + 3);
        size_t slash = s.find('/');
        if (slash != std::string::npos) s = s.substr(0, slash);
        return s;
    }
    return {};
}

static HINTERNET openSession() {
    std::string proxy = gProxy;
    if (proxy.empty()) proxy = envProxy();
    std::wstring agent = widen(kUserAgent);
    HINTERNET ses = nullptr;
    if (!proxy.empty()) {
        std::wstring pw = widen(proxy);
        ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_NAMED_PROXY, pw.c_str(), WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) {
        ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!ses) return nullptr;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    DWORD timeout = 20000;
    WinHttpSetOption(ses, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 30000;
    WinHttpSetOption(ses, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 120000;
    WinHttpSetOption(ses, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    return ses;
}

static bool addHeaders(HINTERNET req, bool useRange, unsigned long long resumeFrom) {
    std::string headers = "User-Agent: " + std::string(kUserAgent) + "\r\nAccept: */*\r\n";
    if (useRange) {
        char range[64];
        snprintf(range, sizeof(range), "Range: bytes=%llu-\r\n", resumeFrom);
        headers += range;
    }
    return WinHttpAddRequestHeaders(req, widen(headers).c_str(), (DWORD)-1,
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != 0;
}

static DWORD statusCode(HINTERNET req) {
    DWORD code = 0;
    DWORD sz = sizeof(code);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    return code;
}

static std::string headerValue(HINTERNET req, DWORD level) {
    DWORD sz = 0;
    WinHttpQueryHeaders(req, level | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &sz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return {};
    std::wstring val(sz / sizeof(wchar_t), 0);
    if (!WinHttpQueryHeaders(req, level, WINHTTP_HEADER_NAME_BY_INDEX, &val[0], &sz, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    size_t chars = sz / sizeof(wchar_t);
    while (chars > 0 && val[chars - 1] == L'\0') chars--;
    val.resize(chars);
    return narrow(val);
}

static std::string resolveUrl(const std::string& currentUrl, const std::string& location) {
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0) return location;
    UrlParts p = crackUrl(currentUrl);
    if (!p.valid) return location;
    std::string base = (p.https ? "https://" : "http://") + narrow(p.host);
    if (!location.empty() && location[0] == '/') return base + location;
    return base + "/" + location;
}

HttpResult httpGet(const std::string& url) {
    HttpResult resp;
    std::string cur = url;
    for (int hop = 0; hop < 8; hop++) {
        UrlParts u = crackUrl(cur);
        if (!u.valid) return resp;
        HINTERNET ses = openSession();
        if (!ses) return resp;
        HINTERNET con = WinHttpConnect(ses, u.host.c_str(),
            u.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!con) {
            WinHttpCloseHandle(ses);
            return resp;
        }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) {
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return resp;
        }
        addHeaders(req, false, 0);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = statusCode(req);
            if (code >= 301 && code <= 308) {
                std::string loc = headerValue(req, WINHTTP_QUERY_LOCATION);
                WinHttpCloseHandle(req);
                WinHttpCloseHandle(con);
                WinHttpCloseHandle(ses);
                if (loc.empty()) return resp;
                cur = resolveUrl(cur, loc);
                continue;
            }
            resp.status = code;
            if (code == 200) {
                std::vector<char> buf(65536);
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                    DWORD toRead = avail > buf.size() ? (DWORD)buf.size() : avail;
                    DWORD rd = 0;
                    if (!WinHttpReadData(req, buf.data(), toRead, &rd) || rd == 0) break;
                    resp.body.append(buf.data(), rd);
                }
            }
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return resp;
    }
    return resp;
}

static DownloadResult winDownloadAttempt(const std::string& url, const std::string& destPath,
                                         unsigned long long resumeFrom, const ChunkCb& onChunk) {
    DownloadResult out;
    bool useRange = resumeFrom > 0;
    std::string cur = url;
    for (int hop = 0; hop < 10; hop++) {
        UrlParts u = crackUrl(cur);
        if (!u.valid) return out;
        HINTERNET ses = openSession();
        if (!ses) return out;
        HINTERNET con = WinHttpConnect(ses, u.host.c_str(),
            u.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
        if (!con) {
            WinHttpCloseHandle(ses);
            return out;
        }
        HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) {
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return out;
        }
        addHeaders(req, useRange, resumeFrom);
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return out;
        }
        DWORD code = statusCode(req);
        if (code >= 301 && code <= 308) {
            std::string loc = headerValue(req, WINHTTP_QUERY_LOCATION);
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            if (loc.empty()) return out;
            cur = resolveUrl(cur, loc);
            continue;
        }
        if (code != 200 && code != 206) {
            out.status = code;
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return out;
        }
        if (useRange && code == 200) {
            out.status = 200;
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return out;
        }
        unsigned long long contentLen = 0;
        wchar_t clBuf[32] = {};
        DWORD clSz = sizeof(clBuf);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
            clBuf, &clSz, WINHTTP_NO_HEADER_INDEX)) {
            contentLen = (unsigned long long)_wtoi64(clBuf);
        }
        unsigned long long base = (code == 206 && useRange) ? resumeFrom : 0;
        FILE* fp = base > 0 ? fs::openAppend(destPath) : fs::openWrite(destPath);
        if (!fp) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return out;
        }
        setvbuf(fp, nullptr, _IOFBF, 1048576);
        out.status = code;
        out.resumedBytes = base;
        out.total = base + contentLen;
        if (onChunk) onChunk(nullptr, 0, base, out.total);
        std::vector<char> buf(262144);
        unsigned long long got = 0;
        bool failed = false;
        bool eof = false;
        DWORD avail = 0;
        while (!failed && !eof) {
            if (!WinHttpQueryDataAvailable(req, &avail)) break;
            if (avail == 0) {
                eof = true;
                break;
            }
            DWORD toRead = avail > buf.size() ? (DWORD)buf.size() : avail;
            DWORD rd = 0;
            if (!WinHttpReadData(req, buf.data(), toRead, &rd) || rd == 0) {
                failed = true;
                break;
            }
            if (fwrite(buf.data(), 1, rd, fp) != rd) {
                failed = true;
                break;
            }
            got += rd;
            if (onChunk) onChunk(buf.data(), rd, base + got, out.total);
        }
        fclose(fp);
        out.received = got;
        out.ok = !failed && eof && got > 0 && (contentLen == 0 || got == contentLen);
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return out;
    }
    return out;
}

DownloadResult httpDownload(const std::string& url, const std::string& destPath,
                            unsigned long long expectedTotal, const ChunkCb& onChunk) {
    unsigned long long part = fs::fileSize(destPath);
    if (part > 0 && expectedTotal > 0 && part >= expectedTotal) {
        fs::removeFile(destPath);
        part = 0;
    }
    if (part > 0) {
        DownloadResult out = winDownloadAttempt(url, destPath, part, onChunk);
        if (out.ok) return out;
        if (out.status == 200 && out.resumedBytes > 0) {
            fs::removeFile(destPath);
            return winDownloadAttempt(url, destPath, 0, onChunk);
        }
        return out;
    }
    return winDownloadAttempt(url, destPath, 0, onChunk);
}

#else

#include <curl/curl.h>

static size_t stringWriteCb(void* contents, size_t size, size_t nmemb, void* user) {
    size_t bytes = size * nmemb;
    std::string* s = (std::string*)user;
    s->append((const char*)contents, bytes);
    return bytes;
}

static CURL* curlSession() {
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;
    if (!gProxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, gProxy.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    return curl;
}

HttpResult httpGet(const std::string& url) {
    HttpResult resp;
    CURL* curl = curlSession();
    if (!curl) return resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status = code;
    }
    curl_easy_cleanup(curl);
    return resp;
}

struct DownloadState {
    const ChunkCb* cb;
    FILE* fp;
    unsigned long long base;
    unsigned long long got;
    unsigned long long total;
    bool failed;
};

static size_t downloadWriteCb(void* contents, size_t size, size_t nmemb, void* user) {
    DownloadState* st = (DownloadState*)user;
    size_t bytes = size * nmemb;
    if (fwrite(contents, 1, bytes, st->fp) != bytes) {
        st->failed = true;
        return 0;
    }
    st->got += bytes;
    if (*st->cb) (*st->cb)(contents, bytes, st->base + st->got, st->total);
    return bytes;
}

static int downloadProgressCb(void* user, curl_off_t dltotal, curl_off_t, curl_off_t, curl_off_t) {
    DownloadState* st = (DownloadState*)user;
    if (st->failed) return 1;
    if (dltotal > 0) st->total = st->base + (unsigned long long)dltotal;
    return 0;
}

static DownloadResult curlDownloadAttempt(const std::string& url, const std::string& destPath,
                                          unsigned long long resumeFrom, unsigned long long expectedTotal,
                                          const ChunkCb& onChunk) {
    DownloadResult out;
    FILE* fp = resumeFrom > 0 ? fs::openAppend(destPath) : fs::openWrite(destPath);
    if (!fp) return out;
    setvbuf(fp, nullptr, _IOFBF, 1048576);
    DownloadState st;
    st.cb = &onChunk;
    st.fp = fp;
    st.base = resumeFrom;
    st.got = 0;
    st.total = resumeFrom + expectedTotal;
    st.failed = false;

    CURL* curl = curlSession();
    if (!curl) {
        fclose(fp);
        return out;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 262144L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, downloadWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadProgressCb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &st);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (resumeFrom > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resumeFrom);
    }
    if (onChunk) onChunk(nullptr, 0, resumeFrom, st.total);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    fclose(fp);

    out.status = code;
    out.resumedBytes = resumeFrom;
    out.received = st.got;
    out.total = st.total;
    if (resumeFrom > 0 && code == 200) return out;
    unsigned long long gotAll = resumeFrom + st.got;
    bool lengthOk = (st.total == 0) || (gotAll == st.total);
    out.ok = (res == CURLE_OK && (code == 200 || code == 206) && !st.failed &&
              gotAll > 0 && lengthOk);
    return out;
}

DownloadResult httpDownload(const std::string& url, const std::string& destPath,
                            unsigned long long expectedTotal, const ChunkCb& onChunk) {
    unsigned long long part = fs::fileSize(destPath);
    if (part > 0 && expectedTotal > 0 && part >= expectedTotal) {
        fs::removeFile(destPath);
        part = 0;
    }
    if (part > 0) {
        DownloadResult out = curlDownloadAttempt(url, destPath, part, expectedTotal, onChunk);
        if (out.ok) return out;
        if (out.status == 200 && out.resumedBytes > 0) {
            fs::removeFile(destPath);
            return curlDownloadAttempt(url, destPath, 0, expectedTotal, onChunk);
        }
        return out;
    }
    return curlDownloadAttempt(url, destPath, 0, expectedTotal, onChunk);
}

#endif
