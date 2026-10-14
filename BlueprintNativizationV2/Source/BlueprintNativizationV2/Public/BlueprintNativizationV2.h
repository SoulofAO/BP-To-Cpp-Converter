/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "Modules/ModuleManager.h"
#include "BlueprintEditorModule.h"


class SBlueprintNativizationTool;
class SDockTab;
class UBlueprint;
class FSpawnTabArgs;

class BLUEPRINTNATIVIZATIONV2_API FBlueprintNativizationV2Module : public IModuleInterface
{

public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

    TSharedPtr<SBlueprintNativizationTool> OpenTool(UBlueprint* Blueprint = nullptr, FName FunctionName = NAME_None);

private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnToolTab(const FSpawnTabArgs& Arguments);
	TWeakPtr<SBlueprintNativizationTool> Tool;
	FDelegateHandle FunctionCustomizationHandle;
	FDelegateHandle EventCustomizationHandle;
	FDelegateHandle PropertyCustomizationHandle;
	FDelegateHandle LocalPropertyCustomizationHandle;
    FDelegateHandle BackgroundIndexingShutdownRegistration;
};
