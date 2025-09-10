// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/BaseTranslatorObject.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "BinaryOperatorTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UBinaryOperatorTranslatorObject : public UTranslatorBPToCppObject
{
	GENERATED_BODY()
	
    UBinaryOperatorTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_CommutativeAssociativeBinaryOperator::StaticClass() };
        NodeNamesForCheck = { };
    }

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
