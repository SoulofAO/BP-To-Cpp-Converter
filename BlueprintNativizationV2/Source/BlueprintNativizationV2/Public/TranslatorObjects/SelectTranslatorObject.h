/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_Select.h"
#include "SelectTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API USelectTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

    USelectTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_Select::StaticClass() };
        NodeNamesForCheck = {};
    }

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
