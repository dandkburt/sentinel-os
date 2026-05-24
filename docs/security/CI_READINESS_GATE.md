# CI Policy Readiness Gate

This guide documents how to run the manual policy readiness gate in GitHub Actions.

## Workflow

The gate is implemented in:
- `.github/workflows/ci.yml`
- Job: `policy-production-readiness-gate`
- Trigger: `workflow_dispatch`

## Required Repository Secrets

For `provider_mode=env`, configure:
- `POLICY_SIGNING_KEY`
- `POLICY_SIGNING_KEY_ID`
- Optional overlap window values:
  - `POLICY_PREVIOUS_SIGNING_KEY`
  - `POLICY_PREVIOUS_SIGNING_KEY_ID`

For `provider_mode=credential-manager` or `provider_mode=dpapi-file`, these env secrets are not required by the checker itself, but the target provider resources must exist for the chosen mode.

## Dispatch Inputs

- `provider_mode`: `auto`, `credential-manager`, `dpapi-file`, `env`
- `require_previous`: `true` or `false`
- `secret_target_prefix`: Credential Manager prefix, default `SentinelOS/Policy`
- `secret_file`: DPAPI file path, default `C:\ProgramData\SentinelOS\policy_secrets.dpapi`

## Recommended Usage

1. Dry-run in CI context with env mode:
   - `provider_mode=env`
   - `require_previous=false`
2. Overlap-window validation:
   - `provider_mode=env`
   - `require_previous=true`
3. Production-like readiness checks:
   - `provider_mode=credential-manager` or `auto`
   - `require_previous` based on rotation stage

## Outputs

Each run publishes:
- Artifact: `policy-readiness-result` containing `policy-readiness-result.json`
- Job summary table in GitHub Actions UI with per-check PASS/FAIL status

A failed readiness check fails the job and should block deployment promotion.
