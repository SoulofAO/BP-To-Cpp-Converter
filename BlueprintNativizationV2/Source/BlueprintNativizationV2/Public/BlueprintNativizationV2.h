/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */


#pragma once

#include "Modules/ModuleManager.h"
#include "BlueprintEditorModule.h"
#include "EditorStyleSet.h"


class FBlueprintNativizationV2Module : public IModuleInterface
{

public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FDelegateHandle FunctionCustomizationHandle;
	FDelegateHandle EventCustomizationHandle;
	FDelegateHandle PropertyCustomizationHandle;
	FDelegateHandle LocalPropertyCustomizationHandle;
};
