# Branch Protection Setup

This guide explains how to enforce policy readiness in repository protection settings.

## Goal

Require a successful production readiness gate before promotion-sensitive changes are considered ready.

Relevant workflow jobs in `.github/workflows/ci.yml`:
- `policy-production-readiness-gate`
- `production-promotion`

## GitHub Settings (UI)

1. Open repository settings.
2. Go to `Branches` and edit protection for `main`.
3. Enable `Require status checks to pass before merging`.
4. Add required checks from CI:
   - `build`
   - `startup-smoke`
   - `policy-readiness-script-smoke`
5. Enable `Require branches to be up to date before merging`.
6. Save rule.

## Production Promotion Control

Use GitHub Environments:
1. Open `Settings` -> `Environments` -> `production`.
2. Add protection rules:
   - Required reviewers (recommended)
   - Optional wait timer
3. Ensure deployment-like workflow dispatch runs use the `production` environment.

The job `production-promotion` is environment-scoped and only runs after `policy-production-readiness-gate` passes, giving a clean approval boundary.

## CLI Automation

Use the repository script:
- `scripts/Set-GitHubBranchProtection.ps1`

Examples:
1. Dry run (review payload before applying):
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-GitHubBranchProtection.ps1 -Owner <owner> -Repo <repo> -Branch main -EnforceAdmins -RequireConversationResolution -DryRun`
2. Apply branch protection defaults:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-GitHubBranchProtection.ps1 -Owner <owner> -Repo <repo> -Branch main -EnforceAdmins -RequireConversationResolution`
3. Apply with custom required checks:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-GitHubBranchProtection.ps1 -Owner <owner> -Repo <repo> -RequiredChecks build,startup-smoke,policy-readiness-script-smoke`

Requirements:
- `gh` CLI installed and authenticated (`gh auth login`).
- Repository admin permissions for branch protection updates.

## Operational Note

The manual readiness gate (`workflow_dispatch`) is designed for promotion windows and rotation checks. Keep normal PR checks lightweight and deterministic, and run production gate checks before release approval.
