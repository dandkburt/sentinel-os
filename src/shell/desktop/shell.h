#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace sentinel::shell::desktop {

/// @brief Window state enumeration
enum class WindowState {
    Hidden = 0,
    Minimized = 1,
    Normal = 2,
    Maximized = 3,
    FullScreen = 4,
};

/// @brief Window properties
struct WindowProperties {
    std::string window_id;
    std::string title;
    int x, y, width, height;
    WindowState state;
    bool is_resizable;
    bool is_modal;
    std::string owner_extension_id;  // Extension that created this window
};

/// @brief Window lifecycle event callback
using WindowEventCallback = std::function<void(const WindowProperties&, const std::string& event)>;

/// @brief Window management interface
class IWindowManager {
public:
    virtual ~IWindowManager() = default;

    /// @brief Create a new window
    /// @param props Initial window properties
    /// @return Window ID if successful, empty string on failure
    virtual std::string create_window(const WindowProperties& props) = 0;

    /// @brief Destroy a window
    /// @param window_id Window to destroy
    /// @return true if window was destroyed
    virtual bool destroy_window(const std::string& window_id) = 0;

    /// @brief Get window properties
    /// @param window_id Window identifier
    /// @return Window properties, or empty if window not found
    virtual WindowProperties get_window(const std::string& window_id) const = 0;

    /// @brief Update window properties
    /// @param props Updated properties
    /// @return true if update succeeded
    virtual bool update_window(const WindowProperties& props) = 0;

    /// @brief Register a window event callback
    /// @param window_id Window to monitor
    /// @param event_name Event to subscribe to (e.g., "closed", "focused", "resized")
    /// @param callback Function to invoke when event occurs
    virtual void on_window_event(const std::string& window_id, const std::string& event_name, WindowEventCallback callback) = 0;

    /// @brief List all windows owned by an extension
    /// @param owner_extension_id Extension identifier
    /// @return Vector of window IDs
    virtual std::vector<std::string> list_windows(const std::string& owner_extension_id) const = 0;

    /// @brief Bring a window to front
    /// @param window_id Window to raise
    /// @return true if successful
    virtual bool bring_to_front(const std::string& window_id) = 0;
};

/// @brief Desktop shell initialization and lifecycle
class IDesktopShell {
public:
    virtual ~IDesktopShell() = default;

    /// @brief Initialize the desktop shell
    /// @return true if initialization succeeded
    virtual bool initialize() = 0;

    /// @brief Shutdown the desktop shell
    /// @return true if shutdown succeeded
    virtual bool shutdown() = 0;

    /// @brief Get the window manager interface
    virtual IWindowManager& window_manager() = 0;

    /// @brief Set the active taskbar item
    /// @param window_id Window to highlight in taskbar
    /// @return true if successful
    virtual bool set_active_taskbar_item(const std::string& window_id) = 0;

    /// @brief Show a notification on the desktop
    /// @param title Notification title
    /// @param message Notification message
    /// @param duration_ms Duration to show notification in milliseconds (0 = persistent)
    /// @return Notification ID
    virtual std::string show_notification(const std::string& title, const std::string& message, 
                                         unsigned int duration_ms = 0) = 0;

    /// @brief Check if shell is ready for rendering
    virtual bool is_ready() const = 0;

    /// @brief Get shell version
    /// @return Semantic version string
    virtual std::string get_version() const = 0;
};

}  // namespace sentinel::shell::desktop

// Public API for accessing the global desktop shell instance
namespace sentinel::shell::desktop {
    /// @brief Get the global desktop shell instance
    IDesktopShell& get_desktop_shell_interface();

    /// @brief Initialize the desktop shell (must be called before use)
    void initialize_desktop_shell();
}
