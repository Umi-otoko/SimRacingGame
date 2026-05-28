using UnrealBuildTool;

public class SimRacingGame : ModuleRules
{
    public SimRacingGame(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            // Fisica de vehiculos Chaos
            "ChaosVehicles",
            "PhysicsCore",
            "Chaos",
            "ChaosSolverEngine",
            // Networking / Online
            "OnlineSubsystem",
            "OnlineSubsystemUtils",
            "NetCore",
            // UI
            "UMG",
            "Slate",
            "SlateCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "OnlineSubsystemSteam",
            "Json",
            "JsonUtilities",
            "HTTP",
            "DeveloperSettings"
        });

        // OpenSSL via vcpkg — rutas absolutas para UBT
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.AddRange(new string[]
            {
                "C:/vcpkg/installed/x64-windows/lib/libssl.lib",
                "C:/vcpkg/installed/x64-windows/lib/libcrypto.lib"
            });
            RuntimeDependencies.Add("C:/vcpkg/installed/x64-windows/bin/libssl-3-x64.dll");
            RuntimeDependencies.Add("C:/vcpkg/installed/x64-windows/bin/libcrypto-3-x64.dll");
        }

        // Raiz del modulo + subdirectorios — necesario para includes cruzados
        PrivateIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            System.IO.Path.Combine(ModuleDirectory, "GameMode"),
            System.IO.Path.Combine(ModuleDirectory, "Network"),
            System.IO.Path.Combine(ModuleDirectory, "Vehicle"),
            System.IO.Path.Combine(ModuleDirectory, "Setup"),
            System.IO.Path.Combine(ModuleDirectory, "UI"),
        });

        // Headers Core C++ standalone (TireModel, Setup, Networking)
        PublicIncludePaths.Add("$(ProjectDir)/src/Core");
        PublicIncludePaths.Add("$(ProjectDir)/src");
    }
}
