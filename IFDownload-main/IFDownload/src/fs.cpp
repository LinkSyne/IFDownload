#include "fs.h"

#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32

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

#endif

namespace fs {

FILE* openRead(const std::string& path) {
#ifdef _WIN32
    return _wfopen(widen(path).c_str(), L"rb");
#else
    return fopen(path.c_str(), "rb");
#endif
}

FILE* openWrite(const std::string& path) {
#ifdef _WIN32
    return _wfopen(widen(path).c_str(), L"wb");
#else
    return fopen(path.c_str(), "wb");
#endif
}

FILE* openAppend(const std::string& path) {
#ifdef _WIN32
    return _wfopen(widen(path).c_str(), L"ab");
#else
    return fopen(path.c_str(), "ab");
#endif
}

bool fileExists(const std::string& path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesW(widen(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
#endif
}

unsigned long long fileSize(const std::string& path) {
#ifdef _WIN32
    struct __stat64 st;
    if (_wstat64(widen(path).c_str(), &st) != 0) return 0;
    return (unsigned long long)st.st_size;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (unsigned long long)st.st_size;
#endif
}

double modifiedTime(const std::string& path) {
#ifdef _WIN32
    struct __stat64 st;
    if (_wstat64(widen(path).c_str(), &st) != 0) return 0;
    return (double)st.st_mtime;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (double)st.st_mtime;
#endif
}

bool removeFile(const std::string& path) {
#ifdef _WIN32
    return DeleteFileW(widen(path).c_str()) != 0;
#else
    return remove(path.c_str()) == 0;
#endif
}

bool renameFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return MoveFileW(widen(from).c_str(), widen(to).c_str()) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool copyFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    return CopyFileW(widen(from).c_str(), widen(to).c_str(), FALSE) != 0;
#else
    FILE* src = openRead(from);
    if (!src) return false;
    FILE* dst = openWrite(to);
    if (!dst) {
        fclose(src);
        return false;
    }
    std::vector<char> buf(1048576);
    bool ok = true;
    size_t rd = 0;
    while ((rd = fread(buf.data(), 1, buf.size(), src)) > 0) {
        if (fwrite(buf.data(), 1, rd, dst) != rd) {
            ok = false;
            break;
        }
    }
    if (ferror(src)) ok = false;
    fclose(src);
    fclose(dst);
    return ok;
#endif
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        char c = path[i];
        cur += c;
        if (c == '\\' || c == '/') {
            if (cur.size() > 3) CreateDirectoryW(widen(cur).c_str(), nullptr);
        }
    }
    CreateDirectoryW(widen(path).c_str(), nullptr);
    DWORD attr = GetFileAttributesW(widen(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if (path[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0755);
    }
    mkdir(path.c_str(), 0755);
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string currentDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetCurrentDirectoryW(MAX_PATH, buf);
    if (n > 0 && n < MAX_PATH) return narrow(buf);
    return ".";
#else
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) return buf;
    return ".";
#endif
}

std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ".";
    std::wstring w(buf, n);
    size_t pos = w.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return ".";
    return narrow(w.substr(0, pos));
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string s(buf, (size_t)n);
    size_t pos = s.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return s.substr(0, pos);
#endif
}

std::string joinPath(const std::string& dir, const std::string& name) {
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

std::string readText(const std::string& path) {
    FILE* fp = openRead(path);
    if (!fp) return {};
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return {};
    }
    long sz = ftell(fp);
    if (sz <= 0) {
        fclose(fp);
        return {};
    }
    rewind(fp);
    std::string content((size_t)sz, 0);
    size_t rd = fread(&content[0], 1, (size_t)sz, fp);
    fclose(fp);
    content.resize(rd);
    return content;
}

bool writeText(const std::string& path, const std::string& content) {
    FILE* fp = openWrite(path);
    if (!fp) return false;
    bool ok = true;
    if (!content.empty()) ok = fwrite(content.data(), 1, content.size(), fp) == content.size();
    fclose(fp);
    return ok;
}

}
