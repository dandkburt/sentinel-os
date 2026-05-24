#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace sentinel::core {

/// @brief Bootstrap status codes
enum class BootstrapStatus {
    Success = 0,
    InitializationFailed = 1,
    DependencyMissing = 2,
    InvalidConfiguration = 3,
};

/// @brief Result type for bootstrap operations
struct BootstrapResult {
    BootstrapStatus status;
    std::string message;
    bool is_success() const { return status == BootstrapStatus::Success; }
};

/// @brief Callback type for lifecycle events
using LifecycleCallback = std::function<void(const std::string&)>;

/// @brief Core runtime initialization and lifecycle management
class IBootstrap {
public:
    virtual ~IBootstrap() = default;

    /// @brief Initialize the core runtime
    /// @return Bootstrap result with status and diagnostic message
    virtual BootstrapResult initialize() = 0;

    /// @brief Shutdown the core runtime cleanly
    /// @param timeout_ms Timeout in milliseconds for graceful shutdown
    /// @return Bootstrap result with status
    virtual BootstrapResult shutdown(unsigned int timeout_ms = 5000) = 0;

    /// @brief Register a lifecycle event callback
    /// @param event_name The event to subscribe to (e.g., "initialized", "shutting_down")
    /// @param callback Function to invoke when event fires
    virtual void on_lifecycle_event(const std::string& event_name, LifecycleCallback callback) = 0;

    /// @brief Get runtime version
    /// @return Semantic version string (e.g., "0.1.0")
    virtual std::string get_version() const = 0;

    /// @brief Check if runtime is initialized and ready
    virtual bool is_ready() const = 0;
};

/// @brief Core runtime facade providing access to subsystems
class IRuntime {
public:
    virtual ~IRuntime() = default;

    /// @brief Get the bootstrap interface for initialization
    virtual IBootstrap& bootstrap() = 0;

    /// @brief Get a named subsystem by capability
    /// @param subsystem_name Name of the subsystem (e.g., "policy", "namespace", "shell")
    /// @param capability Capability token to authorize access
    /// @return Opaque handle to subsystem or nullptr if not available
    virtual void* get_subsystem(const std::string& subsystem_name, const std::string& capability) = 0;

    /// @brief Register a subsystem with the runtime
    /// @param subsystem_name Unique subsystem identifier
    /// @param subsystem_ptr Opaque pointer to subsystem implementation
    /// @return true if registration succeeded
    virtual bool register_subsystem(const std::string& subsystem_name, void* subsystem_ptr) = 0;

    /// @brief List all registered subsystems
    /// @return Vector of subsystem names
    virtual std::vector<std::string> list_subsystems() const = 0;
};

}  // namespace sentinel::core

// Public API for accessing the global runtime instance
namespace sentinel::core {
    /// @brief Get the global runtime instance
    IRuntime& get_runtime_interface();

    /// @brief Initialize the core runtime (must be called before use)
    void initialize_runtime();
}
