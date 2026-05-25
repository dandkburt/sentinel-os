#include <gtest/gtest.h>
#include "shell.h"
#include <memory>
#include <unordered_map>
#include <map>

using namespace sentinel::shell::desktop;

namespace {

std::unordered_map<std::string, std::string> g_desktop_env_for_tests;

std::string fake_desktop_env_lookup(const std::string& name) {
    auto it = g_desktop_env_for_tests.find(name);
    if (it == g_desktop_env_for_tests.end()) {
        return "";
    }
    return it->second;
}

}  // namespace

class WindowManagerTest : public ::testing::Test {
public:
    class MockWindowManager : public IWindowManager {
    public:
        std::string create_window(const WindowProperties& props) override {
            std::string window_id = "window_" + std::to_string(windows.size());
            WindowProperties stored = props;
            stored.window_id = window_id;
            windows[window_id] = stored;
            return window_id;
        }

        bool destroy_window(const std::string& window_id) override {
            return windows.erase(window_id) > 0;
        }

        WindowProperties get_window(const std::string& window_id) const override {
            auto it = windows.find(window_id);
            if (it != windows.end()) {
                return it->second;
            }
            return WindowProperties{"", "", 0, 0, 0, 0, WindowState::Hidden, false, false, ""};
        }

        bool update_window(const WindowProperties& props) override {
            auto it = windows.find(props.window_id);
            if (it != windows.end()) {
                it->second = props;
                return true;
            }
            return false;
        }

        void on_window_event(const std::string& window_id, const std::string& event_name, 
                            WindowEventCallback callback) override {
            // Mock: no-op
        }

        std::vector<std::string> list_windows(const std::string& owner_extension_id) const override {
            std::vector<std::string> result;
            for (const auto& pair : windows) {
                if (pair.second.owner_extension_id == owner_extension_id) {
                    result.push_back(pair.first);
                }
            }
            return result;
        }

        bool bring_to_front(const std::string& window_id) override {
            return windows.find(window_id) != windows.end();
        }

    private:
        std::unordered_map<std::string, WindowProperties> windows;
    };

protected:

    void SetUp() override {
        window_manager = std::make_unique<MockWindowManager>();
    }

    std::unique_ptr<MockWindowManager> window_manager;
};

TEST_F(WindowManagerTest, CreateWindowSucceeds) {
    WindowProperties props{"", "Test Window", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    auto window_id = window_manager->create_window(props);
    
    EXPECT_FALSE(window_id.empty());
    EXPECT_NE(window_id.find("window_"), std::string::npos);
}

TEST_F(WindowManagerTest, GetWindowProperties) {
    WindowProperties props{"", "Test Window", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    auto window_id = window_manager->create_window(props);
    
    auto retrieved = window_manager->get_window(window_id);
    EXPECT_EQ(retrieved.title, "Test Window");
    EXPECT_EQ(retrieved.width, 800);
    EXPECT_EQ(retrieved.height, 600);
}

TEST_F(WindowManagerTest, UpdateWindowSucceeds) {
    WindowProperties props{"", "Original", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    auto window_id = window_manager->create_window(props);
    
    props.window_id = window_id;
    props.title = "Updated Title";
    props.state = WindowState::Maximized;
    
    bool result = window_manager->update_window(props);
    EXPECT_TRUE(result);
    
    auto retrieved = window_manager->get_window(window_id);
    EXPECT_EQ(retrieved.title, "Updated Title");
    EXPECT_EQ(retrieved.state, WindowState::Maximized);
}

TEST_F(WindowManagerTest, DestroyWindowSucceeds) {
    WindowProperties props{"", "Test", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    auto window_id = window_manager->create_window(props);
    
    bool result = window_manager->destroy_window(window_id);
    EXPECT_TRUE(result);
    
    auto retrieved = window_manager->get_window(window_id);
    EXPECT_TRUE(retrieved.window_id.empty());
}

TEST_F(WindowManagerTest, ListWindowsByExtension) {
    WindowProperties props1{"", "Window 1", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    WindowProperties props2{"", "Window 2", 200, 200, 800, 600, WindowState::Normal, true, false, "ext1"};
    WindowProperties props3{"", "Window 3", 300, 300, 800, 600, WindowState::Normal, true, false, "ext2"};
    
    window_manager->create_window(props1);
    window_manager->create_window(props2);
    window_manager->create_window(props3);
    
    auto windows_ext1 = window_manager->list_windows("ext1");
    EXPECT_EQ(windows_ext1.size(), 2);
    
    auto windows_ext2 = window_manager->list_windows("ext2");
    EXPECT_EQ(windows_ext2.size(), 1);
}

class DesktopShellTest : public ::testing::Test {
protected:
    class MockWindowManagerForShell : public IWindowManager {
    public:
        std::string create_window(const WindowProperties& props) override {
            std::string window_id = "shell_window_" + std::to_string(windows.size());
            WindowProperties stored = props;
            stored.window_id = window_id;
            windows[window_id] = stored;
            return window_id;
        }

        bool destroy_window(const std::string& window_id) override {
            return windows.erase(window_id) > 0;
        }

        WindowProperties get_window(const std::string& window_id) const override {
            auto it = windows.find(window_id);
            if (it != windows.end()) {
                return it->second;
            }
            return WindowProperties{"", "", 0, 0, 0, 0, WindowState::Hidden, false, false, ""};
        }

        bool update_window(const WindowProperties& props) override {
            auto it = windows.find(props.window_id);
            if (it != windows.end()) {
                it->second = props;
                return true;
            }
            return false;
        }

        void on_window_event(const std::string& window_id, const std::string& event_name,
                            WindowEventCallback callback) override {
        }

        std::vector<std::string> list_windows(const std::string& owner_extension_id) const override {
            std::vector<std::string> result;
            for (const auto& pair : windows) {
                if (pair.second.owner_extension_id == owner_extension_id) {
                    result.push_back(pair.first);
                }
            }
            return result;
        }

        bool bring_to_front(const std::string& window_id) override {
            return windows.find(window_id) != windows.end();
        }

    private:
        std::unordered_map<std::string, WindowProperties> windows;
    };

    class MockDesktopShell : public IDesktopShell {
    public:
        MockDesktopShell() : window_manager_(std::make_unique<MockWindowManagerForShell>()), initialized_(false) {}

        bool initialize() override {
            initialized_ = true;
            return true;
        }

        bool shutdown() override {
            initialized_ = false;
            return true;
        }

        IWindowManager& window_manager() override {
            return *window_manager_;
        }

        bool set_active_taskbar_item(const std::string& window_id) override {
            return !window_id.empty();
        }

        std::string show_notification(const std::string& title, const std::string& message, 
                                     unsigned int duration_ms = 0) override {
            return "notif_" + std::to_string(notifications_.size());
        }

        bool is_ready() const override {
            return initialized_;
        }

        std::string get_version() const override {
            return "0.1.0";
        }

    private:
        std::unique_ptr<IWindowManager> window_manager_;
        bool initialized_;
        std::map<std::string, std::pair<std::string, std::string>> notifications_;
    };

    void SetUp() override {
        shell = std::make_unique<MockDesktopShell>();
    }

    std::unique_ptr<MockDesktopShell> shell;
};

TEST_F(DesktopShellTest, InitializeSucceeds) {
    bool result = shell->initialize();
    EXPECT_TRUE(result);
    EXPECT_TRUE(shell->is_ready());
}

TEST_F(DesktopShellTest, ShutdownSucceeds) {
    shell->initialize();
    bool result = shell->shutdown();
    EXPECT_TRUE(result);
    EXPECT_FALSE(shell->is_ready());
}

TEST_F(DesktopShellTest, GetVersion) {
    auto version = shell->get_version();
    EXPECT_EQ(version, "0.1.0");
}

TEST_F(DesktopShellTest, ShowNotification) {
    auto notif_id = shell->show_notification("Title", "Message", 5000);
    EXPECT_FALSE(notif_id.empty());
}

class DesktopShellRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        reset_desktop_env_lookup_for_tests();
        g_desktop_env_for_tests.clear();
        initialize_desktop_shell();
        auto& shell = get_desktop_shell_interface();
        (void)shell.shutdown();
        _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=");
        ASSERT_TRUE(shell.initialize());
    }

    void TearDown() override {
        reset_desktop_env_lookup_for_tests();
        g_desktop_env_for_tests.clear();
        auto& shell = get_desktop_shell_interface();
        (void)shell.shutdown();
        _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=");
    }
};

TEST_F(DesktopShellRealTest, CreateWindowRejectsInvalidOwnerOrDimensions) {
    auto& wm = get_desktop_shell_interface().window_manager();

    WindowProperties empty_owner{"", "NoOwner", 0, 0, 640, 480, WindowState::Normal, true, false, ""};
    EXPECT_TRUE(wm.create_window(empty_owner).empty());

    WindowProperties bad_width{"", "BadWidth", 0, 0, 0, 480, WindowState::Normal, true, false, "ext1"};
    EXPECT_TRUE(wm.create_window(bad_width).empty());

    WindowProperties bad_height{"", "BadHeight", 0, 0, 640, -1, WindowState::Normal, true, false, "ext1"};
    EXPECT_TRUE(wm.create_window(bad_height).empty());
}

TEST_F(DesktopShellRealTest, CreateDestroyCreateProducesMonotonicWindowIds) {
    auto& wm = get_desktop_shell_interface().window_manager();
    WindowProperties props{"", "Monotonic", 0, 0, 320, 240, WindowState::Normal, true, false, "ext_create"};

    const auto first_id = wm.create_window(props);
    ASSERT_FALSE(first_id.empty());
    EXPECT_TRUE(wm.destroy_window(first_id));

    const auto second_id = wm.create_window(props);
    ASSERT_FALSE(second_id.empty());
    EXPECT_NE(first_id, second_id);

    EXPECT_TRUE(wm.destroy_window(second_id));
}

TEST_F(DesktopShellRealTest, CreateWindowFailsWhenPlatformAllocationFails) {
    auto& wm = get_desktop_shell_interface().window_manager();
    _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=1");

    WindowProperties props{"", "PlatformFail", 0, 0, 320, 240, WindowState::Normal, true, false, "ext_fail"};
    const auto window_id = wm.create_window(props);
    EXPECT_TRUE(window_id.empty());

    _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=");
}

TEST_F(DesktopShellRealTest, DesktopEnvLookupSeamCanForceWindowCreateFailure) {
    auto& wm = get_desktop_shell_interface().window_manager();

    g_desktop_env_for_tests["SENTINEL_DESKTOP_WINDOW_CREATE_FAIL"] = "1";
    set_desktop_env_lookup_for_tests(fake_desktop_env_lookup);

    WindowProperties props{"", "EnvSeamFail", 0, 0, 320, 240, WindowState::Normal, true, false, "ext_env"};
    EXPECT_TRUE(wm.create_window(props).empty());

    g_desktop_env_for_tests.clear();
    const auto recovered_window = wm.create_window(props);
    EXPECT_FALSE(recovered_window.empty());
    EXPECT_TRUE(wm.destroy_window(recovered_window));
}
