// Fill out your copyright notice in the Description page of Project Settings.


#include "TranslatorObjects/BinaryOperatorTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"


FGenerateResultStruct UBinaryOperatorTranslatorObject::GenerateInputParameterCodeForNode(
    UK2Node* Node,
    UEdGraphPin* Pin,
    int PinIndex,
    TArray<UK2Node*> MacroStack,
    UNativizationV2Subsystem* NativizationV2Subsystem)
{
    return Super::GenerateInputParameterCodeForNode(Node, Pin, PinIndex, MacroStack, NativizationV2Subsystem);
}
