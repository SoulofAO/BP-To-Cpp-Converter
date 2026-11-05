/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_SetFieldsInStruct.h"
#include "SetMemberStructTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USetMemberStructTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	USetMemberStructTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_SetFieldsInStruct::StaticClass(), UK2Node_StructMemberSet::StaticClass()};
		NodeNamesForCheck = { };
	}

    virtual bool CanApply(UK2Node* Node) override;

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    bool HasSharedInput(UK2Node* Node, UNativizationV2Subsystem* Subsystem);
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
};
