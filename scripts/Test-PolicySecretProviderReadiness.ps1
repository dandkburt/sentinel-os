[CmdletBinding()]
param(
    [ValidateSet('auto', 'credential-manager', 'dpapi-file', 'env')]
    [string]$ProviderMode,

    [ValidateSet('production', 'devtest')]
    [string]$Environment = 'production',

    [string]$TargetPrefix = 'SentinelOS/Policy',

    [string]$SecretFile = 'C:\ProgramData\SentinelOS\policy_secrets.dpapi',

    [switch]$RequirePrevious,
    [switch]$OutputJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-EnvOrDefault {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string]$DefaultValue = ''
    )

    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $DefaultValue
    }
    return $value
}

function ConvertTo-BoolLike {
    param(
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    switch ($Value.ToLowerInvariant()) {
        '1' { return $true }
        'true' { return $true }
        'yes' { return $true }
        default { return $false }
    }
}

function Get-CredentialTargets {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix
    )

    return @(
        "$Prefix/SIGNING_KEY",
        "$Prefix/SIGNING_KEY_ID",
        "$Prefix/PREVIOUS_SIGNING_KEY",
        "$Prefix/PREVIOUS_SIGNING_KEY_ID"
    )
}

function Get-CredentialTargetSet {
    $output = & cmdkey.exe /list 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "cmdkey /list failed: $($output -join [Environment]::NewLine)"
    }

    $targets = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $output) {
        if ($line -match 'Target:\s*(.+)$') {
            [void]$targets.Add($Matches[1].Trim())
        }
    }

    return $targets
}

$providerFromEnv = Get-EnvOrDefault -Name 'SENTINEL_POLICY_SECRET_PROVIDER' -DefaultValue 'auto'
if ([string]::IsNullOrWhiteSpace($ProviderMode)) {
    $ProviderMode = $providerFromEnv.ToLowerInvariant()
} else {
    $ProviderMode = $ProviderMode.ToLowerInvariant()
}

if ($ProviderMode -eq 'credential') {
    $ProviderMode = 'credential-manager'
}

$allowEnvFallback = ConvertTo-BoolLike (Get-EnvOrDefault -Name 'SENTINEL_POLICY_ALLOW_ENV_FALLBACK')
$allowEnvOnly = ConvertTo-BoolLike (Get-EnvOrDefault -Name 'SENTINEL_POLICY_ALLOW_ENV_KEYS_ONLY')
$configuredPrefix = Get-EnvOrDefault -Name 'SENTINEL_POLICY_SECRET_TARGET_PREFIX' -DefaultValue $TargetPrefix
$configuredSecretFile = Get-EnvOrDefault -Name 'SENTINEL_POLICY_SECRET_FILE' -DefaultValue $SecretFile

$result = [ordered]@{
    providerMode = $ProviderMode
    environment = $Environment
    checks = @()
    ok = $true
}

function Add-Check {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$Passed,
        [Parameter(Mandatory = $true)]
        [string]$Detail
    )

    $entry = [ordered]@{
        name = $Name
        passed = $Passed
        detail = $Detail
    }

    $result.checks += $entry
    if (-not $Passed) {
        $result.ok = $false
    }
}

if ($Environment -eq 'production') {
    Add-Check -Name 'Env fallback disabled' -Passed (-not $allowEnvFallback) -Detail 'SENTINEL_POLICY_ALLOW_ENV_FALLBACK should be unset/false in production.'
    Add-Check -Name 'Env-only mode disabled' -Passed (-not $allowEnvOnly) -Detail 'SENTINEL_POLICY_ALLOW_ENV_KEYS_ONLY should be unset/false in production.'
}

$targets = Get-CredentialTargets -Prefix $configuredPrefix
$targetSet = Get-CredentialTargetSet

switch ($ProviderMode) {
    'credential-manager' {
        Add-Check -Name 'Credential target SIGNING_KEY present' -Passed $targetSet.Contains($targets[0]) -Detail $targets[0]
        Add-Check -Name 'Credential target SIGNING_KEY_ID present' -Passed $targetSet.Contains($targets[1]) -Detail $targets[1]
        if ($RequirePrevious) {
            Add-Check -Name 'Credential target PREVIOUS_SIGNING_KEY present' -Passed $targetSet.Contains($targets[2]) -Detail $targets[2]
            Add-Check -Name 'Credential target PREVIOUS_SIGNING_KEY_ID present' -Passed $targetSet.Contains($targets[3]) -Detail $targets[3]
        }
    }

    'dpapi-file' {
        $exists = Test-Path -LiteralPath $configuredSecretFile
        Add-Check -Name 'DPAPI secret file exists' -Passed $exists -Detail $configuredSecretFile
    }

    'auto' {
        $hasCredPrimary = $targetSet.Contains($targets[0]) -and $targetSet.Contains($targets[1])
        $hasDpapiFile = Test-Path -LiteralPath $configuredSecretFile
        Add-Check -Name 'Auto provider has OS-backed source' -Passed ($hasCredPrimary -or $hasDpapiFile) -Detail "Credential targets or DPAPI file must exist ($configuredSecretFile)."

        if ($RequirePrevious) {
            $hasCredPrevious = $targetSet.Contains($targets[2]) -and $targetSet.Contains($targets[3])
            Add-Check -Name 'Auto provider has previous-key material' -Passed ($hasCredPrevious -or $hasDpapiFile) -Detail 'Previous key material required for overlap verification checks.'
        }
    }

    'env' {
        $hasEnvPrimary = -not [string]::IsNullOrWhiteSpace((Get-EnvOrDefault -Name 'SENTINEL_POLICY_SIGNING_KEY'))
        $hasEnvPrimaryId = -not [string]::IsNullOrWhiteSpace((Get-EnvOrDefault -Name 'SENTINEL_POLICY_SIGNING_KEY_ID'))

        Add-Check -Name 'Env SIGNING_KEY present' -Passed $hasEnvPrimary -Detail 'SENTINEL_POLICY_SIGNING_KEY'
        Add-Check -Name 'Env SIGNING_KEY_ID present' -Passed $hasEnvPrimaryId -Detail 'SENTINEL_POLICY_SIGNING_KEY_ID'
    }
}

if ($OutputJson) {
    $result | ConvertTo-Json -Depth 4
} else {
    Write-Host "Provider mode: $($result.providerMode)"
    Write-Host "Environment: $($result.environment)"
    foreach ($check in $result.checks) {
        $status = if ($check.passed) { '[PASS]' } else { '[FAIL]' }
        Write-Host "$status $($check.name) - $($check.detail)"
    }
    if ($result.ok) {
        Write-Host 'Readiness checks passed.'
    } else {
        Write-Host 'Readiness checks failed.'
    }
}

if (-not $result.ok) {
    exit 1
}
