[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidateSet('set', 'remove', 'list')]
    [string]$Action = 'set',

    [string]$TargetPrefix = 'SentinelOS/Policy',

    [string]$SigningKey,
    [string]$SigningKeyId,
    [string]$PreviousSigningKey,
    [string]$PreviousSigningKeyId,

    [switch]$IncludePrevious,
    [switch]$ClearPrevious
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-CmdKey {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $result = & cmdkey.exe @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "cmdkey failed: $($result -join [Environment]::NewLine)"
    }

    return $result
}

function Set-GenericCredential {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Target,
        [Parameter(Mandatory = $true)]
        [string]$Secret
    )

    if ([string]::IsNullOrWhiteSpace($Secret)) {
        throw "Secret for target '$Target' cannot be empty."
    }

    if ($PSCmdlet.ShouldProcess($Target, 'Write credential')) {
        Invoke-CmdKey -Arguments @("/generic:$Target", '/user:sentinel-policy', "/pass:$Secret") | Out-Null
    }
}

function Remove-GenericCredential {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Target
    )

    if ($PSCmdlet.ShouldProcess($Target, 'Delete credential')) {
        # cmdkey returns exit code 0 even for missing credentials on many systems.
        Invoke-CmdKey -Arguments @("/delete:$Target") | Out-Null
    }
}

function Get-PolicyTargets {
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

$targets = Get-PolicyTargets -Prefix $TargetPrefix

switch ($Action) {
    'set' {
        if ([string]::IsNullOrWhiteSpace($SigningKey)) {
            throw 'SigningKey is required when Action=set.'
        }
        if ([string]::IsNullOrWhiteSpace($SigningKeyId)) {
            throw 'SigningKeyId is required when Action=set.'
        }

        Set-GenericCredential -Target $targets[0] -Secret $SigningKey
        Set-GenericCredential -Target $targets[1] -Secret $SigningKeyId

        if ($IncludePrevious) {
            if ([string]::IsNullOrWhiteSpace($PreviousSigningKey)) {
                throw 'PreviousSigningKey is required when IncludePrevious is set.'
            }
            if ([string]::IsNullOrWhiteSpace($PreviousSigningKeyId)) {
                throw 'PreviousSigningKeyId is required when IncludePrevious is set.'
            }

            Set-GenericCredential -Target $targets[2] -Secret $PreviousSigningKey
            Set-GenericCredential -Target $targets[3] -Secret $PreviousSigningKeyId
        }

        if ($ClearPrevious) {
            Remove-GenericCredential -Target $targets[2]
            Remove-GenericCredential -Target $targets[3]
        }

        Write-Host 'Policy credentials updated.'
        Write-Host "Target prefix: $TargetPrefix"
        Write-Host 'Updated: SIGNING_KEY, SIGNING_KEY_ID'
        if ($IncludePrevious) {
            Write-Host 'Updated: PREVIOUS_SIGNING_KEY, PREVIOUS_SIGNING_KEY_ID'
        }
        if ($ClearPrevious) {
            Write-Host 'Removed: PREVIOUS_SIGNING_KEY, PREVIOUS_SIGNING_KEY_ID'
        }
    }

    'remove' {
        foreach ($target in $targets) {
            Remove-GenericCredential -Target $target
        }

        Write-Host 'Policy credentials removed.'
        Write-Host "Target prefix: $TargetPrefix"
    }

    'list' {
        $output = Invoke-CmdKey -Arguments @('/list')
        Write-Host "Configured targets under prefix '$TargetPrefix':"

        $matches = @()
        foreach ($line in $output) {
            if ($line -match [Regex]::Escape($TargetPrefix)) {
                $matches += $line
            }
        }

        if ($matches.Count -eq 0) {
            Write-Host '  (none found)'
        } else {
            foreach ($line in $matches) {
                Write-Host "  $line"
            }
        }
    }
}
