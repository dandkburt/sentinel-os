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

## Optional CLI Automation

If using `gh` with repo admin permissions:

```powershell
$owner = "<owner>"
$repo = "<repo>"
$branch = "main"

$payload = @{
  required_status_checks = @{
    strict = $true
    contexts = @(
      "build",
      "startup-smoke",
      "policy-readiness-script-smoke"
    )
  }
  enforce_admins = $true
  required_pull_request_reviews = @{
    required_approving_review_count = 1
  }
  restrictions = $null
} | ConvertTo-Json -Depth 6

$payload | gh api "/repos/$owner/$repo/branches/$branch/protection" --method PUT --input -
```

Adjust contexts if job names change.

## Operational Note

The manual readiness gate (`workflow_dispatch`) is designed for promotion windows and rotation checks. Keep normal PR checks lightweight and deterministic, and run production gate checks before release approval.
