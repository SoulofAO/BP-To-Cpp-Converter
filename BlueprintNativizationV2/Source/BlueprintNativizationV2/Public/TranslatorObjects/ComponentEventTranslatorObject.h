// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_ComponentBoundEvent.h"
#include "ComponentEventTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UComponentEventTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
public:

    UComponentEventTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_ComponentBoundEvent::StaticClass() };
        NodeNamesForCheck = { };
    }

	virtual FString GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
