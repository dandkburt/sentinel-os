# Security Docs

This directory contains the threat model, data classification policy, capability token design, and privileged action approval flow.

## Initial Documents To Add
- Threat model v1
- Data classification policy
- Consent and approval model
- Security testing strategy

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

On first policy service creation, environment-backed key material is loaded automatically.

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
