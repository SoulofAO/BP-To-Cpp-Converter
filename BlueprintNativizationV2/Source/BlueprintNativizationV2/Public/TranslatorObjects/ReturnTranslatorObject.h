/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_FunctionResult.h"
#include "ReturnTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UReturnTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:

    UReturnTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_FunctionResult::StaticClass() };
        NodeNamesForCheck = { };
    }

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem);
};
