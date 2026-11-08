#pragma once
#include <string>
#include <vector>

struct Entry {
    std::string tag;
    std::string build;
    std::string file;
    std::string path;
    unsigned int sizeMb;
    std::string sha256;
};

#ifdef _WIN32
std::vector<Entry> loadManifest(const std::wstring& jsonPath);
#endif
std::vector<Entry> loadManifest(const std::string& jsonPath);
std::string archiveUrl(const Entry& e);
