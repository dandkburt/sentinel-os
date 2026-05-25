#pragma once

#include <string>

namespace sentinel::core {

struct RuntimeConfig {
    std::string log_level = "info";
    bool enable_policy = true;
    bool enable_namespace = true;
    bool enable_event_broker = true;
    unsigned int shutdown_timeout_ms = 5000;
};

RuntimeConfig default_runtime_config();

bool validate_runtime_config(const RuntimeConfig& config, std::string& error);

bool parse_runtime_config_text(const std::string& text, RuntimeConfig& out, std::string& error);

bool load_runtime_config_from_file(const std::string& path, RuntimeConfig& out, std::string& error);

bool apply_runtime_reconfiguration(const RuntimeConfig& candidate,
                                   RuntimeConfig& current,
                                   std::string& error);

}  // namespace sentinel::core
