#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdlib>

namespace minijson {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    const JsonValue* find(const char* key) const {
        if (type != Type::Object) return nullptr;
        for (auto& m : members) {
            if (m.first == key) return &m.second;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : src(text) {}

    bool parse(JsonValue& out) {
        pos = 0;
        err = false;
        skipSpace();
        parseValue(out);
        if (!err) {
            skipSpace();
            if (pos != src.size()) err = true;
        }
        return !err;
    }

private:
    const std::string& src;
    size_t pos = 0;
    bool err = false;

    void fail() {
        err = true;
        pos = src.size();
    }

    void skipSpace() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') pos++;
            else break;
        }
    }

    bool consume(char c) {
        if (pos < src.size() && src[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    void parseValue(JsonValue& v) {
        if (err || pos >= src.size()) {
            if (!err) fail();
            return;
        }
        char c = src[pos];
        if (c == '{') parseObject(v);
        else if (c == '[') parseArray(v);
        else if (c == '"') {
            v.type = JsonValue::Type::String;
            parseString(v.strVal);
        }
        else if (c == 't') parseKeyword("true", v, true);
        else if (c == 'f') parseKeyword("false", v, false);
        else if (c == 'n') parseNull(v);
        else parseNumber(v);
    }

    void parseKeyword(const char* word, JsonValue& v, bool value) {
        size_t len = 0;
        while (word[len]) len++;
        if (pos + len <= src.size() && src.compare(pos, len, word) == 0) {
            v.type = JsonValue::Type::Bool;
            v.boolVal = value;
            pos += len;
        } else {
            fail();
        }
    }

    void parseNull(JsonValue& v) {
        if (pos + 4 <= src.size() && src.compare(pos, 4, "null") == 0) {
            v.type = JsonValue::Type::Null;
            pos += 4;
        } else {
            fail();
        }
    }

    void parseNumber(JsonValue& v) {
        size_t start = pos;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) pos++;
        bool digits = false;
        while (pos < src.size()) {
            char c = src[pos];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                if (c >= '0' && c <= '9') digits = true;
                pos++;
            } else break;
        }
        if (!digits || pos == start) {
            fail();
            return;
        }
        v.type = JsonValue::Type::Number;
        v.numVal = strtod(src.c_str() + start, nullptr);
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }

    unsigned parseHex4() {
        if (pos + 4 > src.size()) {
            fail();
            return 0;
        }
        unsigned v = 0;
        for (int i = 0; i < 4; i++) {
            char c = src[pos++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else {
                fail();
                return 0;
            }
        }
        return v;
    }

    void parseString(std::string& out) {
        if (!consume('"')) {
            fail();
            return;
        }
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos >= src.size()) break;
            char e = src[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = parseHex4();
                    if (err) return;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos + 1 < src.size() &&
                        src[pos] == '\\' && src[pos + 1] == 'u') {
                        pos += 2;
                        unsigned lo = parseHex4();
                        if (err) return;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            appendUtf8(out, cp);
                            cp = lo;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    fail();
                    return;
            }
        }
        fail();
    }

    void parseArray(JsonValue& v) {
        v.type = JsonValue::Type::Array;
        pos++;
        skipSpace();
        if (consume(']')) return;
        while (!err) {
            v.items.emplace_back();
            parseValue(v.items.back());
            if (err) return;
            skipSpace();
            if (consume(',')) {
                skipSpace();
                continue;
            }
            if (consume(']')) return;
            fail();
        }
    }

    void parseObject(JsonValue& v) {
        v.type = JsonValue::Type::Object;
        pos++;
        skipSpace();
        if (consume('}')) return;
        while (!err) {
            skipSpace();
            std::string key;
            parseString(key);
            if (err) return;
            skipSpace();
            if (!consume(':')) {
                fail();
                return;
            }
            v.members.emplace_back(std::move(key), JsonValue());
            parseValue(v.members.back().second);
            if (err) return;
            skipSpace();
            if (consume(',')) continue;
            if (consume('}')) return;
            fail();
        }
    }
};

inline bool parseJson(const std::string& text, JsonValue& out) {
    JsonParser p(text);
    return p.parse(out);
}

}
