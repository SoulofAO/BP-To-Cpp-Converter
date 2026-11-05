/*
 * Publisher: AO
 * Year of Publication: 2026
 * Copyright 2026 AO All Rights Reserved.
 */


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
