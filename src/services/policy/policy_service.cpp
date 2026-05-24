#include "policy.h"
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <set>
#include <vector>
#include <sstream>
#include <cctype>

namespace sentinel::services::policy {

namespace {
constexpr const char* kTokenVersion = "v1";

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

bool extract_json_string_field(const std::string& json, const std::string& key, std::string& out) {
    const std::string quoted_key = "\"" + key + "\"";
    const auto key_pos = json.find(quoted_key);
    if (key_pos == std::string::npos) {
        return false;
    }

    const auto colon_pos = json.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    const auto value_start = json.find('"', colon_pos + 1);
    if (value_start == std::string::npos) {
        return false;
    }

    const auto value_end = json.find('"', value_start + 1);
    if (value_end == std::string::npos || value_end <= value_start + 1) {
        return false;
    }

    out = json.substr(value_start + 1, value_end - value_start - 1);
    return true;
}

bool action_matches(const std::string& action_pattern, const std::string& action) {
    if (action_pattern == "*") {
        return true;
    }

    if (action_pattern == action) {
        return true;
    }

    const auto wildcard_pos = action_pattern.find(':');
    if (wildcard_pos != std::string::npos &&
        wildcard_pos + 1 < action_pattern.size() &&
        action_pattern[wildcard_pos + 1] == '*') {
        const auto pattern_prefix = action_pattern.substr(0, wildcard_pos);
        const auto action_sep = action.find(':');
        if (action_sep != std::string::npos) {
            return action.substr(0, action_sep) == pattern_prefix;
        }
    }

    return false;
}

bool parse_effect(const std::string& effect_raw, PolicyDecision& effect_out) {
    if (effect_raw == "allow") {
        effect_out = PolicyDecision::Allow;
        return true;
    }
    if (effect_raw == "deny") {
        effect_out = PolicyDecision::Deny;
        return true;
    }
    if (effect_raw == "defer") {
        effect_out = PolicyDecision::Defer;
        return true;
    }
    return false;
}

bool is_safe_token_segment(const std::string& value) {
    if (value.empty()) {
        return false;
    }

    for (char c : value) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

struct ParsedToken {
    std::string version;
    std::string extension_id;
    std::string token_id;
};

bool parse_token(const std::string& token, ParsedToken& out) {
    if (token.empty()) {
        return false;
    }

    const auto first_dot = token.find('.');
    if (first_dot == std::string::npos) {
        return false;
    }

    const auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) {
        return false;
    }

    if (token.find('.', second_dot + 1) != std::string::npos) {
        return false;
    }

    out.version = token.substr(0, first_dot);
    out.extension_id = token.substr(first_dot + 1, second_dot - first_dot - 1);
    out.token_id = token.substr(second_dot + 1);

    if (out.version != kTokenVersion) {
        return false;
    }
    if (!is_safe_token_segment(out.extension_id)) {
        return false;
    }
    if (!is_safe_token_segment(out.token_id)) {
        return false;
    }

    return true;
}
}  // namespace

/// @brief Default capability engine implementation
class CapabilityEngineImpl : public ICapabilityEngine {
public:
    CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) override {
        ParsedToken parsed;
        if (!parse_token(token, parsed)) {
            return {false, "", "", 0, 0};
        }
        
        auto it = issued_tokens_.find(token);
        if (it == issued_tokens_.end()) {
            return {false, "", "", 0, 0};
        }

        if (it->second.extension_id != parsed.extension_id) {
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
        if (!is_safe_token_segment(extension_id)) {
            return "";
        }
        
        const auto token_id = std::to_string(next_token_id_++);
        const std::string token = std::string(kTokenVersion) + "." + extension_id + "." + token_id;
        
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
    uint64_t next_token_id_ = 1;
};

/// @brief Default policy service implementation
class PolicyServiceImpl : public IPolicyService {
public:
    PolicyServiceImpl() : capability_engine_(std::make_unique<CapabilityEngineImpl>()) {}

    PolicyDecision evaluate(const std::string& requester_id, const std::string& action, 
                           const std::string& resource_id, const std::string& context = "") override {
        for (const auto& rule_id : rule_order_) {
            const auto it = rules_.find(rule_id);
            if (it == rules_.end()) {
                continue;
            }

            const PolicyRule& rule = it->second;
            if (!action_matches(rule.action_pattern, action)) {
                continue;
            }

            if (!rule.requester_id.empty() && rule.requester_id != requester_id) {
                continue;
            }

            if (!rule.resource_id.empty() && rule.resource_id != resource_id) {
                continue;
            }

            if (!rule.context_contains.empty() &&
                context.find(rule.context_contains) == std::string::npos) {
                continue;
            }

            return rule.effect;
        }

        // Default deny for unknown actions
        return PolicyDecision::Deny;
    }

    bool register_rule(const std::string& rule_id, const std::string& rule_condition) override {
        if (rule_id.empty() || rule_condition.empty()) {
            return false;
        }

        PolicyRule parsed_rule;
        std::string action_pattern;
        std::string effect_raw;
        if (!extract_json_string_field(rule_condition, "action", action_pattern)) {
            return false;
        }
        if (!extract_json_string_field(rule_condition, "effect", effect_raw)) {
            return false;
        }

        if (!parse_effect(effect_raw, parsed_rule.effect)) {
            return false;
        }

        parsed_rule.action_pattern = action_pattern;
        extract_json_string_field(rule_condition, "requester_id", parsed_rule.requester_id);
        extract_json_string_field(rule_condition, "resource_id", parsed_rule.resource_id);
        extract_json_string_field(rule_condition, "context_contains", parsed_rule.context_contains);

        if (rules_.find(rule_id) == rules_.end()) {
            rule_order_.push_back(rule_id);
        }
        rules_[rule_id] = parsed_rule;
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
    struct PolicyRule {
        std::string action_pattern;
        PolicyDecision effect = PolicyDecision::Deny;
        std::string requester_id;
        std::string resource_id;
        std::string context_contains;
    };

    std::unique_ptr<ICapabilityEngine> capability_engine_;
    std::unordered_map<std::string, PolicyRule> rules_;
    std::vector<std::string> rule_order_;
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
