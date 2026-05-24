namespace SentinelOS.Orchestration.Contracts;

public interface IOrchestrationState
{
    bool TrySet(string key, string value);
    bool TryGet(string key, out string value);
}
