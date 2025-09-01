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

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

    virtual FString GenerateGlobalVariables(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem);

}; 