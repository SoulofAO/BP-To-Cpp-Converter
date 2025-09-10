/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_GetArrayItem.h"
#include "GetArrayItemTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UGetArrayItemTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

	UGetArrayItemTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_GetArrayItem::StaticClass() };
		NodeNamesForCheck = { };
	}

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
