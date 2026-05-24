#include <gtest/gtest.h>
#include "bootstrap.h"
#include "policy.h"
#include "namespace.h"
#include "shell.h"
#include <cstdint>

using namespace sentinel::core;
using namespace sentinel::services::policy;
using namespace sentinel::services::namespace_service;
using namespace sentinel::shell::desktop;

/// @brief Integration test: Bootstrap initializes all subsystems
TEST(BootstrapIntegration, InitializeAllSubsystems) {
    // Initialize runtime
    initialize_runtime();
    auto& runtime = get_runtime_interface();
    
    // Bootstrap should be available
    auto& bootstrap = runtime.bootstrap();
    auto result = bootstrap.initialize();
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.status, BootstrapStatus::Success);
    EXPECT_TRUE(bootstrap.is_ready());
    EXPECT_EQ(bootstrap.get_version(), "0.1.0");
}

/// @brief Integration test: Namespace service is initialized
TEST(NamespaceIntegration, NamespaceServiceInitialization) {
    initialize_namespace_service();
    auto& namespace_mgr = get_namespace_manager_interface();
    
    // Create a namespace
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    bool created = namespace_mgr.create_namespace("test_ns", "ext1", quota);
    EXPECT_TRUE(created);
    
    // Verify it exists
    EXPECT_TRUE(namespace_mgr.namespace_exists("test_ns"));
    
    // Add an entry
    NamespaceEntry entry{"/files", NamespaceType::File, "ext1", true};
    bool added = namespace_mgr.add_entry("test_ns", entry);
    EXPECT_TRUE(added);
    
    // List entries
    auto entries = namespace_mgr.list_entries("test_ns");
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].path, "/files");
}

/// @brief Integration test: Policy service is initialized
TEST(PolicyIntegration, PolicyServiceInitialization) {
    initialize_policy_service();
    auto& policy_service = get_policy_service_interface();
    
    // Issue a capability token
    std::vector<std::string> capabilities{"file:read", "file:write"};
    std::string token = policy_service.capability_engine().issue_token("ext1", capabilities);
    EXPECT_FALSE(token.empty());
    
    // Get capabilities back
    auto caps = policy_service.capability_engine().get_capabilities("ext1");
    EXPECT_FALSE(caps.empty());
}

/// @brief Integration test: Desktop shell is initialized
TEST(DesktopShellIntegration, DesktopShellInitialization) {
    initialize_desktop_shell();
    auto& shell = get_desktop_shell_interface();
    
    // Initialize shell
    bool initialized = shell.initialize();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(shell.is_ready());
    EXPECT_EQ(shell.get_version(), "0.1.0");
    
    // Get window manager
    auto& wm = shell.window_manager();
    
    // Create a window
    WindowProperties props{"", "Test Window", 100, 100, 800, 600, WindowState::Normal, true, false, "ext1"};
    auto window_id = wm.create_window(props);
    EXPECT_FALSE(window_id.empty());
    
    // Verify window exists
    auto retrieved = wm.get_window(window_id);
    EXPECT_EQ(retrieved.title, "Test Window");
    EXPECT_EQ(retrieved.width, 800);
    EXPECT_EQ(retrieved.height, 600);
}

/// @brief Integration test: Runtime subsystem registration
TEST(RuntimeIntegration, SubsystemRegistration) {
    initialize_runtime();
    initialize_policy_service();
    auto& runtime = get_runtime_interface();
    auto& policy_service = get_policy_service_interface();
    
    // Subsystems can be registered with the runtime
    // Create a mock subsystem pointer
    void* mock_subsystem = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xDEADBEEF));
    
    // Register the subsystem
    bool registered = runtime.register_subsystem("test_subsystem", mock_subsystem);
    EXPECT_TRUE(registered);
    
    // Verify it appears in the subsystems list
    auto subsystems = runtime.list_subsystems();
    EXPECT_EQ(subsystems.size(), 1);
    EXPECT_EQ(subsystems[0], "test_subsystem");
    
    std::string token = policy_service.capability_engine().issue_token("runtime_test", {"test_subsystem:access"});
    EXPECT_FALSE(token.empty());

    // Verify we can retrieve it with a valid capability request
    void* retrieved = runtime.get_subsystem("test_subsystem", token + "|test_subsystem:access");
    EXPECT_EQ(retrieved, mock_subsystem);
}

TEST(RuntimeIntegration, SubsystemRegistrationDeniedWithoutValidToken) {
    initialize_runtime();
    auto& runtime = get_runtime_interface();

    void* mock_subsystem = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEFDEAD));
    ASSERT_TRUE(runtime.register_subsystem("secured_subsystem", mock_subsystem));

    void* retrieved = runtime.get_subsystem("secured_subsystem", "invalid_token|secured_subsystem:access");
    EXPECT_EQ(retrieved, nullptr);
}

/// @brief Integration test: Capability token workflow
TEST(CapabilityWorkflowIntegration, IssueVerifyRevoke) {
    initialize_policy_service();
    auto& policy_service = get_policy_service_interface();
    auto& capability_engine = policy_service.capability_engine();
    
    // Issue token for an extension
    std::string token = capability_engine.issue_token("test_ext", {"file:read"});
    EXPECT_FALSE(token.empty());
    
    // Verify token exists
    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.extension_id, "test_ext");
    
    // Revoke token
    bool revoked = capability_engine.revoke_token(token);
    EXPECT_TRUE(revoked);
    
    // Try to verify revoked token
    auto revoked_result = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(revoked_result.is_valid);
}

/// @brief Integration test: Extension isolation workflow
TEST(ExtensionIsolationIntegration, FullIsolationWorkflow) {
    // Initialize services
    initialize_namespace_service();
    initialize_policy_service();
    
    auto& namespace_mgr = get_namespace_manager_interface();
    auto& policy_service = get_policy_service_interface();
    auto& capability_engine = policy_service.capability_engine();
    
    std::string extension_id = "secure_extension";
    
    // Step 1: Issue capability token for extension
    std::string token = capability_engine.issue_token(extension_id, {"namespace:create", "file:read"});
    EXPECT_FALSE(token.empty());
    
    // Step 2: Create isolated namespace with quotas
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    bool ns_created = namespace_mgr.create_namespace(extension_id + "_ns", extension_id, quota);
    EXPECT_TRUE(ns_created);
    
    // Step 3: Add resources to namespace
    NamespaceEntry entry{"/sandbox", NamespaceType::File, extension_id, true};
    bool entry_added = namespace_mgr.add_entry(extension_id + "_ns", entry);
    EXPECT_TRUE(entry_added);
    
    // Step 4: Verify token is still valid
    auto verify_result = capability_engine.verify_token(token, "file:read");
    EXPECT_TRUE(verify_result.is_valid);
    
    // Step 5: Get resource usage
    auto usage = namespace_mgr.get_resource_usage(extension_id + "_ns");
    EXPECT_FALSE(usage.empty());
}

/// @brief Integration test: Graceful shutdown
TEST(RuntimeIntegration, GracefulShutdown) {
    // Initialize
    initialize_runtime();
    auto& runtime = get_runtime_interface();
    auto& bootstrap = runtime.bootstrap();
    
    // Start runtime
    auto init_result = bootstrap.initialize();
    EXPECT_TRUE(init_result.is_success());
    EXPECT_TRUE(bootstrap.is_ready());
    
    // Shutdown cleanly
    auto shutdown_result = bootstrap.shutdown();
    EXPECT_TRUE(shutdown_result.is_success());
    EXPECT_FALSE(bootstrap.is_ready());
}
