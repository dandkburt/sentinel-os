#include "policy.h"
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <set>

namespace sentinel::services::policy {

namespace {
bool capability_matches(const std::string& granted_capability, const std::string& required_capability) {
    if (granted_capability == "*") {
        return true;
    }

    if (granted_capability == required_capability) {
        return true;
    }

    const auto wildcard_pos = granted_capability.find(':');
    if (wildcard_pos != std::string::npos &&
        wildcard_pos + 1 < granted_capability.size() &&
        granted_capability[wildcard_pos + 1] == '*') {
        const auto granted_prefix = granted_capability.substr(0, wildcard_pos);
        const auto required_sep = required_capability.find(':');
        if (required_sep != std::string::npos) {
            return required_capability.substr(0, required_sep) == granted_prefix;
        }
    }

    return false;
}
}  // namespace

/// @brief Default capability engine implementation
class CapabilityEngineImpl : public ICapabilityEngine {
public:
    CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) override {
        // TODO: Implement cryptographic token verification
        // - Verify HMAC signature
        // - Check expiry timestamp
        // - Match capability scope
        
        auto it = issued_tokens_.find(token);
        if (it == issued_tokens_.end()) {
            return {false, "", "", 0, 0};
        }

        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        if (it->second.expires_at > 0 && now > it->second.expires_at) {
            return {false, "", "", 0, 0};  // Token expired
        }

        auto matched_capability = std::find_if(
            it->second.capabilities.begin(),
            it->second.capabilities.end(),
            [&required_capability](const std::string& granted) {
                return capability_matches(granted, required_capability);
            });

        if (matched_capability == it->second.capabilities.end()) {
            return {false, "", "", 0, 0};
        }

        return {true, it->second.extension_id, *matched_capability,
                it->second.issued_at, it->second.expires_at};
    }

    std::string issue_token(const std::string& extension_id, const std::vector<std::string>& capabilities, 
                           uint32_t expiry_seconds = 0) override {
        // TODO: Generate cryptographically secure token
        // - Create capability scope from capabilities vector
        // - Sign with private key
        // - Store metadata for verification
        
        std::string token = "token_" + extension_id + "_" + std::to_string(issued_tokens_.size());
        
        const auto now = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        std::vector<std::string> granted_capabilities = capabilities.empty() ? std::vector<std::string>{"*"} : capabilities;

        TokenMetadata metadata{
            extension_id,
            std::move(granted_capabilities),
            now,
            expiry_seconds > 0 ? static_cast<uint64_t>(now + expiry_seconds) : 0
        };
        
        issued_tokens_[token] = metadata;
        return token;
    }

    bool revoke_token(const std::string& token) override {
        auto it = issued_tokens_.find(token);
        if (it != issued_tokens_.end()) {
            issued_tokens_.erase(it);
            return true;
        }
        return false;
    }

    std::vector<std::string> get_capabilities(const std::string& extension_id) const override {
        std::set<std::string> unique_capabilities;
        for (const auto& pair : issued_tokens_) {
            if (pair.second.extension_id == extension_id) {
                for (const auto& capability : pair.second.capabilities) {
                    unique_capabilities.insert(capability);
                }
            }
        }

        std::vector<std::string> result;
        result.reserve(unique_capabilities.size());
        for (const auto& capability : unique_capabilities) {
            result.push_back(capability);
        }

        return result;
    }

private:
    struct TokenMetadata {
        std::string extension_id;
        std::vector<std::string> capabilities;
        uint64_t issued_at;
        uint64_t expires_at;
    };

    std::unordered_map<std::string, TokenMetadata> issued_tokens_;
};

/// @brief Default policy service implementation
class PolicyServiceImpl : public IPolicyService {
public:
    PolicyServiceImpl() : capability_engine_(std::make_unique<CapabilityEngineImpl>()) {}

    PolicyDecision evaluate(const std::string& requester_id, const std::string& action, 
                           const std::string& resource_id, const std::string& context = "") override {
        // TODO: Implement policy evaluation
        // - Load policy rules
        // - Match action against rules
        // - Evaluate conditions
        // - Return decision

        (void)requester_id;
        (void)resource_id;
        (void)context;
        
        auto it = rules_.find(action);
        if (it != rules_.end()) {
            // Rule exists, evaluate it
            return PolicyDecision::Allow;  // Simplified: allow if rule exists
        }
        
        // Default deny for unknown actions
        return PolicyDecision::Deny;
    }

    bool register_rule(const std::string& rule_id, const std::string& rule_condition) override {
        // TODO: Parse and validate rule condition JSON
        rules_[rule_id] = rule_condition;
        return true;
    }

    ICapabilityEngine& capability_engine() override {
        return *capability_engine_;
    }

    bool has_permission(const std::string& extension_id, const std::string& permission) const override {
        // Fast path: explicit permission table.
        auto it = extension_permissions_.find(extension_id);
        if (it != extension_permissions_.end()) {
            auto permission_it = it->second.find(permission);
            if (permission_it != it->second.end() && permission_it->second) {
                return true;
            }
        }

        // Fallback: derive effective permission from capability grants.
        const auto capabilities = capability_engine_->get_capabilities(extension_id);
        for (const auto& capability : capabilities) {
            if (capability_matches(capability, permission)) {
                return true;
            }
        }

        return false;
    }

private:
    std::unique_ptr<ICapabilityEngine> capability_engine_;
    std::unordered_map<std::string, std::string> rules_;
    std::unordered_map<std::string, std::unordered_map<std::string, bool>> extension_permissions_;
};

static PolicyServiceImpl* g_policy_service = nullptr;

PolicyServiceImpl& get_policy_service() {
    if (g_policy_service == nullptr) {
        g_policy_service = new PolicyServiceImpl();
    }
    return *g_policy_service;
}

void initialize_policy_service() {
    if (g_policy_service == nullptr) {
        g_policy_service = new PolicyServiceImpl();
    }
}

IPolicyService& get_policy_service_interface() {
    return get_policy_service();
}

}  // namespace sentinel::services::policy
