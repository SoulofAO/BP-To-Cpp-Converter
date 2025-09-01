/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "Editor\UMGEditor\Private\Nodes\K2Node_CreateWidget.h"
#include "CreateWidgetTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCreateWidgetTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

    UCreateWidgetTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_CreateWidget::StaticClass() };
        NodeNamesForCheck = { };
    }

	virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
