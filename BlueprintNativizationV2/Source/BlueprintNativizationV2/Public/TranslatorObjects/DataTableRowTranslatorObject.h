/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_GetDataTableRow.h"
#include "DataTableRowTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UDataTableRowTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UDataTableRowTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_GetDataTableRow::StaticClass()};
    }
    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* Subsystem) override;
    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual FString GenerateGlobalVariables(UK2Node* Node, FString Prefix, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateLocalVariables(UK2Node* Node, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* Subsystem) override;
    virtual TSet<FString> GenerateCppIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* Subsystem) override;
    virtual bool CanContinueCodeGeneration(UK2Node* Node, FString EntryPinName) override { return false; }
};
