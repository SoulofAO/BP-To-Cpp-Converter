/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright AO All Rights Reserved.
 */

#pragma once

#include "CoreMinimal.h"
#include "TranslatorObjects/CallFunctionTranslatorObject.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "BinaryOperatorTranslatorObject.generated.h"

/**
 * 
 */
UCLASS()
class BLUEPRINTNATIVIZATIONV2_API UBinaryOperatorTranslatorObject : public UCallFunctionTranslatorObject
{
	GENERATED_BODY()
	
    UBinaryOperatorTranslatorObject()
    {
        ClassNodeForCheck = { UK2Node_CommutativeAssociativeBinaryOperator::StaticClass() };
        NodeNamesForCheck = { };
    }

    virtual FGenerateResultStruct GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem) override;

};
