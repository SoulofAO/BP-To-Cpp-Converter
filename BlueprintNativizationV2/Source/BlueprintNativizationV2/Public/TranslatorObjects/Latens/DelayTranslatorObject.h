/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "K2Node_CallFunction.h"
#include "DelayTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UDelayTranslatorObject : public UCallFunctionTranslatorObject
{
    GENERATED_BODY()

public:

    UDelayTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CallFunction::StaticClass()};
        NodeNamesForCheck = { "Delay" };
    }

    virtual bool CanApply(UK2Node* Node) override;

    static bool SupportsNativeOperation(const UFunction* Function);

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem) override;

    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;

    virtual void GenerateNewFunction(UK2Node* InputNode, TArray<UK2Node*> Path, TArray<FGenerateFunctionStruct>& EntryNodes, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;

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
