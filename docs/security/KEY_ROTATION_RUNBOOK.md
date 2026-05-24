# Policy Signing Key Rotation Runbook

This runbook describes how to rotate policy signing keys using the Windows Credential Manager provider path.

## Scope

Applies to policy token signing secrets consumed by `policy_service`:
- `SIGNING_KEY`
- `SIGNING_KEY_ID`
- `PREVIOUS_SIGNING_KEY`
- `PREVIOUS_SIGNING_KEY_ID`

Provisioning utility:
- `scripts/Set-PolicyCredentialSecrets.ps1`

## Preconditions

1. Confirm provider mode for production:
   - `SENTINEL_POLICY_SECRET_PROVIDER=credential-manager` (or `auto` if Credential Manager is first and available).
2. Confirm environment fallback is disabled in production:
   - `SENTINEL_POLICY_ALLOW_ENV_FALLBACK` unset.
   - `SENTINEL_POLICY_ALLOW_ENV_KEYS_ONLY` unset.
3. Generate new signing material and next key id (example: `k7` to `k8`).
4. Record a change ticket with planned overlap window duration.

Preflight verification script:
- `scripts/Test-PolicySecretProviderReadiness.ps1`

## Rotation Checklist

0. Run preflight checks before writing credentials:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode credential-manager -Environment production`

1. Inventory current configured targets:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-PolicyCredentialSecrets.ps1 -Action list`
2. Write new primary and keep old as previous:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<new-secret>" -SigningKeyId "k8" -IncludePrevious -PreviousSigningKey "<old-secret>" -PreviousSigningKeyId "k7"`
3. Restart or roll policy processes so they reload provider-backed secrets.
4. Validate during overlap window:
   - New tokens are minted with `k8-...` token ids.
   - Existing `k7-...` tokens still verify.
5. Monitor logs/metrics for signature validation failures during overlap.

## Cutover Completion

1. Remove previous key material after overlap window:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<new-secret>" -SigningKeyId "k8" -ClearPrevious`
2. Restart or roll policy processes again.
3. Validate post-cutover:
   - Old `k7-...` tokens fail verification.
   - New `k8-...` tokens continue to verify.
4. Re-run preflight checks:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode credential-manager -Environment production`

## Rollback Procedure

Use rollback if validation fails or elevated auth errors appear after rotation.

1. Restore last known good key set as primary:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Set-PolicyCredentialSecrets.ps1 -Action set -SigningKey "<old-secret>" -SigningKeyId "k7" -ClearPrevious`
2. Restart or roll policy processes.
3. Validate restored behavior:
   - New tokens minted with `k7-...`.
   - Signature-invalid rates return to baseline.
4. Keep failed new key material out of production until root cause is identified.

## Incident Override (Dev/Test Only)

Only for controlled diagnostics, not normal production operation:

1. Temporarily enable env fallback:
   - set `SENTINEL_POLICY_ALLOW_ENV_FALLBACK=1`
2. Provide environment signing values for temporary recovery.
3. Remove fallback override after incident resolution.

## Quick Validation Commands

1. Credential Manager production readiness:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode credential-manager -Environment production`
2. Auto mode readiness:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode auto -Environment production`
3. Rotation overlap readiness (requires previous-key material):
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode credential-manager -Environment production -RequirePrevious`
4. JSON output for pipeline parsing:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\Test-PolicySecretProviderReadiness.ps1 -ProviderMode auto -Environment production -OutputJson`

## Audit Notes

Capture these artifacts for each rotation:
- Change ticket id
- Operator identity and timestamp
- Old/new key ids
- Overlap window start/end
- Validation evidence (startup logs, token verification checks, error-rate graphs)
