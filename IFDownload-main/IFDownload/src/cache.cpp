#include "cache.h"
#include "fs.h"
#include "hash.h"

#include <cstdlib>

static std::string sidecarPath(const std::string& file) {
    return cache::path(file) + ".hash";
}

static bool readSidecar(const std::string& file, std::string& algo, std::string& hex) {
    std::string content = fs::readText(sidecarPath(file));
    size_t sp = content.find(' ');
    if (sp == std::string::npos) return false;
    algo = content.substr(0, sp);
    hex = content.substr(sp + 1);
    while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r' || hex.back() == ' ')) hex.pop_back();
    return !algo.empty() && !hex.empty();
}

namespace cache {

std::string dir() {
#ifdef _WIN32
    const char* base = getenv("LOCALAPPDATA");
    if (base && *base) return std::string(base) + "\\IFDownload\\cache";
    return "cache";
#else
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/IFDownload/cache";
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/.cache/IFDownload/cache";
    return "/tmp/IFDownload/cache";
#endif
}

std::string path(const std::string& file) {
    return fs::joinPath(dir(), file);
}

std::string partialPath(const std::string& file) {
    return path(file) + ".part";
}

bool init() {
    return fs::makeDirs(dir());
}

bool lookup(const Entry& e) {
    std::string p = path(e.file);
    unsigned long long sz = fs::fileSize(p);
    if (sz == 0) return false;

    if (e.sizeBytes > 0 && sz != e.sizeBytes) {
        fs::removeFile(p);
        fs::removeFile(sidecarPath(e.file));
        return false;
    }

    bool haveSha256 = !e.sha256.empty();
    bool haveSha1 = haveSha256 ? false : !e.sha1.empty();

    std::string algo, hex;
    if (readSidecar(e.file, algo, hex)) {
        if (haveSha256 && algo == "sha256" && hashEqual(e.sha256, hex)) return true;
        if (haveSha1 && algo == "sha1" && hashEqual(e.sha1, hex)) return true;
    }

    if (haveSha256) {
        std::string actual = computeFileHash(p, HashAlgo::Sha256);
        if (!actual.empty() && hashEqual(e.sha256, actual)) {
            storeHash(e.file, "sha256", actual);
            return true;
        }
        fs::removeFile(p);
        fs::removeFile(sidecarPath(e.file));
        return false;
    }
    if (haveSha1) {
        std::string actual = computeFileHash(p, HashAlgo::Sha1);
        if (!actual.empty() && hashEqual(e.sha1, actual)) {
            storeHash(e.file, "sha1", actual);
            return true;
        }
        fs::removeFile(p);
        fs::removeFile(sidecarPath(e.file));
        return false;
    }
    return e.sizeBytes > 0;
}

void storeHash(const std::string& file, const std::string& algo, const std::string& hex) {
    fs::writeText(sidecarPath(file), algo + " " + hex + "\n");
}

}
