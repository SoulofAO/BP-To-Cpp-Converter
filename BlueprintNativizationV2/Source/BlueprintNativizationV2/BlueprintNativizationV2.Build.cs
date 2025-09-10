/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

using UnrealBuildTool;

public class BlueprintNativizationV2 : ModuleRules
{
	public BlueprintNativizationV2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
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
				"Engine",
				"Slate",
				"SlateCore",
				"AssetRegistry",
				"BlueprintGraph",
				"UnrealEd",
				"ToolMenus",
				"EditorStyle",
				"DeveloperSettings",
				"HotReload",
				"UMGEditor",
				"Slate",
				"SlateCore",
				"UMG",
				"InputCore",
				"Json",
				"DesktopPlatform",
				"BlueprintEditorLibrary",
				"InputBlueprintNodes",
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
