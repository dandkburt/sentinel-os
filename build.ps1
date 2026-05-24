param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $RepoRoot "build"
$ManagedProject = Join-Path $RepoRoot "src\managed\SentinelOS.Orchestration\SentinelOS.Orchestration.csproj"

Write-Host "[1/5] Configuring CMake..."
cmake -S $RepoRoot -B $BuildDir -DSENTINEL_BUILD_TESTS=ON

Write-Host "[2/5] Building native targets ($Configuration)..."
cmake --build $BuildDir --config $Configuration

if (-not $SkipTests) {
    Write-Host "[3/5] Running CTest ($Configuration)..."
    ctest --test-dir $BuildDir --output-on-failure -C $Configuration
} else {
    Write-Host "[3/5] Skipping CTest by request."
}

Write-Host "[4/5] Building managed orchestrator..."
dotnet build $ManagedProject -c $Configuration

$ContractDll = Join-Path $BuildDir "src\core\runtime\$Configuration\sentinel_runtime_contract.dll"
if (-not (Test-Path $ContractDll)) {
    throw "Expected native contract DLL not found: $ContractDll"
}

Write-Host "[5/5] Verifying managed output..."
$ManagedOutput = Join-Path $RepoRoot "src\managed\SentinelOS.Orchestration\bin\$Configuration\net8.0\SentinelOS.Orchestration.dll"
if (-not (Test-Path $ManagedOutput)) {
    throw "Expected managed output not found: $ManagedOutput"
}

$ManagedOutputDir = Split-Path -Parent $ManagedOutput
$StagedContractDll = Join-Path $ManagedOutputDir "sentinel_runtime_contract.dll"
Copy-Item $ContractDll $StagedContractDll -Force

if (-not (Test-Path $StagedContractDll)) {
    throw "Expected staged native contract DLL not found: $StagedContractDll"
}

Write-Host "Build pipeline complete."
Write-Host "Native contract DLL: $ContractDll"
Write-Host "Staged contract DLL: $StagedContractDll"
Write-Host "Managed orchestrator: $ManagedOutput"
