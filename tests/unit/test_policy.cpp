#include <gtest/gtest.h>
#include "policy.h"
#include <memory>
#include <algorithm>

using namespace sentinel::services::policy;

class CapabilityEngineTest : public ::testing::Test {
public:
    class MockCapabilityEngine : public ICapabilityEngine {
    public:
        CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) override {
            if (token == "valid_token") {
                return {true, "test_extension", "file:read", 0, 0};
            }
            return {false, "", "", 0, 0};
        }

        std::string issue_token(const std::string& extension_id, const std::vector<std::string>& capabilities, 
                               uint32_t expiry_seconds = 0) override {
            return "token_" + extension_id + "_generated";
        }

        bool revoke_token(const std::string& token) override {
            return token != "persistent_token";
        }

        std::vector<std::string> get_capabilities(const std::string& extension_id) const override {
            if (extension_id == "test_extension") {
                return {"file:read", "file:write"};
            }
            return {};
        }
    };

protected:

    void SetUp() override {
        engine = std::make_unique<MockCapabilityEngine>();
    }

    std::unique_ptr<MockCapabilityEngine> engine;
};

TEST_F(CapabilityEngineTest, VerifyValidToken) {
    auto result = engine->verify_token("valid_token", "file:read");
    EXPECT_TRUE(result.is_valid);
    EXPECT_EQ(result.extension_id, "test_extension");
}

TEST_F(CapabilityEngineTest, VerifyInvalidToken) {
    auto result = engine->verify_token("invalid_token", "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST_F(CapabilityEngineTest, IssueToken) {
    auto token = engine->issue_token("my_extension", {"file:read"});
    EXPECT_FALSE(token.empty());
    EXPECT_NE(token.find("my_extension"), std::string::npos);
}

TEST_F(CapabilityEngineTest, RevokeToken) {
    bool result = engine->revoke_token("valid_token");
    EXPECT_TRUE(result);
}

TEST_F(CapabilityEngineTest, GetCapabilities) {
    auto caps = engine->get_capabilities("test_extension");
    EXPECT_EQ(caps.size(), 2);
    EXPECT_NE(std::find(caps.begin(), caps.end(), "file:read"), caps.end());
}

class PolicyServiceTest : public ::testing::Test {
protected:
    class MockCapabilityEngineForPolicy : public ICapabilityEngine {
    public:
        CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) override {
            if (token == "policy_valid_token") {
                return {true, "policy_extension", "file:read", 0, 0};
            }
            return {false, "", "", 0, 0};
        }

        std::string issue_token(const std::string& extension_id, const std::vector<std::string>& capabilities,
                               uint32_t expiry_seconds = 0) override {
            return "policy_token_" + extension_id;
        }

        bool revoke_token(const std::string& token) override {
            return true;
        }

        std::vector<std::string> get_capabilities(const std::string& extension_id) const override {
            return {"file:read", "file:write"};
        }
    };

    class MockPolicyService : public IPolicyService {
    public:
        MockPolicyService() : cap_engine_(std::make_unique<MockCapabilityEngineForPolicy>()) {}

        PolicyDecision evaluate(const std::string& requester_id, const std::string& action, 
                               const std::string& resource_id, const std::string& context = "") override {
            if (action == "file:read") {
                return PolicyDecision::Allow;
            }
            return PolicyDecision::Deny;
        }

        bool register_rule(const std::string& rule_id, const std::string& rule_condition) override {
            return true;
        }

        ICapabilityEngine& capability_engine() override {
            return *cap_engine_;
        }

        bool has_permission(const std::string& extension_id, const std::string& permission) const override {
            return extension_id == "privileged_extension" && permission == "extension:install";
        }

    private:
        std::unique_ptr<ICapabilityEngine> cap_engine_;
    };

    void SetUp() override {
        policy_service = std::make_unique<MockPolicyService>();
    }

    std::unique_ptr<MockPolicyService> policy_service;
};

TEST_F(PolicyServiceTest, AllowReadPolicy) {
    auto decision = policy_service->evaluate("ext1", "file:read", "config.json");
    EXPECT_EQ(decision, PolicyDecision::Allow);
}

TEST_F(PolicyServiceTest, DenyUnknownAction) {
    auto decision = policy_service->evaluate("ext1", "network:send", "example.com");
    EXPECT_EQ(decision, PolicyDecision::Deny);
}

TEST_F(PolicyServiceTest, CheckPermission) {
    bool result = policy_service->has_permission("privileged_extension", "extension:install");
    EXPECT_TRUE(result);
}

TEST_F(PolicyServiceTest, DenyUnprivilegedPermission) {
    bool result = policy_service->has_permission("unprivileged_extension", "extension:install");
    EXPECT_FALSE(result);
}
