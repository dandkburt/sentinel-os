# Managed Layer

This directory contains the C# / .NET portion of Sentinel OS.

## Purpose
- Orchestration
- Shell-adjacent tooling
- Higher-level service coordination
- Admin and developer workflows

## Design Rule
Keep the native core in C++ and move product-facing orchestration into C# where practical.

## Explicit Boundaries
- C# in `src/managed` can depend on native contract adapters only.
- C# must not depend on C++ implementation internals from `src/core`, `src/services`, or `src/shell`.
- Native interop must go through explicit ABI contracts (for example `runtime_c_api.h`) and managed adapters.

## Scope Guardrails
- Allowed in managed: orchestration, settings/admin workflows, extension management UI, dev tooling.
- Not allowed in managed: runtime internals, IPC core, policy enforcement engine, namespace isolation internals, low-level windowing.
