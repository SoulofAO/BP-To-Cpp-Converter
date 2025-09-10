/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/MakeStructTranslatorObject.h"
#include "BlueprintNativizationLibrary.h"
#include "K2Node_CallFunction.h"
#include "BlueprintNativizationSubsystem.h"

bool UMakeStructTranslatorObject::CanApply(UK2Node* Node)
{
    if (Super::CanApply(Node))
    {
        return true;
    }
    if (UK2Node_CallFunction* K2Node_CallFunction = Cast<UK2Node_CallFunction>(Node))
    {
        if (K2Node_CallFunction->GetTargetFunction()->HasMetaData(FName("NativeMakeFunc")))
        {
            return true;
        }
    }
    return false;
}

FGenerateResultStruct UMakeStructTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    TArray<UEdGraphPin*> Pins = UBlueprintNativizationLibrary::GetFilteredPins(Node, EPinOutputOrInputFilter::Input, EPinExcludeFilter::None, EPinIncludeOnlyFilter::None);
    TArray<UEdGraphPin*> ParentPins = UBlueprintNativizationLibrary::GetParentPins(Pins);

    FString PinCategory = UBlueprintNativizationLibrary::GetPinType(Pin->PinType, true);

    TArray<FString> Args;
    TSet<FString> Preparations;

    for (UEdGraphPin* ParentPin : ParentPins)
    {
        FString PinInputContent = "";
        FGenerateResultStruct InputResultStruct = NativizationV2Subsystem->GenerateInputParameterCodeForNode(Node, ParentPin, 0, MacroStack);
        Args.Add(InputResultStruct.Code);
		Preparations.Append(InputResultStruct.Preparations);
    }
    return FGenerateResultStruct(FString::Format(TEXT("{0}({1})"), { PinCategory,FString::Join(Args, TEXT(", ")) }), Preparations);
}
