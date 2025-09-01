/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_RemoveDelegate.h"
#include "RemoveDelegateTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API URemoveDelegateTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
public:
	URemoveDelegateTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_RemoveDelegate::StaticClass()};
		NodeNamesForCheck = { };
	}

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
	{
		return true;
	}
};
