/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

using UnrealBuildTool;
using System.IO;

public class BlueprintNativizationV2 : ModuleRules
{
	public BlueprintNativizationV2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        bool bHasSemanticSearchApi = File.Exists(Path.Combine(EngineDirectory,
            "Plugins/Experimental/SemanticSearch/Source/SemanticSearch/Public/ISemanticSearchModule.h"));
        if (bHasSemanticSearchApi)
        {
            PrivateIncludePathModuleNames.Add("SemanticSearch");
            PrivateDefinitions.Add("BPTOCPP_WITH_SEMANTIC_SEARCH=1");
        }
        else
        {
            PrivateDefinitions.Add("BPTOCPP_WITH_SEMANTIC_SEARCH=0");
        }
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
                "BlueprintSourceAnalysis",
				"Engine",
				"Slate",
				"SlateCore",
                "ApplicationCore",
				"AssetRegistry",
				"ContentBrowser",
                "ContentBrowserData",
				"Settings",
				"BlueprintGraph",
                "KismetCompiler",
                "Kismet",
				"UnrealEd",
                "GameProjectGeneration",
				"ToolMenus",
				"EditorStyle",
				"DeveloperSettings",
				"HotReload",
				"UMGEditor",
				"Slate",
				"SlateCore",
				"UMG",
				"InputCore",
                "GameplayTags",
                "Niagara",
                "AIModule",
				"Json",
				"DesktopPlatform",
				"BlueprintEditorLibrary",
				"InputBlueprintNodes",
                "EnhancedInput",
				"ControlRig",
				"PropertyEditor",
                "Blutility",
                "AIGraph"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
