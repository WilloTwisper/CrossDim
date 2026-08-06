#pragma once
#include <string>
#include <cstdio>

// Minimal JSON string building helpers (no external deps).
// Produces valid JSON by escaping strings.

namespace Json {

inline std::string Escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

// Find the value of a JSON field (string or number): {"key":"value"} or {"key":3}
// Returns empty string if not found. Simple parser (no nesting).
static std::string GetField(const std::string& json, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + pat.size());
    if (colon == std::string::npos) return "";
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) i++;
    if (i >= json.size()) return "";
    if (json[i] == '"') {
        // String value
        i++;
        std::string out;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                char c = json[i+1];
                if (c == 'n') out += '\n';
                else if (c == 'r') out += '\r';
                else if (c == 't') out += '\t';
                else if (c == '"') out += '"';
                else if (c == '\\') out += '\\';
                else if (c == '/') out += '/';
                else out += c;
                i += 2;
            } else {
                out += json[i];
                i++;
            }
        }
        return out;
    } else {
        // Number or boolean without quotes: read until , } ] space
        size_t start = i;
        while (i < json.size() && json[i] != ',' && json[i] != '}' && json[i] != ']' &&
               json[i] != ' ' && json[i] != '\t' && json[i] != '\n' && json[i] != '\r') i++;
        return json.substr(start, i - start);
    }
}

// Backward-compatible alias for string fields.
static std::string GetStringField(const std::string& json, const std::string& key) {
    return GetField(json, key);
}

} // namespace Json
