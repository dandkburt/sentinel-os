#include <gtest/gtest.h>
#include "bootstrap.h"
#include <memory>

using namespace sentinel::core;

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
