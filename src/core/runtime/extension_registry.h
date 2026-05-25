#pragma once

#include "extension_manifest.h"

#include <string>
#include <vector>

namespace sentinel::core {

struct ExtensionRecord {
    std::string id;
    std::string version;
    std::string name;
    std::string entry_point;
    std::vector<std::string> permissions;
    std::vector<std::string> dependencies;
    std::string source_path;
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
};

IExtensionRegistry& get_extension_registry_interface();
void initialize_extension_registry();

// Test hook: reset all registered extensions.
void reset_extension_registry_for_tests();

}  // namespace sentinel::core
