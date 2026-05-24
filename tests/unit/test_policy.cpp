#include <gtest/gtest.h>
#include "policy.h"
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>
#include <atomic>

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

namespace {
std::string unique_extension_id(const std::string& base) {
    static std::atomic<unsigned int> counter{0};
    return base + "_" + std::to_string(++counter);
}

std::vector<std::string> split_token(const std::string& token) {
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= token.size()) {
        const size_t dot = token.find('.', start);
        if (dot == std::string::npos) {
            segments.push_back(token.substr(start));
            break;
        }
        segments.push_back(token.substr(start, dot - start));
        start = dot + 1;
    }
    return segments;
}
}  // namespace

TEST(PolicyCapabilityEngineReal, RejectEmptyToken) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    auto result = capability_engine.verify_token("", "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectMalformedToken) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    auto result = capability_engine.verify_token("malformed_token", "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectExpiredToken) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("exp");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"}, 1);
    ASSERT_FALSE(token.empty());

    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectRevokedToken) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("rev");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());
    ASSERT_TRUE(capability_engine.revoke_token(token));

    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, AcceptExactCapability) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("exact");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_TRUE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, AcceptNamespaceWildcardCapability) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("wild");
    const auto token = capability_engine.issue_token(extension_id, {"extension:*"});
    ASSERT_FALSE(token.empty());

    auto result = capability_engine.verify_token(token, "extension:install");
    EXPECT_TRUE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectCapabilityMismatch) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("mismatch");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    auto result = capability_engine.verify_token(token, "network:send");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, AcceptValidSignature) {
    initialize_policy_service();
    reset_policy_signing_key_for_tests();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("sig_valid");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_TRUE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectInvalidSignature) {
    initialize_policy_service();
    reset_policy_signing_key_for_tests();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("sig_invalid");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    auto segments = split_token(token);
    ASSERT_EQ(segments.size(), 4);
    segments[3] = "badsignature";
    const std::string tampered_token = segments[0] + "." + segments[1] + "." + segments[2] + "." + segments[3];

    auto result = capability_engine.verify_token(tampered_token, "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectTamperedPayload) {
    initialize_policy_service();
    reset_policy_signing_key_for_tests();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("payload");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    auto segments = split_token(token);
    ASSERT_EQ(segments.size(), 4);
    segments[1] = unique_extension_id("tampered");
    const std::string tampered_token = segments[0] + "." + segments[1] + "." + segments[2] + "." + segments[3];

    auto result = capability_engine.verify_token(tampered_token, "file:read");
    EXPECT_FALSE(result.is_valid);
}

TEST(PolicyCapabilityEngineReal, RejectKeyMismatch) {
    initialize_policy_service();
    reset_policy_signing_key_for_tests();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    const auto extension_id = unique_extension_id("key_mismatch");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    set_policy_signing_key_for_tests("rotated-policy-key");
    auto result = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(result.is_valid);

    reset_policy_signing_key_for_tests();
}

TEST(PolicyCapabilityEngineReal, HmacSha256KnownVector) {
    // RFC-style known value for key="key", message="The quick brown fox jumps over the lazy dog"
    set_policy_signing_key_for_tests("key");
    const auto mac = compute_policy_hmac_for_tests("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(mac, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    reset_policy_signing_key_for_tests();
}

TEST(PolicyCapabilityEngineReal, DualKeyVerifyWindowSupportsCutover) {
    initialize_policy_service();
    reset_policy_signing_key_for_tests();
    reset_policy_previous_signing_key_for_tests();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    set_policy_signing_key_for_tests("old-rotation-signing-key-123");
    const auto extension_id = unique_extension_id("rotate");
    const auto token = capability_engine.issue_token(extension_id, {"file:read"});
    ASSERT_FALSE(token.empty());

    set_policy_signing_key_for_tests("new-rotation-signing-key-456");
    set_policy_previous_signing_key_for_tests("old-rotation-signing-key-123");

    auto during_window = capability_engine.verify_token(token, "file:read");
    EXPECT_TRUE(during_window.is_valid);

    reset_policy_previous_signing_key_for_tests();
    auto after_cutover = capability_engine.verify_token(token, "file:read");
    EXPECT_FALSE(after_cutover.is_valid);

    reset_policy_signing_key_for_tests();
}

TEST(PolicyCapabilityEngineReal, EnvironmentSigningKeyValidationAndReload) {
    initialize_policy_service();
    auto& capability_engine = get_policy_service_interface().capability_engine();

    _putenv("SENTINEL_POLICY_SIGNING_KEY=too-short");
    reload_policy_signing_keys_from_environment_for_tests();

    const auto extension_id_1 = unique_extension_id("env_invalid");
    const auto token_1 = capability_engine.issue_token(extension_id_1, {"file:read"});
    ASSERT_FALSE(token_1.empty());

    auto verify_1 = capability_engine.verify_token(token_1, "file:read");
    EXPECT_TRUE(verify_1.is_valid);

    _putenv("SENTINEL_POLICY_SIGNING_KEY=env-validated-signing-key-789");
    reload_policy_signing_keys_from_environment_for_tests();

    const auto extension_id_2 = unique_extension_id("env_valid");
    const auto token_2 = capability_engine.issue_token(extension_id_2, {"file:read"});
    ASSERT_FALSE(token_2.empty());

    auto verify_2 = capability_engine.verify_token(token_2, "file:read");
    EXPECT_TRUE(verify_2.is_valid);

    _putenv("SENTINEL_POLICY_SIGNING_KEY=");
    _putenv("SENTINEL_POLICY_PREVIOUS_SIGNING_KEY=");
    reset_policy_signing_key_for_tests();
    reset_policy_previous_signing_key_for_tests();
}
