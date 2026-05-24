namespace SentinelOS.Orchestration.Contracts;

public interface INativeRuntimeContract
{
    int InitializeRuntime();
    int ShutdownRuntime();
    bool IsRuntimeReady();
    string GetRuntimeVersion();
}
