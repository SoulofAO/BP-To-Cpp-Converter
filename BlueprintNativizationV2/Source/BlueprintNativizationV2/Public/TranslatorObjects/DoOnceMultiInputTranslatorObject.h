/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_DoOnceMultiInput.h"
#include "DoOnceMultiInputTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UDoOnceMultiInputTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UDoOnceMultiInputTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_DoOnceMultiInput::StaticClass() };
    }
    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes,
        TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override
    {
        return false;
    }
};
