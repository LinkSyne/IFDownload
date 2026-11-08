#include "cache.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <knownfolders.h>
#include <shlobj.h>

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring cacheDirW() {
    PWSTR knownPath = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &knownPath) != S_OK) {
        return {};
    }
    std::wstring base(knownPath);
    CoTaskMemFree(knownPath);
    return base + L"\\IFDownload\\cache";
}

std::wstring cachePathW(const std::string& file) {
    std::wstring dir = cacheDirW();
    if (dir.empty()) return {};
    return dir + L"\\" + widen(file);
}

bool cacheHitW(const std::string& file, unsigned int expectedMb) {
    std::wstring p = cachePathW(file);
    if (p.empty()) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
        return false;
    }
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        return false;
    }
    ULARGE_INTEGER sz;
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;

    ULONGLONG minBytes = 1048576;
    if (expectedMb > 0) {
        minBytes = (ULONGLONG)expectedMb * 1024ULL * 1024ULL * 9ULL / 10ULL;
    }

    if (sz.QuadPart < minBytes) {
        DeleteFileW(p.c_str());
        return false;
    }
    return true;
}
#else
#include <unistd.h>
#endif

std::string cacheDir() {
#ifdef _WIN32
    PWSTR knownPath = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &knownPath) != S_OK) {
        return {};
    }
    char buf[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, knownPath, -1, buf, MAX_PATH, nullptr, nullptr);
    CoTaskMemFree(knownPath);
    return std::string(buf) + "\\IFDownload\\cache";
#else
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/IFDownload/cache";
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.cache/IFDownload/cache";
    }
    return "/tmp/IFDownload/cache";
#endif
}

std::string cachePath(const std::string& file) {
    std::string dir = cacheDir();
    if (dir.empty()) return {};
#ifdef _WIN32
    return dir + "\\" + file;
#else
    return dir + "/" + file;
#endif
}

bool cacheHit(const std::string& file, unsigned int expectedMb) {
    std::string p = cachePath(file);
    if (p.empty()) return false;
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
        return false;
    }
    unsigned long long minBytes = 1048576;
    if (expectedMb > 0) {
        minBytes = (unsigned long long)expectedMb * 1024ULL * 1024ULL * 9ULL / 10ULL;
    }
    if ((unsigned long long)st.st_size < minBytes) {
        remove(p.c_str());
        return false;
    }
    return true;
}

static void makeDirRecursive(const std::string& path) {
#ifdef _WIN32
    size_t lastSep = path.rfind('\\');
    if (lastSep != std::string::npos) {
        std::string parent = path.substr(0, lastSep);
        CreateDirectoryA(parent.c_str(), nullptr);
    }
    CreateDirectoryA(path.c_str(), nullptr);
#else
    std::string current;
    for (char c : path) {
        current += c;
        if (c == '/' && current.size() > 1) {
            mkdir(current.c_str(), 0755);
        }
    }
    mkdir(path.c_str(), 0755);
#endif
}

bool cacheInit() {
    std::string dir = cacheDir();
    if (dir.empty()) return false;
    makeDirRecursive(dir);
    struct stat st;
    return (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}
