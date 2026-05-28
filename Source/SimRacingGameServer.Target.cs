using UnrealBuildTool;
using System.Collections.Generic;

public class SimRacingGameServerTarget : TargetRules
{
    public SimRacingGameServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        bOverrideBuildEnvironment = true;

        ExtraModuleNames.Add("SimRacingGame");

        // Servidor dedicado: sin render, sin audio
        bUseUnityBuild = true;
    }
}
