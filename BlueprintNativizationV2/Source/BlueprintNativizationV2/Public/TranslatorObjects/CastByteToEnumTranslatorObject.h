// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CastByteToEnum.h"
#include "CastByteToEnumTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UCastByteToEnumTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
	UCastByteToEnumTranslatorObject()
	{
		ClassNodeForCheck = { UK2Node_CastByteToEnum::StaticClass() };
		NodeNamesForCheck = { };
	}
public:
	virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
