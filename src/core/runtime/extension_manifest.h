#pragma once

#include <string>
#include <vector>

namespace sentinel::core {

struct ExtensionManifest {
    std::string id;
    std::string version;
    std::string name;
    std::string entry_point;

    std::string description;
    std::vector<std::string> permissions;
    std::vector<std::string> dependencies;
};

bool parse_extension_manifest_text(const std::string& text,
                                   ExtensionManifest& out,
                                   std::string& error);

bool validate_extension_manifest(const ExtensionManifest& manifest,
                                 std::string& error);

}  // namespace sentinel::core
