/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CallParentFunction.h"
#include "CallParentTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCallParentTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

	UCallParentTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_CallParentFunction::StaticClass() };
		NodeNamesForCheck = { };
	}

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
