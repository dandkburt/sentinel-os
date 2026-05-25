#include "extension_manifest.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace sentinel::core {

namespace {

void trim_in_place(std::string& value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
}

std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string strip_quotes(std::string value) {
    trim_in_place(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool parse_line_key_value(const std::string& line, std::string& key, std::string& value) {
    std::string candidate = line;
    trim_in_place(candidate);
    if (candidate.empty() || candidate == "{" || candidate == "}") {
        return false;
    }
    if (candidate.rfind("//", 0) == 0 || candidate.rfind("#", 0) == 0) {
        return false;
    }

    if (!candidate.empty() && candidate.back() == ',') {
        candidate.pop_back();
        trim_in_place(candidate);
    }

    size_t sep = candidate.find('=');
    if (sep == std::string::npos) {
        sep = candidate.find(':');
    }
    if (sep == std::string::npos) {
        return false;
    }

    key = strip_quotes(candidate.substr(0, sep));
    value = candidate.substr(sep + 1);
    trim_in_place(key);
    trim_in_place(value);
    return !key.empty();
}

bool is_ignorable_line(const std::string& line) {
    std::string candidate = line;
    trim_in_place(candidate);
    if (candidate.empty() || candidate == "{" || candidate == "}") {
        return true;
    }
    if (candidate.rfind("//", 0) == 0 || candidate.rfind("#", 0) == 0) {
        return true;
    }
    return false;
}

bool parse_string_list_value(const std::string& raw_value,
                             std::vector<std::string>& out,
                             std::string& error) {
    out.clear();
    std::string value = raw_value;
    trim_in_place(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        error = "malformed-manifest";
        return false;
    }

    std::string inner = value.substr(1, value.size() - 2);
    trim_in_place(inner);
    if (inner.empty()) {
        return true;
    }

    std::stringstream stream(inner);
    std::string item;
    while (std::getline(stream, item, ',')) {
        trim_in_place(item);
        item = strip_quotes(item);
        trim_in_place(item);
        if (item.empty()) {
            error = "malformed-manifest";
            return false;
        }
        out.push_back(item);
    }

    return true;
}

bool is_safe_extension_id(const std::string& id) {
    if (id.empty()) {
        return false;
    }

    for (char c : id) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool is_semver_like(const std::string& version) {
    static const std::regex kSemverLike(R"(^[0-9]+\.[0-9]+\.[0-9]+$)");
    return std::regex_match(version, kSemverLike);
}

bool has_duplicates(const std::vector<std::string>& values) {
    std::set<std::string> unique;
    for (const auto& value : values) {
        if (!unique.insert(value).second) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool validate_extension_manifest(const ExtensionManifest& manifest, std::string& error) {
    if (manifest.id.empty() || manifest.version.empty() || manifest.name.empty() || manifest.entry_point.empty()) {
        error = "missing-required-field";
        return false;
    }

    if (!is_safe_extension_id(manifest.id)) {
        error = "invalid-id";
        return false;
    }

    if (!is_semver_like(manifest.version)) {
        error = "invalid-version";
        return false;
    }

    if (manifest.entry_point.empty()) {
        error = "invalid-entry-point";
        return false;
    }

    for (const auto& permission : manifest.permissions) {
        if (permission.empty()) {
            error = "invalid-permission";
            return false;
        }
    }
    if (has_duplicates(manifest.permissions)) {
        error = "invalid-permission";
        return false;
    }

    for (const auto& dependency : manifest.dependencies) {
        if (dependency.empty() || dependency == manifest.id) {
            error = "invalid-dependency";
            return false;
        }
    }
    if (has_duplicates(manifest.dependencies)) {
        error = "invalid-dependency";
        return false;
    }

    error.clear();
    return true;
}

bool parse_extension_manifest_text(const std::string& text,
                                   ExtensionManifest& out,
                                   std::string& error) {
    ExtensionManifest candidate;
    bool saw_id = false;
    bool saw_version = false;
    bool saw_name = false;
    bool saw_entry_point = false;

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string key;
        std::string value;
        if (!parse_line_key_value(line, key, value)) {
            if (!is_ignorable_line(line)) {
                error = "malformed-manifest";
                return false;
            }
            continue;
        }

        const std::string normalized_key = to_lower_ascii(key);
        if (normalized_key == "id") {
            candidate.id = strip_quotes(value);
            trim_in_place(candidate.id);
            saw_id = true;
        } else if (normalized_key == "version") {
            candidate.version = strip_quotes(value);
            trim_in_place(candidate.version);
            saw_version = true;
        } else if (normalized_key == "name") {
            candidate.name = strip_quotes(value);
            trim_in_place(candidate.name);
            saw_name = true;
        } else if (normalized_key == "entry_point") {
            candidate.entry_point = strip_quotes(value);
            trim_in_place(candidate.entry_point);
            saw_entry_point = true;
        } else if (normalized_key == "description") {
            candidate.description = strip_quotes(value);
            trim_in_place(candidate.description);
        } else if (normalized_key == "permissions") {
            if (!parse_string_list_value(value, candidate.permissions, error)) {
                return false;
            }
        } else if (normalized_key == "dependencies") {
            if (!parse_string_list_value(value, candidate.dependencies, error)) {
                return false;
            }
        } else {
            error = "malformed-manifest";
            return false;
        }
    }

    if (!saw_id || !saw_version || !saw_name || !saw_entry_point) {
        error = "missing-required-field";
        return false;
    }
    if (candidate.entry_point.empty()) {
        error = "invalid-entry-point";
        return false;
    }

    if (!validate_extension_manifest(candidate, error)) {
        return false;
    }

    out = candidate;
    error.clear();
    return true;
}

}  // namespace sentinel::core
