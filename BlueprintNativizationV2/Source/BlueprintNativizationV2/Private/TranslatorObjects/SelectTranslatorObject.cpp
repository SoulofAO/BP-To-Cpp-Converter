/*
 * Publisher: AO
 * Year of Publication: 2025
 * Copyright AO All Rights Reserved.
 */

#include "TranslatorObjects/SelectTranslatorObject.h"
#include "BlueprintNativizationSubsystem.h"
#include "BlueprintNativizationLibrary.h"

FGenerateResultStruct USelectTranslatorObject::GenerateInputParameterCodeForNode(UK2Node* Node, UEdGraphPin* Pin, int PinIndex, TArray<UK2Node*> MacroStack, UNativizationV2Subsystem* NativizationV2Subsystem)
{
    if (UK2Node_Select* SelectNode = Cast<UK2Node_Select>(Node))
    {
        FName ElementCppType = *UBlueprintNativizationLibrary::GetPinType(Pin->PinType, false);

        TArray<UEdGraphPin*> InputPins = UBlueprintNativizationLibrary::GetFilteredPins(
            SelectNode,
            EPinOutputOrInputFilter::Input,
            EPinExcludeFilter::None,
            EPinIncludeOnlyFilter::None
        );

        TSet<FString> Preparations;

        TArray<FGenerateResultStruct> Elements;
        for (UEdGraphPin* InputPin : InputPins)
        {
            if (InputPin->PinName == SelectNode->GetIndexPin()->PinName)
                continue;

            Elements.Add(NativizationV2Subsystem->GenerateInputParameterCodeForNode(SelectNode, InputPin, 0, MacroStack));
            
        }

        FGenerateResultStruct IndexCode = NativizationV2Subsystem->GenerateInputParameterCodeForNode(SelectNode, SelectNode->GetIndexPin(), 0, MacroStack);
        Preparations.Append(IndexCode.Preparations);

        FString ElementsJoined;
        for (int32 i = 0; i < Elements.Num(); ++i)
        {
            if (i > 0) ElementsJoined += TEXT(", ");
            ElementsJoined += Elements[i].Code;
            Preparations.Append(Elements[i].Preparations);
        }

        return FGenerateResultStruct(FString::Format(TEXT("TArray<{0}>{{1}}[{2}]"), {
            ElementCppType.ToString(),
            ElementsJoined,
            IndexCode.Code
            }), Preparations);
    }

    return FGenerateResultStruct();
}
