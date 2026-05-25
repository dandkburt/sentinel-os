#include <gtest/gtest.h>

#include "extension_manifest.h"

using namespace sentinel::core;

TEST(ExtensionManifestTest, ParseValidManifestSucceeds) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n"
        "description = Demo extension\n"
        "permissions = [event:alerts, namespace:read]\n"
        "dependencies = [core.extension]\n";

    ExtensionManifest manifest;
    std::string error;
    ASSERT_TRUE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(manifest.id, "sample.extension");
    EXPECT_EQ(manifest.version, "1.2.3");
    EXPECT_EQ(manifest.name, "Sample Extension");
    EXPECT_EQ(manifest.entry_point, "./main.js");
    ASSERT_EQ(manifest.permissions.size(), 2u);
    ASSERT_EQ(manifest.dependencies.size(), 1u);
}

TEST(ExtensionManifestTest, MissingIdFails) {
    const std::string text =
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "missing-required-field");
}

TEST(ExtensionManifestTest, MissingVersionFails) {
    const std::string text =
        "id = sample.extension\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "missing-required-field");
}

TEST(ExtensionManifestTest, MissingNameFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "missing-required-field");
}

TEST(ExtensionManifestTest, MissingEntryPointFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "missing-required-field");
}

TEST(ExtensionManifestTest, InvalidIdFails) {
    const std::string text =
        "id = bad id\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-id");
}

TEST(ExtensionManifestTest, InvalidVersionFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = v1\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-version");
}

TEST(ExtensionManifestTest, EmptyEntryPointFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = \n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-entry-point");
}

TEST(ExtensionManifestTest, DuplicatePermissionFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n"
        "permissions = [event:alerts, event:alerts]\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-permission");
}

TEST(ExtensionManifestTest, DuplicateDependencyFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n"
        "dependencies = [core.extension, core.extension]\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-dependency");
}

TEST(ExtensionManifestTest, SelfDependencyFails) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n"
        "dependencies = [sample.extension]\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "invalid-dependency");
}

TEST(ExtensionManifestTest, MalformedDocumentFails) {
    const std::string text =
        "id sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    EXPECT_FALSE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_EQ(error, "malformed-manifest");
}

TEST(ExtensionManifestTest, OptionalFieldsOmittedUseEmptyCollections) {
    const std::string text =
        "id = sample.extension\n"
        "version = 1.2.3\n"
        "name = Sample Extension\n"
        "entry_point = ./main.js\n";

    ExtensionManifest manifest;
    std::string error;
    ASSERT_TRUE(parse_extension_manifest_text(text, manifest, error));
    EXPECT_TRUE(manifest.description.empty());
    EXPECT_TRUE(manifest.permissions.empty());
    EXPECT_TRUE(manifest.dependencies.empty());
}

TEST(ExtensionManifestTest, OrderingInsensitiveForListFields) {
    const std::string text =
        "dependencies = [core.extension]\n"
        "name = Sample Extension\n"
        "permissions = [event:alerts, namespace:read]\n"
        "entry_point = ./main.js\n"
        "version = 1.2.3\n"
        "id = sample.extension\n";

    ExtensionManifest manifest;
    std::string error;
    ASSERT_TRUE(parse_extension_manifest_text(text, manifest, error));
    ASSERT_EQ(manifest.permissions.size(), 2u);
    ASSERT_EQ(manifest.dependencies.size(), 1u);
}
