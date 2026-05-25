# Language Boundaries

## Intent
Sentinel OS uses a mixed-language architecture to balance performance and productivity while preserving security-critical core boundaries.

## Layer Split

### Native C++20 Layer (authoritative core)
- `src/core`: runtime bootstrap, IPC foundation
- `src/services`: policy enforcement, namespace isolation, observability services
- `src/shell` (low-level): desktop/windowing primitives and performance-sensitive shell infrastructure
- Ownership: security, isolation, lifecycle correctness, deterministic behavior

### Managed C# Layer (productivity/orchestration)
- `src/managed`: orchestration flows, admin/settings workflows, extension management UX, developer tooling surfaces
- Ownership: workflow coordination, state orchestration, user-facing management experiences

## Contract Boundary
Managed code must use explicit contracts to call native capabilities.

Current contract:
- Native C ABI: `src/core/runtime/runtime_c_api.h`
- Native implementation bridge: `src/core/runtime/runtime_c_api.cpp`
- Managed adapter: `src/managed/SentinelOS.Orchestration/Native/NativeRuntimeContractAdapter.cs`
- Managed contract interface: `src/managed/SentinelOS.Orchestration/Contracts/INativeRuntimeContract.cs`

## Dependency Rules
- Allowed: managed orchestration -> native contract adapter -> native C ABI contract
- Not allowed: managed code directly referencing native implementation internals
- Not allowed: cross-language logic split inside a single core module

## Deferred by Design
The following remain native-only until explicit contracts are added:
- Policy orchestration contract
- Namespace orchestration contract
- Shell/window orchestration contract
- Extension lifecycle native bridge contract

## Contract Test Strategy
- Contract tests live in `tests/contract` and call C ABI functions directly.
- Runtime boundary coverage currently validates:
	- `sentinel_runtime_initialize()` and `sentinel_runtime_shutdown()`
	- `sentinel_runtime_is_ready()` transitions
	- `sentinel_runtime_get_version()` stable version response
- Contract tests must not call C++ interfaces directly; they must execute exported ABI functions only.
- New ABI bridges (policy, namespace, shell) are added only when managed consumers are ready and each must ship with direct ABI contract tests.

## Implementation Notes
- Keep ABI functions narrow and versionable.
- Keep DTOs and status codes stable across native/managed boundary.
- Add one contract at a time and cover each with integration tests.
- Policy signing key material remains in the native policy service and is loaded via a provider-first configuration path with controlled environment fallback, preserving native ownership of security-sensitive key lifecycle decisions.
