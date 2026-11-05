/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MacroInstance.h"
#include "ForLoopTranslatorObject.generated.h"

/**
 * 
 */

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UForLoopTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()

public:

public:

    UForLoopTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_MacroInstance::StaticClass()};
        NodeNamesForCheck = TArray<FString>({ TEXT("ForLoop"), TEXT("ForLoopWithBreak") });
    }


    virtual bool ShouldPrepareInputs(UK2Node* Node, const FString& EntryPinName) const override
    {
        return EntryPinName != TEXT("Break");
    }

    virtual bool CanApply(UK2Node* Node) override;

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateLocalVariables(UK2Node* InputNode, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    FString GenerateLoopState(UK2Node* Node, UNativizationV2Subsystem* Subsystem, bool bMember);
    FString GenerateBreak(UK2Node* Node, UNativizationV2Subsystem* Subsystem);

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


};
