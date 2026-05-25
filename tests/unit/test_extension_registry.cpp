#include <gtest/gtest.h>

#include "extension_registry.h"

#include <filesystem>
#include <fstream>

using namespace sentinel::core;

namespace {

std::string make_manifest(const std::string& id,
                          const std::string& version,
                          const std::string& name,
                          const std::string& entry_point,
                          const std::string& permissions = "[]",
                          const std::string& dependencies = "[]") {
    return std::string("id = ") + id + "\n" +
           "version = " + version + "\n" +
           "name = " + name + "\n" +
           "entry_point = " + entry_point + "\n" +
           "permissions = " + permissions + "\n" +
           "dependencies = " + dependencies + "\n";
}

bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << text;
    return true;
}

class ExtensionRegistryRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_extension_registry();
        reset_extension_registry_for_tests();
        reset_extension_lifecycle_fail_step_for_tests();

        temp_dir_ = std::filesystem::temp_directory_path() / "sentinel_extension_registry_tests";
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
        std::filesystem::create_directories(temp_dir_, ec);
        ASSERT_FALSE(ec);
    }

    void TearDown() override {
        reset_extension_lifecycle_fail_step_for_tests();
        reset_extension_registry_for_tests();
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
};

TEST_F(ExtensionRegistryRealTest, RegisterValidExtensionSucceeds) {
    auto& registry = get_extension_registry_interface();

    const auto manifest_path = temp_dir_ / "sample.manifest";
    ASSERT_TRUE(write_text_file(
        manifest_path,
        make_manifest("sample.ext", "1.2.3", "Sample Extension", "./main.js", "[event:alerts]", "[core.ext]")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(manifest_path.string(), error));
    EXPECT_TRUE(error.empty());

    const auto snapshot = registry.list_registered_extensions();
    ASSERT_EQ(snapshot.size(), 1u);
    EXPECT_EQ(snapshot[0].id, "sample.ext");
    EXPECT_EQ(snapshot[0].version, "1.2.3");
    EXPECT_EQ(snapshot[0].name, "Sample Extension");
    EXPECT_EQ(snapshot[0].entry_point, "./main.js");
    ASSERT_EQ(snapshot[0].permissions.size(), 1u);
    ASSERT_EQ(snapshot[0].dependencies.size(), 1u);
    EXPECT_FALSE(snapshot[0].source_path.empty());
}

TEST_F(ExtensionRegistryRealTest, RegisterDuplicateIdFailsDeterministically) {
    auto& registry = get_extension_registry_interface();

    const auto first_path = temp_dir_ / "first.manifest";
    const auto second_path = temp_dir_ / "second.manifest";
    ASSERT_TRUE(write_text_file(first_path, make_manifest("dup.ext", "1.0.0", "Dup One", "./one.js")));
    ASSERT_TRUE(write_text_file(second_path, make_manifest("dup.ext", "1.1.0", "Dup Two", "./two.js")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(first_path.string(), error));
    ASSERT_TRUE(error.empty());

    ASSERT_FALSE(registry.register_extension_from_manifest_path(second_path.string(), error));
    EXPECT_EQ(error, "duplicate-extension-id");

    const auto snapshot = registry.list_registered_extensions();
    ASSERT_EQ(snapshot.size(), 1u);
    EXPECT_EQ(snapshot[0].id, "dup.ext");
    EXPECT_EQ(snapshot[0].version, "1.0.0");
}

TEST_F(ExtensionRegistryRealTest, DiscoveryLoadsValidManifestsAndReturnsSortedIds) {
    auto& registry = get_extension_registry_interface();

    ASSERT_TRUE(write_text_file(temp_dir_ / "zeta.manifest", make_manifest("zeta.ext", "1.0.0", "Zeta", "./z.js")));
    ASSERT_TRUE(write_text_file(temp_dir_ / "alpha.manifest", make_manifest("alpha.ext", "1.0.0", "Alpha", "./a.js")));
    ASSERT_TRUE(write_text_file(temp_dir_ / "ignore.txt", "not a manifest\n"));

    std::vector<std::string> non_fatal_errors;
    std::string error;
    ASSERT_TRUE(registry.discover_extensions_in_directory(temp_dir_.string(), non_fatal_errors, error));
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(non_fatal_errors.empty());

    const auto snapshot = registry.list_registered_extensions();
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].id, "alpha.ext");
    EXPECT_EQ(snapshot[1].id, "zeta.ext");
}

TEST_F(ExtensionRegistryRealTest, DiscoverySkipsInvalidManifestAndReportsError) {
    auto& registry = get_extension_registry_interface();

    ASSERT_TRUE(write_text_file(temp_dir_ / "good-a.manifest", make_manifest("good.a", "1.0.0", "Good A", "./a.js")));
    ASSERT_TRUE(write_text_file(temp_dir_ / "bad.manifest", "id bad\nversion = 1.0.0\nname = Bad\nentry_point = ./bad.js\n"));
    ASSERT_TRUE(write_text_file(temp_dir_ / "good-b.manifest", make_manifest("good.b", "1.0.0", "Good B", "./b.js")));

    std::vector<std::string> non_fatal_errors;
    std::string error;
    ASSERT_TRUE(registry.discover_extensions_in_directory(temp_dir_.string(), non_fatal_errors, error));
    EXPECT_TRUE(error.empty());
    ASSERT_EQ(non_fatal_errors.size(), 1u);
    EXPECT_NE(non_fatal_errors[0].find("bad.manifest:manifest-parse-failed"), std::string::npos);

    const auto snapshot = registry.list_registered_extensions();
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].id, "good.a");
    EXPECT_EQ(snapshot[1].id, "good.b");
}

TEST_F(ExtensionRegistryRealTest, SnapshotDoesNotMutateOnFailedRegistration) {
    auto& registry = get_extension_registry_interface();

    const auto good_path = temp_dir_ / "stable.manifest";
    const auto bad_path = temp_dir_ / "invalid.manifest";
    ASSERT_TRUE(write_text_file(good_path, make_manifest("stable.ext", "1.0.0", "Stable", "./stable.js")));
    ASSERT_TRUE(write_text_file(bad_path, make_manifest("bad id", "1.0.0", "Bad", "./bad.js")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(good_path.string(), error));
    ASSERT_TRUE(error.empty());

    const auto before = registry.list_registered_extensions();
    ASSERT_EQ(before.size(), 1u);

    ASSERT_FALSE(registry.register_extension_from_manifest_path(bad_path.string(), error));
    EXPECT_EQ(error, "manifest-validation-failed");

    const auto after = registry.list_registered_extensions();
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].id, before[0].id);
    EXPECT_EQ(after[0].version, before[0].version);
    EXPECT_EQ(after[0].entry_point, before[0].entry_point);
    EXPECT_EQ(after[0].source_path, before[0].source_path);
}

TEST_F(ExtensionRegistryRealTest, LifecycleHappyPathRegisterLoadEnableDisableUnload) {
    auto& registry = get_extension_registry_interface();

    const auto manifest_path = temp_dir_ / "lifecycle.manifest";
    ASSERT_TRUE(write_text_file(manifest_path, make_manifest("life.ext", "1.0.0", "Lifecycle", "./life.js")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(manifest_path.string(), error));
    ASSERT_TRUE(registry.load_extension("life.ext", error));
    ASSERT_TRUE(registry.enable_extension("life.ext", error));
    ASSERT_TRUE(registry.disable_extension("life.ext", error));
    ASSERT_TRUE(registry.unload_extension("life.ext", error));
    EXPECT_TRUE(error.empty());

    ExtensionLifecycleState state = ExtensionLifecycleState::Registered;
    ASSERT_TRUE(registry.get_extension_state("life.ext", state, error));
    EXPECT_EQ(state, ExtensionLifecycleState::Unloaded);
}

TEST_F(ExtensionRegistryRealTest, LifecycleIdempotencyErrorsAreDeterministic) {
    auto& registry = get_extension_registry_interface();

    const auto manifest_path = temp_dir_ / "idempotent.manifest";
    ASSERT_TRUE(write_text_file(manifest_path, make_manifest("idem.ext", "1.0.0", "Idem", "./idem.js")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(manifest_path.string(), error));

    ASSERT_TRUE(registry.load_extension("idem.ext", error));
    ASSERT_FALSE(registry.load_extension("idem.ext", error));
    EXPECT_EQ(error, "already-loaded");

    ASSERT_TRUE(registry.enable_extension("idem.ext", error));
    ASSERT_FALSE(registry.enable_extension("idem.ext", error));
    EXPECT_EQ(error, "already-enabled");

    ASSERT_TRUE(registry.disable_extension("idem.ext", error));
    ASSERT_FALSE(registry.disable_extension("idem.ext", error));
    EXPECT_EQ(error, "already-disabled");

    ASSERT_TRUE(registry.unload_extension("idem.ext", error));
    ASSERT_FALSE(registry.unload_extension("idem.ext", error));
    EXPECT_EQ(error, "already-unloaded");
}

TEST_F(ExtensionRegistryRealTest, InvalidLifecycleTransitionsFailDeterministically) {
    auto& registry = get_extension_registry_interface();

    const auto manifest_path = temp_dir_ / "invalid-transitions.manifest";
    ASSERT_TRUE(write_text_file(manifest_path, make_manifest("invalid.ext", "1.0.0", "Invalid", "./invalid.js")));

    std::string error;
    ASSERT_FALSE(registry.load_extension("missing.ext", error));
    EXPECT_EQ(error, "extension-not-found");

    ASSERT_TRUE(registry.register_extension_from_manifest_path(manifest_path.string(), error));

    ASSERT_FALSE(registry.enable_extension("invalid.ext", error));
    EXPECT_EQ(error, "invalid-state-transition");

    ASSERT_FALSE(registry.unload_extension("invalid.ext", error));
    EXPECT_EQ(error, "already-unloaded");

    ASSERT_TRUE(registry.load_extension("invalid.ext", error));
    ASSERT_TRUE(registry.enable_extension("invalid.ext", error));

    ASSERT_FALSE(registry.unload_extension("invalid.ext", error));
    EXPECT_EQ(error, "invalid-state-transition");
}

TEST_F(ExtensionRegistryRealTest, FailureInjectionRollsBackStateAndKeepsSnapshotStable) {
    auto& registry = get_extension_registry_interface();

    const auto manifest_path = temp_dir_ / "failure.manifest";
    ASSERT_TRUE(write_text_file(manifest_path, make_manifest("failure.ext", "1.0.0", "Failure", "./failure.js")));

    std::string error;
    ASSERT_TRUE(registry.register_extension_from_manifest_path(manifest_path.string(), error));

    const auto before = registry.list_registered_extensions();
    ASSERT_EQ(before.size(), 1u);
    ASSERT_EQ(before[0].lifecycle_state, ExtensionLifecycleState::Registered);

    set_extension_lifecycle_fail_step_for_tests("load");
    ASSERT_FALSE(registry.load_extension("failure.ext", error));
    EXPECT_EQ(error, "load-failed");

    ExtensionLifecycleState state = ExtensionLifecycleState::Registered;
    ASSERT_TRUE(registry.get_extension_state("failure.ext", state, error));
    EXPECT_EQ(state, ExtensionLifecycleState::Registered);

    const auto after_failed_load = registry.list_registered_extensions();
    ASSERT_EQ(after_failed_load.size(), 1u);
    EXPECT_EQ(after_failed_load[0].id, before[0].id);
    EXPECT_EQ(after_failed_load[0].version, before[0].version);
    EXPECT_EQ(after_failed_load[0].source_path, before[0].source_path);

    reset_extension_lifecycle_fail_step_for_tests();
    ASSERT_TRUE(registry.load_extension("failure.ext", error));
    ASSERT_TRUE(registry.enable_extension("failure.ext", error));

    set_extension_lifecycle_fail_step_for_tests("disable");
    ASSERT_FALSE(registry.disable_extension("failure.ext", error));
    EXPECT_EQ(error, "disable-failed");

    ASSERT_TRUE(registry.get_extension_state("failure.ext", state, error));
    EXPECT_EQ(state, ExtensionLifecycleState::Enabled);

    const auto after_failed_disable = registry.list_registered_extensions();
    ASSERT_EQ(after_failed_disable.size(), 1u);
    EXPECT_EQ(after_failed_disable[0].id, before[0].id);
    EXPECT_EQ(after_failed_disable[0].version, before[0].version);
    EXPECT_EQ(after_failed_disable[0].source_path, before[0].source_path);
}

}  // namespace
