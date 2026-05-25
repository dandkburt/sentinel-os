# Sentinel OS

Sentinel OS is a Windows-like desktop operating system project focused on secure-by-design system services, an intelligent shell, and strict user control.

## Monorepo Layout
- `src/core`: runtime and IPC foundations
- `src/shell`: desktop, launcher, taskbar, notifications
- `src/services`: namespace, policy, observability services
- `src/intelligence`: intent, ranking, prefetch modules
- `src/managed`: C# orchestration and shell-adjacent tooling
- `tests`: unit, integration, performance, reliability
- `docs`: architecture, API, ADRs, security
- `assets/branding`: logos, splash screens, visual assets

## Initial Technical Direction
- Core language: C++20 for low-level runtime, IPC, and performance-critical services
- Managed language: C# for orchestration, shell tooling, admin surfaces, and higher-level service coordination
- Build system: CMake for native targets, .NET for managed targets
- CI: GitHub Actions
- No Python required

## Mixed-Language Boundary Rules
- `src/core`, `src/services`, and low-level shell components remain native C++20.
- `src/managed` contains C# orchestration and productivity surfaces only.
- Managed code calls native code only through explicit ABI contracts and adapters.
- Dependency direction is one-way: managed orchestration depends on contracts, not C++ internals.
- Core service ownership remains in native modules; no runtime/policy/namespace/windowing rewrites in C#.

## Next Steps
1. Freeze ADR-001 through ADR-003.
2. Add the first C++ targets for `core_runtime`, `policy_service`, and `shell_desktop`.
3. Add the managed C# orchestration project and wire it into the solution workflow.
4. Wire unit and integration tests into CI.
5. Import the GitHub backlog and begin Sprint 1.

## Build Entrypoints
- Windows: `./build.ps1`
- Unix-like: `./build.sh`

Both entrypoints run configure, native build, CTest, managed build, and output verification.

## Policy Operations Quick Reference

### Reload State Meanings
- `idle`: no reload currently in progress.
- `reloading`: policy service is actively applying provider/environment key material.
- `applied`: reload succeeded and new key state is active.
- `rolled_back`: reload succeeded and a key generation decrease was applied (rollback event).
- `failed`: reload could not apply new state and fail-safe retention remained in effect.

### Fail-Safe Behavior
- If provider loading is unavailable and `SENTINEL_POLICY_ALLOW_ENV_FALLBACK` is disabled, policy service retains the existing in-memory keys.
- This path is explicit fail-safe retain behavior and does not switch to environment keys.

### Operational Telemetry Fields
- Provider and source counters: `provider_attempts`, `provider_successes`, `provider_failures`, `environment_loads`.
- Reload lifecycle counters: `reload_attempts`, `reload_successes`, `reload_failures`, `rollback_events`, `rollback_applies`.
- Safety and state fields: `fail_safe_retained_state`, `zeroization_events`, `reload_state`, `last_reload_outcome`, `last_reload_timestamp`, `last_source`, `last_error`.
