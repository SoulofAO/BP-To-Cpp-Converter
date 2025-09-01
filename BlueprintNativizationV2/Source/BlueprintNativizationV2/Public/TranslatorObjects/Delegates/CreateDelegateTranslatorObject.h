/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CreateDelegate.h"
#include "CreateDelegateTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCreateDelegateTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:
    UCreateDelegateTranslatorObject()
    {
        ClassNodeForCheck = {UK2Node_CreateDelegate::StaticClass()};
        NodeNamesForCheck = { };
    }

    virtual FString GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
