/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MacroInstance.h"
#include "ForEachLoopTranslatorObject.generated.h"

/**
 * 
 */


UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UForEachLoopTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

    UForEachLoopTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_MacroInstance::StaticClass()};
        NodeNamesForCheck = TArray<FString>({ TEXT("ForEachLoop"), TEXT("ForEachLoopWithBreak") });
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanGenerateOutPinVariable(UEdGraphPin* Pin, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }

    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName) override
    {
        if (EnterPinName == "Break")
        {
            return false;
        }

        return true;
    }

private:
    UK2Node* FindLastForLoopInStackNode(const TArray<FVisitedNodeStack>& VisitedNodes);
};
