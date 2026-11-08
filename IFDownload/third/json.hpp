#pragma once
#include <string>
#include <vector>
#include <sstream>

struct JsonValue {
    std::string str;
    unsigned int num = 0;
};

struct JsonObject {
    std::string tag;
    std::string build;
    std::string file;
    std::string path;
    unsigned int sizeMb = 0;
    std::string sha256;
};

inline std::vector<JsonObject> parseManifestJson(const std::string& content) {
    std::vector<JsonObject> list;
    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        std::string block = content.substr(pos, end - pos + 1);

        auto extractStr = [&](const std::string& key) -> std::string {
            std::string pattern = "\"" + key + "\"";
            size_t kpos = block.find(pattern);
            if (kpos == std::string::npos) return {};
            size_t colon = block.find(':', kpos + pattern.size());
            if (colon == std::string::npos) return {};
            size_t firstQuote = block.find('\"', colon + 1);
            if (firstQuote == std::string::npos || firstQuote > end) return {};
            size_t secondQuote = block.find('\"', firstQuote + 1);
            if (secondQuote == std::string::npos) return {};
            return block.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        };

        auto extractNum = [&](const std::string& key) -> unsigned int {
            std::string pattern = "\"" + key + "\"";
            size_t kpos = block.find(pattern);
            if (kpos == std::string::npos) return 0;
            size_t colon = block.find(':', kpos + pattern.size());
            if (colon == std::string::npos) return 0;
            size_t numStart = block.find_first_of("0123456789", colon + 1);
            if (numStart == std::string::npos) return 0;
            size_t numEnd = block.find_first_not_of("0123456789", numStart);
            std::string valStr = block.substr(numStart, numEnd - numStart);
            return (unsigned int)std::strtoul(valStr.c_str(), nullptr, 10);
        };

        JsonObject obj;
        obj.tag = extractStr("tag");
        obj.build = extractStr("build");
        obj.file = extractStr("file");
        obj.path = extractStr("path");
        obj.sizeMb = extractNum("sizeMb");
        obj.sha256 = extractStr("sha256");

        if (!obj.file.empty()) {
            list.push_back(std::move(obj));
        }
        pos = end + 1;
    }
    return list;
}
