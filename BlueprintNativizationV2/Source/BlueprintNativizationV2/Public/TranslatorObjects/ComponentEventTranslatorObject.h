/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

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
    static FMulticastDelegateProperty* ResolveComponentDelegate(UK2Node_ComponentBoundEvent* Event, FObjectPropertyBase*& ComponentProperty, FString& Error);

    UComponentEventTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_ComponentBoundEvent::StaticClass() };
        NodeNamesForCheck = { };
    }

	virtual FString GenerateConstruction(UK2Node* InputNode, FString Prefix, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
