#include "shell.h"
#include <unordered_map>
#include <iostream>
#include <memory>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <cctype>

namespace sentinel::shell::desktop {

namespace {
constexpr const char* kDesktopBootstrapFailStepEnv = "SENTINEL_DESKTOP_BOOTSTRAP_FAIL_STEP";

enum class DesktopLifecycleState {
    Uninitialized,
    Initializing,
    Ready,
    Failed,
    ShuttingDown,
};

std::string read_env_var(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return "";
    }
    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? "" : std::string(value);
#endif
}

std::string to_lower_ascii(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return input;
}
}  // namespace

/// @brief Default window manager implementation
class WindowManagerImpl : public IWindowManager {
public:
    std::string create_window(const WindowProperties& props) override {
        // TODO: Create native window resource
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::string window_id = "window_" + std::to_string(windows_.size());
        WindowProperties stored_props = props;
        stored_props.window_id = window_id;
        windows_[window_id] = stored_props;
        z_order_.push_back(window_id);
        return window_id;
    }

    bool destroy_window(const std::string& window_id) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = windows_.find(window_id);
        if (it != windows_.end()) {
            window_callbacks_.erase(window_id);
            z_order_.erase(std::remove(z_order_.begin(), z_order_.end(), window_id), z_order_.end());
            windows_.erase(it);
            return true;
        }
        return false;
    }

    WindowProperties get_window(const std::string& window_id) const override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = windows_.find(window_id);
        if (it != windows_.end()) {
            return it->second;
        }
        return WindowProperties{"", "", 0, 0, 0, 0, WindowState::Hidden, false, false, ""};
    }

    bool update_window(const WindowProperties& props) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = windows_.find(props.window_id);
        if (it != windows_.end()) {
            it->second = props;
            return true;
        }
        return false;
    }

    void on_window_event(const std::string& window_id, const std::string& event_name, 
                        WindowEventCallback callback) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        window_callbacks_[window_id][event_name].push_back(callback);
    }

    std::vector<std::string> list_windows(const std::string& owner_extension_id) const override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        std::vector<std::string> result;
        for (const auto& pair : windows_) {
            if (pair.second.owner_extension_id == owner_extension_id) {
                result.push_back(pair.first);
            }
        }
        return result;
    }

    bool bring_to_front(const std::string& window_id) override {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = windows_.find(window_id);
        if (it == windows_.end()) {
            return false;
        }

        z_order_.erase(std::remove(z_order_.begin(), z_order_.end(), window_id), z_order_.end());
        z_order_.push_back(window_id);
        return true;
    }

    void clear_windows() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        windows_.clear();
        window_callbacks_.clear();
        z_order_.clear();
    }

private:
    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, WindowProperties> windows_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<WindowEventCallback>>> window_callbacks_;
    std::vector<std::string> z_order_;
};

/// @brief Default desktop shell implementation
class DesktopShellImpl : public IDesktopShell {
public:
    DesktopShellImpl() 
        : window_manager_(std::make_unique<WindowManagerImpl>()),
          initialized_(false),
          lifecycle_state_(DesktopLifecycleState::Uninitialized),
          version_("0.1.0") {}

    bool initialize() override {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        if (lifecycle_state_ == DesktopLifecycleState::Ready) {
            return true;
        }
        if (lifecycle_state_ == DesktopLifecycleState::Initializing ||
            lifecycle_state_ == DesktopLifecycleState::ShuttingDown) {
            return false;
        }

        lifecycle_state_ = DesktopLifecycleState::Initializing;
        last_bootstrap_error_.clear();

        try {
            if (!connect_display_server()) {
                return fail_initialization("display-connection");
            }
            if (!create_root_window_scaffold()) {
                return fail_initialization("root-window-scaffold");
            }
            if (!register_shell_event_handlers()) {
                return fail_initialization("event-handler-registration");
            }
            if (!start_input_event_routing()) {
                return fail_initialization("input-routing");
            }

            initialized_ = true;
            lifecycle_state_ = DesktopLifecycleState::Ready;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Desktop shell initialization failed: " << e.what() << std::endl;
            return fail_initialization("exception");
        }
    }

    bool shutdown() override {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        if (lifecycle_state_ == DesktopLifecycleState::ShuttingDown) {
            return false;
        }
        if (lifecycle_state_ == DesktopLifecycleState::Uninitialized && !initialized_) {
            return true;
        }

        lifecycle_state_ = DesktopLifecycleState::ShuttingDown;

        try {
            if (auto* wm = dynamic_cast<WindowManagerImpl*>(window_manager_.get())) {
                wm->clear_windows();
            }
            notifications_.clear();
            cleanup_platform_scaffold();
            initialized_ = false;
            lifecycle_state_ = DesktopLifecycleState::Uninitialized;
            last_bootstrap_error_.clear();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Desktop shell shutdown failed: " << e.what() << std::endl;
            lifecycle_state_ = DesktopLifecycleState::Failed;
            return false;
        }
    }

    IWindowManager& window_manager() override {
        return *window_manager_;
    }

    bool set_active_taskbar_item(const std::string& window_id) override {
        if (!is_ready()) {
            return false;
        }
        return window_manager_->bring_to_front(window_id);
    }

    std::string show_notification(const std::string& title, const std::string& message, 
                                 unsigned int duration_ms = 0) override {
        if (!is_ready()) {
            return "";
        }

        // TODO: Call platform-native notification API.
        std::string notification_id = "notif_" + std::to_string(notifications_.size());
        notifications_[notification_id] = {
            title,
            message,
            duration_ms,
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count())};
        return notification_id;
    }

    bool is_ready() const override {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        return lifecycle_state_ == DesktopLifecycleState::Ready && initialized_;
    }

    std::string get_version() const override {
        return version_;
    }

private:
    bool should_fail_bootstrap_step(const char* step_name) const {
        const std::string configured = to_lower_ascii(read_env_var(kDesktopBootstrapFailStepEnv));
        if (configured.empty()) {
            return false;
        }
        return configured == "any" || configured == to_lower_ascii(step_name);
    }

    bool connect_display_server() {
        if (should_fail_bootstrap_step("display")) {
            return false;
        }
        platform_display_connected_ = true;
        return true;
    }

    bool create_root_window_scaffold() {
        if (should_fail_bootstrap_step("root")) {
            return false;
        }
        root_window_ready_ = true;
        return true;
    }

    bool register_shell_event_handlers() {
        if (should_fail_bootstrap_step("events")) {
            return false;
        }
        shell_event_handlers_registered_ = true;
        return true;
    }

    bool start_input_event_routing() {
        if (should_fail_bootstrap_step("input")) {
            return false;
        }
        input_routing_active_ = true;
        return true;
    }

    void cleanup_platform_scaffold() {
        input_routing_active_ = false;
        shell_event_handlers_registered_ = false;
        root_window_ready_ = false;
        platform_display_connected_ = false;
    }

    bool fail_initialization(const std::string& reason) {
        last_bootstrap_error_ = reason;
        cleanup_platform_scaffold();
        initialized_ = false;
        lifecycle_state_ = DesktopLifecycleState::Failed;
        std::cerr << "Desktop shell initialization failed at step: " << reason << std::endl;
        return false;
    }

    mutable std::mutex lifecycle_mutex_;
    std::unique_ptr<IWindowManager> window_manager_;
    bool initialized_;
    DesktopLifecycleState lifecycle_state_;
    std::string last_bootstrap_error_;
    std::string version_;
    bool platform_display_connected_ = false;
    bool root_window_ready_ = false;
    bool shell_event_handlers_registered_ = false;
    bool input_routing_active_ = false;
    struct NotificationInfo {
        std::string title;
        std::string message;
        unsigned int duration_ms;
        uint64_t created_at_ms;
    };
    std::unordered_map<std::string, NotificationInfo> notifications_;
};

static DesktopShellImpl* g_desktop_shell = nullptr;

DesktopShellImpl& get_desktop_shell() {
    if (g_desktop_shell == nullptr) {
        g_desktop_shell = new DesktopShellImpl();
    }
    return *g_desktop_shell;
}

void initialize_desktop_shell() {
    if (g_desktop_shell == nullptr) {
        g_desktop_shell = new DesktopShellImpl();
    }
}

IDesktopShell& get_desktop_shell_interface() {
    return get_desktop_shell();
}

}  // namespace sentinel::shell::desktop
