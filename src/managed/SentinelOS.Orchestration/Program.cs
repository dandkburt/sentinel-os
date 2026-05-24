using SentinelOS.Orchestration.Contracts;
using SentinelOS.Orchestration.Models;
using SentinelOS.Orchestration.Native;
using SentinelOS.Orchestration.Services;

Console.WriteLine($"AppContext.BaseDirectory: {AppContext.BaseDirectory}");

var orchestrator = new SentinelBootstrapOrchestrator(
	new InMemoryOrchestrationState(),
	new NativeRuntimeContractAdapter());
var result = orchestrator.Start();

Console.WriteLine("Managed orchestrator startup complete");
Console.WriteLine($"Runtime version: {orchestrator.NativeRuntimeVersion}");

Console.WriteLine($"Sentinel OS orchestration status: {result.Status}");
Console.WriteLine(result.Message);
