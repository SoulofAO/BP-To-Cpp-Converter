/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "K2Node_PromotableOperator.h"
#include "PromotabaleOperatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UPromotabaleOperatorTranslatorObject : public UCallFunctionTranslatorObject
{
	GENERATED_BODY()
	
	UPromotabaleOperatorTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_PromotableOperator::StaticClass()};
		NodeNamesForCheck = { };
	}

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
