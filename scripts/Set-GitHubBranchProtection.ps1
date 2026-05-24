[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [string]$Owner,

    [Parameter(Mandatory = $true)]
    [string]$Repo,

    [string]$Branch = 'main',

    [string[]]$RequiredChecks = @(
        'build',
        'startup-smoke',
        'policy-readiness-script-smoke'
    ),

    [int]$RequiredApprovals = 1,

    [switch]$EnforceAdmins,
    [switch]$RequireConversationResolution,
    [switch]$RequireLastPushApproval,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-GhAvailable {
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if ($null -eq $gh) {
        throw 'GitHub CLI (gh) is required. Install from https://cli.github.com/ and authenticate before running this script.'
    }
}

function Assert-GhAuth {
    $output = & gh auth status 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "gh auth status failed. Authenticate with 'gh auth login'. Details:`n$($output -join [Environment]::NewLine)"
    }
}

if ($RequiredChecks.Count -eq 0) {
    throw 'At least one required status check must be supplied via -RequiredChecks.'
}

$payloadObject = [ordered]@{
    required_status_checks = [ordered]@{
        strict = $true
        contexts = $RequiredChecks
    }
    enforce_admins = [bool]$EnforceAdmins
    required_pull_request_reviews = [ordered]@{
        dismiss_stale_reviews = $false
        require_code_owner_reviews = $false
        required_approving_review_count = $RequiredApprovals
        require_last_push_approval = [bool]$RequireLastPushApproval
    }
    restrictions = $null
    required_linear_history = $false
    allow_force_pushes = $false
    allow_deletions = $false
    block_creations = $false
    required_conversation_resolution = [bool]$RequireConversationResolution
    lock_branch = $false
    allow_fork_syncing = $false
}

$payloadJson = $payloadObject | ConvertTo-Json -Depth 8
$apiPath = "/repos/$Owner/$Repo/branches/$Branch/protection"

Write-Host "Target repository: $Owner/$Repo"
Write-Host "Target branch: $Branch"
Write-Host "Required checks: $($RequiredChecks -join ', ')"
Write-Host "Required approvals: $RequiredApprovals"

if ($DryRun) {
    Write-Host 'Dry run enabled; payload that would be sent:'
    Write-Host $payloadJson
    exit 0
}

Assert-GhAvailable
Assert-GhAuth

if ($PSCmdlet.ShouldProcess("${Owner}/${Repo}:${Branch}", 'Apply branch protection')) {
    $tempFile = [System.IO.Path]::GetTempFileName()
    try {
        Set-Content -Path $tempFile -Value $payloadJson -Encoding utf8
        $output = & gh api $apiPath --method PUT --input $tempFile 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply branch protection. gh output:`n$($output -join [Environment]::NewLine)"
        }

        Write-Host 'Branch protection applied successfully.'
    } finally {
        Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
    }
}
