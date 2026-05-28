using UnrealBuildTool;
using System.Collections.Generic;

public class SimRacingGameTarget : TargetRules
{
    public SimRacingGameTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        bOverrideBuildEnvironment = true;

        ExtraModuleNames.Add("SimRacingGame");

        // Optimizaciones para el cliente
        bUseUnityBuild = true;
        bUsePCHFiles = true;
    }
}
