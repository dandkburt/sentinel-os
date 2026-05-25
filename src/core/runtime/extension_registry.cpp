#include "extension_registry.h"

#include <algorithm>
#include <functional>
#include <filesystem>
#include <fstream>
#include <set>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace sentinel::core {

namespace {

namespace fs = std::filesystem;
ExtensionManifestTextLoaderForTests g_manifest_text_loader_for_tests = nullptr;
ExtensionManifestPathEnumeratorForTests g_manifest_path_enumerator_for_tests = nullptr;

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

bool is_valid_lifecycle_fail_step(const std::string& step) {
    return step.empty() || step == "load" || step == "enable" || step == "disable" || step == "unload";
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
        std::string manifest_text;

        if (g_manifest_text_loader_for_tests != nullptr) {
            if (!g_manifest_text_loader_for_tests(manifest_path, manifest_text, error)) {
                if (error.empty()) {
                    error = "source-not-readable";
                }
                return false;
            }
        } else {
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
            manifest_text = buffer.str();
        }

        ExtensionManifest manifest;
        std::string manifest_error;
        if (!parse_extension_manifest_text(manifest_text, manifest, manifest_error)) {
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
            normalize_source_path(source_path),
            ExtensionLifecycleState::Registered
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
        if (g_manifest_path_enumerator_for_tests == nullptr) {
            if (!fs::exists(root, ec)) {
                error = "source-not-found";
                return false;
            }
            if (!fs::is_directory(root, ec)) {
                error = "source-not-readable";
                return false;
            }
        }

        std::vector<fs::path> manifest_paths;
        if (g_manifest_path_enumerator_for_tests != nullptr) {
            std::vector<std::string> enumerated_paths;
            if (!g_manifest_path_enumerator_for_tests(directory_path, enumerated_paths, error)) {
                if (error.empty()) {
                    error = "source-not-readable";
                }
                return false;
            }

            for (const auto& path : enumerated_paths) {
                manifest_paths.push_back(fs::path(path));
            }
        } else {
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

    bool resolve_extension_dependencies(std::vector<std::string>& ordered_ids,
                                        std::string& error) const override {
        std::lock_guard<std::mutex> lock(mutex_);

        std::set<std::string> nodes;
        for (const auto& pair : records_) {
            nodes.insert(pair.first);
        }

        return resolve_nodes_locked(nodes, ordered_ids, error);
    }

    bool resolve_extension_dependencies_for_target(const std::string& extension_id,
                                                   std::vector<std::string>& ordered_ids,
                                                   std::string& error) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.find(extension_id) == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        std::set<std::string> closure;
        if (!collect_transitive_dependencies_locked(extension_id, closure, error)) {
            return false;
        }

        return resolve_nodes_locked(closure, ordered_ids, error);
    }

    bool get_dependencies(const std::string& extension_id,
                          std::vector<std::string>& dependencies,
                          std::string& error) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        dependencies = it->second.dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        error.clear();
        return true;
    }

    bool get_dependents(const std::string& extension_id,
                        std::vector<std::string>& dependents,
                        std::string& error) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (records_.find(extension_id) == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        dependents.clear();
        for (const auto& pair : records_) {
            const auto& deps = pair.second.dependencies;
            if (std::find(deps.begin(), deps.end(), extension_id) != deps.end()) {
                dependents.push_back(pair.first);
            }
        }

        std::sort(dependents.begin(), dependents.end());
        error.clear();
        return true;
    }

    bool load_extension(const std::string& extension_id,
                        std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        ExtensionRecord& record = it->second;
        if (record.lifecycle_state == ExtensionLifecycleState::Loaded ||
            record.lifecycle_state == ExtensionLifecycleState::Enabled ||
            record.lifecycle_state == ExtensionLifecycleState::Disabled) {
            error = "already-loaded";
            return false;
        }

        if (record.lifecycle_state != ExtensionLifecycleState::Registered &&
            record.lifecycle_state != ExtensionLifecycleState::Unloaded &&
            record.lifecycle_state != ExtensionLifecycleState::Failed) {
            error = "invalid-state-transition";
            return false;
        }

        const ExtensionLifecycleState prior = record.lifecycle_state;
        if (lifecycle_fail_step_ == "load") {
            record.lifecycle_state = prior;
            error = "load-failed";
            return false;
        }

        record.lifecycle_state = ExtensionLifecycleState::Loaded;
        error.clear();
        return true;
    }

    bool enable_extension(const std::string& extension_id,
                          std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        ExtensionRecord& record = it->second;
        if (record.lifecycle_state == ExtensionLifecycleState::Enabled) {
            error = "already-enabled";
            return false;
        }
        if (record.lifecycle_state != ExtensionLifecycleState::Loaded &&
            record.lifecycle_state != ExtensionLifecycleState::Disabled) {
            error = "invalid-state-transition";
            return false;
        }

        for (const auto& dependency_id : record.dependencies) {
            auto dep_it = records_.find(dependency_id);
            if (dep_it == records_.end()) {
                error = "dependency-not-found";
                return false;
            }
            if (dep_it->second.lifecycle_state != ExtensionLifecycleState::Enabled) {
                error = "dependency-resolution-failed";
                return false;
            }
        }

        const ExtensionLifecycleState prior = record.lifecycle_state;
        if (lifecycle_fail_step_ == "enable") {
            record.lifecycle_state = prior;
            error = "enable-failed";
            return false;
        }

        record.lifecycle_state = ExtensionLifecycleState::Enabled;
        error.clear();
        return true;
    }

    bool disable_extension(const std::string& extension_id,
                           std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        ExtensionRecord& record = it->second;
        if (record.lifecycle_state == ExtensionLifecycleState::Disabled) {
            error = "already-disabled";
            return false;
        }
        if (record.lifecycle_state != ExtensionLifecycleState::Enabled) {
            error = "invalid-state-transition";
            return false;
        }

        const ExtensionLifecycleState prior = record.lifecycle_state;
        if (lifecycle_fail_step_ == "disable") {
            record.lifecycle_state = prior;
            error = "disable-failed";
            return false;
        }

        record.lifecycle_state = ExtensionLifecycleState::Disabled;
        error.clear();
        return true;
    }

    bool unload_extension(const std::string& extension_id,
                          std::string& error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        ExtensionRecord& record = it->second;
        if (record.lifecycle_state == ExtensionLifecycleState::Enabled) {
            error = "invalid-state-transition";
            return false;
        }
        if (record.lifecycle_state == ExtensionLifecycleState::Registered ||
            record.lifecycle_state == ExtensionLifecycleState::Unloaded) {
            error = "already-unloaded";
            return false;
        }
        if (record.lifecycle_state != ExtensionLifecycleState::Loaded &&
            record.lifecycle_state != ExtensionLifecycleState::Disabled &&
            record.lifecycle_state != ExtensionLifecycleState::Failed) {
            error = "invalid-state-transition";
            return false;
        }

        const ExtensionLifecycleState prior = record.lifecycle_state;
        if (lifecycle_fail_step_ == "unload") {
            record.lifecycle_state = prior;
            error = "unload-failed";
            return false;
        }

        record.lifecycle_state = ExtensionLifecycleState::Unloaded;
        error.clear();
        return true;
    }

    bool get_extension_state(const std::string& extension_id,
                             ExtensionLifecycleState& out_state,
                             std::string& error) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        out_state = it->second.lifecycle_state;
        error.clear();
        return true;
    }

    void reset_for_tests() {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
        lifecycle_fail_step_.clear();
    }

    void set_lifecycle_fail_step_for_tests(const std::string& step) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_valid_lifecycle_fail_step(step)) {
            lifecycle_fail_step_.clear();
            return;
        }
        lifecycle_fail_step_ = step;
    }

    bool set_dependencies_for_tests(const std::string& extension_id,
                                    const std::vector<std::string>& dependencies,
                                    std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(extension_id);
        if (it == records_.end()) {
            error = "extension-not-found";
            return false;
        }

        it->second.dependencies = dependencies;
        error.clear();
        return true;
    }

    void reset_lifecycle_fail_step_for_tests() {
        std::lock_guard<std::mutex> lock(mutex_);
        lifecycle_fail_step_.clear();
    }

private:
    bool collect_transitive_dependencies_locked(const std::string& extension_id,
                                                std::set<std::string>& closure,
                                                std::string& error) const {
        std::set<std::string> visiting;
        std::function<bool(const std::string&)> visit = [&](const std::string& current_id) -> bool {
            auto it = records_.find(current_id);
            if (it == records_.end()) {
                error = "dependency-not-found";
                return false;
            }

            if (closure.find(current_id) != closure.end()) {
                return true;
            }

            if (visiting.find(current_id) != visiting.end()) {
                error = "dependency-cycle-detected";
                return false;
            }

            visiting.insert(current_id);
            for (const auto& dependency_id : it->second.dependencies) {
                if (dependency_id == current_id) {
                    error = "dependency-self-reference";
                    return false;
                }
                if (!visit(dependency_id)) {
                    return false;
                }
            }
            visiting.erase(current_id);
            closure.insert(current_id);
            return true;
        };

        if (!visit(extension_id)) {
            return false;
        }

        error.clear();
        return true;
    }

    bool resolve_nodes_locked(const std::set<std::string>& nodes,
                              std::vector<std::string>& ordered_ids,
                              std::string& error) const {
        ordered_ids.clear();

        std::unordered_map<std::string, std::size_t> indegree;
        std::unordered_map<std::string, std::set<std::string>> adjacency;
        for (const auto& id : nodes) {
            indegree[id] = 0;
            adjacency[id] = {};
        }

        for (const auto& id : nodes) {
            auto it = records_.find(id);
            if (it == records_.end()) {
                error = "dependency-resolution-failed";
                return false;
            }

            for (const auto& dependency_id : it->second.dependencies) {
                if (dependency_id == id) {
                    error = "dependency-self-reference";
                    return false;
                }

                if (records_.find(dependency_id) == records_.end()) {
                    error = "dependency-not-found";
                    return false;
                }

                if (nodes.find(dependency_id) == nodes.end()) {
                    continue;
                }

                if (adjacency[dependency_id].insert(id).second) {
                    indegree[id] += 1;
                }
            }
        }

        std::set<std::string> ready;
        for (const auto& pair : indegree) {
            if (pair.second == 0) {
                ready.insert(pair.first);
            }
        }

        while (!ready.empty()) {
            const std::string next = *ready.begin();
            ready.erase(ready.begin());
            ordered_ids.push_back(next);

            for (const auto& dependent : adjacency[next]) {
                auto dep_it = indegree.find(dependent);
                if (dep_it == indegree.end()) {
                    error = "dependency-resolution-failed";
                    return false;
                }

                if (dep_it->second > 0) {
                    dep_it->second -= 1;
                    if (dep_it->second == 0) {
                        ready.insert(dependent);
                    }
                }
            }
        }

        if (ordered_ids.size() != nodes.size()) {
            ordered_ids.clear();
            error = "dependency-cycle-detected";
            return false;
        }

        error.clear();
        return true;
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ExtensionRecord> records_;
    std::string lifecycle_fail_step_;
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

void set_extension_lifecycle_fail_step_for_tests(const std::string& step) {
    ensure_registry().set_lifecycle_fail_step_for_tests(step);
}

void reset_extension_lifecycle_fail_step_for_tests() {
    ensure_registry().reset_lifecycle_fail_step_for_tests();
}

bool set_extension_dependencies_for_tests(const std::string& extension_id,
                                          const std::vector<std::string>& dependencies,
                                          std::string& error) {
    return ensure_registry().set_dependencies_for_tests(extension_id, dependencies, error);
}

void set_extension_manifest_text_loader_for_tests(ExtensionManifestTextLoaderForTests loader) {
    g_manifest_text_loader_for_tests = loader;
}

void reset_extension_manifest_text_loader_for_tests() {
    g_manifest_text_loader_for_tests = nullptr;
}

void set_extension_manifest_path_enumerator_for_tests(ExtensionManifestPathEnumeratorForTests enumerator) {
    g_manifest_path_enumerator_for_tests = enumerator;
}

void reset_extension_manifest_path_enumerator_for_tests() {
    g_manifest_path_enumerator_for_tests = nullptr;
}

}  // namespace sentinel::core
