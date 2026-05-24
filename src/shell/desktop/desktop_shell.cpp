#include "shell.h"
#include <unordered_map>
#include <iostream>
#include <memory>
#include <algorithm>
#include <mutex>
#include <chrono>

namespace sentinel::shell::desktop {

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
          version_("0.1.0") {}

    bool initialize() override {
        if (initialized_) {
            return true;
        }

        try {
            // TODO: Connect to platform display server and input routing.
            initialized_ = true;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Desktop shell initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

    bool shutdown() override {
        if (!initialized_) {
            return true;
        }

        try {
            if (auto* wm = dynamic_cast<WindowManagerImpl*>(window_manager_.get())) {
                wm->clear_windows();
            }
            notifications_.clear();
            initialized_ = false;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Desktop shell shutdown failed: " << e.what() << std::endl;
            return false;
        }
    }

    IWindowManager& window_manager() override {
        return *window_manager_;
    }

    bool set_active_taskbar_item(const std::string& window_id) override {
        return window_manager_->bring_to_front(window_id);
    }

    std::string show_notification(const std::string& title, const std::string& message, 
                                 unsigned int duration_ms = 0) override {
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
        return initialized_;
    }

    std::string get_version() const override {
        return version_;
    }

private:
    std::unique_ptr<IWindowManager> window_manager_;
    bool initialized_;
    std::string version_;
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
