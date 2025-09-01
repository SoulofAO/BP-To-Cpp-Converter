/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MakeMap.h"
#include "MakeMapTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMakeMapTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:

	virtual FString GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

	UMakeMapTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_MakeMap::StaticClass()};
		NodeNamesForCheck = { };
	}
};
