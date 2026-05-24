#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace sentinel::services::policy {

/// @brief Policy decision result
enum class PolicyDecision {
    Allow = 0,
    Deny = 1,
    Defer = 2,  // Deferred for async evaluation or user input
};

/// @brief Capability verification result
struct CapabilityVerificationResult {
    bool is_valid;
    std::string extension_id;
    std::string capability_scope;
    uint64_t token_issued_at;  // Unix timestamp
    uint64_t token_expires_at;  // Unix timestamp (0 = no expiry)
};

/// @brief Engine for verifying and managing capability tokens
class ICapabilityEngine {
public:
    virtual ~ICapabilityEngine() = default;

    /// @brief Verify a capability token
    /// @param token The capability token (opaque bytes)
    /// @param required_capability The capability being requested (e.g., "file:read")
    /// @return Verification result with details
    virtual CapabilityVerificationResult verify_token(const std::string& token, const std::string& required_capability) = 0;

    /// @brief Issue a new capability token for an extension
    /// @param extension_id Unique extension identifier
    /// @param capabilities List of capabilities to grant (e.g., ["file:read", "policy:query"])
    /// @param expiry_seconds Seconds until token expires (0 = no expiry)
    /// @return Opaque token string, or empty string if issuance failed
    virtual std::string issue_token(const std::string& extension_id, const std::vector<std::string>& capabilities, uint32_t expiry_seconds = 0) = 0;

    /// @brief Revoke a capability token
    /// @param token The token to revoke
    /// @return true if revocation succeeded
    virtual bool revoke_token(const std::string& token) = 0;

    /// @brief Get capabilities for an extension
    /// @param extension_id Extension identifier
    /// @return List of granted capabilities
    virtual std::vector<std::string> get_capabilities(const std::string& extension_id) const = 0;
};

/// @brief Policy evaluation and enforcement interface
class IPolicyService {
public:
    virtual ~IPolicyService() = default;

    /// @brief Evaluate a security policy against a request
    /// @param requester_id Extension or component ID making the request
    /// @param action Action being requested (e.g., "file:write", "network:send")
    /// @param resource_id Identifier of the resource being accessed
    /// @param context Optional JSON context for policy evaluation
    /// @return Policy decision (Allow, Deny, or Defer)
    virtual PolicyDecision evaluate(const std::string& requester_id, const std::string& action, 
                                   const std::string& resource_id, const std::string& context = "") = 0;

    /// @brief Register a custom policy rule
    /// @param rule_id Unique identifier for the rule
    /// @param rule_condition JSON-serialized condition expression
    /// @return true if registration succeeded
    virtual bool register_rule(const std::string& rule_id, const std::string& rule_condition) = 0;

    /// @brief Get the capability engine for token management
    virtual ICapabilityEngine& capability_engine() = 0;

    /// @brief Check if an extension has a specific permission
    /// @param extension_id Extension identifier
    /// @param permission Permission string (e.g., "extension:install")
    /// @return true if extension has permission
    virtual bool has_permission(const std::string& extension_id, const std::string& permission) const = 0;
};

}  // namespace sentinel::services::policy

// Public API for accessing the global policy service instance
namespace sentinel::services::policy {
    /// @brief Get the global policy service instance
    IPolicyService& get_policy_service_interface();

    /// @brief Initialize the policy service (must be called before use)
    void initialize_policy_service();

    /// @brief Test hook: override signing key used by token issue/verify scaffolding.
    void set_policy_signing_key_for_tests(const std::string& key);

    /// @brief Test hook: reset signing key back to the default value.
    void reset_policy_signing_key_for_tests();

    /// @brief Test hook: compute HMAC-SHA256 hex signature for deterministic unit tests.
    std::string compute_policy_hmac_for_tests(const std::string& message);
}
