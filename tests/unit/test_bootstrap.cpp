#include <gtest/gtest.h>
#include "bootstrap.h"
#include "runtime_config.h"
#include <memory>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <unordered_map>

using namespace sentinel::core;

namespace {

bool g_fake_runtime_config_read_fail = false;
std::string g_fake_runtime_config_text;

bool fake_runtime_config_loader(const std::string& path, std::string& content, std::string& error) {
    (void)path;
    if (g_fake_runtime_config_read_fail) {
        error = "mock-read-failed";
        return false;
    }

    content = g_fake_runtime_config_text;
    error.clear();
    return true;
}

std::unordered_map<std::string, std::string> g_runtime_env_for_tests;

std::string fake_runtime_env_lookup(const std::string& name) {
    auto it = g_runtime_env_for_tests.find(name);
    if (it == g_runtime_env_for_tests.end()) {
        return "";
    }
    return it->second;
}

}  // namespace

class BootstrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock implementation for testing
        bootstrap = std::make_unique<MockBootstrap>();
    }

    class MockBootstrap : public IBootstrap {
    public:
        BootstrapResult initialize() override {
            init_called = true;
            return {BootstrapStatus::Success, "Mock initialized"};
        }

        BootstrapResult shutdown(unsigned int timeout_ms = 5000) override {
            shutdown_called = true;
            return {BootstrapStatus::Success, "Mock shutdown"};
        }

        void on_lifecycle_event(const std::string& event_name, LifecycleCallback callback) override {
            // Mock: Just store the callback
        }

        std::string get_version() const override {
            return "0.1.0";
        }

        bool is_ready() const override {
            return init_called && !shutdown_called;
        }

        bool init_called = false;
        bool shutdown_called = false;
    };

    std::unique_ptr<MockBootstrap> bootstrap;
};

TEST_F(BootstrapTest, InitializationSucceeds) {
    auto result = bootstrap->initialize();
    EXPECT_EQ(result.status, BootstrapStatus::Success);
    EXPECT_TRUE(result.is_success());
    EXPECT_TRUE(bootstrap->is_ready());
}

TEST_F(BootstrapTest, VersionIsCorrect) {
    auto version = bootstrap->get_version();
    EXPECT_EQ(version, "0.1.0");
}

TEST_F(BootstrapTest, ShutdownCleansUp) {
    bootstrap->initialize();
    auto result = bootstrap->shutdown();
    EXPECT_EQ(result.status, BootstrapStatus::Success);
    EXPECT_FALSE(bootstrap->is_ready());
}

TEST_F(BootstrapTest, DoubleInitializeIsIdempotent) {
    bootstrap->initialize();
    auto result2 = bootstrap->initialize();
    EXPECT_EQ(result2.status, BootstrapStatus::Success);
}

class BootstrapRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        reset_runtime_config_file_loader_for_tests();
        reset_runtime_env_lookup_for_tests();
        g_runtime_env_for_tests.clear();

        initialize_runtime();
        auto& bootstrap = get_runtime_interface().bootstrap();
        (void)bootstrap.shutdown();
#ifdef _WIN32
        _putenv("SENTINEL_BOOTSTRAP_FAIL_STEP=");
#else
        unsetenv("SENTINEL_BOOTSTRAP_FAIL_STEP");
#endif
    }

    void TearDown() override {
        reset_runtime_config_file_loader_for_tests();
        reset_runtime_env_lookup_for_tests();
        g_runtime_env_for_tests.clear();

        auto& bootstrap = get_runtime_interface().bootstrap();
        (void)bootstrap.shutdown();
#ifdef _WIN32
        _putenv("SENTINEL_BOOTSTRAP_FAIL_STEP=");
#else
        unsetenv("SENTINEL_BOOTSTRAP_FAIL_STEP");
#endif
    }
};

TEST_F(BootstrapRealTest, InitializeFiresDeterministicStepSequence) {
    auto& bootstrap = get_runtime_interface().bootstrap();

    static std::mutex events_mutex;
    static std::vector<std::string> seen_events;
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        seen_events.clear();
    }

    const std::vector<std::string> expected_events = {
        "init_step:configuration",
        "init_step:logging",
        "init_step:policy",
        "init_step:namespace",
        "init_step:event_broker",
    };

    for (const auto& event_name : expected_events) {
        bootstrap.on_lifecycle_event(event_name, [](const std::string& event) {
            std::lock_guard<std::mutex> lock(events_mutex);
            seen_events.push_back(event);
        });
    }

    auto result = bootstrap.initialize();
    EXPECT_TRUE(result.is_success());
    EXPECT_TRUE(bootstrap.is_ready());
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        EXPECT_EQ(seen_events, expected_events);
    }
}

TEST_F(BootstrapRealTest, InitializationFailureStepCanRecover) {
    auto& bootstrap = get_runtime_interface().bootstrap();

#ifdef _WIN32
    _putenv("SENTINEL_BOOTSTRAP_FAIL_STEP=namespace");
#else
    setenv("SENTINEL_BOOTSTRAP_FAIL_STEP", "namespace", 1);
#endif

    auto failed = bootstrap.initialize();
    EXPECT_EQ(failed.status, BootstrapStatus::InitializationFailed);
    EXPECT_FALSE(bootstrap.is_ready());

#ifdef _WIN32
    _putenv("SENTINEL_BOOTSTRAP_FAIL_STEP=");
#else
    unsetenv("SENTINEL_BOOTSTRAP_FAIL_STEP");
#endif

    auto recovered = bootstrap.initialize();
    EXPECT_TRUE(recovered.is_success());
    EXPECT_TRUE(bootstrap.is_ready());
}

TEST(RuntimeConfigTest, ParseTomlStyleConfigWithValidation) {
    const std::string text =
        "log_level = debug\n"
        "enable_policy = false\n"
        "enable_namespace = true\n"
        "enable_event_broker = false\n"
        "shutdown_timeout_ms = 7500\n";

    RuntimeConfig config;
    std::string error;
    ASSERT_TRUE(parse_runtime_config_text(text, config, error));
    EXPECT_EQ(config.log_level, "debug");
    EXPECT_FALSE(config.enable_policy);
    EXPECT_TRUE(config.enable_namespace);
    EXPECT_FALSE(config.enable_event_broker);
    EXPECT_EQ(config.shutdown_timeout_ms, 7500u);
}

TEST(RuntimeConfigTest, ParseInvalidConfigFailsValidation) {
    const std::string text = "shutdown_timeout_ms = 0\n";
    RuntimeConfig config;
    std::string error;
    EXPECT_FALSE(parse_runtime_config_text(text, config, error));
    EXPECT_FALSE(error.empty());
}

TEST(RuntimeConfigTest, UnsupportedVersionIsRejected) {
    const std::string text = "version = 2\n";
    RuntimeConfig config;
    std::string error;
    EXPECT_FALSE(parse_runtime_config_text(text, config, error));
    EXPECT_EQ(error, "unsupported-config-version");
}

TEST(RuntimeConfigTest, MissingConfigFileUsesDefaults) {
    RuntimeConfig config;
    std::string error;
    ASSERT_TRUE(load_runtime_config_from_file("nonexistent_runtime_config.conf", config, error));

    const RuntimeConfig defaults = default_runtime_config();
    EXPECT_EQ(config.log_level, defaults.log_level);
    EXPECT_EQ(config.enable_policy, defaults.enable_policy);
    EXPECT_EQ(config.enable_namespace, defaults.enable_namespace);
    EXPECT_EQ(config.enable_event_broker, defaults.enable_event_broker);
    EXPECT_EQ(config.shutdown_timeout_ms, defaults.shutdown_timeout_ms);
}

TEST(RuntimeConfigTest, FileLoaderSeamReadFailureIsDeterministic) {
    set_runtime_config_file_loader_for_tests(fake_runtime_config_loader);
    g_fake_runtime_config_read_fail = true;
    g_fake_runtime_config_text.clear();

    RuntimeConfig config = default_runtime_config();
    std::string error;
    EXPECT_FALSE(load_runtime_config_from_file("mock://runtime.conf", config, error));
    EXPECT_EQ(error, "mock-read-failed");

    g_fake_runtime_config_read_fail = false;
    reset_runtime_config_file_loader_for_tests();
}

TEST(RuntimeConfigTest, FileLoaderSeamCanSupplyConfigTextWithoutFilesystem) {
    set_runtime_config_file_loader_for_tests(fake_runtime_config_loader);
    g_fake_runtime_config_read_fail = false;
    g_fake_runtime_config_text =
        "version = 1\n"
        "log_level = error\n"
        "enable_policy = false\n"
        "shutdown_timeout_ms = 4321\n";

    RuntimeConfig config;
    std::string error;
    ASSERT_TRUE(load_runtime_config_from_file("mock://runtime.conf", config, error));
    EXPECT_EQ(config.log_level, "error");
    EXPECT_FALSE(config.enable_policy);
    EXPECT_EQ(config.shutdown_timeout_ms, 4321u);

    g_fake_runtime_config_text.clear();
    reset_runtime_config_file_loader_for_tests();
}

TEST_F(BootstrapRealTest, BootstrapLoadsConfigAndSkipsDisabledSteps) {
    auto& bootstrap = get_runtime_interface().bootstrap();

    const auto config_path = std::filesystem::temp_directory_path() / "sentinel_runtime_test.conf";
    {
        std::ofstream out(config_path.string(), std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "enable_policy = false\n";
        out << "enable_namespace = true\n";
        out << "enable_event_broker = false\n";
    }

#ifdef _WIN32
    _putenv((std::string("SENTINEL_RUNTIME_CONFIG_PATH=") + config_path.string()).c_str());
#else
    setenv("SENTINEL_RUNTIME_CONFIG_PATH", config_path.string().c_str(), 1);
#endif

    static std::mutex events_mutex;
    static std::vector<std::string> seen_events;
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        seen_events.clear();
    }

    bootstrap.on_lifecycle_event("init_step_skipped:policy", [](const std::string& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        seen_events.push_back(event);
    });
    bootstrap.on_lifecycle_event("init_step_skipped:event_broker", [](const std::string& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        seen_events.push_back(event);
    });

    const auto result = bootstrap.initialize();
    EXPECT_TRUE(result.is_success());

    {
        std::lock_guard<std::mutex> lock(events_mutex);
        EXPECT_EQ(seen_events.size(), 2u);
        EXPECT_EQ(seen_events[0], "init_step_skipped:policy");
        EXPECT_EQ(seen_events[1], "init_step_skipped:event_broker");
    }

#ifdef _WIN32
    _putenv("SENTINEL_RUNTIME_CONFIG_PATH=");
#else
    unsetenv("SENTINEL_RUNTIME_CONFIG_PATH");
#endif
    std::error_code ignored;
    std::filesystem::remove(config_path, ignored);
}

TEST_F(BootstrapRealTest, RuntimeReconfigurationSuccessAppliesCandidate) {
    auto& bootstrap = get_runtime_interface().bootstrap();
    ASSERT_TRUE(bootstrap.initialize().is_success());

    const auto config_path = std::filesystem::temp_directory_path() / "sentinel_runtime_reconfig_success.conf";
    {
        std::ofstream out(config_path.string(), std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "version = 1\n";
        out << "log_level = debug\n";
        out << "enable_event_broker = false\n";
        out << "shutdown_timeout_ms = 9000\n";
    }

    std::string error;
    EXPECT_TRUE(trigger_runtime_reconfiguration(config_path.string(), error));
    EXPECT_TRUE(error.empty());

    const RuntimeConfig snapshot = get_runtime_config_snapshot_for_tests();
    EXPECT_EQ(snapshot.config_version, 1u);
    EXPECT_EQ(snapshot.log_level, "debug");
    EXPECT_FALSE(snapshot.enable_event_broker);
    EXPECT_EQ(snapshot.shutdown_timeout_ms, 9000u);

    std::error_code ignored;
    std::filesystem::remove(config_path, ignored);
}

TEST_F(BootstrapRealTest, RuntimeReconfigurationRejectsInvalidCandidateAndRollsBack) {
    auto& bootstrap = get_runtime_interface().bootstrap();
    ASSERT_TRUE(bootstrap.initialize().is_success());

    const RuntimeConfig before = get_runtime_config_snapshot_for_tests();

    const auto config_path = std::filesystem::temp_directory_path() / "sentinel_runtime_reconfig_invalid.conf";
    {
        std::ofstream out(config_path.string(), std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "version = 1\n";
        out << "log_level = debug\n";
        out << "shutdown_timeout_ms = 0\n";
    }

    std::string error;
    EXPECT_FALSE(trigger_runtime_reconfiguration(config_path.string(), error));
    EXPECT_FALSE(error.empty());

    const RuntimeConfig after = get_runtime_config_snapshot_for_tests();
    EXPECT_EQ(after.config_version, before.config_version);
    EXPECT_EQ(after.log_level, before.log_level);
    EXPECT_EQ(after.enable_policy, before.enable_policy);
    EXPECT_EQ(after.enable_namespace, before.enable_namespace);
    EXPECT_EQ(after.enable_event_broker, before.enable_event_broker);
    EXPECT_EQ(after.shutdown_timeout_ms, before.shutdown_timeout_ms);

    std::error_code ignored;
    std::filesystem::remove(config_path, ignored);
}

TEST_F(BootstrapRealTest, RuntimeEnvLookupSeamControlsFailureInjectionDeterministically) {
    auto& bootstrap = get_runtime_interface().bootstrap();

    g_runtime_env_for_tests["SENTINEL_BOOTSTRAP_FAIL_STEP"] = "policy";
    set_runtime_env_lookup_for_tests(fake_runtime_env_lookup);

    auto failed = bootstrap.initialize();
    EXPECT_EQ(failed.status, BootstrapStatus::InitializationFailed);
    EXPECT_FALSE(bootstrap.is_ready());

    g_runtime_env_for_tests.clear();
    auto recovered = bootstrap.initialize();
    EXPECT_TRUE(recovered.is_success());
    EXPECT_TRUE(bootstrap.is_ready());
}
