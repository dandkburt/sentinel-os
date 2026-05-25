# Sentinel OS Build & Test Quick Start

## Prerequisites

### Windows
- **CMake**: 3.28 or later (https://cmake.org/download/)
- **C++ Compiler**: MSVC (Visual Studio 2022) or clang-cl
- **Git**: For version control

### Linux
- **CMake**: 3.28 or later
- **C++ Compiler**: GCC 11+ or Clang 14+
- **Build Tools**: `build-essential` or equivalent
- **Git**: For version control

### macOS
- **Xcode**: Latest version (includes clang)
- **CMake**: 3.28 or later
- **Homebrew** (recommended): `brew install cmake`

## Quick Start

### 1. Clone/Enter Repository
```bash
cd c:\Users\dandk\Desktop\sentinel-os
# or
cd ~/sentinel-os
```

### 2. Configure Build
```bash
# Create build directory and run CMake
cmake -B build -DSENTINEL_BUILD_TESTS=ON

# Optional: Specify generator (e.g., for Visual Studio)
# cmake -B build -G "Visual Studio 17 2022" -DSENTINEL_BUILD_TESTS=ON

# Optional: Release build
# cmake -B build -DCMAKE_BUILD_TYPE=Release -DSENTINEL_BUILD_TESTS=ON
```

### 3. Build All Targets
```bash
cmake --build build

# For Release build:
# cmake --build build --config Release

# Build only unit tests:
# cmake --build build --target sentinel_unit_tests

# Build only integration tests:
# cmake --build build --target sentinel_integration_tests
```

### 4. Run Tests
```bash
# Run all tests with verbose output
ctest --output-on-failure -C Debug

# Run only unit tests
ctest --output-on-failure -C Debug -R "UnitTests"

# Run only integration tests
ctest --output-on-failure -C Debug -R "IntegrationTests"

# Run with more detail
ctest --output-on-failure --verbose
```

## Policy Reload Verification (Quick Block)

Use the unit test binary when you need direct policy reload behavior validation:

```bash
# Windows Debug output path
./build/tests/unit/Debug/sentinel_unit_tests.exe --gtest_filter="PolicyCapabilityEngineReal.SecretProviderRotationCutoverSupportsRollback:PolicyCapabilityEngineReal.ReloadFailureTelemetryIsNonSensitive:PolicyCapabilityEngineReal.ReloadRotationTracksZeroizationEvents"
```

Expected outcomes:
- Success/apply path: `ReloadRotationTracksZeroizationEvents` passes and telemetry ends with `reload_state == applied`.
- Rollback path: `SecretProviderRotationCutoverSupportsRollback` passes and telemetry shows rollback activity (`rollback_events > 0`, `rollback_applies > 0`, `reload_state == rolled_back`).
- Fail-safe retain path: `ReloadFailureTelemetryIsNonSensitive` passes and telemetry shows `reload_state == failed` with non-sensitive `last_error` (`provider-unavailable-env-fallback-disabled`).

## Build Outputs

After successful build, you'll have:

- **Core Libraries** (in `build/src/`):
  - `core_runtime.lib` (Windows) or `libcore_runtime.a` (Unix)
  - `policy_service.lib` or `libpolicy_service.a`
  - `namespace_service.lib` or `libnamespace_service.a`
  - `shell_desktop.lib` or `libshell_desktop.a`
  - `sentinel_runtime_contract.dll` (Windows) or `libsentinel_runtime_contract.so` (Unix-like)

- **Test Executables** (in `build/tests/`):
  - `sentinel_unit_tests` - All unit tests
  - `sentinel_integration_tests` - All integration tests
  - `sentinel_contract_tests` - Direct C ABI contract tests

- **Managed Output**:
  - `src/managed/SentinelOS.Orchestration/bin/<Configuration>/net8.0/SentinelOS.Orchestration.dll`
  - `src/managed/SentinelOS.Orchestration/bin/<Configuration>/net8.0/sentinel_runtime_contract.dll` (staged for execution)

### Windows Debug Output Reference
- Native contract DLL:
  - `build/src/core/runtime/Debug/sentinel_runtime_contract.dll`
- Unit tests:
  - `build/tests/unit/Debug/sentinel_unit_tests.exe`
- Integration tests:
  - `build/tests/integration/Debug/sentinel_integration_tests.exe`
- Contract tests:
  - `build/tests/contract/Debug/sentinel_contract_tests.exe`
- Managed orchestrator:
  - `src/managed/SentinelOS.Orchestration/bin/Debug/net8.0/SentinelOS.Orchestration.dll`
- Staged native contract DLL for managed execution:
  - `src/managed/SentinelOS.Orchestration/bin/Debug/net8.0/sentinel_runtime_contract.dll`

## Development Workflow

### Adding a New Test
1. Create test file in `tests/unit/` or `tests/integration/`
2. Follow Google Test format: `TEST(Suite, TestName) { ... }`
3. Rebuild: `cmake --build build`
4. Run: `ctest --output-on-failure`

### Adding a New Module
1. Create subdirectory under `src/` with:
   - `module_interface.h` - Interface classes
   - `module_impl.cpp` - Implementation
   - `CMakeLists.txt` - Build configuration
2. Add subdirectory to main `CMakeLists.txt`
3. Update parent module's CMakeLists.txt to link if needed
4. Rebuild and test

### Debugging
**Visual Studio** (Windows):
```bash
# Open solution in Visual Studio
start build\SentinelOS.sln
```

**GDB/LLDB** (Linux/macOS):
```bash
# Build with debug symbols (default)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Run tests under debugger
gdb ./build/tests/unit/sentinel_unit_tests
lldb ./build/tests/unit/sentinel_unit_tests
```

## Common Issues

### CMake Not Found
**Windows**: Add CMake to PATH or use full path: `"C:\Program Files\CMake\bin\cmake"`
**Linux/macOS**: Install via package manager: `apt install cmake` or `brew install cmake`

### Compiler Not Found
**Windows**: Install Visual Studio 2022 with C++ workload
**Linux**: `sudo apt install build-essential`
**macOS**: `xcode-select --install`

### Tests Don't Compile
- Ensure CMake version >= 3.28: `cmake --version`
- Check C++20 support: `g++ -std=c++20 --version`
- Rebuild from scratch: `rm -rf build && cmake -B build -DSENTINEL_BUILD_TESTS=ON`

### Google Test Download Fails
- Ensure internet connection
- Check firewall settings
- Manual: Download from https://github.com/google/googletest/releases/tag/v1.14.0

## Environment Notes

- **No Python**: Pure C++20 + CMake (no Python build scripts)
- **C++20 Enforced**: All targets use `-std=c++20`
- **Warnings as Errors**: Compilation enforces clean code (`/W4` on MSVC, `-Wall -Wextra -pedantic` on others)
- **Sanitizers**: For leak detection, add `-DSANITIZER=address` (GCC/Clang only)

## IDE Integration

### Visual Studio
```bash
cmake -B build -G "Visual Studio 17 2022"
start build\SentinelOS.sln
```

### VS Code
1. Install "CMake Tools" extension
2. Select kit: "Visual Studio 17 2022" (or your compiler)
3. Configure: "CMake: Configure" from command palette
4. Build: "CMake: Build" from command palette
5. Test: "CMake: Run Tests" or use CTest sidebar

### CLion
- Open repository root
- CMake configuration auto-detected
- Run tests from Test Explorer

## Next Steps

1. **Review Architecture**: See [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)
2. **Read ADRs**: Check `docs/adr/` for design decisions
3. **Run Tests**: Verify build with `ctest`
4. **Implement TODO Items**: See code comments starting with `// TODO:`
5. **Contribute**: See [CODEOWNERS](CODEOWNERS) for module ownership

## Support

- Architecture: See [docs/adr/](docs/adr/)
- API Reference: See headers in `src/`
- Test Examples: See `tests/`
- Build System: See `CMakeLists.txt` files
