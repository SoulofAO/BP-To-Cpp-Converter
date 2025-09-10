/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_GetSubsystem.h"
#include "GetSubsystemTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UGetSubsystemTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	UGetSubsystemTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_GetSubsystem::StaticClass() };
		NodeNamesForCheck = { };
	}

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;
	virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem)  override;
	TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem);
};
