/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_DynamicCast.h"
#include "DynamicCastTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UDynamicCastTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

    UDynamicCastTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_DynamicCast::StaticClass()};
        NodeNamesForCheck = {};
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }


};
