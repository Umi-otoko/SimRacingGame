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
            "DeveloperSettings",
            // Síntesis de audio procedural (URacingAudio : USynthComponent)
            // En UE5.7 USynthComponent vive en AudioMixer (no en Synthesis)
            "AudioMixer",
        });

        // OpenSSL via vcpkg — solo se enlaza si está instalado; si no, HMAC queda pendiente
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string VcpkgLib = "C:/vcpkg/installed/x64-windows/lib/";
            string VcpkgBin = "C:/vcpkg/installed/x64-windows/bin/";
            if (System.IO.File.Exists(VcpkgLib + "libssl.lib"))
            {
                PublicAdditionalLibraries.AddRange(new string[]
                {
                    VcpkgLib + "libssl.lib",
                    VcpkgLib + "libcrypto.lib"
                });
                RuntimeDependencies.Add(VcpkgBin + "libssl-3-x64.dll");
                RuntimeDependencies.Add(VcpkgBin + "libcrypto-3-x64.dll");
            }
        }

        // Raiz del modulo + subdirectorios — necesario para includes cruzados
        PrivateIncludePaths.AddRange(new string[] {
            ModuleDirectory,
            System.IO.Path.Combine(ModuleDirectory, "GameMode"),
            System.IO.Path.Combine(ModuleDirectory, "Network"),
            System.IO.Path.Combine(ModuleDirectory, "Vehicle"),
            System.IO.Path.Combine(ModuleDirectory, "Setup"),
            System.IO.Path.Combine(ModuleDirectory, "UI"),
            System.IO.Path.Combine(ModuleDirectory, "Audio"),
            System.IO.Path.Combine(ModuleDirectory, "Track"),
        });

        // Headers Core C++ standalone (TireModel, Setup, Networking)
        PublicIncludePaths.Add("$(ProjectDir)/src/Core");
        PublicIncludePaths.Add("$(ProjectDir)/src");
    }
}
