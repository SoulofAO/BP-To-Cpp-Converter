/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MultiGate.h"
#include "MultiGateTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMultiGateTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:
    UMultiGateTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_MultiGate::StaticClass() };
    }

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
        TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }
};
