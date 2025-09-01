/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MakeSet.h"
#include "MakeSetTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMakeSetTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	UMakeSetTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_MakeSet::StaticClass()};
		NodeNamesForCheck = { };
	}

	virtual FString GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
