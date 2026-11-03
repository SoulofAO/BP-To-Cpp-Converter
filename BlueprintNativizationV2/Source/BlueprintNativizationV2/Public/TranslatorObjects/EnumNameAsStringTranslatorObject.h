/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "EnumNameAsStringTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UEnumNameAsStringTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()

public:
	UEnumNameAsStringTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_GetEnumeratorName::StaticClass() };
		NodeNamesForCheck = { };
	}

	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
