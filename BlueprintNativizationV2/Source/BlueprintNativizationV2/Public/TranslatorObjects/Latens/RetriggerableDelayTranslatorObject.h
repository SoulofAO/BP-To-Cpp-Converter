/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CallFunction.h"


#include "RetriggerableDelayTranslatorObject.generated.h"


UCLASS()
class BLUEPRINTNATIVIZATIONV2_API URetriggarableDelayTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

    URetriggarableDelayTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { "RetriggerableDelay" };
    }

    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem);

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanGenerateLocalVariables(UK2Node* InputNode, TArray<FGenerateFunctionStruct> EntryNodes)
    {
        return true;
    }

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
    {
        return false;
    }

    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName)
    {
        return true;
    }

};
