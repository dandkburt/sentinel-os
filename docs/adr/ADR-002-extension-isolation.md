# ADR-002: Extension Sandbox and Isolation Model

## Status
Accepted

## Context
Sentinel OS runs untrusted third-party extensions. These must be isolated to prevent:
- Unauthorized system API access
- Privilege escalation
- Resource exhaustion attacks
- Data exfiltration

We need a lightweight isolation model that doesn't require OS-level processes (heavyweight) but provides strong security guarantees.

## Decision
Adopt a **capability-based isolation model** with capability tokens issued at extension load time.

### Isolation Primitives

#### Capability Tokens
- Each extension receives a cryptographically signed capability token at registration
- Token encodes: extension ID, version, allowed subsystems, and resource limits
- Token verified on every security-critical operation via `check_capability(token, required_capability)`
- Tokens are immutable after issuance

#### Subsystem Boundaries
Divide Sentinel OS into isolated subsystems:
1. **Policy Engine** - Security policy evaluation and enforcement
2. **Namespace Manager** - Virtual resource namespacing (virtual files, sockets, memory)
3. **Event Broker** - Pub/Sub system with capability-based filtering
4. **Shell API** - Desktop shell and UI interactions
5. **IPC Bus** - Inter-process communication with capability gates

#### Isolation Rules
- Extensions cannot directly load system libraries; requests go through capability-gated facades
- Extension threads run within a thread pool with per-extension resource quotas
- Memory allocation tracked per-extension; quota violations trigger extension termination
- All I/O operations (file, network, event subscription) require explicit capability

### Verification Strategy
- At load time: Verify token signature using hardcoded public key
- At runtime: Enforce capability checks via `ICapabilityEngine::verify()` before sensitive operations
- Audit log all capability checks for security forensics

## Rationale
1. **No OS Process Overhead**: Lighter than containerization or OS sandboxes
2. **Fine-Grained Control**: Subsystem-level capability granularity
3. **Cryptographic Guarantees**: Capability tokens cannot be forged
4. **Auditable**: All access attempts logged with token details
5. **Scalable**: Supports hundreds of extensions per OS instance

## Consequences
- **Positive**: Efficient, secure, and testable in unit tests without heavyweight infrastructure
- **Negative**: Requires strict capability propagation discipline; token management overhead
- **Mitigations**: Automated token generation and distribution; capability validation in CI/CD

## Implementation Schedule
1. Phase 1: Capability token infrastructure and ICapabilityEngine interface
2. Phase 2: Integrate into policy service and namespace manager
3. Phase 3: Wire into shell and event broker
4. Phase 4: Audit logging and forensics

## Future Considerations
- Consider hardware-assisted isolation (e.g., MPK on x86) for multi-tenancy at scale
- Evaluate sandboxing at the compiler level (e.g., wasm-like bytecode for trusted extensions)
