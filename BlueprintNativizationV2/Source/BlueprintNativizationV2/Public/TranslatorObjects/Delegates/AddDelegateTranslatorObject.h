/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_AddDelegate.h"
#include "AddDelegateTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UAddDelegateTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:	
    UAddDelegateTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_AddDelegate::StaticClass()};
        NodeNamesForCheck = { };
    }

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return true;
    }

};
