#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace sentinel::services::namespace_service {

/// @brief Namespace isolation type
enum class NamespaceType {
    File,      // Virtual file system namespace
    Socket,    // Virtual socket namespace
    Memory,    // Virtual memory isolation
    Process,   // Process namespace
};

/// @brief Virtual namespace entry
struct NamespaceEntry {
    std::string path;
    NamespaceType type;
    std::string owner_id;  // Extension or component ID that owns this namespace entry
    bool is_writable;
};

/// @brief Namespace resource quota
struct ResourceQuota {
    uint64_t max_memory_bytes;
    uint32_t max_file_handles;
    uint32_t max_threads;
    uint32_t max_network_connections;
};

/// @brief Manager for virtual namespaces and resource isolation
class INamespaceManager {
public:
    virtual ~INamespaceManager() = default;

    /// @brief Create a new namespace for an extension
    /// @param namespace_id Unique namespace identifier
    /// @param owner_id Extension or component ID owning this namespace
    /// @param quota Resource quotas for this namespace
    /// @return true if namespace creation succeeded
    virtual bool create_namespace(const std::string& namespace_id, const std::string& owner_id, 
                                 const ResourceQuota& quota) = 0;

    /// @brief Remove a namespace
    /// @param namespace_id Namespace to remove
    /// @return true if removal succeeded
    virtual bool remove_namespace(const std::string& namespace_id) = 0;

    /// @brief Add an entry to a namespace
    /// @param namespace_id Target namespace
    /// @param entry The namespace entry to add
    /// @return true if entry was added
    virtual bool add_entry(const std::string& namespace_id, const NamespaceEntry& entry) = 0;

    /// @brief Get all entries in a namespace
    /// @param namespace_id Target namespace
    /// @return Vector of namespace entries
    virtual std::vector<NamespaceEntry> list_entries(const std::string& namespace_id) const = 0;

    /// @brief Get resource usage for a namespace
    /// @param namespace_id Target namespace
    /// @return Map of resource names to current usage (e.g., {"memory_bytes" -> 104857600})
    virtual std::map<std::string, uint64_t> get_resource_usage(const std::string& namespace_id) const = 0;

    /// @brief Enforce resource quota for a namespace
    /// @param namespace_id Target namespace
    /// @param quota New quotas to apply
    /// @return true if quota enforcement succeeded
    virtual bool set_resource_quota(const std::string& namespace_id, const ResourceQuota& quota) = 0;

    /// @brief Check if a namespace exists
    /// @param namespace_id Namespace identifier
    /// @return true if namespace exists
    virtual bool namespace_exists(const std::string& namespace_id) const = 0;
};

}  // namespace sentinel::services::namespace_service

// Public API for accessing the global namespace manager instance
namespace sentinel::services::namespace_service {
    /// @brief Get the global namespace manager instance
    INamespaceManager& get_namespace_manager_interface();

    /// @brief Initialize the namespace service (must be called before use)
    void initialize_namespace_service();
}
