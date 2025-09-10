/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/BreakStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "BlueprintNativizationSubsystem.h"
#include "K2Node_CallFunction.h"

bool UBreakStructTranslatorObject::CanApply(UK2Node* Node)
{
    if (Super::CanApply(Node))
    {
        return true;
    }
    if (UK2Node_CallFunction* K2Node_CallFunction = Cast<UK2Node_CallFunction>(Node))
    {
        if (K2Node_CallFunction->GetTargetFunction()->HasMetaData(FName("NativeBreakFunc")))
        {
            return true;
        }
    }
    return false;
}

FGenerateResultStruct UBreakStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_BreakStruct* BreakStruct = Cast<UK2Node_BreakStruct>(Node))
    {
        TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);

        FGenerateResultStruct GenerateInputCodeStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, Pins[0], 0, MacroStack);

        TArray<UEdGraphPin*> PathPins = UBlueprintNativizationLibrary::GetParentPathPins(Pin);
        Algo::Reverse(PathPins);

        for (UEdGraphPin* CheckPin : PathPins)
        {
            FProperty* Property = BreakStruct->StructType->FindPropertyByName(*CheckPin->GetName());
            FText Text = Property->GetDisplayNameText();
            GenerateInputCodeStruct.Code += "." + UBlueprintNativizationLibrary::GetUniquePropertyName(Property);
            CheckPin = CheckPin->ParentPin;
        }

        return GenerateInputCodeStruct;
    }
    return FGenerateResultStruct();
}

