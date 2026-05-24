using System.Collections.Generic;
using SentinelOS.Orchestration.Contracts;

namespace SentinelOS.Orchestration.Services;

public sealed class InMemoryOrchestrationState : IOrchestrationState
{
    private readonly Dictionary<string, string> _values = new();

    public bool TrySet(string key, string value)
    {
        _values[key] = value;
        return true;
    }

    public bool TryGet(string key, out string value)
    {
        return _values.TryGetValue(key, out value!);
    }
}
