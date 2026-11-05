/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once
#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_GetNumEnumEntries.h"
#include "EnumLoopTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UEnumLoopTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UEnumLoopTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_ForEachElementInEnum::StaticClass(), UK2Node_GetNumEnumEntries::StaticClass()};
    }
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* Node, FString EntryPinName) override { return false; }
    virtual bool ShouldPrepareInputs(UK2Node* Node, const FString& EntryPinName) const override { return false; }
};
