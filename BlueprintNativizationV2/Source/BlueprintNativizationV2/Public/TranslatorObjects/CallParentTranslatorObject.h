/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "K2Node_CallParentFunction.h"
#include "CallParentTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCallParentTranslatorObject : public UCallFunctionTranslatorObject
{
	GENERATED_BODY()

	UCallParentTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_CallParentFunction::StaticClass() };
		NodeNamesForCheck = { };
	}

public:
    static FString GetParentMethodName(UK2Node_CallParentFunction* Call);

    virtual FString GenerateInstanceFunctionCall(UK2Node_CallFunction* Call, const FString& Target, const FString& Arguments) const override;
};
