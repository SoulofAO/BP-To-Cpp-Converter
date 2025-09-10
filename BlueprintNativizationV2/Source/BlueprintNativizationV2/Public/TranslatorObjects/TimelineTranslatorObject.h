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
#include "K2Node_Timeline.h"
#include "TimelineTranslatorObject.generated.h"

UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UTimelineTranslatorObject : public UTranslatorBPToCppObject
{
    GENERATED_BODY()
public:
    UTimelineTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_Timeline::StaticClass()};
        NodeNamesForCheck = {};
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, TSet<FString>& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual UCurveBase* GetCurveByName(UK2Node_Timeline* Timeline, FString Name);

    virtual FString GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual bool CanContinueCodeGeneration(UK2Node* InputNode, FString EntryPinName) override;

    virtual TSet<FString> GenerateHeaderIncludeInstructions(UK2Node* Node, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem);

}; 