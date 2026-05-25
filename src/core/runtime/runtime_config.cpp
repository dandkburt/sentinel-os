#include "runtime_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace sentinel::core {

namespace {
constexpr unsigned int kSupportedConfigVersion = 1;

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

bool parse_bool_value(const std::string& value, bool& out) {
    const std::string normalized = to_lower_ascii(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes") {
        out = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no") {
        out = false;
        return true;
    }
    return false;
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
    value = strip_quotes(candidate.substr(sep + 1));
    trim_in_place(key);
    trim_in_place(value);
    return !key.empty();
}

}  // namespace

RuntimeConfig default_runtime_config() {
    return RuntimeConfig{};
}

bool validate_runtime_config(const RuntimeConfig& config, std::string& error) {
    if (config.config_version != kSupportedConfigVersion) {
        error = "unsupported-config-version";
        return false;
    }

    static const std::set<std::string> kAllowedLevels = {
        "trace", "debug", "info", "warn", "error"
    };

    const std::string level = to_lower_ascii(config.log_level);
    if (kAllowedLevels.find(level) == kAllowedLevels.end()) {
        error = "invalid-log-level";
        return false;
    }

    if (config.shutdown_timeout_ms == 0 || config.shutdown_timeout_ms > 600000) {
        error = "invalid-shutdown-timeout";
        return false;
    }

    error.clear();
    return true;
}

bool parse_runtime_config_text(const std::string& text, RuntimeConfig& out, std::string& error) {
    RuntimeConfig candidate = default_runtime_config();

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string key;
        std::string value;
        if (!parse_line_key_value(line, key, value)) {
            continue;
        }

        const std::string normalized_key = to_lower_ascii(key);
        if (normalized_key == "version" || normalized_key == "config_version") {
            try {
                const unsigned long parsed = std::stoul(value);
                candidate.config_version = static_cast<unsigned int>(parsed);
            } catch (...) {
                error = "invalid-config-version";
                return false;
            }
        } else if (normalized_key == "log_level") {
            candidate.log_level = to_lower_ascii(value);
        } else if (normalized_key == "enable_policy") {
            bool parsed = false;
            if (!parse_bool_value(value, parsed)) {
                error = "invalid-enable-policy";
                return false;
            }
            candidate.enable_policy = parsed;
        } else if (normalized_key == "enable_namespace") {
            bool parsed = false;
            if (!parse_bool_value(value, parsed)) {
                error = "invalid-enable-namespace";
                return false;
            }
            candidate.enable_namespace = parsed;
        } else if (normalized_key == "enable_event_broker") {
            bool parsed = false;
            if (!parse_bool_value(value, parsed)) {
                error = "invalid-enable-event-broker";
                return false;
            }
            candidate.enable_event_broker = parsed;
        } else if (normalized_key == "shutdown_timeout_ms") {
            try {
                const unsigned long parsed = std::stoul(value);
                candidate.shutdown_timeout_ms = static_cast<unsigned int>(parsed);
            } catch (...) {
                error = "invalid-shutdown-timeout";
                return false;
            }
        }
    }

    if (!validate_runtime_config(candidate, error)) {
        return false;
    }

    out = candidate;
    error.clear();
    return true;
}

bool load_runtime_config_from_file(const std::string& path, RuntimeConfig& out, std::string& error) {
    if (path.empty()) {
        out = default_runtime_config();
        error.clear();
        return true;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        out = default_runtime_config();
        error.clear();
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_runtime_config_text(buffer.str(), out, error);
}

bool apply_runtime_reconfiguration(const RuntimeConfig& candidate,
                                   RuntimeConfig& current,
                                   std::string& error) {
    const RuntimeConfig next = candidate;
    if (!validate_runtime_config(next, error)) {
        return false;
    }

    current = next;
    error.clear();
    return true;
}

bool apply_runtime_reconfiguration_from_file(const std::string& path,
                                             RuntimeConfig& current,
                                             std::string& error) {
    RuntimeConfig candidate = default_runtime_config();
    if (!load_runtime_config_from_file(path, candidate, error)) {
        return false;
    }

    return apply_runtime_reconfiguration(candidate, current, error);
}

}  // namespace sentinel::core
