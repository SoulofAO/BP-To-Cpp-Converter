/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

using UnrealBuildTool;

public class BlueprintSourceAnalysis : ModuleRules
{
    public BlueprintSourceAnalysis(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        OptimizeCode = CodeOptimization.Always;
        bEnableExceptions = true;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "UnrealEd", "DesktopPlatform", "Slate", "SlateCore", "Projects", "CppSourceParser", "NativeApiCache" });
    }
}
