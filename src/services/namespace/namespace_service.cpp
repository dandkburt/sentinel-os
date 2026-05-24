#include "namespace.h"
#include <unordered_map>
#include <iostream>

namespace sentinel::services::namespace_service {

namespace {
constexpr uint64_t kEstimatedEntryMemoryBytes = 4096;

bool is_valid_entry_path(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

std::map<std::string, uint64_t> calculate_usage(const std::vector<NamespaceEntry>& entries) {
    std::map<std::string, uint64_t> usage;
    usage["memory_bytes"] = 0;
    usage["file_handles"] = 0;
    usage["threads"] = 0;
    usage["network_connections"] = 0;

    for (const auto& entry : entries) {
        usage["memory_bytes"] += kEstimatedEntryMemoryBytes;

        switch (entry.type) {
            case NamespaceType::File:
                usage["file_handles"] += 1;
                break;
            case NamespaceType::Socket:
                usage["network_connections"] += 1;
                break;
            case NamespaceType::Process:
                usage["threads"] += 1;
                break;
            case NamespaceType::Memory:
                break;
        }
    }

    return usage;
}

bool usage_within_quota(const std::map<std::string, uint64_t>& usage, const ResourceQuota& quota) {
    return usage.at("memory_bytes") <= quota.max_memory_bytes &&
           usage.at("file_handles") <= quota.max_file_handles &&
           usage.at("threads") <= quota.max_threads &&
           usage.at("network_connections") <= quota.max_network_connections;
}
}  // namespace

/// @brief Default namespace manager implementation
class NamespaceManagerImpl : public INamespaceManager {
public:
    bool create_namespace(const std::string& namespace_id, const std::string& owner_id, 
                         const ResourceQuota& quota) override {
        if (namespaces_.find(namespace_id) != namespaces_.end()) {
            return false;  // Already exists
        }

        if (namespace_id.empty() || owner_id.empty()) {
            return false;
        }

        NamespaceInfo info{owner_id, quota, {}};
        info.usage_metrics["memory_bytes"] = 0;
        info.usage_metrics["file_handles"] = 0;
        info.usage_metrics["threads"] = 0;
        info.usage_metrics["network_connections"] = 0;
        namespaces_[namespace_id] = info;
        return true;
    }

    bool remove_namespace(const std::string& namespace_id) override {
        auto it = namespaces_.find(namespace_id);
        if (it != namespaces_.end()) {
            it->second.entries.clear();
            it->second.usage_metrics = calculate_usage(it->second.entries);
            namespaces_.erase(it);
            return true;
        }
        return false;
    }

    bool add_entry(const std::string& namespace_id, const NamespaceEntry& entry) override {
        auto it = namespaces_.find(namespace_id);
        if (it == namespaces_.end()) {
            return false;  // Namespace doesn't exist
        }

        if (!is_valid_entry_path(entry.path) || entry.owner_id != it->second.owner_id) {
            return false;
        }

        auto projected_entries = it->second.entries;
        projected_entries.push_back(entry);
        const auto projected_usage = calculate_usage(projected_entries);
        if (!usage_within_quota(projected_usage, it->second.quota)) {
            return false;
        }

        it->second.entries.push_back(entry);
        it->second.usage_metrics = projected_usage;
        return true;
    }

    std::vector<NamespaceEntry> list_entries(const std::string& namespace_id) const override {
        auto it = namespaces_.find(namespace_id);
        if (it != namespaces_.end()) {
            return it->second.entries;
        }
        return {};
    }

    std::map<std::string, uint64_t> get_resource_usage(const std::string& namespace_id) const override {
        auto it = namespaces_.find(namespace_id);
        if (it != namespaces_.end()) {
            return calculate_usage(it->second.entries);
        }
        return {};
    }

    bool set_resource_quota(const std::string& namespace_id, const ResourceQuota& quota) override {
        auto it = namespaces_.find(namespace_id);
        if (it != namespaces_.end()) {
            const auto current_usage = calculate_usage(it->second.entries);
            if (!usage_within_quota(current_usage, quota)) {
                return false;
            }

            it->second.quota = quota;
            it->second.usage_metrics = current_usage;
            return true;
        }
        return false;
    }

    bool namespace_exists(const std::string& namespace_id) const override {
        return namespaces_.find(namespace_id) != namespaces_.end();
    }

private:
    struct NamespaceInfo {
        std::string owner_id;
        ResourceQuota quota;
        std::vector<NamespaceEntry> entries;
        std::map<std::string, uint64_t> usage_metrics;
    };

    std::unordered_map<std::string, NamespaceInfo> namespaces_;
};

static NamespaceManagerImpl* g_namespace_manager = nullptr;

NamespaceManagerImpl& get_namespace_manager() {
    if (g_namespace_manager == nullptr) {
        g_namespace_manager = new NamespaceManagerImpl();
    }
    return *g_namespace_manager;
}

void initialize_namespace_service() {
    if (g_namespace_manager == nullptr) {
        g_namespace_manager = new NamespaceManagerImpl();
    }
}

INamespaceManager& get_namespace_manager_interface() {
    return get_namespace_manager();
}

}  // namespace sentinel::services::namespace_service
