/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_EnhancedInputAction.h"
#include "EnhancedInputTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UEnhancedInputTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:
	UEnhancedInputTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_EnhancedInputAction::StaticClass() };
		NodeNamesForCheck = { };
	}


	virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual FString GenerateSetupPlayerInputComponentFunction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem)  override;

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual bool CanContinueGenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual TSet<FString> GenerateCSPrivateDependencyModuleNames(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
