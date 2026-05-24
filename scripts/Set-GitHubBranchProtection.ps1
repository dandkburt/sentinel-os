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

    [ValidateSet('auto', 'gh', 'token')]
    [string]$AuthMode = 'auto',

    [string]$GitHubToken,

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

function Get-EffectiveToken {
    param(
        [string]$ExplicitToken
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitToken)) {
        return $ExplicitToken
    }

    $envToken = [Environment]::GetEnvironmentVariable('GITHUB_TOKEN')
    if (-not [string]::IsNullOrWhiteSpace($envToken)) {
        return $envToken
    }

    $ghToken = [Environment]::GetEnvironmentVariable('GH_TOKEN')
    if (-not [string]::IsNullOrWhiteSpace($ghToken)) {
        return $ghToken
    }

    return ''
}

function Invoke-GitHubProtectionPut {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Owner,
        [Parameter(Mandatory = $true)]
        [string]$Repo,
        [Parameter(Mandatory = $true)]
        [string]$Branch,
        [Parameter(Mandatory = $true)]
        [string]$PayloadJson,
        [Parameter(Mandatory = $true)]
        [ValidateSet('auto', 'gh', 'token')]
        [string]$AuthMode,
        [string]$Token
    )

    $apiPath = "/repos/$Owner/$Repo/branches/$Branch/protection"

    $ghAvailable = $null -ne (Get-Command gh -ErrorAction SilentlyContinue)
    $useGh = $false

    if ($AuthMode -eq 'gh') {
        $useGh = $true
    } elseif ($AuthMode -eq 'auto' -and $ghAvailable) {
        $useGh = $true
    }

    if ($useGh) {
        Assert-GhAvailable
        Assert-GhAuth

        $tempFile = [System.IO.Path]::GetTempFileName()
        try {
            Set-Content -Path $tempFile -Value $PayloadJson -Encoding utf8
            $output = & gh api $apiPath --method PUT --input $tempFile 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "Failed to apply branch protection with gh. Output:`n$($output -join [Environment]::NewLine)"
            }
            return
        } finally {
            Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
        }
    }

    $effectiveToken = Get-EffectiveToken -ExplicitToken $Token
    if ([string]::IsNullOrWhiteSpace($effectiveToken)) {
        throw "No GitHub token available. Set -GitHubToken or GITHUB_TOKEN/GH_TOKEN, or use -AuthMode gh with authenticated gh CLI."
    }

    $headers = @{
        Authorization = "Bearer $effectiveToken"
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
    }

    $uri = "https://api.github.com/repos/$Owner/$Repo/branches/$Branch/protection"
    Invoke-RestMethod -Method Put -Uri $uri -Headers $headers -Body $PayloadJson -ContentType 'application/json' | Out-Null
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
Write-Host "Target repository: $Owner/$Repo"
Write-Host "Target branch: $Branch"
Write-Host "Required checks: $($RequiredChecks -join ', ')"
Write-Host "Required approvals: $RequiredApprovals"
Write-Host "Auth mode: $AuthMode"

if ($DryRun) {
    Write-Host 'Dry run enabled; payload that would be sent:'
    Write-Host $payloadJson
    exit 0
}

if ($PSCmdlet.ShouldProcess("${Owner}/${Repo}:${Branch}", 'Apply branch protection')) {
    Invoke-GitHubProtectionPut -Owner $Owner -Repo $Repo -Branch $Branch -PayloadJson $payloadJson -AuthMode $AuthMode -Token $GitHubToken
    Write-Host 'Branch protection applied successfully.'
}
