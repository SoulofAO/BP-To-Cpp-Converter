using UnrealBuildTool;
using System.IO;

public class BlueprintSourceAnalysis : ModuleRules
{
    public BlueprintSourceAnalysis(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        OptimizeCode = CodeOptimization.Always;
        bEnableExceptions = true;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "UnrealEd", "DesktopPlatform", "Slate", "SlateCore", "Projects" });
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../../Libraries/CppSourceParser/Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "../../Libraries/NativeApiCache/Public"));
    }
}
