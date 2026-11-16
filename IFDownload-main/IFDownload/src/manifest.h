#pragma once
#include <string>
#include <vector>

struct Entry {
    std::string tag;
    std::string build;
    std::string file;
    std::string path;
    unsigned long long sizeBytes = 0;
    unsigned int sizeMb = 0;
    std::string sha256;
    std::string sha1;
};

std::vector<Entry> loadManifest(const std::string& jsonPath);
std::string archiveUrl(const Entry& e);
