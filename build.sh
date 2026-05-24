#!/usr/bin/env bash
set -euo pipefail

CONFIGURATION="${1:-Debug}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
MANAGED_PROJECT="${REPO_ROOT}/src/managed/SentinelOS.Orchestration/SentinelOS.Orchestration.csproj"

echo "[1/5] Configuring CMake..."
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DSENTINEL_BUILD_TESTS=ON

echo "[2/5] Building native targets (${CONFIGURATION})..."
cmake --build "${BUILD_DIR}" --config "${CONFIGURATION}"

echo "[3/5] Running CTest (${CONFIGURATION})..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure -C "${CONFIGURATION}"

echo "[4/5] Building managed orchestrator..."
dotnet build "${MANAGED_PROJECT}" -c "${CONFIGURATION}"

CONTRACT_DLL="${BUILD_DIR}/src/core/runtime/${CONFIGURATION}/sentinel_runtime_contract.dll"
if [[ ! -f "${CONTRACT_DLL}" ]]; then
  echo "Expected native contract DLL not found: ${CONTRACT_DLL}" >&2
  exit 1
fi

MANAGED_OUTPUT="${REPO_ROOT}/src/managed/SentinelOS.Orchestration/bin/${CONFIGURATION}/net8.0/SentinelOS.Orchestration.dll"
if [[ ! -f "${MANAGED_OUTPUT}" ]]; then
  echo "Expected managed output not found: ${MANAGED_OUTPUT}" >&2
  exit 1
fi

MANAGED_OUTPUT_DIR="$(dirname "${MANAGED_OUTPUT}")"
STAGED_CONTRACT_DLL="${MANAGED_OUTPUT_DIR}/sentinel_runtime_contract.dll"
cp "${CONTRACT_DLL}" "${STAGED_CONTRACT_DLL}"

if [[ ! -f "${STAGED_CONTRACT_DLL}" ]]; then
  echo "Expected staged native contract DLL not found: ${STAGED_CONTRACT_DLL}" >&2
  exit 1
fi

echo "[5/5] Outputs verified"
echo "Native contract DLL: ${CONTRACT_DLL}"
echo "Staged contract DLL: ${STAGED_CONTRACT_DLL}"
echo "Managed orchestrator: ${MANAGED_OUTPUT}"
