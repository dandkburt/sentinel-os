#include "extension_registry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace sentinel::core {

namespace {

namespace fs = std::filesystem;

std::string normalize_source_path(const fs::path& path) {
    std::error_code ec;
    const fs::path weak = fs::weakly_canonical(path, ec);
    if (!ec) {
        return weak.lexically_normal().string();
    }

    return fs::absolute(path, ec).lexically_normal().string();
}

bool has_manifest_extension(const fs::path& path) {
    return path.extension() == ".manifest";
}

std::string map_manifest_error_to_registry_error(const std::string& manifest_error) {
    if (manifest_error == "malformed-manifest") {
        return "manifest-parse-failed";
    }
    return "manifest-validation-failed";
}

class ExtensionRegistryImpl : public IExtensionRegistry {
public:
    bool register_extension_from_manifest_path(const std::string& manifest_path,
                                               std::string& error) override {
        if (manifest_path.empty()) {
            error = "source-not-found";
            return false;
        }

        const fs::path source_path(manifest_path);
        std::error_code ec;
        if (!fs::exists(source_path, ec)) {
            error = "source-not-found";
            return false;
        }
        if (!fs::is_regular_file(source_path, ec)) {
            error = "source-not-readable";
            return false;
        }

        std::ifstream in(source_path, std::ios::in);
        if (!in.is_open()) {
            error = "source-not-readable";
            return false;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();

        ExtensionManifest manifest;
        std::string manifest_error;
        if (!parse_extension_manifest_text(buffer.str(), manifest, manifest_error)) {
            error = map_manifest_error_to_registry_error(manifest_error);
            return false;
        }

        ExtensionRecord record{
            manifest.id,
            manifest.version,
            manifest.name,
            manifest.entry_point,
            manifest.permissions,
            manifest.dependencies,
            normalize_source_path(source_path)
        };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (records_.find(record.id) != records_.end()) {
                error = "duplicate-extension-id";
                return false;
            }
            records_[record.id] = record;
        }

        error.clear();
        return true;
    }

    std::vector<ExtensionRecord> list_registered_extensions() const override {
        std::vector<ExtensionRecord> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot.reserve(records_.size());
            for (const auto& pair : records_) {
                snapshot.push_back(pair.second);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const ExtensionRecord& a, const ExtensionRecord& b) {
            return a.id < b.id;
        });
        return snapshot;
    }

    bool discover_extensions_in_directory(const std::string& directory_path,
                                          std::vector<std::string>& non_fatal_errors,
                                          std::string& error) override {
        non_fatal_errors.clear();

        if (directory_path.empty()) {
            error = "source-not-found";
            return false;
        }

        const fs::path root(directory_path);
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            error = "source-not-found";
            return false;
        }
        if (!fs::is_directory(root, ec)) {
            error = "source-not-readable";
            return false;
        }

        std::vector<fs::path> manifest_paths;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (ec) {
                error = "source-not-readable";
                return false;
            }

            if (!entry.is_regular_file()) {
                continue;
            }
            if (!has_manifest_extension(entry.path())) {
                continue;
            }
            manifest_paths.push_back(entry.path());
        }

        std::sort(manifest_paths.begin(), manifest_paths.end(), [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });

        for (const auto& manifest_path : manifest_paths) {
            std::string register_error;
            if (!register_extension_from_manifest_path(manifest_path.string(), register_error)) {
                non_fatal_errors.push_back(manifest_path.filename().string() + ":" + register_error);
            }
        }

        error.clear();
        return true;
    }

    void reset_for_tests() {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ExtensionRecord> records_;
};

ExtensionRegistryImpl& ensure_registry() {
    static ExtensionRegistryImpl instance;
    return instance;
}

}  // namespace

IExtensionRegistry& get_extension_registry_interface() {
    return ensure_registry();
}

void initialize_extension_registry() {
    (void)ensure_registry();
}

void reset_extension_registry_for_tests() {
    ensure_registry().reset_for_tests();
}

}  // namespace sentinel::core
