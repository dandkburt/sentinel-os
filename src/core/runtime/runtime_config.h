#pragma once

#include <string>

namespace sentinel::core {

struct RuntimeConfig {
    unsigned int config_version = 1;
    std::string log_level = "info";
    bool enable_policy = true;
    bool enable_namespace = true;
    bool enable_event_broker = true;
    unsigned int shutdown_timeout_ms = 5000;
};

using RuntimeConfigFileLoaderForTests = bool (*)(const std::string& path,
                                                 std::string& content,
                                                 std::string& error);

RuntimeConfig default_runtime_config();

bool validate_runtime_config(const RuntimeConfig& config, std::string& error);

bool parse_runtime_config_text(const std::string& text, RuntimeConfig& out, std::string& error);

bool load_runtime_config_from_file(const std::string& path, RuntimeConfig& out, std::string& error);

bool apply_runtime_reconfiguration(const RuntimeConfig& candidate,
                                   RuntimeConfig& current,
                                   std::string& error);

bool apply_runtime_reconfiguration_from_file(const std::string& path,
                                             RuntimeConfig& current,
                                             std::string& error);

void set_runtime_config_file_loader_for_tests(RuntimeConfigFileLoaderForTests loader);

void reset_runtime_config_file_loader_for_tests();

}  // namespace sentinel::core
