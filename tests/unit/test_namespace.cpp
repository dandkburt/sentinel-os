#include <gtest/gtest.h>
#include "namespace.h"
#include <memory>
#include <unordered_map>
#include <map>

using namespace sentinel::services::namespace_service;

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
