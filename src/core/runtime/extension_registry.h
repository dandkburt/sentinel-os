#pragma once

#include "extension_manifest.h"

#include <string>
#include <vector>

namespace sentinel::core {

enum class ExtensionLifecycleState {
    Registered = 0,
    Loaded = 1,
    Enabled = 2,
    Disabled = 3,
    Unloaded = 4,
    Failed = 5,
};

struct ExtensionRecord {
    std::string id;
    std::string version;
    std::string name;
    std::string entry_point;
    std::vector<std::string> permissions;
    std::vector<std::string> dependencies;
    std::string source_path;
    ExtensionLifecycleState lifecycle_state = ExtensionLifecycleState::Registered;
};

class IExtensionRegistry {
public:
    virtual ~IExtensionRegistry() = default;

    virtual bool register_extension_from_manifest_path(const std::string& manifest_path,
                                                       std::string& error) = 0;

    virtual std::vector<ExtensionRecord> list_registered_extensions() const = 0;

    virtual bool discover_extensions_in_directory(const std::string& directory_path,
                                                  std::vector<std::string>& non_fatal_errors,
                                                  std::string& error) = 0;

    virtual bool load_extension(const std::string& extension_id,
                                std::string& error) = 0;

    virtual bool enable_extension(const std::string& extension_id,
                                  std::string& error) = 0;

    virtual bool disable_extension(const std::string& extension_id,
                                   std::string& error) = 0;

    virtual bool unload_extension(const std::string& extension_id,
                                  std::string& error) = 0;

    virtual bool get_extension_state(const std::string& extension_id,
                                     ExtensionLifecycleState& out_state,
                                     std::string& error) const = 0;
};

IExtensionRegistry& get_extension_registry_interface();
void initialize_extension_registry();

// Test hook: reset all registered extensions.
void reset_extension_registry_for_tests();

// Test hook: fail a lifecycle step ("load", "enable", "disable", "unload").
void set_extension_lifecycle_fail_step_for_tests(const std::string& step);

// Test hook: clear lifecycle failure injection.
void reset_extension_lifecycle_fail_step_for_tests();

}  // namespace sentinel::core
