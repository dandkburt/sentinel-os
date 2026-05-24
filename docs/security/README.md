# Security Docs

This directory contains the threat model, data classification policy, capability token design, and privileged action approval flow.

## Initial Documents To Add
- Threat model v1
- Data classification policy
- Consent and approval model
- Security testing strategy

## Operational Runbooks
- `KEY_ROTATION_RUNBOOK.md`: Credential Manager rotation checklist and rollback steps.

## Operational Scripts
- `scripts/Set-PolicyCredentialSecrets.ps1`: write/list/remove policy credential targets.
- `scripts/Test-PolicySecretProviderReadiness.ps1`: preflight validation for provider mode, credential targets, and production-safe fallback settings.

## Capability Token Signing Key Id (kid) Policy

Capability tokens use a 4-segment format:
- `version.extension_id.token_id.signature`

The `kid` is encoded inside `token_id` as:
- `k<rotationNumber>-<counter>`
- Examples: `k1-42`, `k12-109`

Current validation rules:
- `kid` must start with lowercase `k`.
- Remaining `kid` characters must be digits only.
- `kid` length is constrained by implementation limits.
- Tokens with an explicit but unrecognized `kid` are rejected.

### Environment Variables

Signing and verification keys are configured with:
- `SENTINEL_POLICY_SIGNING_KEY`
- `SENTINEL_POLICY_PREVIOUS_SIGNING_KEY`
- `SENTINEL_POLICY_SIGNING_KEY_ID`
- `SENTINEL_POLICY_PREVIOUS_SIGNING_KEY_ID`

Provider selection controls:
- `SENTINEL_POLICY_SECRET_PROVIDER`:
	- `auto` (default): try stronger OS-backed providers first.
	- `credential-manager`: Windows Credential Manager provider.
	- `dpapi-file`: DPAPI-encrypted local file provider.
	- `env`: environment only.
- `SENTINEL_POLICY_SECRET_TARGET_PREFIX`:
	- Credential Manager target prefix (default: `SentinelOS/Policy`).
- `SENTINEL_POLICY_SECRET_FILE`:
	- Path to DPAPI-encrypted secret payload file.
- `SENTINEL_POLICY_ALLOW_ENV_FALLBACK`:
	- Dev/test fallback switch; when set truthy, environment keys may be used if providers are unavailable.
- `SENTINEL_POLICY_ALLOW_ENV_KEYS_ONLY`:
	- Forces environment-only loading (development/testing only).

On first policy service creation, policy key material is loaded automatically from configured provider strategy.

### Rotation Convention

Use this operational sequence:
1. Set new primary key and id (`SIGNING_KEY`, `SIGNING_KEY_ID`).
2. Move prior primary key and id into previous (`PREVIOUS_SIGNING_KEY`, `PREVIOUS_SIGNING_KEY_ID`).
3. Keep previous values during a short overlap window for in-flight token verification.
4. Remove previous key/id after overlap to complete cutover.

Recommended conventions:
- Increment rotation numbers monotonically: `k1`, `k2`, `k3`, ...
- Keep overlap windows short and auditable.
- Do not reuse old key ids for new key material.

### Credential Manager Provisioning

Use the PowerShell provisioning utility:
- `scripts/Set-PolicyCredentialSecrets.ps1`

Examples:
1. Initial write (primary key/id only):
	- `./scripts/Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<secret>" -SigningKeyId "k1"`
2. Rotation with overlap window:
	- `./scripts/Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<new-secret>" -SigningKeyId "k2" -IncludePrevious -PreviousSigningKey "<old-secret>" -PreviousSigningKeyId "k1"`
3. Complete cutover (remove previous key/id):
	- `./scripts/Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<new-secret>" -SigningKeyId "k2" -ClearPrevious`
4. List configured targets (non-secret):
	- `./scripts/Set-PolicyCredentialSecrets.ps1 -Action list`
5. Remove all policy credential targets:
	- `./scripts/Set-PolicyCredentialSecrets.ps1 -Action remove`

Provisioning notes:
- The script writes generic credentials under `SENTINEL_POLICY_SECRET_TARGET_PREFIX` (default `SentinelOS/Policy`).
- The script never prints secret values.
- Keep environment fallback disabled in production unless incident response requires temporary override.
