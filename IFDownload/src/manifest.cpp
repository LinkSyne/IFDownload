#include "manifest.h"
#include "../third/json.hpp"
#include <cstdio>
#include <vector>

#ifdef _WIN32
std::vector<Entry> loadManifest(const std::wstring& jsonPath) {
    FILE* fp = _wfopen(jsonPath.c_str(), L"rb");
    if (!fp) return {};
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        return {};
    }
    std::string content(sz, 0);
    fread(&content[0], 1, sz, fp);
    fclose(fp);

    auto parsed = parseManifestJson(content);
    std::vector<Entry> entries;
    entries.reserve(parsed.size());
    for (auto& item : parsed) {
        Entry e;
        e.tag = std::move(item.tag);
        e.build = std::move(item.build);
        e.file = std::move(item.file);
        e.path = std::move(item.path);
        e.sizeMb = item.sizeMb;
        e.sha256 = std::move(item.sha256);
        entries.push_back(std::move(e));
    }
    return entries;
}
#endif

std::vector<Entry> loadManifest(const std::string& jsonPath) {
    FILE* fp = fopen(jsonPath.c_str(), "rb");
    if (!fp) return {};
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        return {};
    }
    std::string content(sz, 0);
    fread(&content[0], 1, sz, fp);
    fclose(fp);

    auto parsed = parseManifestJson(content);
    std::vector<Entry> entries;
    entries.reserve(parsed.size());
    for (auto& item : parsed) {
        Entry e;
        e.tag = std::move(item.tag);
        e.build = std::move(item.build);
        e.file = std::move(item.file);
        e.path = std::move(item.path);
        e.sizeMb = item.sizeMb;
        e.sha256 = std::move(item.sha256);
        entries.push_back(std::move(e));
    }
    return entries;
}

std::string archiveUrl(const Entry& e) {
    return "https://archive.org/download/vmwareworkstationarchive/" + e.path;
}
