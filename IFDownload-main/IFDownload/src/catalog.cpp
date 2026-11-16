#include "catalog.h"
#include "http.h"
#include "cache.h"
#include "fs.h"
#include "../third/json.hpp"

using namespace minijson;

#include <algorithm>
#include <cstdlib>
#include <ctime>

static const char* kItemId = "vmwareworkstationarchive";
static const char* kCacheFile = "catalog-metadata.json";
static const double kCacheTtlSec = 86400.0;

static std::string gServer;
static std::string gDir;

static std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

static std::string baseName(const std::string& p) {
    size_t s1 = p.find_last_of('/');
    size_t s2 = p.find_last_of('\\');
    if (s1 == std::string::npos && s2 == std::string::npos) return p;
    size_t s = (s1 == std::string::npos) ? s2 : (s2 == std::string::npos ? s1 : (s1 > s2 ? s1 : s2));
    return p.substr(s + 1);
}

static bool parseInstallerName(const std::string& file, std::string& tag, std::string& build) {
    const std::string prefix = "vmware-workstation-full-";
    std::string low = toLower(file);
    if (low.rfind(prefix, 0) != 0) return false;
    if (low.size() < prefix.size() + 5) return false;
    if (low.compare(low.size() - 4, 4, ".exe") != 0) return false;
    std::string core = file.substr(prefix.size(), file.size() - prefix.size() - 4);
    size_t dash = core.find_last_of('-');
    if (dash == std::string::npos) return false;
    tag = core.substr(0, dash);
    build = core.substr(dash + 1);
    if (tag.empty() || build.empty()) return false;
    for (char c : build) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

struct VersionKey {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned long long build = 0;
};

static VersionKey keyOf(const Entry& e) {
    VersionKey k;
    k.build = strtoull(e.build.c_str(), nullptr, 10);
    size_t idx = 0;
    unsigned parts[3] = {0, 0, 0};
    for (int i = 0; i < 3 && idx < e.tag.size(); i++) {
        unsigned v = 0;
        bool any = false;
        while (idx < e.tag.size() && e.tag[idx] >= '0' && e.tag[idx] <= '9') {
            v = v * 10u + unsigned(e.tag[idx] - '0');
            idx++;
            any = true;
        }
        if (any) parts[i] = v;
        if (idx < e.tag.size() && e.tag[idx] == '.') idx++;
    }
    k.a = parts[0];
    k.b = parts[1];
    k.c = parts[2];
    return k;
}

static bool newerFirst(const Entry& l, const Entry& r) {
    VersionKey a = keyOf(l);
    VersionKey b = keyOf(r);
    if (a.a != b.a) return a.a > b.a;
    if (a.b != b.b) return a.b > b.b;
    if (a.c != b.c) return a.c > b.c;
    return a.build > b.build;
}

static std::vector<Entry> entriesFromMeta(const std::string& body) {
    std::vector<Entry> out;
    JsonValue root;
    if (!parseJson(body, root) || root.type != JsonValue::Type::Object) return out;

    const JsonValue* files = root.find("files");
    if (!files || files->type != JsonValue::Type::Array) return out;

    const JsonValue* srv = root.find("server");
    if (srv && srv->type == JsonValue::Type::String) gServer = srv->strVal;
    const JsonValue* dir = root.find("dir");
    if (dir && dir->type == JsonValue::Type::String) gDir = dir->strVal;

    for (const JsonValue& f : files->items) {
        if (f.type != JsonValue::Type::Object) continue;
        const JsonValue* name = f.find("name");
        if (!name || name->type != JsonValue::Type::String) continue;

        std::string file = baseName(name->strVal);
        std::string tag, build;
        if (!parseInstallerName(file, tag, build)) continue;

        Entry e;
        e.tag = tag;
        e.build = build;
        e.file = file;
        e.path = name->strVal;

        const JsonValue* size = f.find("size");
        if (size) {
            if (size->type == JsonValue::Type::String) {
                e.sizeBytes = strtoull(size->strVal.c_str(), nullptr, 10);
            } else if (size->type == JsonValue::Type::Number) {
                e.sizeBytes = (unsigned long long)size->numVal;
            }
        }
        if (e.sizeBytes > 0) {
            e.sizeMb = (unsigned int)((e.sizeBytes + 1048575) / 1048576);
        }
        const JsonValue* sha1 = f.find("sha1");
        if (sha1 && sha1->type == JsonValue::Type::String) e.sha1 = toLower(sha1->strVal);
        out.push_back(std::move(e));
    }
    return out;
}

bool fetchCatalog(const std::vector<Entry>& manifestEntries, std::vector<Entry>& entries, std::string& note) {
    gServer.clear();
    gDir.clear();
    std::string cacheFile = cache::path(kCacheFile);

    std::vector<Entry> found;
    bool cacheFresh = false;
    if (fs::fileExists(cacheFile)) {
        double age = difftime(time(nullptr), (time_t)fs::modifiedTime(cacheFile));
        cacheFresh = age >= 0 && age < kCacheTtlSec;
    }
    if (cacheFresh) {
        found = entriesFromMeta(fs::readText(cacheFile));
        if (!found.empty()) note = "cached";
    }
    if (found.empty()) {
        HttpResult r = httpGet(std::string("https://archive.org/metadata/") + kItemId);
        if (r.status == 200) found = entriesFromMeta(r.body);
        if (!found.empty()) {
            note = "live";
            fs::writeText(cacheFile, r.body);
        }
    }
    if (found.empty() && fs::fileExists(cacheFile)) {
        found = entriesFromMeta(fs::readText(cacheFile));
        if (!found.empty()) note = "stale cache";
    }
    if (found.empty()) {
        entries = manifestEntries;
        if (!entries.empty()) note = "local manifest";
        return !entries.empty();
    }

    for (const Entry& mf : manifestEntries) {
        if (mf.sha256.empty()) continue;
        for (Entry& e : found) {
            if (e.file == mf.file) {
                e.sha256 = mf.sha256;
                break;
            }
        }
    }
    std::sort(found.begin(), found.end(), newerFirst);
    entries = std::move(found);
    return true;
}

std::string catalogNodeUrl(const std::string& itemPath) {
    if (gServer.empty()) return {};
    std::string dir = gDir;
    if (!dir.empty() && dir[0] != '/') dir = "/" + dir;
    if (dir.empty() || dir.back() != '/') dir += '/';
    return "https://" + gServer + dir + itemPath;
}
