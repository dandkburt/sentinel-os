# ADR-001: Language and Runtime Choices

## Status
Accepted

## Context
Sentinel OS requires a high-performance, memory-safe core runtime that:
- Manages extension lifecycle and isolation
- Provides deterministic performance (no garbage collector pauses)
- Integrates with Windows and Linux system APIs
- Allows incremental development with clear boundaries

## Decision
Adopt a **hybrid language stack**:
- **C++20** for core runtime, IPC, policy enforcement, and other performance-critical/native boundaries
- **C#** for orchestration, shell tooling, admin experiences, and higher-level service coordination
- **CMake 3.28+** for native build targets and **.NET** tooling/projects for managed components

### Runtime Architecture
- **Core Native Layer**: C++20 with `-std=c++20` enforced
- **Managed Layer**: C# / .NET for non-native orchestration and UI-adjacent workflows
- **Standard**: ISO C++ with standard library (STL) for native modules; .NET runtime and class libraries for managed modules
- **Build System**: CMake 3.28+ for native targets, with managed projects isolated behind explicit interfaces
- **Platform Support**: Prioritize Windows 11+ and Linux (kernel 5.10+) on x86-64
- **Extension Runtime**: Lightweight capability-based runtime (see ADR-002)

## Rationale
1. **Performance**: C++20 provides zero-overhead abstractions with compile-time verification for hot paths
2. **Control**: Direct memory management without GC pauses ensures predictable performance in the core
3. **Productivity**: C# accelerates orchestration, tooling, and shell-adjacent development
4. **Integration**: Native Windows/Linux API access stays in C++, while higher-level coordination can remain managed
5. **Determinism**: Security-critical and real-time components stay native; less sensitive surfaces move faster in C#

## Consequences
- **Positive**: Native core remains fast and deterministic, while managed layers improve delivery speed
- **Negative**: Two toolchains add integration complexity and require interface discipline
- **Mitigations**: Keep boundaries explicit, use versioned contracts, enforce CI checks for both ecosystems

## Future Considerations
- Consider Rust for security-critical subsystems if native safety requirements tighten further
- Keep managed components limited to orchestration and shell-adjacent code unless profiling shows otherwise
