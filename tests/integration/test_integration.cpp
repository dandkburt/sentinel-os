#include <gtest/gtest.h>
#include "bootstrap.h"
#include "policy.h"
#include "namespace.h"
#include "shell.h"
#include <cstdint>
#include <cstdlib>

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

TEST(BootstrapIntegration, InitializationFailureStepAndRecovery) {
    initialize_runtime();
    auto& runtime = get_runtime_interface();
    auto& bootstrap = runtime.bootstrap();

    EXPECT_TRUE(bootstrap.shutdown().is_success());

#ifdef _WIN32
    _putenv("SENTINEL_BOOTSTRAP_FAIL_STEP=policy");
#else
    setenv("SENTINEL_BOOTSTRAP_FAIL_STEP", "policy", 1);
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

TEST(NamespaceIntegration, RejectInvalidPathOrOwnerAndEnforceQuota) {
    initialize_namespace_service();
    auto& namespace_mgr = get_namespace_manager_interface();

    ResourceQuota quota{8192, 1, 1, 1};
    ASSERT_TRUE(namespace_mgr.create_namespace("quota_ns", "ext_owner", quota));

    NamespaceEntry bad_path{"files/no_root", NamespaceType::File, "ext_owner", true};
    EXPECT_FALSE(namespace_mgr.add_entry("quota_ns", bad_path));

    NamespaceEntry bad_owner{"/files/a", NamespaceType::File, "other_owner", true};
    EXPECT_FALSE(namespace_mgr.add_entry("quota_ns", bad_owner));

    NamespaceEntry file1{"/files/a", NamespaceType::File, "ext_owner", true};
    EXPECT_TRUE(namespace_mgr.add_entry("quota_ns", file1));

    NamespaceEntry file2{"/files/b", NamespaceType::File, "ext_owner", true};
    EXPECT_FALSE(namespace_mgr.add_entry("quota_ns", file2));
}

TEST(NamespaceIntegration, RejectQuotaThatIsBelowCurrentUsage) {
    initialize_namespace_service();
    auto& namespace_mgr = get_namespace_manager_interface();

    ResourceQuota initial_quota{16384, 4, 4, 4};
    ASSERT_TRUE(namespace_mgr.create_namespace("quota_update_ns", "ext_owner_2", initial_quota));

    NamespaceEntry entry{"/socket/a", NamespaceType::Socket, "ext_owner_2", true};
    ASSERT_TRUE(namespace_mgr.add_entry("quota_update_ns", entry));

    ResourceQuota invalid_quota{1024, 4, 4, 0};
    EXPECT_FALSE(namespace_mgr.set_resource_quota("quota_update_ns", invalid_quota));

    ResourceQuota valid_quota{16384, 4, 4, 4};
    EXPECT_TRUE(namespace_mgr.set_resource_quota("quota_update_ns", valid_quota));

    auto usage = namespace_mgr.get_resource_usage("quota_update_ns");
    EXPECT_EQ(usage["network_connections"], 1);
}

TEST(NamespaceIntegration, CreateThenAddEntryUsesInitializedQuotaAndUsageState) {
    initialize_namespace_service();
    auto& namespace_mgr = get_namespace_manager_interface();

    ResourceQuota quota{8192, 2, 1, 1};
    ASSERT_TRUE(namespace_mgr.create_namespace("ns_init_path", "owner_init", quota));

    auto usage_before = namespace_mgr.get_resource_usage("ns_init_path");
    ASSERT_FALSE(usage_before.empty());
    EXPECT_EQ(usage_before["memory_bytes"], 0);
    EXPECT_EQ(usage_before["file_handles"], 0);
    EXPECT_EQ(usage_before["threads"], 0);
    EXPECT_EQ(usage_before["network_connections"], 0);

    NamespaceEntry first{"/files/first", NamespaceType::File, "owner_init", true};
    EXPECT_TRUE(namespace_mgr.add_entry("ns_init_path", first));

    auto usage_after_first = namespace_mgr.get_resource_usage("ns_init_path");
    EXPECT_EQ(usage_after_first["memory_bytes"], 4096);
    EXPECT_EQ(usage_after_first["file_handles"], 1);

    NamespaceEntry second{"/files/second", NamespaceType::File, "owner_init", true};
    EXPECT_TRUE(namespace_mgr.add_entry("ns_init_path", second));

    auto usage_after_second = namespace_mgr.get_resource_usage("ns_init_path");
    EXPECT_EQ(usage_after_second["memory_bytes"], 8192);
    EXPECT_EQ(usage_after_second["file_handles"], 2);

    NamespaceEntry third{"/files/third", NamespaceType::File, "owner_init", true};
    EXPECT_FALSE(namespace_mgr.add_entry("ns_init_path", third));

    auto usage_after_reject = namespace_mgr.get_resource_usage("ns_init_path");
    EXPECT_EQ(usage_after_reject["memory_bytes"], 8192);
    EXPECT_EQ(usage_after_reject["file_handles"], 2);

    EXPECT_TRUE(namespace_mgr.remove_namespace("ns_init_path"));
}

TEST(NamespaceIntegration, CreateRemoveCreateSameIdIsPredictable) {
    initialize_namespace_service();
    auto& namespace_mgr = get_namespace_manager_interface();

    ResourceQuota first_quota{4096, 1, 1, 1};
    ASSERT_TRUE(namespace_mgr.create_namespace("ns_recreate", "owner_one", first_quota));

    NamespaceEntry first_entry{"/files/a", NamespaceType::File, "owner_one", true};
    ASSERT_TRUE(namespace_mgr.add_entry("ns_recreate", first_entry));

    auto first_usage = namespace_mgr.get_resource_usage("ns_recreate");
    EXPECT_EQ(first_usage["file_handles"], 1);

    ASSERT_TRUE(namespace_mgr.remove_namespace("ns_recreate"));
    EXPECT_FALSE(namespace_mgr.namespace_exists("ns_recreate"));

    ResourceQuota second_quota{8192, 2, 1, 1};
    ASSERT_TRUE(namespace_mgr.create_namespace("ns_recreate", "owner_two", second_quota));

    auto reset_usage = namespace_mgr.get_resource_usage("ns_recreate");
    EXPECT_EQ(reset_usage["memory_bytes"], 0);
    EXPECT_EQ(reset_usage["file_handles"], 0);

    NamespaceEntry old_owner_entry{"/files/b", NamespaceType::File, "owner_one", true};
    EXPECT_FALSE(namespace_mgr.add_entry("ns_recreate", old_owner_entry));

    NamespaceEntry new_owner_entry{"/files/c", NamespaceType::File, "owner_two", true};
    EXPECT_TRUE(namespace_mgr.add_entry("ns_recreate", new_owner_entry));

    auto recreated_usage = namespace_mgr.get_resource_usage("ns_recreate");
    EXPECT_EQ(recreated_usage["file_handles"], 1);

    EXPECT_TRUE(namespace_mgr.remove_namespace("ns_recreate"));
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

    EXPECT_TRUE(wm.bring_to_front(window_id));
    EXPECT_FALSE(wm.bring_to_front("missing_window"));

    EXPECT_TRUE(shell.set_active_taskbar_item(window_id));
    EXPECT_FALSE(shell.set_active_taskbar_item("missing_window"));

    auto notif_id = shell.show_notification("Build", "Desktop shell notification", 1500);
    EXPECT_FALSE(notif_id.empty());
}

TEST(DesktopShellIntegration, ShutdownClearsWindowResources) {
    initialize_desktop_shell();
    auto& shell = get_desktop_shell_interface();
    auto& wm = shell.window_manager();

    ASSERT_TRUE(shell.initialize());
    WindowProperties props{"", "Resource Window", 0, 0, 320, 240, WindowState::Normal, true, false, "cleanup_ext"};
    auto window_id = wm.create_window(props);
    ASSERT_FALSE(window_id.empty());
    ASSERT_EQ(wm.list_windows("cleanup_ext").size(), 1);

    EXPECT_TRUE(shell.shutdown());
    EXPECT_TRUE(wm.list_windows("cleanup_ext").empty());
}

TEST(DesktopShellIntegration, InitializationFailureModeAndRecovery) {
    initialize_desktop_shell();
    auto& shell = get_desktop_shell_interface();

    // Ensure stable baseline before injecting a bootstrap failure mode.
    EXPECT_TRUE(shell.shutdown());

#ifdef _WIN32
    _putenv("SENTINEL_DESKTOP_BOOTSTRAP_FAIL_STEP=display");
#else
    setenv("SENTINEL_DESKTOP_BOOTSTRAP_FAIL_STEP", "display", 1);
#endif

    EXPECT_FALSE(shell.initialize());
    EXPECT_FALSE(shell.is_ready());
    EXPECT_TRUE(shell.show_notification("Init", "Should fail", 100).empty());
    EXPECT_FALSE(shell.set_active_taskbar_item("window_0"));

#ifdef _WIN32
    _putenv("SENTINEL_DESKTOP_BOOTSTRAP_FAIL_STEP=");
#else
    unsetenv("SENTINEL_DESKTOP_BOOTSTRAP_FAIL_STEP");
#endif

    EXPECT_TRUE(shell.initialize());
    EXPECT_TRUE(shell.is_ready());

    auto notif_id = shell.show_notification("Init", "Recovered", 100);
    EXPECT_FALSE(notif_id.empty());

    EXPECT_TRUE(shell.shutdown());
}

TEST(DesktopShellIntegration, WindowCreateFailureModeAndRecovery) {
    initialize_desktop_shell();
    auto& shell = get_desktop_shell_interface();
    auto& wm = shell.window_manager();

    ASSERT_TRUE(shell.initialize());

#ifdef _WIN32
    _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=1");
#else
    setenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL", "1", 1);
#endif

    WindowProperties failing_props{"", "FailCreate", 10, 10, 400, 300, WindowState::Normal, true, false, "int_ext"};
    EXPECT_TRUE(wm.create_window(failing_props).empty());

#ifdef _WIN32
    _putenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL=");
#else
    unsetenv("SENTINEL_DESKTOP_WINDOW_CREATE_FAIL");
#endif

    const auto first = wm.create_window(failing_props);
    ASSERT_FALSE(first.empty());

    EXPECT_TRUE(wm.destroy_window(first));
    const auto second = wm.create_window(failing_props);
    ASSERT_FALSE(second.empty());
    EXPECT_NE(first, second);

    EXPECT_TRUE(wm.destroy_window(second));
    EXPECT_TRUE(shell.shutdown());
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

    // Verify required capability mismatch is denied
    auto denied_result = capability_engine.verify_token(token, "network:send");
    EXPECT_FALSE(denied_result.is_valid);
    
    // Revoke token
    bool revoked = capability_engine.revoke_token(token);
    EXPECT_TRUE(revoked);
    
    // Try to verify revoked token
    auto revoked_result = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(revoked_result.is_valid);
}

TEST(PolicyIntegration, HasPermissionFallsBackToCapabilities) {
    initialize_policy_service();
    auto& policy_service = get_policy_service_interface();
    auto& capability_engine = policy_service.capability_engine();

    std::string extension_id = "permission_ext";
    std::string token = capability_engine.issue_token(extension_id, {"extension:*"});
    EXPECT_FALSE(token.empty());

    EXPECT_TRUE(policy_service.has_permission(extension_id, "extension:install"));
    EXPECT_FALSE(policy_service.has_permission(extension_id, "network:send"));
}

TEST(PolicyIntegration, RegisterRuleAndEvaluateActionPatterns) {
    initialize_policy_service();
    auto& policy_service = get_policy_service_interface();

    EXPECT_TRUE(policy_service.register_rule(
        "allow_file_actions",
        "{\"action\":\"file:*\",\"effect\":\"allow\"}"));

    EXPECT_TRUE(policy_service.register_rule(
        "allow_open_for_specific_requester",
        "{\"action\":\"resource:open\",\"effect\":\"allow\",\"requester_id\":\"trusted_ext\"}"));

    EXPECT_EQ(policy_service.evaluate("ext_a", "file:read", "config.json"), PolicyDecision::Allow);
    EXPECT_EQ(policy_service.evaluate("trusted_ext", "resource:open", "doc_1"), PolicyDecision::Allow);
    EXPECT_EQ(policy_service.evaluate("other_ext", "resource:open", "doc_1"), PolicyDecision::Deny);
}

TEST(PolicyIntegration, RegisterRuleRejectsInvalidCondition) {
    initialize_policy_service();
    auto& policy_service = get_policy_service_interface();

    EXPECT_FALSE(policy_service.register_rule("missing_effect", "{\"action\":\"file:*\"}"));
    EXPECT_FALSE(policy_service.register_rule("missing_action", "{\"effect\":\"allow\"}"));
    EXPECT_FALSE(policy_service.register_rule("bad_effect", "{\"action\":\"file:*\",\"effect\":\"permit\"}"));
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
