# Sentinel OS Implementation TODOs

This document tracks concrete implementation tasks across the Sentinel OS codebase.
Each TODO is scoped to a specific component and layer.

## Priority 1: Foundation (Required for Phase 2)

### Core Runtime (`src/core/runtime/bootstrap.h`, `runtime.cpp`)

- [x] **DONE**: Initialize core subsystems
  - Location: `BootstrapImpl::initialize()`
  - Task: Implement initialization sequence:
    1. Load configuration from file (TOML/JSON)
    2. Initialize logging system
    3. Start policy engine
    4. Start namespace manager
    5. Register event broker
  - Depends: Configuration loader, logging framework
  - Estimated effort: 2-3 days

- [x] **DONE**: Gracefully shutdown subsystems
  - Location: `BootstrapImpl::shutdown()`
  - Task: Implement cleanup sequence with timeout:
    1. Signal "shutting_down" event
    2. Wait for in-flight operations (timeout)
    3. Close policy engine connections
    4. Cleanup namespace manager state
    5. Flush logs
  - Depends: Event system, subsystem coordination
  - Estimated effort: 1-2 days

- [x] **DONE**: Implement event subscription with thread safety
  - Location: `BootstrapImpl::on_lifecycle_event()`
  - Task: Add thread-safe callback storage:
    1. Use `std::mutex` for callback list protection
    2. Allow multiple subscribers per event
    3. Handle callback exceptions gracefully
    4. Support unsubscribe functionality
  - Depends: C++ standard library synchronization primitives
  - Estimated effort: 1 day

- [x] **DONE**: Verify capability before returning subsystem
  - Location: `RuntimeImpl::get_subsystem()`
  - Task: Integrate with capability engine:
    1. Parse capability string format
    2. Call `capability_engine.verify_token(token, capability)`
    3. Log access attempt for audit
    4. Return subsystem or nullptr based on verification
  - Depends: Capability engine implementation
  - Estimated effort: 1 day

### Policy Service (`src/services/policy/policy.h`, `policy_service.cpp`)

- [x] **DONE**: Implement cryptographic token verification
  - Location: `CapabilityEngineImpl::verify_token()`
  - Task: Add HMAC-SHA256 signature verification:
    1. Parse token format (header.payload.signature)
    2. Verify signature with hardcoded public key
    3. Check expiry timestamp (if present)
    4. Validate capability scope matches request
    5. Update issued_tokens_ metadata
  - Depends: OpenSSL/Crypto++ library
  - Estimated effort: 2 days

- [x] **DONE**: Generate cryptographically secure tokens
  - Location: `CapabilityEngineImpl::issue_token()`
  - Task: Create and sign new capability tokens:
    1. Generate random token ID
    2. Create JSON payload with extension_id, capabilities, expiry
    3. Sign with private key (HMAC-SHA256)
    4. Return formatted token string (base64 encoded)
    5. Store metadata for future verification
  - Depends: OpenSSL/Crypto++, random number generator
  - Estimated effort: 2 days

- [x] **DONE**: Implement policy evaluation
  - Location: `PolicyServiceImpl::evaluate()`
  - Task: Evaluate access policies:
    1. Load policy rules from configuration
    2. Match action against rule patterns (e.g., "file:*")
    3. Evaluate conditions (requester_id, resource_id, context)
    4. Return Allow/Deny/Defer decision
    5. Log policy decision for audit
  - Depends: Policy rule engine, JSON parsing
  - Estimated effort: 3 days

- [x] **DONE**: Parse and validate rule condition JSON
  - Location: `PolicyServiceImpl::register_rule()`
  - Task: Validate and store policy rules:
    1. Parse JSON condition expression
    2. Validate rule syntax (required fields, types)
    3. Store rule in rules_ map
    4. Support rule updates and deletions
    5. Handle invalid rule format with errors
  - Depends: JSON parser (nlohmann/json or similar)
  - Estimated effort: 1-2 days

- [x] **DONE**: Check if extension has permission capability
  - Location: `PolicyServiceImpl::has_permission()`
  - Task: Query extension permissions:
    1. Lookup extension_id in extension_permissions_ map
    2. Check if permission is in permission set
    3. Consult capability engine if needed
    4. Return true/false result
  - Depends: Permission database/cache
  - Estimated effort: 1 day

### Namespace Service (`src/services/namespace/namespace.h`, `namespace_service.cpp`)

- [x] **DONE**: Allocate namespace with quotas
  - Location: `NamespaceManagerImpl::create_namespace()`
  - Task: Initialize namespace resource tracking:
    1. Validate namespace_id doesn't already exist
    2. Create quota tracking structure
    3. Initialize resource usage metrics (all zeros)
    4. Store owner_id for access control
    5. Return true on success
  - Depends: Quota definition validation
  - Estimated effort: 1 day

- [x] **DONE**: Cleanup namespace and reclaim resources
  - Location: `NamespaceManagerImpl::remove_namespace()`
  - Task: Release namespace resources:
    1. Check if namespace exists
    2. Cleanup all entries in namespace
    3. Reclaim resources from global pool
    4. Log cleanup for audit
    5. Remove from namespaces_ map
  - Depends: Entry cleanup procedures
  - Estimated effort: 1 day

- [x] **DONE**: Validate entry and check namespace quota
  - Location: `NamespaceManagerImpl::add_entry()`
  - Task: Add entry with validation:
    1. Check namespace exists
    2. Validate entry path format
    3. Check if quota would be exceeded
    4. Add entry to namespace
    5. Update usage metrics
    6. Log quota changes
  - Depends: Quota checking logic
  - Estimated effort: 1-2 days

- [x] **DONE**: Collect current resource usage metrics
  - Location: `NamespaceManagerImpl::get_resource_usage()`
  - Task: Calculate namespace resource usage:
    1. Sum memory usage of all entries
    2. Count open file handles
    3. Count active threads
    4. Count network connections
    5. Return metrics map
  - Depends: Resource tracking mechanisms
  - Estimated effort: 1-2 days

- [x] **DONE**: Validate new quota against current usage and apply
  - Location: `NamespaceManagerImpl::set_resource_quota()`
  - Task: Update quotas with validation:
    1. Get current resource usage
    2. Validate new quota >= current usage
    3. Check global resource availability
    4. Apply new quota
    5. Log quota change for audit
  - Depends: Global resource pool management
  - Estimated effort: 1-2 days

### Desktop Shell (`src/shell/desktop/shell.h`, `desktop_shell.cpp`)

- [x] **DONE**: Create native window resource
  - Location: `WindowManagerImpl::create_window()`
  - Task: Allocate native window:
    1. Call platform API (CreateWindow on Windows, XCreateWindow on X11)
    2. Set window properties (title, size, position)
    3. Store window handle in windows_ map
    4. Generate and return window_id
    5. Register event handlers for window events
  - Depends: Platform-specific window APIs
  - Estimated effort: 2-3 days

- [x] **DONE**: Release native window resource
  - Location: `WindowManagerImpl::destroy_window()`
  - Task: Cleanup window:
    1. Lookup window in windows_ map
    2. Call platform destroy API
    3. Clear event callbacks
    4. Remove from windows_ map
    5. Return success status
  - Depends: Platform window APIs, cleanup logic
  - Estimated effort: 1 day

- [x] **DONE**: Register event callback with proper thread safety
  - Location: `WindowManagerImpl::on_window_event()`
  - Task: Add event subscription with synchronization:
    1. Use mutex to protect window_callbacks_ map
    2. Add callback to event's callback list
    3. Support multiple callbacks per event
    4. Return unsubscribe handle if possible
  - Depends: Synchronization primitives
  - Estimated effort: 1 day

- [x] **DONE**: Desktop initialization scaffold with explicit readiness/failure states
  - Location: `DesktopShellImpl::initialize()`
  - Task: Add deterministic bootstrap scaffold:
    1. Display connection step
    2. Root window scaffold step
    3. Event handler registration step
    4. Input routing start step
    5. Explicit failed/ready/shutdown lifecycle state handling
  - Validation: Integration failure-mode + recovery assertions added.
  - Follow-up: Replace scaffold steps with platform-specific adapters.

- [x] **DONE**: Cleanup window system resources
  - Location: `DesktopShellImpl::shutdown()`
  - Task: Release all window system resources:
    1. Close all windows
    2. Disconnect from display server
    3. Release graphics context
    4. Stop input event routing
    5. Cleanup background threads
  - Depends: Window system cleanup APIs
  - Estimated effort: 2 days

- [x] **DONE**: Highlight window in taskbar
  - Location: `DesktopShellImpl::set_active_taskbar_item()`
  - Task: Update taskbar appearance:
    1. Lookup window by window_id
    2. Call taskbar update API
    3. Highlight/flash taskbar item
    4. Bring window to front if requested
    5. Log taskbar change
  - Depends: Taskbar APIs
  - Estimated effort: 1 day

- [x] **DONE**: Show native desktop notification
  - Location: `DesktopShellImpl::show_notification()`
  - Task: Display desktop notification:
    1. Call platform notification API (WinToast on Windows, dbus on Linux)
    2. Set notification content (title, message)
    3. Set timeout duration
    4. Store notification metadata
    5. Return notification_id for later reference
  - Depends: Platform notification APIs
  - Estimated effort: 1-2 days

- [x] **DONE**: Implement Z-order manipulation
  - Location: `WindowManagerImpl::bring_to_front()`
  - Task: Change window Z-order:
    1. Lookup window by window_id
    2. Call platform Z-order API (SetWindowPos on Windows, XRaiseWindow on X11)
    3. Update internal Z-order tracking if needed
    4. Fire "raised" event
  - Depends: Platform Z-order APIs
  - Estimated effort: 1 day

## Priority 2: Core Features (Phase 2)

### Configuration System
- [x] Load runtime configuration from TOML/JSON
- [x] Configuration validation and defaults
- [x] Runtime reconfiguration support
- [x] Configuration versioning

### Event System
- [x] Pub/Sub event broker with capability filtering
- [x] Event routing and delivery guarantees
- [x] Async event handling with thread pool
- [x] Event ordering and sequencing

### Extension System
- [x] Extension manifest format and parser
- [x] Extension registration and discovery
- [x] Extension lifecycle management (load, enable, disable, unload)
- [x] Extension dependency resolution

## Priority 3: Integration & Testing (Phase 3)

### Unit Test Implementation
- [ ] Implement all integration test placeholders (see `tests/integration/test_integration.cpp`)
- [ ] Add mock implementations for external dependencies
- [ ] Add negative test cases for error handling
- [ ] Add performance benchmarks

### Platform Integration
- [ ] Windows platform-specific code (DWM, Win32)
- [ ] Linux platform-specific code (Wayland/X11, dbus)
- [ ] macOS platform-specific code (Cocoa, launchd)

### Security & Hardening
- [ ] Audit logging for all capability checks
- [ ] Security policy enforcement
- [ ] Vulnerability scanning in build pipeline
- [ ] Penetration testing framework

## Tracking Guidelines

### When Adding a TODO
1. Use `// TODO:` comment in code
2. Include task description and location
3. List dependencies
4. Estimate effort (1/2/3/5/8 days)
5. Link to related ADRs

### When Completing a TODO
1. Remove `// TODO:` comment
2. Add inline comments explaining implementation
3. Ensure corresponding tests pass
4. Update this document

### Template
```cpp
// TODO: Brief task description
// Location: File and function
// Task: Detailed steps
// Depends: Other components/libraries
// Estimated: X days
```

## Statistics

- **Total TODOs**: 27 (Priority 1: 21, Priority 2: 4, Priority 3: 2)
- **Estimated Total Effort**: 45-60 person-days for Phase 1 completion
- **Critical Path**: Cryptographic token verification → Policy evaluation → Namespace enforcement

## See Also

- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Architecture overview
- [BUILD_AND_TEST.md](BUILD_AND_TEST.md) - Build instructions
- [docs/adr/](docs/adr/) - Architecture Decision Records
