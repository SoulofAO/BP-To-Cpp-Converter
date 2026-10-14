/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BlueprintNativizationData.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_VariableGet.h"
#include "K2Node_StructMemberGet.h"
#include "VariableGetTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UVariableGetTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

    UVariableGetTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_VariableGet::StaticClass(), UK2Node_StructMemberGet::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual bool CanApply(UK2Node* Node) override;

    virtual FGenerateResultStruct  GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }

    virtual bool CanCreateCircleWithPin(UK2Node* Node, FString EnterPinName) override
    {
        return true;
    }
};

