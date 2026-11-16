#include "manifest.h"
#include "fs.h"
#include "../third/json.hpp"

using namespace minijson;

std::vector<Entry> loadManifest(const std::string& jsonPath) {
    std::vector<Entry> out;
    std::string content = fs::readText(jsonPath);
    if (content.empty()) return out;

    JsonValue root;
    if (!parseJson(content, root) || root.type != JsonValue::Type::Array) return out;

    for (const JsonValue& item : root.items) {
        if (item.type != JsonValue::Type::Object) continue;
        const JsonValue* file = item.find("file");
        if (!file || file->type != JsonValue::Type::String || file->strVal.empty()) continue;

        Entry e;
        e.file = file->strVal;
        const JsonValue* tag = item.find("tag");
        if (tag && tag->type == JsonValue::Type::String) e.tag = tag->strVal;
        const JsonValue* build = item.find("build");
        if (build && build->type == JsonValue::Type::String) e.build = build->strVal;
        const JsonValue* path = item.find("path");
        if (path && path->type == JsonValue::Type::String && !path->strVal.empty()) {
            e.path = path->strVal;
        } else {
            e.path = e.file;
        }
        const JsonValue* sizeMb = item.find("sizeMb");
        if (sizeMb && sizeMb->type == JsonValue::Type::Number) e.sizeMb = (unsigned int)sizeMb->numVal;
        const JsonValue* sha256 = item.find("sha256");
        if (sha256 && sha256->type == JsonValue::Type::String) e.sha256 = sha256->strVal;
        out.push_back(std::move(e));
    }
    return out;
}

std::string archiveUrl(const Entry& e) {
    return "https://archive.org/download/vmwareworkstationarchive/" + e.path;
}
