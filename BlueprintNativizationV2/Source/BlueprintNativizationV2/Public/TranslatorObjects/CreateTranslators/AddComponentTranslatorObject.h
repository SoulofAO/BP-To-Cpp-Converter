/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_AddComponent.h"
#include "K2Node_AddComponentByClass.h"
#include "AddComponentTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UAddComponentTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
    UAddComponentTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_AddComponent::StaticClass(), UK2Node_AddComponentByClass::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual bool ShouldExpandNode(UK2Node* Node) const override
    {
        if (const UK2Node_AddComponent* Component = Cast<UK2Node_AddComponent>(Node))
        {
            return Component->bHasExposedVariable;
        }
        return Node->IsA<UK2Node_AddComponentByClass>();
    }

    virtual FString GenerateCodeFromNode(UK2Node* Node, FString EntryPinName, TArray<FVisitedNodeStack> VisitedNodes, TArray<UK2Node*> MacroStack, FGenerationPreparations& Preparations, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
