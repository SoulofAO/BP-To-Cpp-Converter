/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "BlueprintNativizationV2.h"
#include "AutomationBlueprintFunctionLibrary.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorModeManager.h"
#include "ToolMenuSection.h"
#include "BlueprintActionMenuUtils.h"
#include "ConversionDetailsCustomizations.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"


#define LOCTEXT_NAMESPACE "FBlueprintNativizationV2Module"

void FBlueprintNativizationV2Module::StartupModule()
{
    FBlueprintEditorModule& BlueprintModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");

    FunctionCustomizationHandle = BlueprintModule.RegisterFunctionCustomization(
        UK2Node_FunctionEntry::StaticClass(),
        FOnGetFunctionCustomizationInstance::CreateStatic(&FConversionDetailsFunctionCustomization::MakeInstance));

    EventCustomizationHandle = BlueprintModule.RegisterFunctionCustomization(
            UK2Node_Event::StaticClass(),
            FOnGetFunctionCustomizationInstance::CreateStatic(&FConversionDetailsFunctionCustomization::MakeInstance));

	PropertyCustomizationHandle = BlueprintModule.RegisterVariableCustomization(
		FProperty::StaticClass(),
        FOnGetVariableCustomizationInstance::CreateStatic(&FConversionDetailsPropertyCustomization::MakeInstance));

    LocalPropertyCustomizationHandle = BlueprintModule.RegisterLocalVariableCustomization(
        FProperty::StaticClass(),
        FOnGetVariableCustomizationInstance::CreateStatic(&FConversionDetailsPropertyCustomization::MakeInstance));
}

void FBlueprintNativizationV2Module::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded("Kismet"))
    {
        FBlueprintEditorModule& BlueprintModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
        BlueprintModule.UnregisterFunctionCustomization(UK2Node_CallFunction::StaticClass(), FunctionCustomizationHandle);
        BlueprintModule.UnregisterFunctionCustomization(UK2Node_Event::StaticClass(), EventCustomizationHandle);
        BlueprintModule.UnregisterVariableCustomization(FProperty::StaticClass(), PropertyCustomizationHandle);
        BlueprintModule.UnregisterLocalVariableCustomization(FProperty::StaticClass(), LocalPropertyCustomizationHandle);
    }
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBlueprintNativizationV2Module, BlueprintNativizationV2)