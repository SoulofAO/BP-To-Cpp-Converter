/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright 2025 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_Switch.h"
#include "SwitchTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USwitchTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
    USwitchTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_Switch::StaticClass()};
        NodeNamesForCheck = {};
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName)
    {
        return false;
    }
};
