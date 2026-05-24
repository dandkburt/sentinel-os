namespace SentinelOS.Orchestration.Models;

public sealed record BootstrapResult(string Status, string Message)
{
    public static BootstrapResult Success(string message) => new("Success", message);
    public static BootstrapResult Failure(string message) => new("Failure", message);
}
