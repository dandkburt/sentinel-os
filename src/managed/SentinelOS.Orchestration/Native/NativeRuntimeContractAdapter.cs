using System.Runtime.InteropServices;
using System.Text;
using System.Reflection;
using System.IO;
using SentinelOS.Orchestration.Contracts;

namespace SentinelOS.Orchestration.Native;

public sealed class NativeRuntimeContractAdapter : INativeRuntimeContract
{
    private const string NativeLibraryName = "sentinel_runtime_contract";

    static NativeRuntimeContractAdapter()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeRuntimeContractAdapter).Assembly, ResolveNativeLibrary);
    }

    public int InitializeRuntime()
    {
        Console.WriteLine($"Loading {NativeLibraryName}.dll from {AppContext.BaseDirectory}");
        Console.WriteLine("Calling sentinel_runtime_initialize()");
        return sentinel_runtime_initialize();
    }

    public int ShutdownRuntime()
    {
        Console.WriteLine("Calling sentinel_runtime_shutdown()");
        return sentinel_runtime_shutdown();
    }

    public bool IsRuntimeReady()
    {
        return sentinel_runtime_is_ready() == 1;
    }

    public string GetRuntimeVersion()
    {
        Console.WriteLine("Calling sentinel_runtime_get_version()");
        var buffer = new StringBuilder(64);
        var required = sentinel_runtime_get_version(buffer, (UIntPtr)buffer.Capacity);
        if (required < 0) {
            return "unknown";
        }
        return buffer.ToString();
    }

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int sentinel_runtime_initialize();

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int sentinel_runtime_shutdown();

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl)]
    private static extern int sentinel_runtime_is_ready();

    [DllImport(NativeLibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    private static extern int sentinel_runtime_get_version(StringBuilder buffer, UIntPtr bufferSize);

    private static IntPtr ResolveNativeLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, NativeLibraryName, StringComparison.Ordinal))
        {
            return IntPtr.Zero;
        }

        var baseDirectory = AppContext.BaseDirectory;
        var libraryPath = Path.Combine(baseDirectory, "sentinel_runtime_contract.dll");
        if (File.Exists(libraryPath))
        {
            Console.WriteLine($"Loaded native contract: {libraryPath}");
            return NativeLibrary.Load(libraryPath, assembly, searchPath);
        }

        return IntPtr.Zero;
    }
}
