#include "bootstrap.h"
#include "../../services/policy/policy.h"
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace sentinel::core {

namespace {
struct ParsedCapability {
    std::string token;
    std::string required_capability;
};

ParsedCapability parse_capability_request(const std::string& subsystem_name, const std::string& capability_input) {
    ParsedCapability parsed{};
    const auto sep = capability_input.find('|');
    if (sep == std::string::npos) {
        parsed.token = capability_input;
        parsed.required_capability = subsystem_name + ":access";
        return parsed;
    }

    parsed.token = capability_input.substr(0, sep);
    parsed.required_capability = capability_input.substr(sep + 1);
    if (parsed.required_capability.empty()) {
        parsed.required_capability = subsystem_name + ":access";
    }
    return parsed;
}
}  // namespace

/// @brief Default bootstrap implementation
class BootstrapImpl : public IBootstrap {
public:
    BootstrapImpl() : initialized_(false), version_("0.1.0") {}

    BootstrapResult initialize() override {
        if (initialized_) {
            return {BootstrapStatus::Success, "Runtime already initialized"};
        }

        try {
            // Fire "pre_init" event to allow subsystems to initialize
            fire_lifecycle_event("pre_init");
            
            // Mark runtime as initialized
            initialized_ = true;
            fire_lifecycle_event("initialized");
            return {BootstrapStatus::Success, "Core runtime initialized successfully"};
        } catch (const std::exception& e) {
            return {BootstrapStatus::InitializationFailed, std::string("Initialization failed: ") + e.what()};
        }
    }

    BootstrapResult shutdown(unsigned int timeout_ms = 5000) override {
        if (!initialized_) {
            return {BootstrapStatus::Success, "Runtime not running"};
        }

        try {
            fire_lifecycle_event("shutting_down");

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            std::unique_lock<std::mutex> lock(shutdown_mutex_);
            const bool drained = shutdown_cv_.wait_until(lock, deadline, [this]() {
                return in_flight_operations_ == 0;
            });

            if (!drained) {
                return {BootstrapStatus::InitializationFailed, "Shutdown timed out waiting for in-flight operations"};
            }

            // TODO: Close policy engine connections and cleanup namespace state.
            fire_lifecycle_event("shutdown_complete");
            
            initialized_ = false;
            return {BootstrapStatus::Success, "Core runtime shutdown complete"};
        } catch (const std::exception& e) {
            return {BootstrapStatus::InitializationFailed, std::string("Shutdown failed: ") + e.what()};
        }
    }

    void on_lifecycle_event(const std::string& event_name, LifecycleCallback callback) override {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        callbacks_[event_name].push_back(callback);
    }

    std::string get_version() const override {
        return version_;
    }

    bool is_ready() const override {
        return initialized_;
    }

private:
    void fire_lifecycle_event(const std::string& event_name) {
        std::vector<LifecycleCallback> event_callbacks;
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            auto it = callbacks_.find(event_name);
            if (it != callbacks_.end()) {
                event_callbacks = it->second;
            }
        }

        for (const auto& callback : event_callbacks) {
            try {
                callback(event_name);
            } catch (const std::exception& e) {
                std::cerr << "Error in lifecycle callback: " << e.what() << std::endl;
            }
        }
    }

    bool initialized_;
    std::string version_;
    std::mutex callbacks_mutex_;
    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;
    std::size_t in_flight_operations_ = 0;
    std::unordered_map<std::string, std::vector<LifecycleCallback>> callbacks_;
};

/// @brief Default runtime implementation
class RuntimeImpl : public IRuntime {
public:
    RuntimeImpl() : bootstrap_(std::make_unique<BootstrapImpl>()) {
        bootstrap_->on_lifecycle_event("pre_init", [](const std::string&) {
            std::cout << "bootstrap event: pre_init" << std::endl;
        });

        bootstrap_->on_lifecycle_event("initialized", [](const std::string&) {
            std::cout << "bootstrap event: initialized" << std::endl;
        });
    }

    IBootstrap& bootstrap() override {
        return *bootstrap_;
    }

    void* get_subsystem(const std::string& subsystem_name, const std::string& capability) override {
        auto it = subsystems_.find(subsystem_name);
        if (it == subsystems_.end()) {
            return nullptr;
        }

        const auto request = parse_capability_request(subsystem_name, capability);
        if (request.token.empty()) {
            std::cerr << "Capability denied for subsystem '" << subsystem_name << "': empty token" << std::endl;
            return nullptr;
        }

        auto& policy_service = sentinel::services::policy::get_policy_service_interface();
        const auto verification = policy_service.capability_engine().verify_token(request.token, request.required_capability);
        if (!verification.is_valid) {
            std::cerr << "Capability denied for subsystem '" << subsystem_name << "': token validation failed" << std::endl;
            return nullptr;
        }

        return it->second;
    }

    bool register_subsystem(const std::string& subsystem_name, void* subsystem_ptr) override {
        if (subsystems_.find(subsystem_name) != subsystems_.end()) {
            return false;  // Already registered
        }
        subsystems_[subsystem_name] = subsystem_ptr;
        return true;
    }

    std::vector<std::string> list_subsystems() const override {
        std::vector<std::string> result;
        for (const auto& pair : subsystems_) {
            result.push_back(pair.first);
        }
        return result;
    }

private:
    std::unique_ptr<IBootstrap> bootstrap_;
    std::unordered_map<std::string, void*> subsystems_;
};

/// @brief Module-level runtime instance (singleton pattern)
static RuntimeImpl* g_runtime = nullptr;

RuntimeImpl& get_runtime() {
    if (g_runtime == nullptr) {
        g_runtime = new RuntimeImpl();
    }
    return *g_runtime;
}

void initialize_runtime() {
    if (g_runtime == nullptr) {
        g_runtime = new RuntimeImpl();
    }
}

IRuntime& get_runtime_interface() {
    return get_runtime();
}

}  // namespace sentinel::core
