#include "namespace.h"
#include <unordered_map>
#include <iostream>
#include <cctype>

namespace sentinel::services::namespace_service {

namespace {
constexpr uint64_t kEstimatedEntryMemoryBytes = 4096;
constexpr uint64_t kDefaultQuotaMemoryBytes = 1024 * 1024 * 100;
constexpr uint32_t kDefaultQuotaFileHandles = 1024;
constexpr uint32_t kDefaultQuotaThreads = 16;
constexpr uint32_t kDefaultQuotaNetworkConnections = 32;

bool is_valid_identifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    for (char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }

    return true;
}

std::map<std::string, uint64_t> zero_usage_metrics() {
    return {
        {"memory_bytes", 0},
        {"file_handles", 0},
        {"threads", 0},
        {"network_connections", 0},
    };
}

ResourceQuota quota_with_defaults(const ResourceQuota& input) {
    ResourceQuota result = input;
    if (result.max_memory_bytes == 0) {
        result.max_memory_bytes = kDefaultQuotaMemoryBytes;
    }
    if (result.max_file_handles == 0) {
        result.max_file_handles = kDefaultQuotaFileHandles;
    }
    if (result.max_threads == 0) {
        result.max_threads = kDefaultQuotaThreads;
    }
    if (result.max_network_connections == 0) {
        result.max_network_connections = kDefaultQuotaNetworkConnections;
    }
    return result;
}

bool quota_is_valid(const ResourceQuota& quota) {
    return quota.max_memory_bytes > 0 &&
           quota.max_file_handles > 0 &&
           quota.max_threads > 0 &&
           quota.max_network_connections > 0;
}

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
        if (!is_valid_identifier(namespace_id) || !is_valid_identifier(owner_id)) {
            return false;
        }

        const auto existing = namespaces_.find(namespace_id);
        if (existing != namespaces_.end()) {
            return false;  // Already exists; preserve existing state
        }

        const ResourceQuota normalized_quota = quota_with_defaults(quota);
        if (!quota_is_valid(normalized_quota)) {
            return false;
        }

        NamespaceInfo info;
        info.owner_id = owner_id;
        info.quota = normalized_quota;
        info.entries = {};
        info.usage_metrics = zero_usage_metrics();
        namespaces_.emplace(namespace_id, std::move(info));
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
            return it->second.usage_metrics;
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
