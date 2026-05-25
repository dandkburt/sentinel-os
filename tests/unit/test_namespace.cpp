#include <gtest/gtest.h>
#include "namespace.h"
#include <memory>
#include <unordered_map>
#include <map>
#include <atomic>

using namespace sentinel::services::namespace_service;

namespace {
std::string unique_namespace_id(const std::string& base) {
    static std::atomic<unsigned int> counter{0};
    return base + "_" + std::to_string(++counter);
}
}

class NamespaceManagerTest : public ::testing::Test {
protected:
    class MockNamespaceManager : public INamespaceManager {
    public:
        bool create_namespace(const std::string& namespace_id, const std::string& owner_id, 
                             const ResourceQuota& quota) override {
            if (namespaces.find(namespace_id) != namespaces.end()) {
                return false;
            }
            namespaces[namespace_id] = {owner_id, quota, {}};
            return true;
        }

        bool remove_namespace(const std::string& namespace_id) override {
            return namespaces.erase(namespace_id) > 0;
        }

        bool add_entry(const std::string& namespace_id, const NamespaceEntry& entry) override {
            auto it = namespaces.find(namespace_id);
            if (it == namespaces.end()) return false;
            it->second.entries.push_back(entry);
            return true;
        }

        std::vector<NamespaceEntry> list_entries(const std::string& namespace_id) const override {
            auto it = namespaces.find(namespace_id);
            if (it != namespaces.end()) {
                return it->second.entries;
            }
            return {};
        }

        std::map<std::string, uint64_t> get_resource_usage(const std::string& namespace_id) const override {
            std::map<std::string, uint64_t> usage;
            usage["memory_bytes"] = 104857600;
            return usage;
        }

        bool set_resource_quota(const std::string& namespace_id, const ResourceQuota& quota) override {
            auto it = namespaces.find(namespace_id);
            if (it != namespaces.end()) {
                it->second.quota = quota;
                return true;
            }
            return false;
        }

        bool namespace_exists(const std::string& namespace_id) const override {
            return namespaces.find(namespace_id) != namespaces.end();
        }

    private:
        struct NamespaceInfo {
            std::string owner_id;
            ResourceQuota quota;
            std::vector<NamespaceEntry> entries;
        };
        std::unordered_map<std::string, NamespaceInfo> namespaces;
    };

    void SetUp() override {
        manager = std::make_unique<MockNamespaceManager>();
    }

    std::unique_ptr<MockNamespaceManager> manager;
};

TEST_F(NamespaceManagerTest, CreateNamespaceSucceeds) {
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    bool result = manager->create_namespace("ns1", "ext1", quota);
    EXPECT_TRUE(result);
    EXPECT_TRUE(manager->namespace_exists("ns1"));
}

TEST_F(NamespaceManagerTest, CreateDuplicateNamespaceFails) {
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    manager->create_namespace("ns1", "ext1", quota);
    bool result = manager->create_namespace("ns1", "ext2", quota);
    EXPECT_FALSE(result);
}

TEST_F(NamespaceManagerTest, RemoveNamespaceSucceeds) {
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    manager->create_namespace("ns1", "ext1", quota);
    bool result = manager->remove_namespace("ns1");
    EXPECT_TRUE(result);
    EXPECT_FALSE(manager->namespace_exists("ns1"));
}

TEST_F(NamespaceManagerTest, AddEntrySucceeds) {
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    manager->create_namespace("ns1", "ext1", quota);
    
    NamespaceEntry entry{"/files", NamespaceType::File, "ext1", true};
    bool result = manager->add_entry("ns1", entry);
    EXPECT_TRUE(result);

    auto entries = manager->list_entries("ns1");
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].path, "/files");
}

TEST_F(NamespaceManagerTest, GetResourceUsage) {
    ResourceQuota quota{1024*1024*100, 1024, 16, 32};
    manager->create_namespace("ns1", "ext1", quota);
    
    auto usage = manager->get_resource_usage("ns1");
    EXPECT_EQ(usage["memory_bytes"], 104857600);
}

class NamespaceManagerRealTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_namespace_service();
    }

    INamespaceManager& manager() {
        return get_namespace_manager_interface();
    }
};

TEST_F(NamespaceManagerRealTest, CreateInitializesOwnerQuotaAndZeroedUsage) {
    const auto namespace_id = unique_namespace_id("ns_create_init");
    const std::string owner = "owner_create_init";
    ResourceQuota quota{1024 * 1024, 3, 2, 1};

    ASSERT_TRUE(manager().create_namespace(namespace_id, owner, quota));
    EXPECT_TRUE(manager().namespace_exists(namespace_id));

    NamespaceEntry wrong_owner{"/files/a", NamespaceType::File, "other_owner", true};
    EXPECT_FALSE(manager().add_entry(namespace_id, wrong_owner));

    NamespaceEntry file1{"/files/a", NamespaceType::File, owner, true};
    EXPECT_TRUE(manager().add_entry(namespace_id, file1));

    NamespaceEntry file2{"/files/b", NamespaceType::File, owner, true};
    EXPECT_TRUE(manager().add_entry(namespace_id, file2));

    NamespaceEntry file3{"/files/c", NamespaceType::File, owner, true};
    EXPECT_TRUE(manager().add_entry(namespace_id, file3));

    NamespaceEntry file4{"/files/d", NamespaceType::File, owner, true};
    EXPECT_FALSE(manager().add_entry(namespace_id, file4));

    const auto usage = manager().get_resource_usage(namespace_id);
    EXPECT_EQ(usage.at("memory_bytes"), 3u * 4096u);
    EXPECT_EQ(usage.at("file_handles"), 3u);
    EXPECT_EQ(usage.at("threads"), 0u);
    EXPECT_EQ(usage.at("network_connections"), 0u);

    EXPECT_TRUE(manager().remove_namespace(namespace_id));
}

TEST_F(NamespaceManagerRealTest, DuplicateCreateFailsAndPreservesOriginalState) {
    const auto namespace_id = unique_namespace_id("ns_duplicate");
    const std::string owner = "owner_duplicate";
    ResourceQuota original_quota{8192, 1, 1, 1};
    ResourceQuota replacement_quota{1024 * 1024, 16, 16, 16};

    ASSERT_TRUE(manager().create_namespace(namespace_id, owner, original_quota));
    NamespaceEntry existing{"/files/a", NamespaceType::File, owner, true};
    ASSERT_TRUE(manager().add_entry(namespace_id, existing));

    EXPECT_FALSE(manager().create_namespace(namespace_id, "other_owner", replacement_quota));

    NamespaceEntry still_owner{"/files/b", NamespaceType::File, owner, true};
    EXPECT_FALSE(manager().add_entry(namespace_id, still_owner));

    NamespaceEntry wrong_owner{"/files/c", NamespaceType::File, "other_owner", true};
    EXPECT_FALSE(manager().add_entry(namespace_id, wrong_owner));

    const auto usage = manager().get_resource_usage(namespace_id);
    EXPECT_EQ(usage.at("file_handles"), 1u);
    EXPECT_EQ(usage.at("memory_bytes"), 4096u);

    EXPECT_TRUE(manager().remove_namespace(namespace_id));
}

TEST_F(NamespaceManagerRealTest, InvalidNamespaceOrOwnerIsRejectedAndDefaultsApply) {
    const auto invalid_namespace = unique_namespace_id("bad namespace");
    ResourceQuota quota{0, 0, 0, 0};
    EXPECT_FALSE(manager().create_namespace(invalid_namespace, "owner_valid", quota));

    const auto invalid_owner_namespace = unique_namespace_id("valid_namespace");
    EXPECT_FALSE(manager().create_namespace(invalid_owner_namespace, "bad owner", quota));

    const auto defaulted_namespace = unique_namespace_id("defaulted_namespace");
    ASSERT_TRUE(manager().create_namespace(defaulted_namespace, "owner_defaulted", quota));

    NamespaceEntry entry{"/files/defaulted", NamespaceType::File, "owner_defaulted", true};
    EXPECT_TRUE(manager().add_entry(defaulted_namespace, entry));

    const auto usage = manager().get_resource_usage(defaulted_namespace);
    EXPECT_EQ(usage.at("file_handles"), 1u);
    EXPECT_EQ(usage.at("memory_bytes"), 4096u);

    EXPECT_TRUE(manager().remove_namespace(defaulted_namespace));
}
