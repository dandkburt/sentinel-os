using SentinelOS.Orchestration.Models;
using SentinelOS.Orchestration.Contracts;

namespace SentinelOS.Orchestration.Services;

public sealed class SentinelBootstrapOrchestrator
{
    private readonly IOrchestrationState _state;
    private readonly INativeRuntimeContract _nativeRuntime;

    public string NativeRuntimeVersion { get; private set; } = "unknown";

    public SentinelBootstrapOrchestrator(IOrchestrationState state, INativeRuntimeContract nativeRuntime)
    {
        _state = state;
        _nativeRuntime = nativeRuntime;
    }

    public BootstrapResult Start()
    {
        var initCode = _nativeRuntime.InitializeRuntime();
        if (initCode != 0) {
            return BootstrapResult.Failure($"Native runtime initialization failed with code {initCode}.");
        }

        var runtimeReady = _nativeRuntime.IsRuntimeReady();
        _state.TrySet("runtime", runtimeReady ? "initialized" : "not_ready");
        NativeRuntimeVersion = _nativeRuntime.GetRuntimeVersion();
        _state.TrySet("runtime_version", NativeRuntimeVersion);

        // Policy and shell orchestration through managed layer is intentionally deferred
        // until dedicated native contracts are added for those services.
        _state.TrySet("policy", "deferred_contract");
        _state.TrySet("shell", "deferred_contract");

        if (!runtimeReady) {
            return BootstrapResult.Failure("Native runtime initialized but is not ready.");
        }

        return BootstrapResult.Success("Managed orchestration initialized the native runtime through explicit contract boundaries.");
    }
}
