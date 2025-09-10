/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_MakeStruct.h"
#include "MakeStructTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UMakeStructTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:
	UMakeStructTranslatorObject()
	{
		ClassNodeForCheck = {UK2Node_MakeStruct::StaticClass()};
		NodeNamesForCheck = { };
	}

	virtual bool CanApply(UK2Node* Node) override;

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;
};
