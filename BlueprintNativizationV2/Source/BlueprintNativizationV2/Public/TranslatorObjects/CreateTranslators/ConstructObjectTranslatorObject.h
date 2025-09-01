/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "ConstructObjectTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UConstructObjectTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
    UConstructObjectTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_ConstructObjectFromClass::StaticClass() };
        NodeNamesForCheck = { };
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
